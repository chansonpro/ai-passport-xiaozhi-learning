#include "learning_ui.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <algorithm>
#include "application.h"
#include "audio/demuxer/ogg_demuxer.h"
#include "audio_codec.h"
#include "board.h"
#include "learning_media.h"
#include "learning_render.h"
#include "learning_voice.h"
#include "settings.h"

LearningUi::LearningUi(Display* display) : display_(display) {
    Settings saved("learning_v1");
    model_.word = std::clamp<int32_t>(saved.GetInt("word", 0), 0, 3);
    // Board construction precedes audio initialization. Timer only queues work;
    // Application processes it after initializing the codec and audio tasks.
    DisplayLockGuard lock(display_);
    if (lock)
        lv_timer_create(StateTimerCallback, 100, this);
}
void LearningUi::Show(Page entry) {
    DisplayLockGuard lock(display_);
    if (!lock)
        return;
    if (!screen_) {
        chat_screen_ = lv_screen_active();
        screen_ = lv_obj_create(nullptr);
        ESP_LOGI("LearningUi", "learning-v1.1 ready: XiaoZhi voice control + offline learning");
    }
    chat_mode_ = false;
    voice_control_ = false;
    return_requested_ = false;
    model_.page = entry;
    active_ = true;
    Render();
    lv_screen_load(screen_);
}
void LearningUi::Hide() {
    DisplayLockGuard lock(display_);
    if (!lock || !active_)
        return;
    CancelAudio();
    SavePosition();
    lv_screen_load(chat_screen_);
    active_ = false;
}
void LearningUi::Render() {
    last_volume_ = Board::GetInstance().GetAudioCodec()->output_volume();
    const auto state = Application::GetInstance().GetDeviceState();
    last_voice_state_ = int(state);
    const char* status = !voice_control_                   ? nullptr
                         : state == kDeviceStateSpeaking   ? "小智在说"
                         : state == kDeviceStateListening  ? "小智在听"
                         : state == kDeviceStateConnecting ? "连接小智"
                                                           : "小智陪学";
    learning::Render(screen_, model_, last_volume_, speaking_, status);
}
void LearningUi::SavePosition() {
    if (!dirty_)
        return;
    Settings saved("learning_v1", true);
    saved.SetInt("word", model_.word);
    dirty_ = false;
}
void LearningUi::CancelAudio() {
    std::lock_guard<std::mutex> guard(audio_mutex_);
    ++generation_;
    pending_clip_ = -1;
    feeding_.store(false);
    if (speaking_)
        Application::GetInstance().GetAudioService().ResetDecoder();
    speaking_ = false;
}
void LearningUi::Speak(int clip) {
    if (!audio_task_) {
        if (xTaskCreate([](void* p) { static_cast<LearningUi*>(p)->AudioWorker(); },
                        "learning_audio", 4096, this, 3, &audio_task_) != pdPASS) {
            model_.message = "声音暂不可用，请再试一次";
            model_.question_heard = false;
            return;
        }
    }
    {
        std::lock_guard<std::mutex> guard(audio_mutex_);
        pending_clip_ = clip;
        feeding_.store(true);
        speaking_ = true;
    }
    xTaskNotifyGive(audio_task_);
}
void LearningUi::AudioWorker() {
    auto& audio = Application::GetInstance().GetAudioService();
    auto demuxer = std::make_unique<OggDemuxer>();
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint32_t generation;
        int clip;
        {
            std::lock_guard<std::mutex> guard(audio_mutex_);
            generation = generation_;
            clip = pending_clip_;
            pending_clip_ = -1;
        }
        if (clip < 0 || clip >= 8)
            continue;
        demuxer->Reset();
        demuxer->OnPacket(
            [&, generation](const uint8_t* data, int rate, int duration, size_t size) {
                // Queue capacity is owned by AudioService; retry only on this worker.
                // Guard generation and nonblocking enqueue together so cancellation
                // cannot reset the decoder and then receive a stale local packet.
                for (;;) {
                    {
                        std::lock_guard<std::mutex> guard(audio_mutex_);
                        if (generation != generation_)
                            return;
                        auto packet = std::make_unique<AudioStreamPacket>();
                        packet->sample_rate = rate;
                        packet->frame_duration = duration;
                        packet->payload.assign(data, data + size);
                        if (audio.PushPacketToDecodeQueue(std::move(packet), false))
                            return;
                    }
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            });
        const auto ogg = learning::kVoice[clip];
        demuxer->Process(reinterpret_cast<const uint8_t*>(ogg.data()), ogg.size());
        {
            std::lock_guard<std::mutex> guard(audio_mutex_);
            if (generation == generation_)
                feeding_.store(false);
        }
    }
}
void LearningUi::HandleButton(Button button, Press press) {
    if (!active_ || press == Press::kDoubleClick)
        return;
    DisplayLockGuard lock(display_);
    if (!lock)
        return;
    if (press == Press::kLongPress && button != Button::kOk)
        return;
    if (model_.page == Page::kHome && button == Button::kOk && press == Press::kLongPress) {
        auto& app = Application::GetInstance();
        if (voice_control_) {
            return_requested_ = true;
            abort_sent_ = close_sent_ = false;
            Tick();
            return;
        }
        if (app.GetDeviceState() != kDeviceStateIdle)
            return;
        CancelAudio();
        voice_control_ = true;
        chat_mode_ = false;
        return_requested_ = abort_sent_ = close_sent_ = false;
        ESP_LOGI("LearningUi", "Start XiaoZhi on learning home");
        app.ToggleChatState();
        Render();
        return;
    }
    const int word = model_.word;
    if (!(model_.page == Page::kQuiz && press == Press::kClick && button != Button::kOk))
        CancelAudio();
    int clip = model_.Act(button, press);
    if (word != model_.word) {
        dirty_ = true;
        save_after_ = esp_timer_get_time() + 1500000;
    }
    if (press == Press::kLongPress)
        SavePosition();
    if (clip >= 0) {
        if (voice_control_) {
            // Finish cloud playback/channel before playing a physical-key request.
            pending_local_clip_ = clip;
            return_requested_ = true;
            abort_sent_ = close_sent_ = false;
        } else
            Speak(clip);
    }
    Render();
    ESP_LOGI("LearningUi", "v1.2 page=%d word=%d round=%d choice=%d correct=%d clip=%d",
             int(model_.page), model_.word, model_.round, model_.choice,
             model_.answered, clip);
}
void LearningUi::ReturnToLearning() {
    if (!chat_mode_)
        return;
    return_requested_ = true;
    Tick();
}
void LearningUi::Tick() {
    DisplayLockGuard lock(display_);
    if (!lock)
        return;
    const auto state = Application::GetInstance().GetDeviceState();
    if (return_requested_) {
        auto& app = Application::GetInstance();
        if (state == kDeviceStateSpeaking && !abort_sent_) {
            abort_sent_ = true;
            app.AbortSpeaking(kAbortReasonNone);
            app.GetAudioService().ResetDecoder();
        } else if (state == kDeviceStateListening && !close_sent_) {
            close_sent_ = true;
            app.ToggleChatState();
        } else if (state == kDeviceStateIdle) {
            app.GetAudioService().ResetDecoder();
            const Page target = voice_control_ ? model_.page : Page::kHome;
            Show(target);
            if (pending_local_clip_ >= 0) {
                Speak(pending_local_clip_);
                pending_local_clip_ = -1;
                Render();
            }
            ESP_LOGI("LearningUi", "Return from XiaoZhi chat to learning");
        }
        return;
    }
    if (chat_mode_)
        return;
    const bool available =
        state == kDeviceStateIdle || state == kDeviceStateStarting ||
        (voice_control_ && (state == kDeviceStateListening || state == kDeviceStateSpeaking ||
                            state == kDeviceStateConnecting));
    if (!available) {
        if (active_)
            Hide();
        return;
    }
    if (!active_)
        Show(screen_ ? model_.page : Page::kHome);
    bool refresh = last_volume_ != Board::GetInstance().GetAudioCodec()->output_volume() ||
                   (voice_control_ && last_voice_state_ != int(state));
    if (speaking_ && !feeding_.load() &&
        Application::GetInstance().GetAudioService().IsPlaybackIdle()) {
        speaking_ = false;
        if (!*model_.message)
            model_.message = model_.page == Page::kQuiz ? "上下选图，中间确认" : "轮到你啦";
        refresh = true;
    }
    if (dirty_ && esp_timer_get_time() >= save_after_)
        SavePosition();
    if (refresh)
        Render();
}
void LearningUi::StateTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<LearningUi*>(lv_timer_get_user_data(timer));
    if (self->poll_pending_.exchange(true))
        return;
    Application::GetInstance().Schedule([self]() {
        self->Tick();
        self->poll_pending_.store(false);
    });
}
std::string LearningUi::ControlByVoice(const std::string& action, int index) {
    DisplayLockGuard lock(display_);
    if (!lock)
        return "{\"error\":\"display unavailable\"}";
    const auto state = Application::GetInstance().GetDeviceState();
    if (state != kDeviceStateIdle && state != kDeviceStateListening &&
        state != kDeviceStateSpeaking)
        return "{\"error\":\"device busy; learning command not applied\"}";
    if (return_requested_)
        return "{\"error\":\"return in progress; retry later\"}";
    auto next = model_;
    int clip = -1;
    if (!learning::VoiceCommand(next, action, index, clip))
        return "{\"error\":\"invalid action, index, or current page; command not applied\"}";
    CancelAudio();
    model_ = next;
    Show(model_.page);
    voice_control_ = true;
    dirty_ = true;
    save_after_ = esp_timer_get_time() + 1500000;
    auto result = cJSON_Parse(GetCurrentCardJson().c_str());
    if (!result)
        return "{}";
    std::string say;
    if (clip >= 0 && clip < 4)
        say = learning::kWords[clip];
    else if (clip >= 4 && clip < 8)
        say = std::string("Find the ") + learning::kWords[clip - 4];
    else if (action == "answer")
        say = model_.answered ? "找对啦！" : "再试一次";
    else if (action == "home")
        say = "我们开始学英语吧！";
    else
        say = "继续试试看。";
    cJSON_AddStringToObject(result, "say", say.c_str());
    cJSON_AddStringToObject(result, "audio_owner", "xiaozhi_cloud; local audio is not started");
    char* json = cJSON_PrintUnformatted(result);
    std::string response = json ? json : "{}";
    cJSON_free(json);
    cJSON_Delete(result);
    ESP_LOGI("LearningUi", "voice action=%s index=%d page=%d clip=%d", action.c_str(), index,
             int(model_.page), clip);
    return response;
}
std::string LearningUi::GetCurrentCardJson() const {
    DisplayLockGuard lock(display_);
    if (!lock)
        return "{\"page\":\"unavailable\"}";
    auto card = cJSON_CreateObject();
    if (!card)
        return "{}";
    const char* names[] = {"none", "home", "word", "quiz"};
    cJSON_AddStringToObject(card, "page", names[int(model_.page)]);
    cJSON_AddStringToObject(card, "version", "learning-v1.2-english");
    if (model_.page == Page::kWord) {
        cJSON_AddStringToObject(card, "word", learning::kWords[model_.word]);
        cJSON_AddStringToObject(card, "zh", learning::kChinese[model_.word]);
        cJSON_AddNumberToObject(card, "index", model_.word + 1);
        cJSON_AddNumberToObject(card, "total", 4);
    } else if (model_.page == Page::kQuiz) {
        cJSON_AddNumberToObject(card, "round", model_.round + 1);
        cJSON_AddBoolToObject(card, "answered", model_.answered);
        cJSON_AddStringToObject(card, "left_option", learning::kWords[model_.Option(0)]);
        cJSON_AddStringToObject(card, "right_option", learning::kWords[model_.Option(1)]);
    }
    char* json = cJSON_PrintUnformatted(card);
    std::string result = json ? json : "{}";
    cJSON_free(json);
    cJSON_Delete(card);
    return result;
}
