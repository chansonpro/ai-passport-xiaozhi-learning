#include "websocket_protocol.h"
#include "application.h"
#include "board.h"
#include "protocol_audio_params.h"
#include "settings.h"
#include "system_info.h"
#include "websocket_audio_frame.h"

#include <esp_log.h>
#include <arpa/inet.h>
#include <cJSON.h>
#include <cstring>
#include "assets/lang_config.h"

#define TAG "WS"

static_assert(sizeof(BinaryProtocol2) == 16);
static_assert(sizeof(BinaryProtocol3) == 4);

WebsocketProtocol::WebsocketProtocol() { event_group_handle_ = xEventGroupCreate(); }

WebsocketProtocol::~WebsocketProtocol() {
    ResetConnection();
    vEventGroupDelete(event_group_handle_);
}

void WebsocketProtocol::ResetConnection() {
    ++connection_generation_;
    awaiting_hello_ = false;
    // @CfSecAICode 遵循消费金融安全编码 SEC-MEM-001 规范: 先释放回调源，再释放事件组。
    websocket_.reset();
    xEventGroupClearBits(event_group_handle_, WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT);
}

bool WebsocketProtocol::Start() {
    // Only connect to server when audio channel is needed
    return true;
}

bool WebsocketProtocol::SendAudio(std::unique_ptr<AudioStreamPacket> packet) {
    if (websocket_ == nullptr || !websocket_->IsConnected()) {
        return false;
    }

    if (version_ == 2) {
        std::string serialized;
        serialized.resize(sizeof(BinaryProtocol2) + packet->payload.size());
        auto bp2 = (BinaryProtocol2*)serialized.data();
        bp2->version = htons(version_);
        bp2->type = 0;
        bp2->reserved = 0;
        bp2->timestamp = htonl(packet->timestamp);
        bp2->payload_size = htonl(packet->payload.size());
        memcpy(bp2->payload, packet->payload.data(), packet->payload.size());

        return websocket_->Send(serialized.data(), serialized.size(), true);
    } else if (version_ == 3) {
        std::string serialized;
        serialized.resize(sizeof(BinaryProtocol3) + packet->payload.size());
        auto bp3 = (BinaryProtocol3*)serialized.data();
        bp3->type = 0;
        bp3->reserved = 0;
        bp3->payload_size = htons(packet->payload.size());
        memcpy(bp3->payload, packet->payload.data(), packet->payload.size());

        return websocket_->Send(serialized.data(), serialized.size(), true);
    } else {
        return websocket_->Send(packet->payload.data(), packet->payload.size(), true);
    }
}

bool WebsocketProtocol::SendText(const std::string& text) {
    if (websocket_ == nullptr || !websocket_->IsConnected()) {
        return false;
    }

    if (!websocket_->Send(text)) {
        ESP_LOGE(TAG, "Failed to send text: %s", text.c_str());
        SetError(Lang::Strings::SERVER_ERROR);
        return false;
    }

    return true;
}

bool WebsocketProtocol::IsAudioChannelOpened() const {
    return websocket_ != nullptr && websocket_->IsConnected() && !error_occurred_ && !IsTimeout();
}

void WebsocketProtocol::CloseAudioChannel(bool send_goodbye) {
    (void)send_goodbye;  // Websocket doesn't need to send goodbye message
    const bool had_connection = websocket_ != nullptr;
    ResetConnection();
    if (had_connection && on_audio_channel_closed_ != nullptr) {
        on_audio_channel_closed_();
    }
}

