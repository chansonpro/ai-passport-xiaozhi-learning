#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <mutex>
#include <string>
#include "display/display.h"
#include "learning_model.h"

// Board-owned learning UI. All UI and model mutations run on Application's task.
// A single bounded worker feeds local Opus packets, never blocks the app/LVGL task.
class LearningUi {
public:
    using Page = learning::Page;
    using Button = learning::Button;
    using Press = learning::Press;
    explicit LearningUi(Display* display);
    void Show(Page entry = Page::kHome);
    void Hide();
    bool chat_mode() const { return chat_mode_; }
    void ReturnToLearning();
    std::string ControlByVoice(const std::string& action, int index);
    bool active() const { return active_; }
    Page page() const { return model_.page; }
    void HandleButton(Button button, Press press);
    std::string GetCurrentCardJson() const;

private:
    void Render();
    void CancelAudio();
    void Speak(int clip);
    void AudioWorker();
    void Tick();
    void SavePosition();
    static void StateTimerCallback(lv_timer_t* timer);
    Display* display_;
    learning::Model model_;
    lv_obj_t* chat_screen_ = nullptr;
    lv_obj_t* screen_ = nullptr;
    bool active_ = false, speaking_ = false, dirty_ = false;
    bool voice_control_ = false;
    int pending_local_clip_ = -1;
    bool chat_mode_ = false, return_requested_ = false, abort_sent_ = false, close_sent_ = false;
    int last_volume_ = -1, last_voice_state_ = -1;
    int64_t save_after_ = 0;
    std::atomic<bool> poll_pending_{false};
    // Worker never calls LVGL or Schedule; latest request replaces pending audio.
    std::mutex audio_mutex_;
    TaskHandle_t audio_task_ = nullptr;
    uint32_t generation_ = 0;
    int pending_clip_ = -1;
    std::atomic<bool> feeding_{false};
};
