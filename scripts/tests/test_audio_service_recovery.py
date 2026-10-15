"""Run the real audio-service recovery/queue methods with host SDK stubs."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def method(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


HARNESS = r'''
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <vector>
#include "fixed_queue.h"
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_AUDIO_MONO 1
#define MAX_DECODE_PACKETS_IN_QUEUE 2
struct esp_opus_dec_cfg_t { int rate; int duration; };
struct esp_ae_rate_cvt_cfg_t { int src; int dst; int channels; };
#define OPUS_DEC_CFG(rate, duration) esp_opus_dec_cfg_t{rate, duration}
#define RATE_CVT_CFG(src, dst, channels) esp_ae_rate_cvt_cfg_t{src, dst, channels}
static bool fail_open = false;
static int open_count = 0;
static int close_count = 0;
int esp_opus_dec_open(const esp_opus_dec_cfg_t*, size_t, void** decoder) {
    ++open_count;
    *decoder = fail_open ? nullptr : new int(1);
    return fail_open ? -1 : 0;
}
void esp_opus_dec_close(void* decoder) { ++close_count; delete static_cast<int*>(decoder); }
void esp_ae_rate_cvt_close(void* converter) { delete static_cast<int*>(converter); }
int esp_ae_rate_cvt_open(const esp_ae_rate_cvt_cfg_t*, void** converter) {
    *converter = new int(1); return 0;
}
struct Codec { int output_sample_rate() const { return 16000; } };
struct Board {
    static Board& GetInstance() { static Board board; return board; }
    Codec* GetAudioCodec() { return &codec; }
    Codec codec;
};
struct AudioStreamPacket { std::vector<unsigned char> payload; };
struct AudioService {
    void SetDecodeSampleRate(int sample_rate, int frame_duration);
    bool TryPushPacketToDecodeQueue(std::unique_ptr<AudioStreamPacket>& packet);
    bool PushPacketToDecodeQueue(std::unique_ptr<AudioStreamPacket> packet, bool wait);
    void* opus_decoder_ = nullptr;
    void* output_resampler_ = nullptr;
    std::mutex decoder_mutex_;
    int decoder_sample_rate_ = 0;
    int decoder_duration_ms_ = 60;
    int decoder_frame_size_ = 0;
    std::mutex audio_queue_mutex_;
    std::condition_variable audio_queue_cv_;
    std::atomic<bool> service_stopped_{false};
    unsigned playback_generation_ = 0;
    bool playback_drained_notified_ = true;
    FixedQueue<std::unique_ptr<AudioStreamPacket>, 4> audio_decode_queue_;
    ~AudioService() {
        if (opus_decoder_) esp_opus_dec_close(opus_decoder_);
        if (output_resampler_) esp_ae_rate_cvt_close(output_resampler_);
    }
};
'''

SCENARIOS = r'''
int main() {
    AudioService audio;
    audio.SetDecodeSampleRate(16000, 60);
    assert(audio.opus_decoder_ && open_count == 1);
    audio.SetDecodeSampleRate(16000, 60);
    assert(open_count == 1); // Steady state must not recreate a working decoder.
    fail_open = true;
    audio.SetDecodeSampleRate(24000, 60);
    assert(!audio.opus_decoder_ && close_count == 1);
    fail_open = false;
    audio.SetDecodeSampleRate(16000, 60);
    assert(audio.opus_decoder_ && open_count == 3); // Recover original format.
    fail_open = true;
    audio.SetDecodeSampleRate(24000, 60);
    fail_open = false;
    audio.SetDecodeSampleRate(24000, 60);
    assert(audio.opus_decoder_ && audio.decoder_frame_size_ == 1440);

    auto first = std::make_unique<AudioStreamPacket>();
    auto second = std::make_unique<AudioStreamPacket>();
    assert(audio.TryPushPacketToDecodeQueue(first) && !first);
    assert(audio.TryPushPacketToDecodeQueue(second) && !second);
    auto pending = std::make_unique<AudioStreamPacket>();
    pending->payload = {1, 2, 3, 4};
    auto* identity = pending.get();
    auto* payload = pending->payload.data();
    for (int i = 0; i < 100; ++i) {
        assert(!audio.TryPushPacketToDecodeQueue(pending));
        assert(pending.get() == identity && pending->payload.data() == payload);
        assert(pending->payload[3] == 4);
    }
    audio.audio_decode_queue_.pop_front();
    assert(audio.TryPushPacketToDecodeQueue(pending) && !pending);
    audio.audio_decode_queue_.pop_front();
    assert(audio.audio_decode_queue_.front().get() == identity);
    audio.audio_decode_queue_.clear();
    audio.service_stopped_.store(true);
    pending = std::make_unique<AudioStreamPacket>();
    identity = pending.get();
    assert(!audio.TryPushPacketToDecodeQueue(pending) && pending.get() == identity);
    audio.service_stopped_.store(false);
    std::unique_ptr<AudioStreamPacket> empty;
    assert(!audio.TryPushPacketToDecodeQueue(empty));
    assert(audio.PushPacketToDecodeQueue(std::move(pending), false));
    assert(audio.PushPacketToDecodeQueue(std::make_unique<AudioStreamPacket>(), false));
    assert(!audio.PushPacketToDecodeQueue(std::make_unique<AudioStreamPacket>(), false));
}
'''


class AudioServiceRecoveryTests(unittest.TestCase):
    def test_decoder_recovery_and_packet_ownership(self):
        compiler = shutil.which(os.environ.get("CXX", "clang++"))
        if compiler is None:
            self.skipTest("Host C++ compiler unavailable")
        source = (ROOT / "main/audio/audio_service.cc").read_text()
        methods = "\n".join(method(source, signature) for signature in (
            "void AudioService::SetDecodeSampleRate(",
            "bool AudioService::TryPushPacketToDecodeQueue(",
            "bool AudioService::PushPacketToDecodeQueue(",
        ))
        with tempfile.TemporaryDirectory(prefix="audio-service-test-") as directory:
            work = Path(directory)
            (work / "esp_system.h").write_text(
                '#pragma once\n#include <cstdlib>\n'
                'inline void esp_system_abort(const char*) { std::abort(); }\n'
            )
            harness = work / "test.cc"
            harness.write_text(HARNESS + methods + SCENARIOS)
            binary = work / "test"
            subprocess.run([
                compiler, "-std=c++17", "-pthread", "-I", str(work),
                "-I", str(ROOT / "main/audio"), str(harness), "-o", str(binary),
            ], check=True, capture_output=True, text=True)
            subprocess.run([str(binary)], check=True, capture_output=True, text=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
