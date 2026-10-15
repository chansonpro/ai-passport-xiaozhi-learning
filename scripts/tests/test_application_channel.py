"""Exercise actual async channel methods with a deterministic task scheduler."""
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
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


HARNESS = r'''
#include <atomic>
#include <cassert>
#include <deque>
#include <functional>
#include <memory>
#include <utility>
#define pdPASS 1
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
enum DeviceState { kDeviceStateIdle, kDeviceStateConnecting, kDeviceStateListening,
                   kDeviceStateSpeaking, kDeviceStateNotifying, kDeviceStateStarting,
                   kDeviceStateWifiConfiguring, kDeviceStateActivating };
enum ListeningMode { kListeningModeAutoStop, kListeningModeManualStop };
enum class PowerSaveLevel { LOW_POWER, PERFORMANCE };
struct Display { void UpdateStatusBar(bool) {} };
struct Board {
    Display display;
    Display* GetDisplay() { return &display; }
    PowerSaveLevel power = PowerSaveLevel::LOW_POWER;
    static Board& GetInstance() { static Board b; return b; }
    void SetPowerSaveLevel(PowerSaveLevel p) { power = p; }
};
static std::deque<std::function<void()>> workers;
static bool fail_create = false;
static constexpr int MAIN_EVENT_SEND_AUDIO = 1;
static constexpr int MAIN_EVENT_NETWORK_CONNECTED = 2;
static int rearmed_events = 0;
void xEventGroupSetBits(int, int bits) { rearmed_events |= bits; }
int xTaskCreate(void (*entry)(void*), const char*, int, void* context, int, void*) {
    if (fail_create) return 0;
    workers.push_back([=]() { entry(context); }); return pdPASS;
}
void vTaskDelete(void*) {}
void RunWorker() {
    assert(workers.size() == 1);
    auto task = std::move(workers.front()); workers.pop_front(); task();
}
struct AudioPacket {};
struct Protocol {
    static int destroyed;
    bool opened = false;
    bool succeed = true;
    int opens = 0;
    int closes = 0;
    int sends = 0;
    bool send_success = true;
    std::function<void()> on_send;
    bool SendAudio(std::unique_ptr<AudioPacket>) {
        ++sends;
        if (on_send) on_send();
        return send_success;
    }
    bool IsAudioChannelOpened() { return opened; }
    bool OpenAudioChannel() { ++opens; opened = succeed; return succeed; }
    std::function<void()> on_close;
    void CloseAudioChannel() { ++closes; opened = false; if (on_close) on_close(); }
    ~Protocol() { ++destroyed; }
};
int Protocol::destroyed = 0;
struct AudioService {
    int resets = 0;
    int queued = 0;
    std::unique_ptr<AudioPacket> PopPacketFromSendQueue() {
        if (queued == 0) return nullptr;
        --queued; return std::make_unique<AudioPacket>();
    }
    void EnableVoiceProcessing(bool) {}
    void ResetDecoder() { ++resets; }
};
struct Application {
    std::unique_ptr<Protocol> protocol_ = std::make_unique<Protocol>();
    std::atomic<bool> channel_opening_{false};
    std::atomic<bool> protocol_ready_{true};
    bool channel_open_cancelled_ = false;
    bool protocol_reset_pending_ = false;
    bool activation_pending_ = false;
    void* activation_task_handle_ = nullptr;
    int activations = 0;
    std::atomic<unsigned> conversation_generation_{0};
    std::function<void()> channel_open_continuation_;
    bool play_popup_on_listening_ = false;
    bool pending_listening_start_ = false;
    bool aborted_ = true;
    AudioService audio_service_;
    DeviceState state = kDeviceStateConnecting;
    ListeningMode mode = kListeningModeAutoStop;
    int continued = 0;
    std::deque<std::function<void()>> scheduled;
    DeviceState GetDeviceState() { return state; }
    void SetDeviceState(DeviceState s) { state = s; }
    void SetListeningMode(ListeningMode m) { ++continued; mode = m; state = kDeviceStateListening; }
    void StopNotification() { state = kDeviceStateIdle; }
    void Schedule(std::function<void()> f) { scheduled.push_back(std::move(f)); }
    void Drain() {
        while (!scheduled.empty()) {
            auto f = std::move(scheduled.front()); scheduled.pop_front(); f();
        }
    }
    void ContinueOpenAudioChannel(ListeningMode mode);
    void OpenAudioChannelAsync(std::function<void()> continuation);
    void FinishOpenAudioChannel(bool success);
    void ScheduleProtocolTask(std::function<void()> callback);
    void EndConversationOnMain();
    void EndConversation();
    void HandleTtsStart();
    void ResetProtocol();
    void HandleNetworkConnectedEvent();
    void HandleNetworkDisconnectedEvent();
    void ActivationTask() { ++activations; protocol_ = std::make_unique<Protocol>(); }
    void DrainSendAudio();
    int event_group_ = 0;
};
'''

