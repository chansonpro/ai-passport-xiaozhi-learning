#include <cstdio>
#include <cstdlib>
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
static void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
static std::vector<lv_obj_t*> Objects(lv_obj_t* parent) {
    std::vector<lv_obj_t*> objects;
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
        auto* child = lv_obj_get_child(parent, i);
        objects.push_back(child);
        auto descendants = Objects(child);
        objects.insert(objects.end(), descendants.begin(), descendants.end());
    }
    return objects;
}
static std::vector<uint16_t> Capture(lv_display_t* display, lv_obj_t* screen) {
    lv_screen_load(screen);
    lv_refr_now(display);
    return {pixels, pixels + 240 * 320};
}
static void CheckRender(lv_display_t* display) {
    using namespace learning;
    auto* original = lv_screen_active();
    auto* incremental = lv_obj_create(nullptr);
    auto* reference = lv_obj_create(nullptr);
    Renderer renderer;
    Model model;
    std::vector<Model> scenes{model};
    model.Act(Button::kOk, Press::kClick);
    scenes.push_back(model);
    for (int i = 0; i < 4; ++i) {
        model.Act(Button::kDown, Press::kClick);
        scenes.push_back(model);
    }
    model.Act(Button::kOk, Press::kClick);
    scenes.push_back(model);
    model.Act(Button::kOk, Press::kClick);
    scenes.push_back(model);
    model.Act(Button::kDown, Press::kClick);
    scenes.push_back(model);
    model.Act(Button::kOk, Press::kLongPress);
    scenes.push_back(model);
    model.Act(Button::kOk, Press::kLongPress);
    scenes.push_back(model);
    struct Status {
        int volume;
        bool speaking;
        const char* assistant;
    };
    const Status updates[] = {{50, false, nullptr},     {0, true, nullptr},
                              {100, false, "小智在听"}, {70, true, "小智在说"},
                              {50, false, "连接小智"},  {50, false, nullptr}};
    int deletions = 0;
    size_t comparisons = 0;
    for (const auto& scene : scenes) {
        renderer.Render(incremental, scene, 50, false);
        auto objects = Objects(incremental);
        Require(!objects.empty(), "scene has no objects");
        for (auto* object : objects) {
            lv_obj_add_event_cb(
                object,
                [](lv_event_t* event) { ++*static_cast<int*>(lv_event_get_user_data(event)); },
                LV_EVENT_DELETE, &deletions);
        }
        deletions = 0;
        for (const auto& update : updates) {
            renderer.Render(incremental, scene, update.volume, update.speaking, update.assistant);
            Require(deletions == 0, "status update deleted scene objects");
            Require(Objects(incremental) == objects, "status update replaced scene objects");
            const auto actual = Capture(display, incremental);
            Render(reference, scene, update.volume, update.speaking, update.assistant);
            const auto expected = Capture(display, reference);
            Require(actual == expected, "incremental and full-render pixels differ");
            ++comparisons;
        }
    }
    lv_screen_load(original);
    lv_obj_delete(incremental);
    lv_obj_delete(reference);
    std::cout << "PASS: " << comparisons
              << " real-LVGL status updates preserve objects and match full-render pixels\n";
}
int main(int argc, char** argv) {
    using namespace learning;
    Model m;
    if (argc > 1 && std::string(argv[1]) == "trace") {
        int button, press;
        while (std::cin >> button >> press) {
            int clip = m.Act(Button(button), Press(press));
            std::cout << int(m.page) << ' ' << m.word << ' ' << m.round << ' ' << m.choice << ' '
                      << m.answered << ' ' << m.question_heard << ' ' << clip << '\n';
        }
        return 0;
    }
    if (argc != 2) {
        std::cerr << "Usage: learning_preview <output-directory|trace|--check-render>\n";
        return 1;
    }
    lv_init();
    auto d = lv_display_create(240, 320);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, buffer, nullptr, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, Flush);
    if (std::string(argv[1]) == "--check-render") {
        CheckRender(d);
        return 0;
    }
    Renderer renderer;
    auto save = [&](const char* name, const char* status = nullptr) {
        renderer.Render(lv_screen_active(), m, 50, false, status);
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
    save("return-word");
    m.Act(Button::kOk, Press::kLongPress);
    save("return-home");
}
