#ifndef LEARNING_UI_H
#define LEARNING_UI_H

#include "display/display.h"

#include <lvgl.h>
#include <string>

// AI Passport 儿童学习界面（小智学堂）：
//   首页菜单 → 唐诗乐园 / 英语角 / 和小智聊天
// 仅在设备 Idle 状态显示；开始对话时自动隐藏回到聊天屏。
// 所有方法只允许在主任务（Application::Schedule）上下文调用，
// 状态轮询定时器运行在 LVGL 任务（持递归显示锁，安全）。

class LearningUi {
public:
    enum class Page { kNone, kHome, kPoem, kRecall, kWord };
    enum class Button { kUp, kDown, kOk };
    enum class Press { kClick, kLongPress, kDoubleClick };

    explicit LearningUi(Display* display);

    // 显示学习界面（幂等）。entry 指定进入的页面。
    void Show(Page entry = Page::kHome);
    // 隐藏学习界面，回到聊天屏幕。
    void Hide();

    bool active() const { return active_; }
    Page page() const { return page_; }

    // 板级按键入口。学习界面激活时消费全部按键事件。
    void HandleButton(Button button, Press press);

    // 当前卡片 JSON 描述（供 MCP 工具 self.learning.get_current_card 使用）
    std::string GetCurrentCardJson() const;

private:
    void BuildScreens();      // 惰性构建 LVGL 对象（需已持有显示锁）
    void BuildHomePage();
    void BuildPoemPage();
    void BuildWordPage();
    void SwitchPage(Page page);
    void RefreshHome();
    void RefreshPoem();
    void RefreshWord();
    void NextPoem(int delta);
    void RandomRecallPoem();
    void NextWord(int delta);
    void StartChat();  // 隐藏学习界面并开始与小智对话
    static void StateTimerCallback(lv_timer_t* timer);

    Display* display_;
    lv_obj_t* chat_screen_ = nullptr;  // 应用聊天主屏（SetupUI 默认屏）
    lv_obj_t* screen_ = nullptr;       // 学习屏
    lv_obj_t* home_page_ = nullptr;
    lv_obj_t* poem_page_ = nullptr;
    lv_obj_t* word_page_ = nullptr;
    lv_timer_t* state_timer_ = nullptr;
    bool built_ = false;

    // 首页菜单状态
    int home_selected_ = 0;

    // 唐诗页状态
    int poem_index_ = 0;
    // 背诵挑战页状态
    int recall_index_ = 0;
    bool recall_revealed_ = false;

    // 单词页状态
    int word_index_ = 0;

    Page page_ = Page::kNone;
    bool active_ = false;

    // 首页控件
    lv_obj_t* home_items_[3] = {nullptr, nullptr, nullptr};

    // 唐诗/背诵页控件
    lv_obj_t* poem_title_ = nullptr;
    lv_obj_t* poem_author_ = nullptr;
    lv_obj_t* poem_body_ = nullptr;
    lv_obj_t* poem_page_hint_ = nullptr;

    // 单词页控件
    lv_obj_t* word_theme_ = nullptr;
    lv_obj_t* word_text_ = nullptr;
    lv_obj_t* word_phonetic_ = nullptr;
    lv_obj_t* word_zh_ = nullptr;
    lv_obj_t* word_index_label_ = nullptr;
};

#endif  // LEARNING_UI_H
