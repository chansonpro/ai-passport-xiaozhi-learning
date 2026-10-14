#include "learning_render.h"
#include <cstdio>
#include "learning_media.h"
LV_FONT_DECLARE(learning_v1_12);
LV_FONT_DECLARE(learning_v1_16);
LV_FONT_DECLARE(learning_v1_22);
LV_FONT_DECLARE(learning_v1_34);
namespace learning {
namespace {
constexpr uint32_t kBg = 0xfff9ec, kInk = 0x30413b, kMuted = 0x74847a, kGreen = 0x386b50;
lv_obj_t* Box(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color, int radius = 0,
              bool selected = false) {
    auto o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_border_width(o, selected ? 3 : 0, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(kGreen), 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
void Text(lv_obj_t* parent, const char* text, int x, int y, int width,
          const lv_font_t* font = &learning_v1_16, uint32_t color = kInk) {
    auto o = lv_label_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_width(o, width);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(o, text);
}
void Art(lv_obj_t* parent, int index, int x, int y, int size) {
    auto o = lv_image_create(parent);
    lv_image_set_src(o, &kArt[index][size == 96 ? 0 : size == 76 ? 1 : size == 64 ? 2 : 3]);
    lv_obj_set_size(o, size, size);
    lv_obj_set_pos(o, x, y);
}
void Pill(lv_obj_t* screen, const char* text, int y) {
    auto p = Box(screen, 20, y, 200, 29, 0xe2ecd8, 15);
    Text(p, text, 0, 6, 200, &learning_v1_12, 0x43694a);
}
}  // namespace
void Render(lv_obj_t* screen, const Model& m, int volume, bool speaking,
            const char* assistant_status) {
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(kBg), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    Text(screen, assistant_status ? assistant_status : "小智学堂", 14, 8, 64, &learning_v1_12,
         kMuted);
    char buf[96];
    snprintf(buf, sizeof(buf), "音量 %d%%", volume);
    Text(screen, buf, 148, 8, 80, &learning_v1_12, kMuted);
    const char* hint = "中间进入 · 长按聊天";
    const char* phase = speaking ? "听一听…" : m.message;
    if (m.page == Page::kHome) {
        Text(screen, "每天一点点，学会好多点", 12, 37, 216, &learning_v1_12, kMuted);
        Text(screen, "今天学英语！", 12, 59, 216, &learning_v1_22);
        auto a = Box(screen, 16, 108, 208, 96, 0xe6eddc, 17);
        Art(a, 0, 32, 24, 48);
        Text(a, "英语乐园", 92, 37, 100, &learning_v1_22);
        Text(a, "听一听 · 认一认 · 找一找", 20, 68, 168, &learning_v1_12, kMuted);
        Text(screen, assistant_status ? "长按中间：结束对话" : "长按中间：和小智聊天", 8, 260, 224,
             &learning_v1_12, kGreen);
    } else if (m.page == Page::kWord) {
        snprintf(buf, sizeof(buf), "英语乐园 · %d / 4", m.word + 1);
        Text(screen, buf, 12, 36, 216, &learning_v1_12, kMuted);
        Art(screen, m.word, 72, 58, 96);
        Text(screen, kWords[m.word], 8, 162, 224, &learning_v1_34);
        Text(screen, kChinese[m.word], 12, 207, 216, &learning_v1_16, 0x6c7f6c);
        Pill(screen, *phase ? phase : "按中间，听一听", 239);
        hint = m.word == 3 ? "下一项：听音找图 · 长按返回" : "上下换词 · 中间听 · 长按返回";
    } else if (m.page == Page::kQuiz) {
        snprintf(buf, sizeof(buf), "听音找图 · %d / 4", m.round + 1);
        Text(screen, buf, 12, 36, 216, &learning_v1_12, kMuted);
        Text(screen, m.answered ? "找对啦！" : "哪个是它？", 12, 61, 216, &learning_v1_22);
        for (int i = 0; i < 2; ++i) {
            auto card = Box(screen, 16 + i * 108, 114, 100, 108, 0xf0eedc, 18, m.choice == i);
            Art(card, m.Option(i), 9, 12, 76);
        }
        Pill(screen, *phase ? phase : "中间键，听题目", 241);
        hint = m.answered ? "中间继续 · 长按返回" : "上下选图 · 中间确认 · 长按返回";
    }
    auto footer = Box(screen, 0, 286, 240, 34, 0xf0eddf);
    Text(footer, hint, 0, 10, 240, &learning_v1_12, 0x64756b);
}
}  // namespace learning
