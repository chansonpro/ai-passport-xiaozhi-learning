#pragma once
#include <lvgl.h>
#include "learning_model.h"
namespace learning {
// Caller owns the display lock and screen lifetime. Do not externally clean a
// screen while its Renderer is in use. Model changes rebuild the small scene;
// playback, volume and assistant status changes update existing labels.
class Renderer {
public:
    void Render(lv_obj_t* screen, const Model& model, int volume, bool speaking,
                const char* assistant_status = nullptr);

private:
    void Rebuild(lv_obj_t* screen, const Model& model, int volume, bool speaking,
                 const char* assistant_status);
    void UpdateStatus(const Model& model, int volume, bool speaking, const char* assistant_status);
    lv_obj_t* screen_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* volume_ = nullptr;
    lv_obj_t* phase_ = nullptr;
    lv_obj_t* home_hint_ = nullptr;
    Model model_;
};

// One-shot full render for independent previews and reference comparisons.
void Render(lv_obj_t* screen, const Model& model, int volume, bool speaking,
            const char* assistant_status = nullptr);
}  // namespace learning
