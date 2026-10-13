#pragma once
#include <lvgl.h>
#include "learning_model.h"
namespace learning {
// Caller owns display lock. The small scene is rebuilt only on input / playback end.
void Render(lv_obj_t* screen, const Model& model, int volume, bool speaking,
            const char* assistant_status = nullptr);
}  // namespace learning