SCENARIOS = r'''
int main() {
    { // Partially initialized protocols cannot be opened from user controls.
        Application app; app.protocol_ready_ = false;
        app.ContinueOpenAudioChannel(kListeningModeAutoStop);
        assert(workers.empty() && app.protocol_->opens == 0);
    }
    { // Opening returns immediately; continuation runs only on the main scheduler.
        Application app;
        app.ContinueOpenAudioChannel(kListeningModeManualStop);
        assert(app.channel_opening_ && app.protocol_->opens == 0 && app.continued == 0);
        app.OpenAudioChannelAsync([]() { assert(false); });
        assert(workers.size() == 1);
        RunWorker();
        assert(app.channel_opening_ && app.continued == 0);
        app.Drain();
        assert(!app.channel_opening_ && app.continued == 1);
        assert(app.state == kDeviceStateListening && app.mode == kListeningModeManualStop);
    }
    { // Cancellation keeps transport alive until completion and discards continuation.
        Application app;
        app.ContinueOpenAudioChannel(kListeningModeAutoStop);
        app.EndConversationOnMain();
        assert(app.channel_opening_ && app.protocol_->closes == 0);
        assert(app.state == kDeviceStateIdle);
        RunWorker(); app.Drain();
        assert(app.continued == 0 && !app.protocol_->opened);
        assert(!app.channel_opening_ && app.protocol_->closes == 1);
    }
    { // Reset during opening cannot delete transport out from under the worker.
        Application app;
        int destroyed = Protocol::destroyed;
        app.ContinueOpenAudioChannel(kListeningModeAutoStop);
        app.ResetProtocol(); app.Drain();
        assert(app.protocol_ && app.protocol_reset_pending_);
        assert(Protocol::destroyed == destroyed);
        RunWorker(); app.Drain();
        assert(!app.protocol_ && !app.protocol_reset_pending_);
        assert(Protocol::destroyed == destroyed + 1 && app.continued == 0);
    }
    { // FreeRTOS task creation failure cleans up synchronously.
        Application app;
        fail_create = true;
        app.ContinueOpenAudioChannel(kListeningModeAutoStop);
        fail_create = false;
        assert(workers.empty() && !app.channel_opening_);
        assert(app.state == kDeviceStateIdle && app.continued == 0);
        assert(Board::GetInstance().power == PowerSaveLevel::LOW_POWER);
    }
    { // Transport open failure never enters listening.
        Application app;
        app.protocol_->succeed = false;
        app.ContinueOpenAudioChannel(kListeningModeAutoStop);
        RunWorker(); app.Drain();
        assert(app.state == kDeviceStateIdle && app.continued == 0);
    }
    { // Previously queued protocol callbacks cannot overwrite a new conversation.
        Application app;
        app.ScheduleProtocolTask([&]() { app.SetDeviceState(kDeviceStateSpeaking); });
        app.EndConversationOnMain();
        app.SetDeviceState(kDeviceStateConnecting);
        app.ContinueOpenAudioChannel(kListeningModeManualStop);
        RunWorker(); app.Drain();
        assert(app.state == kDeviceStateListening && app.continued == 1);
        int calls = 0;
        app.ScheduleProtocolTask([&]() { ++calls; });
        app.Drain(); assert(calls == 1);
    }
    { // Cancellation before scheduled opening must not start a worker.
        Application app;
        app.Schedule([&]() { app.ContinueOpenAudioChannel(kListeningModeAutoStop); });
        app.EndConversationOnMain(); app.Drain();
        assert(workers.empty() && app.continued == 0);
    }
    { // A successful slow transport can refill continuously, but one turn is bounded.
        Application app;
        app.audio_service_.queued = 5;
        app.protocol_->on_send = [&]() { ++app.audio_service_.queued; };
        rearmed_events = 0;
        app.DrainSendAudio();
        assert(app.protocol_->sends == 4 && app.audio_service_.queued == 5);
        assert(rearmed_events & MAIN_EVENT_SEND_AUDIO);
    }
    { // Failed send still schedules a turn for already-buffered audio.
        Application app;
        app.audio_service_.queued = 3;
        app.protocol_->send_success = false;
        rearmed_events = 0;
        app.DrainSendAudio();
        assert(app.protocol_->sends == 1 && app.audio_service_.queued == 2);
        assert(rearmed_events & MAIN_EVENT_SEND_AUDIO);
    }
    { // Synchronous Close callbacks belong to the cancelled conversation too.
        Application app;
        int closed_callbacks = 0;
        app.protocol_->on_close = [&]() {
            app.ScheduleProtocolTask([&]() { ++closed_callbacks; });
        };
        app.EndConversationOnMain(); app.Drain();
        assert(closed_callbacks == 0);
    }
    { // Old queued start must not steal the mode from a newly requested session.
        Application app;
        app.ScheduleProtocolTask([&]() {
            app.ContinueOpenAudioChannel(kListeningModeAutoStop);
        });
        app.EndConversationOnMain();
        app.SetDeviceState(kDeviceStateConnecting);
        app.ScheduleProtocolTask([&]() {
            app.ContinueOpenAudioChannel(kListeningModeManualStop);
        });
        app.Drain(); RunWorker(); app.Drain();
        assert(app.mode == kListeningModeManualStop && app.continued == 1);
    }
    { // Opening worker retains exclusive access to the protocol transport.
        Application app;
        app.channel_opening_ = true;
        app.audio_service_.queued = 8;
        app.DrainSendAudio();
        assert(app.protocol_->sends == 0 && app.audio_service_.queued == 4);
    }
    { // Reconfiguration cannot replace the protocol while opening still owns it.
        Application app;
        app.ContinueOpenAudioChannel(kListeningModeAutoStop);
        app.ResetProtocol(); app.Drain();
        app.SetDeviceState(kDeviceStateWifiConfiguring);
        auto* old_protocol = app.protocol_.get();
        int destroyed = Protocol::destroyed;
        app.HandleNetworkConnectedEvent();
        assert(app.activation_pending_ && app.protocol_.get() == old_protocol);
        assert(workers.size() == 1 && app.activations == 0);
        assert(Protocol::destroyed == destroyed);
        rearmed_events = 0;
        RunWorker(); app.Drain();
        assert(!app.protocol_ && Protocol::destroyed == destroyed + 1);
        assert(!app.activation_pending_ && (rearmed_events & MAIN_EVENT_NETWORK_CONNECTED));
        app.HandleNetworkConnectedEvent();
        assert(workers.size() == 1 && app.state == kDeviceStateActivating);
        RunWorker();
        assert(app.activations == 1 && app.protocol_);
    }
    { // A later disconnect cancels the deferred activation request.
        Application app;
        app.ContinueOpenAudioChannel(kListeningModeAutoStop);
        app.ResetProtocol(); app.Drain();
        app.SetDeviceState(kDeviceStateWifiConfiguring);
        app.HandleNetworkConnectedEvent();
        app.HandleNetworkDisconnectedEvent();
        assert(!app.activation_pending_);
        rearmed_events = 0;
        RunWorker(); app.Drain();
        assert(!(rearmed_events & MAIN_EVENT_NETWORK_CONNECTED));
        assert(app.activations == 0 && workers.empty());
    }
    { // A public End requeued by a UI callback must not cancel a newer session.
        Application app;
        app.Schedule([&]() { app.EndConversation(); });
        auto ui_callback = std::move(app.scheduled.front());
        app.scheduled.pop_front();
        ui_callback(); // EndConversation now has its own pending callback.
        assert(app.scheduled.size() == 1);
        app.ContinueOpenAudioChannel(kListeningModeManualStop); // New generation.
        RunWorker(); app.Drain();
        assert(app.state == kDeviceStateListening && app.continued == 1);
        assert(!app.channel_open_cancelled_ && app.protocol_->closes == 0);
    }
    { // Manual-stop leaves Idle while awaiting a legitimate TTS response.
        Application app;
        app.mode = kListeningModeManualStop;
        app.SetDeviceState(kDeviceStateIdle);
        app.HandleTtsStart();
        assert(app.state == kDeviceStateIdle);
        app.Drain();
        assert(app.state == kDeviceStateSpeaking && !app.aborted_);
    }
    { // TTS received after explicit cancellation must not restart speaking.
        Application app;
        app.SetDeviceState(kDeviceStateIdle);
        app.channel_open_cancelled_ = true;
        app.HandleTtsStart(); app.Drain();
        assert(app.state == kDeviceStateIdle && app.aborted_);
    }
    { // TTS queued before cancellation is invalidated even if a new session starts.
        Application app;
        app.SetDeviceState(kDeviceStateIdle);
        app.HandleTtsStart();
        app.EndConversationOnMain();
        app.SetDeviceState(kDeviceStateConnecting);
        app.ContinueOpenAudioChannel(kListeningModeManualStop);
        RunWorker(); app.Drain();
        assert(app.state == kDeviceStateListening && app.continued == 1);
        assert(app.aborted_); // Stale TTS never ran its speaking mutation.
    }
    { // Existing open transport keeps the synchronous fast path.
        Application app;
        app.protocol_->opened = true;
        app.ContinueOpenAudioChannel(kListeningModeManualStop);
        assert(workers.empty() && app.continued == 1);
    }
}
'''


