"""Run production protocol methods against deterministic host transport/timer doubles.

This intentionally compiles the actual methods, not a Python reimplementation.
It covers handshake validation, stale callbacks, cleanup and reconnect scheduling;
ESP-IDF/physical transport behavior remains a device-test responsibility.
"""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def method(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


COMMON = r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define pdTRUE true
#define pdFALSE false
#define pdMS_TO_TICKS(x) (x)
#define WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT 1
#define MQTT_RECONNECT_INTERVAL_MS 60000
using EventBits_t = unsigned;
unsigned events = 0;
unsigned xEventGroupClearBits(int, unsigned bits) { events &= ~bits; return events; }
unsigned xEventGroupSetBits(int, unsigned bits) { events |= bits; return events; }
unsigned xEventGroupWaitBits(int, unsigned bits, bool clear, bool, int) {
    unsigned result = events & bits;
    if (clear) events &= ~bits;
    return result;
}
int xEventGroupCreate() { return 1; }
struct cJSON {
    int type = 0;
    const char* valuestring = nullptr;
    double valuedouble = 0;
    int valueint = 0;
    std::map<std::string, cJSON*> fields;
};
cJSON* cJSON_GetObjectItem(const cJSON* root, const char* key) {
    if (!root) return nullptr;
    auto it = root->fields.find(key);
    return it == root->fields.end() ? nullptr : it->second;
}
bool cJSON_IsString(const cJSON* p) { return p && p->type == 1; }
bool cJSON_IsNumber(const cJSON* p) { return p && p->type == 2; }
bool cJSON_IsObject(const cJSON* p) { return p && p->type == 3; }
cJSON* incoming_root = nullptr;
cJSON* cJSON_ParseWithLength(const char*, size_t) { return incoming_root; }
void cJSON_Delete(cJSON*) {}
struct AudioStreamPacket {
    int sample_rate; int frame_duration; uint32_t timestamp;
    std::vector<uint8_t> payload;
};
struct WebsocketAudioFrame { uint32_t timestamp; const uint8_t* payload; size_t size; };
bool ParseWebsocketAudioFrame(const char*, size_t, int, WebsocketAudioFrame&) { return false; }
namespace Lang { namespace Strings {
const char* SERVER_NOT_CONNECTED = "connection";
const char* SERVER_TIMEOUT = "timeout";
}}
struct Result {
    bool ok;
    explicit operator bool() const { return ok; }
    Result error() { return *this; }
    std::string ToString() { return "error"; }
};
struct Socket;
Socket* current_socket = nullptr;
std::function<void()> send_hook;
bool connect_success = true, send_success = true;
int sockets_destroyed = 0, errors = 0;
struct Socket {
    std::function<void(const char*, size_t, bool)> data;
    std::function<void()> disconnected;
    ~Socket() { ++sockets_destroyed; if (disconnected) disconnected(); }
    void OnData(decltype(data) callback) { data = callback; }
    void OnDisconnected(decltype(disconnected) callback) { disconnected = callback; }
    void SetHeader(const char*, const char*) {}
    Result Connect(const char*) { return {connect_success}; }
};
struct Network {
    std::unique_ptr<Socket> CreateWebSocket(int) {
        auto result = std::make_unique<Socket>(); current_socket = result.get(); return result;
    }
};
struct Board {
    static Board& GetInstance() { static Board board; return board; }
    Network* GetNetwork() { static Network network; return &network; }
    std::string GetUuid() { return "device"; }
};
struct SystemInfo { static std::string GetMacAddress() { return "mac"; } };
struct Settings {
    Settings(const char*, bool) {}
    std::string GetString(const char*) { return ""; }
    int GetInt(const char*) { return 0; }
};
struct WebsocketProtocol {
    int event_group_handle_ = 1, version_ = 1;
    int server_sample_rate_ = 24000, server_frame_duration_ = 60;
    std::atomic<uint32_t> connection_generation_{0};
    std::atomic<bool> awaiting_hello_{false};
    bool error_occurred_ = false;
    std::string session_id_;
    std::unique_ptr<Socket> websocket_;
    std::function<void(std::unique_ptr<AudioStreamPacket>)> on_incoming_audio_;
    std::function<void(const cJSON*)> on_incoming_json_;
    std::function<void()> on_audio_channel_closed_, on_audio_channel_opened_;
    std::chrono::steady_clock::time_point last_incoming_time_;
    void ResetConnection();
    void CloseAudioChannel(bool);
    bool OpenAudioChannel();
    void ParseServerHello(const cJSON*);
    std::string GetHelloMessage() { return "hello"; }
    bool SendText(const std::string&) { if(send_hook) send_hook(); return send_success; }
    void SetError(const char*, const std::string& = "") { ++errors; }
};
'''

WS_TEST = r'''
int main() {
    cJSON hello{3}, type{1,"hello"}, transport{1,"websocket"}, number{2,nullptr,7,7};
    cJSON params{3}, rate{2,nullptr,16000,16000}, duration{2,nullptr,20,20};
    cJSON session{1,"new-session"};
    hello.fields = {{"type",&type}};
    WebsocketProtocol protocol;
    protocol.awaiting_hello_ = true;
    protocol.ParseServerHello(&hello); // missing transport must not crash
    assert(events == 0);
    hello.fields["transport"] = &number;
    protocol.ParseServerHello(&hello); // non-string transport must not crash
    assert(events == 0);
    hello.fields["transport"] = &transport;
    hello.fields["audio_params"] = &params;
    params.fields = {{"sample_rate",&rate},{"frame_duration",&duration}};
    for (double bad : {0.0,-1.0,44100.0,16000.5}) {
        rate.valuedouble = bad; rate.valueint = (int)bad;
        protocol.ParseServerHello(&hello);
        assert(events == 0 && protocol.server_sample_rate_ == 24000);
    }
    rate.valuedouble = rate.valueint = 16000;
    for (double bad : {0.0,-1.0,19.0,1000000.0,20.5}) {
        duration.valuedouble = bad; duration.valueint = (int)bad;
        protocol.ParseServerHello(&hello);
        assert(events == 0 && protocol.server_frame_duration_ == 60);
    }
    duration.valuedouble = duration.valueint = 20;
    cJSON wrong_format{1,"pcm"}, stereo{2,nullptr,2,2};
    params.fields["format"] = &wrong_format;
    protocol.ParseServerHello(&hello);
    assert(events == 0);
    params.fields.erase("format");
    params.fields["channels"] = &stereo;
    protocol.ParseServerHello(&hello);
    assert(events == 0);
    params.fields.erase("channels");
    hello.fields["session_id"] = &number;
    protocol.ParseServerHello(&hello);
    assert(events == 0);
    hello.fields["session_id"] = &session;
    protocol.ParseServerHello(&hello);
    assert(events == 1 && protocol.session_id_ == "new-session");
    assert(protocol.server_sample_rate_ == 16000 && protocol.server_frame_duration_ == 20);
    events = 0;
    protocol.ParseServerHello(&hello); // duplicate unsolicited hello ignored
    assert(events == 0);
    int closed = 0, opened = 0;
    protocol.on_audio_channel_closed_ = [&] { ++closed; };
    protocol.on_audio_channel_opened_ = [&] { ++opened; };
    incoming_root = &hello;
    send_hook = [] { current_socket->data("x",1,false); };
    assert(protocol.OpenAudioChannel() && opened == 1);
    auto old_callback = current_socket->data;
    protocol.CloseAudioChannel(false);
    assert(closed == 1 && !protocol.websocket_);
    old_callback("x",1,false); // callback retained by transport cannot set old hello bit
    assert(events == 0);
    events = 1; // previously signalled event must not satisfy a new handshake
    send_hook = [&] { old_callback("x",1,false); };
    assert(!protocol.OpenAudioChannel() && opened == 1 && !protocol.websocket_);
    assert(events == 0 && protocol.session_id_.empty());
    assert(closed == 1); // failed/replaced socket must not enqueue a stale close callback
    connect_success = false;
    assert(!protocol.OpenAudioChannel() && !protocol.websocket_);
    connect_success = true; send_success = false; send_hook = {};
    assert(!protocol.OpenAudioChannel() && !protocol.websocket_);
    send_success = true;
    send_hook = [] { current_socket->data("x",1,false); };
    assert(protocol.OpenAudioChannel() && opened == 2);
    protocol.CloseAudioChannel(false);
    assert(closed == 2);
    // Negotiated values cover every integral Opus duration supported by AudioService.
    for (int sample_rate : {8000,12000,16000,24000,48000}) {
        for (int frame_duration : {5,10,20,40,60,80,100,120}) {
            rate.valuedouble = rate.valueint = sample_rate;
            duration.valuedouble = duration.valueint = frame_duration;
            assert(protocol.OpenAudioChannel());
            assert(protocol.server_sample_rate_ == sample_rate);
            assert(protocol.server_frame_duration_ == frame_duration);
            protocol.CloseAudioChannel(false);
        }
    }
    hello.fields.erase("audio_params");
    hello.fields.erase("session_id");
    assert(protocol.OpenAudioChannel());
    assert(protocol.server_sample_rate_ == 24000 && protocol.server_frame_duration_ == 60);
    assert(protocol.session_id_.empty());
    protocol.CloseAudioChannel(false);
}
'''

MQTT_STUB = r'''
using esp_timer_handle_t = int;
struct esp_timer_create_args_t { void (*callback)(void*); void* arg; };
esp_timer_create_args_t timer_callback{};
int armed = 0;
void esp_timer_create(const esp_timer_create_args_t* args, esp_timer_handle_t* timer) {
    timer_callback = *args; *timer = 1;
}
void esp_timer_stop(int) { armed = 0; }
void esp_timer_start_once(int, int) { armed = 1; }
const int kDeviceStateIdle = 0;
struct Application {
    int state = 0;
    bool channel_opening = false;
    int ended = 0;
    bool IsAudioChannelOpening() const { return channel_opening; }
    void EndConversation() { ++ended; }
    std::vector<std::function<void()>> tasks;
    static Application& GetInstance() { static Application app; return app; }
    int GetDeviceState() { return state; }
    void Schedule(std::function<void()> callback) { tasks.push_back(callback); }
    void Run() { auto pending = std::move(tasks); tasks.clear(); for(auto& task : pending) task(); }
};
struct Mqtt { bool connected = false; bool IsConnected() { return connected; } };
struct MqttProtocol {
    int event_group_handle_; esp_timer_handle_t reconnect_timer_;
    std::atomic<bool> opening_{false};
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
    std::unique_ptr<Mqtt> mqtt_ = std::make_unique<Mqtt>();
    int attempts = 0; bool connect_ok = true;
    std::mutex channel_mutex_;
    std::string session_id_;
    int closed = 0;
    void CloseAudioChannel(bool send_goodbye) { assert(!send_goodbye); ++closed; }
    void HandleServerGoodbye(const std::string& session_id);
    MqttProtocol();
    void ScheduleReconnect();
    bool StartMqttClient(bool) { ++attempts; return connect_ok; }
};
void FireTimer() { armed = 0; timer_callback.callback(timer_callback.arg); }
'''
MQTT_TEST = r'''
int main() {
    MqttProtocol protocol;
    auto& app = Application::GetInstance();
    app.state = 1;
    FireTimer(); app.Run();
    assert(protocol.attempts == 0 && armed == 1); // busy -> retained retry
    app.state = kDeviceStateIdle;
    protocol.opening_ = true;
    FireTimer(); app.Run();
    assert(protocol.attempts == 0 && armed == 1); // asynchronous open -> no concurrent reset
    protocol.opening_ = false;
    app.channel_opening = true; // task dispatched, worker has not entered OpenAudioChannel yet
    FireTimer(); app.Run();
    assert(protocol.attempts == 0 && armed == 1);
    app.channel_opening = false;
    FireTimer(); app.Run();
    assert(protocol.attempts == 1 && armed == 0);
    protocol.connect_ok = false;
    FireTimer(); app.Run();
    assert(protocol.attempts == 2 && armed == 1); // failed attempt -> retry
    protocol.mqtt_->connected = true;
    FireTimer(); app.Run();
    assert(protocol.attempts == 2 && armed == 0); // already connected -> no reset
    protocol.mqtt_->connected = false;
    FireTimer();
    *protocol.alive_ = false;
    app.Run();
    assert(protocol.attempts == 2); // queued callback after destruction ignored
}
'''


GOODBYE_TEST = r'''
int main() {
    MqttProtocol protocol;
    auto& app = Application::GetInstance();
    protocol.session_id_ = "current";
    protocol.HandleServerGoodbye("old");
    app.Run();
    assert(protocol.closed == 0 && app.ended == 0);
    protocol.HandleServerGoodbye("current");
    protocol.session_id_ = "next"; // queued old goodbye must not close the new session
    app.Run();
    assert(protocol.closed == 0 && app.ended == 0);
    protocol.HandleServerGoodbye("next");
    app.Run();
    assert(protocol.closed == 1 && app.ended == 0);
    protocol.opening_ = true;
    protocol.HandleServerGoodbye("next");
    app.Run();
    assert(protocol.closed == 1 && app.ended == 1);
    protocol.opening_ = false;
    app.channel_opening = true; // also cover before the background worker starts
    protocol.HandleServerGoodbye("next");
    app.Run();
    assert(protocol.closed == 1 && app.ended == 2);
    protocol.HandleServerGoodbye("next");
    *protocol.alive_ = false;
    app.Run();
    assert(protocol.closed == 1 && app.ended == 2);
}
'''


class ProtocolLifecycleTests(unittest.TestCase):
    def compile_and_run(self, source):
        compiler = shutil.which('clang++') or shutil.which('g++')
        if not compiler:
            self.skipTest('C++ host compiler unavailable')
        with tempfile.TemporaryDirectory(prefix='protocol-lifecycle-') as tmp:
            cpp = pathlib.Path(tmp) / 'test.cc'
            binary = pathlib.Path(tmp) / 'test'
            cpp.write_text(source)
            # cJSON is a test double in COMMON; the production header still resolves its include.
            (pathlib.Path(tmp) / 'cJSON.h').write_text('#pragma once\n')
            subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Wno-missing-field-initializers',
                            '-I', tmp, '-I', str(ROOT / 'main/protocols'),
                            str(cpp), '-o', str(binary)], check=True, capture_output=True, text=True)
            subprocess.run([str(binary)], check=True, capture_output=True, text=True, timeout=10)

    def test_websocket_handshake_and_connection_generations(self):
        source = (ROOT / 'main/protocols/websocket_protocol.cc').read_text()
        signatures = ['void WebsocketProtocol::ResetConnection(',
                      'void WebsocketProtocol::CloseAudioChannel(', 'bool WebsocketProtocol::OpenAudioChannel(',
                      'void WebsocketProtocol::ParseServerHello(']
        production = '#include "protocol_audio_params.h"\n'
        production += '\n'.join(method(source, signature) for signature in signatures)
        self.compile_and_run(COMMON + production + WS_TEST)

    def test_mqtt_reconnect_deferred_until_idle_and_open_complete(self):
        source = (ROOT / 'main/protocols/mqtt_protocol.cc').read_text()
        self.compile_and_run(COMMON + MQTT_STUB + method(source, 'MqttProtocol::MqttProtocol(')
                             + method(source, 'void MqttProtocol::ScheduleReconnect(') + MQTT_TEST)

    def test_mqtt_goodbye_rechecks_session_and_defers_close_while_opening(self):
        source = (ROOT / 'main/protocols/mqtt_protocol.cc').read_text()
        self.compile_and_run(COMMON + MQTT_STUB + method(source, 'MqttProtocol::MqttProtocol(')
                             + method(source, 'void MqttProtocol::ScheduleReconnect(')
                             + method(source, 'void MqttProtocol::HandleServerGoodbye(') + GOODBYE_TEST)



if __name__ == '__main__':
    unittest.main()
