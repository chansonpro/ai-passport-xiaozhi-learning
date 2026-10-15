#include <cassert>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>
#include "websocket_audio_frame.h"

int main() {
    WebsocketAudioFrame frame;
    assert(!ParseWebsocketAudioFrame(nullptr, 0, 2, frame));
    for (int version : {2, 3}) {
        const size_t header = version == 2 ? 16 : 4;
        std::vector<uint8_t> valid(header + 3, 0);
        if (version == 2) {
            valid[1] = 2;
            valid[8] = 0x12;
            valid[9] = 0x34;
            valid[10] = 0x56;
            valid[11] = 0x78;
        }
        valid[header - 1] = 3;
        valid[header] = 0xf8;
        valid[header + 1] = 0xff;
        valid[header + 2] = 0xfe;
        auto parse = [&](const std::vector<uint8_t>& bytes) {
            return ParseWebsocketAudioFrame(reinterpret_cast<const char*>(bytes.data()),
                                            bytes.size(), version, frame);
        };
        assert(parse(valid) && frame.size == 3);
        assert(frame.timestamp == (version == 2 ? 0x12345678u : 0));
        assert(frame.payload[0] == 0xf8 && frame.payload[2] == 0xfe);
        for (size_t length = 0; length < valid.size(); ++length) {
            // 每个截断长度均使用对应大小的真实缓冲，供 ASan 检查越界读取。
            std::vector<uint8_t> short_frame(valid.begin(), valid.begin() + length);
            assert(!parse(short_frame));
            assert(frame.payload == nullptr && frame.size == 0);
        }
        auto bad = valid;
        bad.push_back(0);  // 声明长度小于实际长度。
        assert(!parse(bad));
        bad = valid;
        for (size_t i = version == 2 ? 12 : 2; i < header; ++i)
            bad[i] = 0xff;  // 声明长度远大于实际帧。
        assert(!parse(bad));
        bad = valid;
        bad[version == 2 ? 3 : 0] = 1;  // JSON/未知类型不能作为音频投递。
        assert(!parse(bad));
        if (version == 2) {
            bad = valid;
            bad[1] = 3;
            assert(!parse(bad));
        }
        auto unaligned = valid;
        unaligned.insert(unaligned.begin(), 0);
        const auto original = unaligned;
        assert(ParseWebsocketAudioFrame(reinterpret_cast<const char*>(unaligned.data() + 1),
                                        valid.size(), version, frame));
        assert(unaligned == original);  // 不修改传输层的只读缓冲。
        std::vector<uint8_t> empty(header, 0);
        if (version == 2)
            empty[1] = 2;
        assert(parse(empty) && frame.size == 0);  // 保留零载荷长度的既有解析语义。
    }
    const char raw[] = {1, 2, 3};
    assert(ParseWebsocketAudioFrame(raw, sizeof(raw), 1, frame));
    assert(frame.size == sizeof(raw) && frame.payload[2] == 3 && frame.timestamp == 0);
    std::mt19937 random(513);
    for (int i = 0; i < 20000; ++i) {
        std::vector<uint8_t> bytes(random() % 256);
        for (auto& byte : bytes)
            byte = random();
        for (int version : {2, 3}) {
            if (ParseWebsocketAudioFrame(reinterpret_cast<const char*>(bytes.data()), bytes.size(),
                                         version, frame)) {
                assert(frame.size <= bytes.size());
                assert(frame.payload == bytes.data() + (version == 2 ? 16 : 4));
                for (size_t j = 0; j < frame.size; ++j)
                    assert(frame.payload[j] == bytes[bytes.size() - frame.size + j]);
            }
        }
    }
    puts(
        "PASS: WebSocket v1/v2/v3 valid, truncated, length/type/version mismatch, unaligned and "
        "random frames.");
}