class ApplicationChannelTests(unittest.TestCase):
    def test_async_channel_lifecycle(self):
        compiler = shutil.which(os.environ.get("CXX", "clang++"))
        if compiler is None:
            self.skipTest("Host C++ compiler unavailable")
        source = (ROOT / "main/application.cc").read_text()
        methods = "\n".join(method(source, "void Application::" + name + "(") for name in (
            "ContinueOpenAudioChannel", "OpenAudioChannelAsync", "FinishOpenAudioChannel",
            "ScheduleProtocolTask", "EndConversation", "EndConversationOnMain", "ResetProtocol",
            "HandleNetworkConnectedEvent", "HandleNetworkDisconnectedEvent",
        ))
        with tempfile.TemporaryDirectory(prefix="application-channel-test-") as directory:
            work = Path(directory)
            harness, binary = work / "test.cc", work / "test"
            send_block = method(source, "if (bits & MAIN_EVENT_SEND_AUDIO)")
            send_method = (
                "void Application::DrainSendAudio() { int bits = MAIN_EVENT_SEND_AUDIO;"
                + send_block + "}"
            )
            tts_block = method(source, 'if (strcmp(state->valuestring, "start") == 0)')
            tts_method = "void Application::HandleTtsStart() " + tts_block[tts_block.index("{"):]
            harness.write_text(HARNESS + methods + send_method + tts_method + SCENARIOS)
            subprocess.run([
                compiler, "-std=c++17", "-pthread", str(harness), "-o", str(binary),
            ], check=True, capture_output=True, text=True)
            subprocess.run([str(binary)], check=True, capture_output=True, text=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
