#pragma once
#include <cstddef>
#include <cstdint>

// 仅描述当前回调中的只读报文，不拥有或修改网络缓冲区。
struct WebsocketAudioFrame {
    const uint8_t* payload = nullptr;
    size_t size = 0;
    uint32_t timestamp = 0;
};

inline bool ParseWebsocketAudioFrame(const char* data, size_t len, int version,
                                     WebsocketAudioFrame& frame) {
    frame = {};
    if (data == nullptr)
        return false;
    const auto* bytes = reinterpret_cast<const uint8_t*>(data);
    const size_t header = version == 2 ? 16 : version == 3 ? 4 : 0;
    // @CfSecAICode 遵循消费金融安全编码 SEC-MEM-001 规范:
    // 读取帧头前验证边界，按字节解析避免未对齐读取。
    if (len < header)
        return false;
    const auto read16 = [bytes](size_t offset) -> uint16_t {
        return (uint16_t(bytes[offset]) << 8) | bytes[offset + 1];
    };
    const auto read32 = [bytes](size_t offset) -> uint32_t {
        return (uint32_t(bytes[offset]) << 24) | (uint32_t(bytes[offset + 1]) << 16) |
               (uint32_t(bytes[offset + 2]) << 8) | bytes[offset + 3];
    };
    size_t payload_size = len;
    uint32_t timestamp = 0;
    if (version == 2) {
        if (read16(0) != 2 || read16(2) != 0)
            return false;
        payload_size = read32(12);
        timestamp = read32(8);
    } else if (version == 3) {
        if (bytes[0] != 0)
            return false;
        payload_size = read16(2);
    }
    // @CfSecAICode 遵循消费金融安全编码 SEC-MEM-001 规范:
    // 分配和复制前核对实际载荷长度，用减法避免溢出。
    if (payload_size != len - header)
        return false;
    frame = {bytes + header, payload_size, timestamp};
    return true;
}
