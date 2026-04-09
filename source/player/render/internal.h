#pragma once

#include <stdbool.h>

#include <switch.h>

#ifdef HAVE_SWITCH_EGL_GLES
#include <EGL/egl.h>
#endif

#include "player/view.h"

typedef enum
{
    FRONTEND_RENDER_NONE = 0,
    FRONTEND_RENDER_SW,
    FRONTEND_RENDER_GL
} FrontendRenderPath;

typedef struct
{
    PlayerViewStatus status;
    Framebuffer framebuffer;
    bool framebuffer_ready;
    FrontendRenderPath render_path;
    uint64_t last_video_state_ms;
    uint64_t stop_hold_until_ms;
#ifdef HAVE_SWITCH_EGL_GLES
    EGLDisplay egl_display;
    EGLSurface egl_surface;
    EGLContext egl_context;
#endif
} ViewContext;

bool frontend_connect(ViewContext *ctx);
bool frontend_open(ViewContext *ctx);
void frontend_close(ViewContext *ctx, bool restore_console);
bool frontend_render(ViewContext *ctx);
