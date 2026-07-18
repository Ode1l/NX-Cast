#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <switch.h>

#ifdef HAVE_SWITCH_EGL_GLES
#include <EGL/egl.h>
#endif

#ifdef HAVE_MPV_RENDER_DK3D
#include <deko3d.h>
#endif

#include "player/view.h"

#define FRONTEND_DK3D_FRAMEBUFFER_COUNT 2

typedef enum
{
    FRONTEND_RENDER_NONE = 0,
    FRONTEND_RENDER_SW,
    FRONTEND_RENDER_GL,
    FRONTEND_RENDER_DK3D
} FrontendRenderPath;

typedef struct
{
    PlayerViewStatus status;
    Framebuffer framebuffer;
    bool framebuffer_ready;
    FrontendRenderPath render_path;
    uint64_t last_video_state_ms;
    uint64_t stop_hold_until_ms;
    bool home_override;
    uint32_t first_video_frame_trace_seq;
    PlayerHomeViewState home_state;
    bool home_state_valid;
#ifdef HAVE_SWITCH_EGL_GLES
    EGLDisplay egl_display;
    EGLSurface egl_surface;
    EGLContext egl_context;
#endif
#ifdef HAVE_MPV_RENDER_DK3D
    bool dk3d_device_ready;
    bool dk3d_swapchain_ready;
    DkDevice dk3d_device;
    DkQueue dk3d_queue;
    DkMemBlock dk3d_overlay_cmd_mem;
    DkCmdBuf dk3d_overlay_cmdbuf;
    DkMemBlock dk3d_framebuffer_mem;
    DkImage dk3d_framebuffers[FRONTEND_DK3D_FRAMEBUFFER_COUNT];
    DkSwapchain dk3d_swapchain;
    uint32_t dk3d_fence_timeout_count;
    bool dk3d_overlay_dirty;
#endif
} ViewContext;

void frontend_overlay_render_sw(void *pixels, size_t stride, u32 width, u32 height, const ViewContext *ctx);

#if defined(HAVE_SWITCH_EGL_GLES) && !defined(HAVE_MPV_RENDER_DK3D)
void frontend_overlay_render_gl(ViewContext *ctx);
#endif

#ifdef HAVE_MPV_RENDER_DK3D
void frontend_overlay_render_dk3d(ViewContext *ctx, int slot);
#endif

bool frontend_connect(ViewContext *ctx);
bool frontend_open_home(ViewContext *ctx);
bool frontend_open(ViewContext *ctx);
void frontend_close(ViewContext *ctx, bool restore_console);
void frontend_shutdown(ViewContext *ctx);
bool frontend_render(ViewContext *ctx);
