#ifndef PROTOCOL_AUDIO_PARAMS_H
#define PROTOCOL_AUDIO_PARAMS_H

#include <cJSON.h>
#include <cstring>

// Both transports negotiate the same mono Opus decoder parameters.
inline bool ParseProtocolAudioParameters(const cJSON* root, int& sample_rate, int& frame_duration) {
    auto params = cJSON_GetObjectItem(root, "audio_params");
    if (params == nullptr) {
        return true;
    }
    if (!cJSON_IsObject(params)) {
        return false;
    }
    auto format = cJSON_GetObjectItem(params, "format");
    if (format != nullptr &&
        (!cJSON_IsString(format) || strcmp(format->valuestring, "opus") != 0)) {
        return false;
    }
    auto channels = cJSON_GetObjectItem(params, "channels");
    if (channels != nullptr && (!cJSON_IsNumber(channels) || channels->valuedouble != 1)) {
        return false;
    }
    auto rate = cJSON_GetObjectItem(params, "sample_rate");
    auto duration = cJSON_GetObjectItem(params, "frame_duration");
    // @CfSecAICode 遵循消费金融安全编码 SEC-MEM-001 规范: 限制解码参数后才据其分配 PCM 缓冲。
    if (rate != nullptr) {
        if (!cJSON_IsNumber(rate) || rate->valuedouble != rate->valueint ||
            (rate->valueint != 8000 && rate->valueint != 12000 && rate->valueint != 16000 &&
             rate->valueint != 24000 && rate->valueint != 48000)) {
            return false;
        }
        sample_rate = rate->valueint;
    }
    if (duration != nullptr) {
        if (!cJSON_IsNumber(duration) || duration->valuedouble != duration->valueint ||
            (duration->valueint != 5 && duration->valueint != 10 && duration->valueint != 20 &&
             duration->valueint != 40 && duration->valueint != 60 && duration->valueint != 80 &&
             duration->valueint != 100 && duration->valueint != 120)) {
            return false;
        }
        frame_duration = duration->valueint;
    }
    return true;
}

#endif  // PROTOCOL_AUDIO_PARAMS_H
