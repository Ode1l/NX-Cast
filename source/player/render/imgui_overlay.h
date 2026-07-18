#pragma once

#include <stdbool.h>

#include "player/render/internal.h"

#ifdef __cplusplus
extern "C" {
#endif

bool frontend_imgui_overlay_init(ViewContext *ctx);
void frontend_imgui_overlay_shutdown(void);
bool frontend_imgui_home_render(ViewContext *ctx, int slot);
bool frontend_imgui_loading_render(ViewContext *ctx, int slot);
bool frontend_imgui_overlay_render(ViewContext *ctx, int slot);

#ifdef __cplusplus
}
#endif
