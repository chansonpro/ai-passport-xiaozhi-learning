#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "learning_render.h"
static uint16_t pixels[240 * 320];
static uint16_t buffer[240 * 40];
static void Flush(lv_display_t* d, const lv_area_t* a, uint8_t* p) {
    auto* src = reinterpret_cast<uint16_t*>(p);
    for (int y = a->y1; y <= a->y2; y++)
        for (int x = a->x1; x <= a->x2; x++)
            pixels[y * 240 + x] = *src++;
    lv_display_flush_ready(d);
}
int main(int argc, char** argv) {
    using namespace learning;
    Model m;
    if (argc > 1 && std::string(argv[1]) == "trace") {
        int button, press;
        while (std::cin >> button >> press) {
            int clip = m.Act(Button(button), Press(press));
            std::cout << int(m.page) << ' ' << m.word << ' ' << m.line << ' ' << m.selected << ' '
                      << m.round << ' ' << m.choice << ' ' << m.answered << ' ' << m.recall_mode
                      << ' ' << clip << '\n';
        }
        return 0;
    }
    lv_init();
    auto d = lv_display_create(240, 320);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, buffer, nullptr, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, Flush);
    auto save = [&](const char* name, const char* status = nullptr) {
        Render(lv_screen_active(), m, 50, false, status);
        lv_refr_now(d);
        std::ofstream file(std::string(argv[1]) + "/" + name + ".rgb565", std::ios::binary);
        file.write(reinterpret_cast<char*>(pixels), sizeof(pixels));
    };
    save("home");
    save("home-listening", "小智在听");
    m.Act(Button::kOk, Press::kClick);
    save("apple");
    for (int i = 0; i < 3; i++)
        m.Act(Button::kDown, Press::kClick);
    save("dog");
    m.Act(Button::kDown, Press::kClick);
    save("quiz");
    m.Act(Button::kOk, Press::kClick);
    save("correct");
    m.Act(Button::kOk, Press::kClick);
    save("quiz2");
    m.Act(Button::kOk, Press::kLongPress);
    m.Act(Button::kOk, Press::kLongPress);
    m.Act(Button::kDown, Press::kClick);
    m.Act(Button::kOk, Press::kClick);
    save("poem");
    for (int i = 0; i < 4; i++)
        m.Act(Button::kDown, Press::kClick);
    save("recall-button");
    m.Act(Button::kOk, Press::kClick);
    save("recall");
    for (int i = 0; i < 4; i++)
        m.Act(Button::kDown, Press::kClick);
    save("recall-end");
}