bool WebsocketProtocol::OpenAudioChannel() {
    ResetConnection();
    session_id_.clear();
    server_sample_rate_ = 24000;
    server_frame_duration_ = 60;
    const uint32_t generation = connection_generation_.load();
    Settings settings("websocket", false);
    std::string url = settings.GetString("url");
    std::string token = settings.GetString("token");
    int version = settings.GetInt("version");
    if (version != 0) {
        version_ = version;
    }

    error_occurred_ = false;

    auto network = Board::GetInstance().GetNetwork();
    websocket_ = network->CreateWebSocket(1);
    if (websocket_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create websocket");
        return false;
    }

    if (!token.empty()) {
        // If token not has a space, add "Bearer " prefix
        if (token.find(" ") == std::string::npos) {
            token = "Bearer " + token;
        }
        websocket_->SetHeader("Authorization", token.c_str());
    }
    websocket_->SetHeader("Protocol-Version", std::to_string(version_).c_str());
    websocket_->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    websocket_->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());

    websocket_->OnData([this, generation](const char* data, size_t len, bool binary) {
        if (generation != connection_generation_.load()) {
            return;
        }
        if (binary) {
            if (on_incoming_audio_ != nullptr) {
                WebsocketAudioFrame frame;
                if (!ParseWebsocketAudioFrame(data, len, version_, frame)) {
                    ESP_LOGW(TAG, "Invalid binary audio frame: version=%d, size=%zu", version_,
                             len);
                    return;
                }
                on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                    .sample_rate = server_sample_rate_,
                    .frame_duration = server_frame_duration_,
                    .timestamp = frame.timestamp,
                    .payload = std::vector<uint8_t>(frame.payload, frame.payload + frame.size)}));
            }
        } else {
            // Parse JSON data
            auto root = cJSON_ParseWithLength(data, len);
            auto type = cJSON_GetObjectItem(root, "type");
            if (cJSON_IsString(type)) {
                if (strcmp(type->valuestring, "hello") == 0) {
                    ParseServerHello(root);
                } else {
                    if (on_incoming_json_ != nullptr) {
                        on_incoming_json_(root);
                    }
                }
            } else {
                ESP_LOGE(TAG, "Missing message type, data: %s", std::string(data, len).c_str());
            }
            cJSON_Delete(root);
        }
        last_incoming_time_ = std::chrono::steady_clock::now();
    });

    websocket_->OnDisconnected([this, generation]() {
        if (generation != connection_generation_.load()) {
            return;
        }
        ESP_LOGI(TAG, "Websocket disconnected");
        if (on_audio_channel_closed_ != nullptr) {
            on_audio_channel_closed_();
        }
    });

    ESP_LOGI(TAG, "Connecting to websocket server: %s with version: %d", url.c_str(), version_);
    if (auto connected = websocket_->Connect(url.c_str()); !connected) {
        ESP_LOGE(TAG, "Failed to connect to websocket server: %s",
                 connected.error().ToString().c_str());
        ResetConnection();
        SetError(Lang::Strings::SERVER_NOT_CONNECTED, url);
        return false;
    }

    // Send hello message to describe the client
    auto message = GetHelloMessage();
    awaiting_hello_ = true;
    if (!SendText(message)) {
        ResetConnection();
        return false;
    }

    // Wait for server hello
    EventBits_t bits =
        xEventGroupWaitBits(event_group_handle_, WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT, pdTRUE,
                            pdFALSE, pdMS_TO_TICKS(10000));
    if (!(bits & WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT)) {
        ESP_LOGE(TAG, "Failed to receive server hello");
        ResetConnection();
        SetError(Lang::Strings::SERVER_TIMEOUT);
        return false;
    }

    if (on_audio_channel_opened_ != nullptr) {
        on_audio_channel_opened_();
    }

    return true;
}

std::string WebsocketProtocol::GetHelloMessage() {
    // keys: message type, version, audio_params (format, sample_rate, channels)
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddNumberToObject(root, "version", version_);
    cJSON* features = cJSON_CreateObject();
#if CONFIG_USE_SERVER_AEC
    cJSON_AddBoolToObject(features, "aec", true);
#endif
    cJSON_AddBoolToObject(features, "mcp", true);
    cJSON_AddItemToObject(root, "features", features);
    AddTextFontCapabilities(root);
    cJSON_AddStringToObject(root, "transport", "websocket");
    cJSON* audio_params = cJSON_CreateObject();
    cJSON_AddStringToObject(audio_params, "format", "opus");
    cJSON_AddNumberToObject(audio_params, "sample_rate", 16000);
    cJSON_AddNumberToObject(audio_params, "channels", 1);
    cJSON_AddNumberToObject(audio_params, "frame_duration", OPUS_FRAME_DURATION_MS);
    cJSON_AddItemToObject(root, "audio_params", audio_params);
    auto json_str = cJSON_PrintUnformatted(root);
    std::string message(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
    return message;
}

void WebsocketProtocol::ParseServerHello(const cJSON* root) {
    if (!awaiting_hello_) {
        return;
    }
    auto transport = cJSON_GetObjectItem(root, "transport");
    // @CfSecAICode 遵循消费金融安全编码 SEC-MEM-001 规范: 校验 JSON 类型后才能读取字符串。
    if (!cJSON_IsString(transport) || strcmp(transport->valuestring, "websocket") != 0) {
        ESP_LOGE(TAG, "Unsupported or missing transport");
        return;
    }

    auto session_id = cJSON_GetObjectItem(root, "session_id");
    if (session_id != nullptr && !cJSON_IsString(session_id)) {
        ESP_LOGE(TAG, "Invalid session ID");
        return;
    }
    int sample_rate = 24000;
    int frame_duration = 60;
    if (!ParseProtocolAudioParameters(root, sample_rate, frame_duration)) {
        ESP_LOGE(TAG, "Unsupported audio parameters");
        return;
    }

    session_id_ = cJSON_IsString(session_id) ? session_id->valuestring : "";
    server_sample_rate_ = sample_rate;
    server_frame_duration_ = frame_duration;
    awaiting_hello_ = false;
    xEventGroupSetBits(event_group_handle_, WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT);
}
