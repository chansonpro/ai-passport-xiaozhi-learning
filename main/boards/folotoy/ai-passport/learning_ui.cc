#include "learning_ui.h"

#include "application.h"
#include "learning_english_words.h"
#include "learning_tangshi_data.h"

#include <esp_log.h>
#include <esp_random.h>

#include <cstdio>
#include <string>

#define TAG "LearningUi"

// 学习界面专用字体（由 tools/gen_learning_font.py 生成）
LV_FONT_DECLARE(learning_font_16);
LV_FONT_DECLARE(learning_font_en_28);

namespace {

// 儿童友好的扁平配色
constexpr uint32_t kColorBgHex = 0xFFF6E5;       // 奶油底
constexpr uint32_t kColorTitleHex = 0xE85D24;    // 橙红标题
constexpr uint32_t kColorTextHex = 0x333333;     // 深灰正文
constexpr uint32_t kColorHintHex = 0x999999;     // 浅灰提示
constexpr uint32_t kColorCardAHex = 0xFF9800;    // 橙卡片
constexpr uint32_t kColorCardBHex = 0x42A5F5;    // 蓝卡片
constexpr uint32_t kColorCardCHex = 0x66BB6A;    // 绿卡片
constexpr uint32_t kColorWhiteHex = 0xFFFFFF;    // 白
constexpr uint32_t kColorCardBgHex = 0xFFFFFF;   // 卡片纸面

constexpr int kScreenWidth = 240;
constexpr int kScreenHeight = 320;

const char* const kHomeMenuText[3] = {"唐诗乐园", "英语角", "和小智聊天"};

void ApplyBaseStyle(lv_obj_t* obj, lv_color_t bg) {
    lv_obj_set_style_bg_color(obj, bg, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

lv_obj_t* CreateLabel(lv_obj_t* parent, const lv_font_t* font, lv_color_t color,
                      lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

}  // namespace

LearningUi::LearningUi(Display* display) : display_(display) {}

void LearningUi::Show(Page entry) {
    DisplayLockGuard lock(display_);
    if (!lock) {
        return;
    }
    if (!built_) {
        BuildScreens();
        built_ = true;
    }
    if (!active_) {
        lv_screen_load(screen_);
        active_ = true;
    }
    SwitchPage(entry);
    ESP_LOGI(TAG, "Learning UI shown (page=%d)", static_cast<int>(entry));
}

void LearningUi::Hide() {
    if (!active_) {
        return;
    }
    DisplayLockGuard lock(display_);
    if (!lock) {
        return;
    }
    if (chat_screen_ != nullptr) {
        lv_screen_load(chat_screen_);
    }
    active_ = false;
    page_ = Page::kNone;
    ESP_LOGI(TAG, "Learning UI hidden");
}

void LearningUi::BuildScreens() {
    // 记录应用聊天主屏，便于退出时切回
    chat_screen_ = lv_screen_active();

    screen_ = lv_obj_create(nullptr);
    ApplyBaseStyle(screen_, lv_color_hex(kColorBgHex));

    BuildHomePage();
    BuildPoemPage();
    BuildWordPage();

    // 轮询设备状态：对话开始（离开 Idle）时自动隐藏学习界面。
    // 回调内部检查 active_，空闲时开销可忽略。
    state_timer_ = lv_timer_create(StateTimerCallback, 500, this);
}

void LearningUi::BuildHomePage() {
    home_page_ = lv_obj_create(screen_);
    ApplyBaseStyle(home_page_, lv_color_hex(kColorBgHex));
    lv_obj_set_size(home_page_, kScreenWidth, kScreenHeight);
    lv_obj_set_align(home_page_, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(home_page_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(home_page_, 20, 0);
    lv_obj_set_style_pad_top(home_page_, 28, 0);
    lv_obj_set_style_pad_row(home_page_, 14, 0);

    lv_obj_t* title = CreateLabel(home_page_, &learning_font_16, lv_color_hex(kColorTitleHex),
                                  LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(title, "小智学堂");
    lv_obj_set_width(title, kScreenWidth - 40);

    const lv_color_t card_colors[3] = {lv_color_hex(kColorCardAHex), lv_color_hex(kColorCardBHex), lv_color_hex(kColorCardCHex)};
    for (int i = 0; i < 3; i++) {
        lv_obj_t* card = lv_obj_create(home_page_);
        ApplyBaseStyle(card, card_colors[i]);
        lv_obj_set_size(card, kScreenWidth - 40, 64);
        lv_obj_set_style_radius(card, 16, 0);
        lv_obj_t* text = CreateLabel(card, &learning_font_16, lv_color_hex(kColorWhiteHex),
                                     LV_TEXT_ALIGN_CENTER);
        lv_label_set_text(text, kHomeMenuText[i]);
        lv_obj_center(text);
        lv_obj_add_flag(card, LV_OBJ_FLAG_EVENT_BUBBLE);
        home_items_[i] = card;
    }
}

void LearningUi::BuildPoemPage() {
    poem_page_ = lv_obj_create(screen_);
    ApplyBaseStyle(poem_page_, lv_color_hex(kColorBgHex));
    lv_obj_set_size(poem_page_, kScreenWidth, kScreenHeight);
    lv_obj_set_align(poem_page_, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(poem_page_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(poem_page_, 16, 0);
    lv_obj_set_style_pad_top(poem_page_, 16, 0);
    lv_obj_set_style_pad_bottom(poem_page_, 8, 0);
    lv_obj_set_style_pad_row(poem_page_, 8, 0);

    poem_title_ = CreateLabel(poem_page_, &learning_font_16, lv_color_hex(kColorTitleHex),
                              LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(poem_title_, kScreenWidth - 32);

    poem_author_ = CreateLabel(poem_page_, &learning_font_16, lv_color_hex(kColorHintHex),
                               LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(poem_author_, kScreenWidth - 32);

    // 诗文的可滚动区域
    lv_obj_t* scroll_body = lv_obj_create(poem_page_);
    ApplyBaseStyle(scroll_body, lv_color_hex(kColorCardBgHex));
    lv_obj_set_size(scroll_body, kScreenWidth - 32, 212);
    lv_obj_set_style_radius(scroll_body, 12, 0);
    lv_obj_set_style_pad_all(scroll_body, 10, 0);
    lv_obj_set_scroll_dir(scroll_body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scroll_body, LV_SCROLLBAR_MODE_AUTO);

    poem_body_ = CreateLabel(scroll_body, &learning_font_16, lv_color_hex(kColorTextHex),
                             LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(poem_body_, kScreenWidth - 52);
    lv_obj_set_style_text_line_space(poem_body_, 6, 0);
    lv_label_set_text(poem_body_, "");

    poem_page_hint_ = CreateLabel(poem_page_, &learning_font_16, lv_color_hex(kColorHintHex),
                                  LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(poem_page_hint_, kScreenWidth - 32);
    lv_label_set_text(poem_page_hint_, "中间键问小智 长按返回");
}

void LearningUi::BuildWordPage() {
    word_page_ = lv_obj_create(screen_);
    ApplyBaseStyle(word_page_, lv_color_hex(kColorBgHex));
    lv_obj_set_size(word_page_, kScreenWidth, kScreenHeight);
    lv_obj_set_align(word_page_, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(word_page_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(word_page_, 16, 0);
    lv_obj_set_style_pad_top(word_page_, 24, 0);
    lv_obj_set_style_pad_row(word_page_, 10, 0);

    word_theme_ = CreateLabel(word_page_, &learning_font_16, lv_color_hex(kColorHintHex),
                              LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(word_theme_, kScreenWidth - 32);

    // 单词卡片
    lv_obj_t* card = lv_obj_create(word_page_);
    ApplyBaseStyle(card, lv_color_hex(kColorCardBgHex));
    lv_obj_set_size(card, kScreenWidth - 32, 190);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_top(card, 24, 0);
    lv_obj_set_style_pad_row(card, 12, 0);

    word_text_ = CreateLabel(card, &learning_font_en_28, lv_color_hex(kColorTitleHex),
                             LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(word_text_, kScreenWidth - 48);

    word_phonetic_ = CreateLabel(card, &learning_font_en_28, lv_color_hex(kColorHintHex),
                                 LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(word_phonetic_, kScreenWidth - 48);

    word_zh_ = CreateLabel(card, &learning_font_16, lv_color_hex(kColorTextHex),
                           LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(word_zh_, kScreenWidth - 48);

    word_index_label_ = CreateLabel(word_page_, &learning_font_16, lv_color_hex(kColorHintHex),
                                    LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(word_index_label_, kScreenWidth - 32);

    lv_obj_t* hint = CreateLabel(word_page_, &learning_font_16, lv_color_hex(kColorHintHex),
                                 LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(hint, kScreenWidth - 32);
    lv_label_set_text(hint, "中间键练发音 长按返回");
}

void LearningUi::SwitchPage(Page page) {
    if (!built_) {
        return;
    }
    lv_obj_add_flag(home_page_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(poem_page_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(word_page_, LV_OBJ_FLAG_HIDDEN);
    switch (page) {
        case Page::kHome:
            lv_obj_clear_flag(home_page_, LV_OBJ_FLAG_HIDDEN);
            break;
        case Page::kPoem:
        case Page::kRecall:
            lv_obj_clear_flag(poem_page_, LV_OBJ_FLAG_HIDDEN);
            break;
        case Page::kWord:
            lv_obj_clear_flag(word_page_, LV_OBJ_FLAG_HIDDEN);
            break;
        default:
            lv_obj_clear_flag(home_page_, LV_OBJ_FLAG_HIDDEN);
            page = Page::kHome;
            break;
    }
    page_ = page;
    if (page == Page::kHome) {
        RefreshHome();
    } else if (page == Page::kPoem || page == Page::kRecall) {
        if (page == Page::kRecall) {
            RandomRecallPoem();
        }
        RefreshPoem();
    } else if (page == Page::kWord) {
        RefreshWord();
    }
}

void LearningUi::RefreshHome() {
    for (int i = 0; i < 3; i++) {
        if (home_items_[i] == nullptr) {
            continue;
        }
        // 选中的卡片：加深加高亮边框；未选中：半透明
        if (i == home_selected_) {
            lv_obj_set_style_bg_opa(home_items_[i], LV_OPA_COVER, 0);
            lv_obj_set_style_outline_width(home_items_[i], 4, 0);
            lv_obj_set_style_outline_color(home_items_[i], lv_color_hex(kColorTextHex), 0);
            lv_obj_set_style_outline_pad(home_items_[i], 2, 0);
        } else {
            lv_obj_set_style_bg_opa(home_items_[i], LV_OPA_60, 0);
            lv_obj_set_style_outline_width(home_items_[i], 0, 0);
        }
    }
}

void LearningUi::RefreshPoem() {
    if (poem_title_ == nullptr) {
        return;
    }
    const tangshi_poem_t* poem = nullptr;
    int index = 0;
    int total = g_tangshi_poem_count;
    if (page_ == Page::kRecall) {
        index = recall_index_;
        poem = &g_tangshi_poems[recall_index_];
    } else {
        index = poem_index_;
        poem = &g_tangshi_poems[poem_index_];
    }
    if (poem == nullptr) {
        return;
    }
    lv_label_set_text(poem_title_, poem->title);
    char author[64];
    snprintf(author, sizeof(author), "%s · %d/%d", poem->author, index + 1, total);
    lv_label_set_text(poem_author_, author);
    if (page_ == Page::kRecall && !recall_revealed_) {
        lv_label_set_text(poem_body_, "想一想，\n按中间键揭晓");
        lv_obj_set_style_text_color(poem_body_, lv_color_hex(kColorHintHex), 0);
        lv_label_set_text(poem_page_hint_, "中间键揭晓 长按返回");
    } else {
        lv_label_set_text(poem_body_, poem->text);
        lv_obj_set_style_text_color(poem_body_, lv_color_hex(kColorTextHex), 0);
        lv_label_set_text(poem_page_hint_, "中间键问小智 长按返回");
    }
}

void LearningUi::RefreshWord() {
    if (word_text_ == nullptr || word_index_ >= (int)g_english_word_count) {
        return;
    }
    const english_word_t& word = g_english_words[word_index_];
    lv_label_set_text(word_theme_, word.theme);
    lv_label_set_text(word_text_, word.word);
    char phonetic[48];
    snprintf(phonetic, sizeof(phonetic), "/%s/", word.phonetic);
    lv_label_set_text(word_phonetic_, phonetic);
    lv_label_set_text(word_zh_, word.zh);
    char index[32];
    snprintf(index, sizeof(index), "%d / %d", word_index_ + 1,
             (int)g_english_word_count);
    lv_label_set_text(word_index_label_, index);
}

void LearningUi::NextPoem(int delta) {
    int total = (int)g_tangshi_poem_count;
    poem_index_ = (poem_index_ + delta + total) % total;
    RefreshPoem();
}

void LearningUi::RandomRecallPoem() {
    recall_index_ = (int)(esp_random() % g_tangshi_poem_count);
    recall_revealed_ = false;
}

void LearningUi::NextWord(int delta) {
    word_index_ = (word_index_ + delta + (int)g_english_word_count) % (int)g_english_word_count;
    RefreshWord();
}

void LearningUi::StartChat() {
    // 隐藏学习界面并触发对话；MCP 工具会让小智读到当前卡片
    Hide();
    auto& app = Application::GetInstance();
    if (app.GetDeviceState() == kDeviceStateIdle) {
        app.ToggleChatState();
    }
}

void LearningUi::HandleButton(Button button, Press press) {
    if (!active_) {
        return;
    }
    // 主任务上下文操作 LVGL，需持显示锁（递归锁，StartChat→Hide 内部可重入）
    DisplayLockGuard lock(display_);
    if (!lock) {
        return;
    }
    switch (page_) {
        case Page::kHome:
            if (press == Press::kClick) {
                if (button == Button::kUp) {
                    home_selected_ = (home_selected_ + 2) % 3;
                    RefreshHome();
                } else if (button == Button::kDown) {
                    home_selected_ = (home_selected_ + 1) % 3;
                    RefreshHome();
                } else {  // OK
                    if (home_selected_ == 0) {
                        SwitchPage(Page::kPoem);
                    } else if (home_selected_ == 1) {
                        SwitchPage(Page::kWord);
                    } else {
                        StartChat();
                    }
                }
            } else if (press == Press::kDoubleClick) {
                if (home_selected_ == 0) {
                    // 彩蛋：唐诗乐园双击进入背诵挑战
                    SwitchPage(Page::kRecall);
                }
            }
            break;
        case Page::kPoem:
            if (button == Button::kOk) {
                if (press == Press::kClick) {
                    StartChat();
                } else if (press == Press::kLongPress) {
                    SwitchPage(Page::kHome);
                }
            } else if (press == Press::kClick) {
                // 单击上下键滚动诗文
                lv_obj_scroll_by(lv_obj_get_parent(poem_body_),
                                 0, button == Button::kUp ? -60 : 60, LV_ANIM_ON);
            } else if (press == Press::kDoubleClick) {
                NextPoem(button == Button::kUp ? -1 : 1);
            }
            break;
        case Page::kRecall:
            if (button == Button::kOk) {
                if (press == Press::kClick) {
                    if (!recall_revealed_) {
                        recall_revealed_ = true;
                        RefreshPoem();
                    } else {
                        StartChat();
                    }
                } else if (press == Press::kLongPress) {
                    SwitchPage(Page::kHome);
                }
            } else if (press == Press::kClick) {
                // 换一首随机诗
                RandomRecallPoem();
                RefreshPoem();
            }
            break;
        case Page::kWord:
            if (button == Button::kOk) {
                if (press == Press::kClick) {
                    StartChat();
                } else if (press == Press::kLongPress) {
                    SwitchPage(Page::kHome);
                }
            } else if (press == Press::kClick) {
                NextWord(button == Button::kUp ? -1 : 1);
            }
            break;
        default:
            break;
    }
}

std::string LearningUi::GetCurrentCardJson() const {
    char buf[1024];
    switch (page_) {
        case Page::kPoem: {
            const tangshi_poem_t& poem = g_tangshi_poems[poem_index_];
            snprintf(buf, sizeof(buf),
                     "{\"page\":\"poem\",\"index\":%d,\"total\":%d,"
                     "\"title\":\"%s\",\"author\":\"%s\",\"text\":\"%s\"}",
                     poem_index_ + 1, (int)g_tangshi_poem_count, poem.title,
                     poem.author, poem.text);
            break;
        }
        case Page::kRecall: {
            const tangshi_poem_t& poem = g_tangshi_poems[recall_index_];
            snprintf(buf, sizeof(buf),
                     "{\"page\":\"poem_recite_challenge\",\"index\":%d,"
                     "\"total\":%d,\"title\":\"%s\",\"author\":\"%s\","
                     "\"revealed\":%s,\"text\":\"%s\"}",
                     recall_index_ + 1, (int)g_tangshi_poem_count, poem.title,
                     poem.author, recall_revealed_ ? "true" : "false",
                     recall_revealed_ ? poem.text : "");
            break;
        }
        case Page::kWord: {
            const english_word_t& word = g_english_words[word_index_];
            snprintf(buf, sizeof(buf),
                     "{\"page\":\"word\",\"index\":%d,\"total\":%d,"
                     "\"word\":\"%s\",\"phonetic\":\"%s\",\"zh\":\"%s\","
                     "\"theme\":\"%s\"}",
                     word_index_ + 1, (int)g_english_word_count, word.word,
                     word.phonetic, word.zh, word.theme);
            break;
        }
        case Page::kHome:
            snprintf(buf, sizeof(buf), "{\"page\":\"home\"}");
            break;
        default:
            snprintf(buf, sizeof(buf), "{\"page\":\"none\"}");
            break;
    }
    return std::string(buf);
}

void LearningUi::StateTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<LearningUi*>(lv_timer_get_user_data(timer));
    if (self == nullptr || !self->active_) {
        return;
    }
    // 设备离开 Idle（开始对话等）时，自动让位给聊天界面
    if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle) {
        self->Hide();
    }
}
