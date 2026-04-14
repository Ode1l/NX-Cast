#include "player/render/internal.h"

#include <stdint.h>
#include <string.h>

#if defined(HAVE_SWITCH_EGL_GLES) && !defined(HAVE_MPV_RENDER_DK3D)
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#endif

#include "log/log.h"
#include "player/player.h"

#define VIEW_DEFAULT_WIDTH 1280
#define VIEW_DEFAULT_HEIGHT 720
#define DK3D_FRAMEBUFFER_COUNT 2

static void frontend_query_display_size(u32 *out_width, u32 *out_height);

#if defined(HAVE_SWITCH_EGL_GLES) && !defined(HAVE_MPV_RENDER_DK3D)
static void *frontend_gl_get_proc_address(void *ctx, const char *name)
{
    (void)ctx;
    return (void *)eglGetProcAddress(name);
}

static void frontend_gl_reset(ViewContext *ctx)
{
    if (!ctx)
        return;
    ctx->egl_display = EGL_NO_DISPLAY;
    ctx->egl_surface = EGL_NO_SURFACE;
    ctx->egl_context = EGL_NO_CONTEXT;
}

static bool frontend_gl_init(ViewContext *ctx)
{
    EGLConfig config;
    EGLint num_configs = 0;
    static const EGLint framebuffer_attribute_list[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    static const EGLint context_attribute_list[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    frontend_gl_reset(ctx);
    ctx->egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (ctx->egl_display == EGL_NO_DISPLAY)
    {
        log_warn("[player-view] eglGetDisplay failed: 0x%04x\n", eglGetError());
        return false;
    }
    if (eglInitialize(ctx->egl_display, NULL, NULL) != EGL_TRUE)
    {
        log_warn("[player-view] eglInitialize failed: 0x%04x\n", eglGetError());
        goto fail;
    }
    if (eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE)
    {
        log_warn("[player-view] eglBindAPI(OpenGL ES) failed: 0x%04x\n", eglGetError());
        goto fail;
    }
    if (eglChooseConfig(ctx->egl_display, framebuffer_attribute_list, &config, 1, &num_configs) != EGL_TRUE ||
        num_configs == 0)
    {
        log_warn("[player-view] eglChooseConfig failed: 0x%04x\n", eglGetError());
        goto fail;
    }

    ctx->egl_surface = eglCreateWindowSurface(ctx->egl_display, config, nwindowGetDefault(), NULL);
    if (ctx->egl_surface == EGL_NO_SURFACE)
    {
        log_warn("[player-view] eglCreateWindowSurface failed: 0x%04x\n", eglGetError());
        goto fail;
    }

    ctx->egl_context = eglCreateContext(ctx->egl_display, config, EGL_NO_CONTEXT, context_attribute_list);
    if (ctx->egl_context == EGL_NO_CONTEXT)
    {
        log_warn("[player-view] eglCreateContext failed: 0x%04x\n", eglGetError());
        goto fail;
    }

    if (eglMakeCurrent(ctx->egl_display, ctx->egl_surface, ctx->egl_surface, ctx->egl_context) != EGL_TRUE)
    {
        log_warn("[player-view] eglMakeCurrent failed: 0x%04x\n", eglGetError());
        goto fail;
    }

    eglSwapInterval(ctx->egl_display, 1);
    return true;

fail:
    if (ctx->egl_display != EGL_NO_DISPLAY)
    {
        eglMakeCurrent(ctx->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (ctx->egl_context != EGL_NO_CONTEXT)
            eglDestroyContext(ctx->egl_display, ctx->egl_context);
        if (ctx->egl_surface != EGL_NO_SURFACE)
            eglDestroySurface(ctx->egl_display, ctx->egl_surface);
        eglTerminate(ctx->egl_display);
    }
    frontend_gl_reset(ctx);
    return false;
}

static void frontend_gl_deinit(ViewContext *ctx)
{
    if (!ctx || ctx->egl_display == EGL_NO_DISPLAY)
        return;

    eglMakeCurrent(ctx->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (ctx->egl_context != EGL_NO_CONTEXT)
        eglDestroyContext(ctx->egl_display, ctx->egl_context);
    if (ctx->egl_surface != EGL_NO_SURFACE)
        eglDestroySurface(ctx->egl_display, ctx->egl_surface);
    eglTerminate(ctx->egl_display);
    frontend_gl_reset(ctx);
}

static bool frontend_open_gl(ViewContext *ctx)
{
    u32 width = VIEW_DEFAULT_WIDTH;
    u32 height = VIEW_DEFAULT_HEIGHT;

    frontend_query_display_size(&width, &height);
    consoleExit(NULL);
    if (!frontend_gl_init(ctx))
    {
        consoleInit(NULL);
        return false;
    }
    if (!player_video_attach_gl(frontend_gl_get_proc_address, NULL))
    {
        frontend_gl_deinit(ctx);
        consoleInit(NULL);
        log_warn("[player-view] render_attach_gl rejected\n");
        return false;
    }

    glViewport(0, 0, (GLsizei)width, (GLsizei)height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(ctx->egl_display, ctx->egl_surface);

    ctx->status.render_api_connected = true;
    ctx->status.foreground_video_active = true;
    ctx->status.display_width = width;
    ctx->status.display_height = height;
    ctx->render_path = FRONTEND_RENDER_GL;
    log_info("[player-view] render_api_connected=1 backend=gl\n");
    return true;
}
#endif

#ifdef HAVE_MPV_RENDER_DK3D
static void frontend_dk3d_reset(ViewContext *ctx)
{
    if (!ctx)
        return;

    ctx->dk3d_device_ready = false;
    ctx->dk3d_swapchain_ready = false;
    ctx->dk3d_device = NULL;
    ctx->dk3d_queue = NULL;
    ctx->dk3d_framebuffer_mem = NULL;
    memset(ctx->dk3d_framebuffers, 0, sizeof(ctx->dk3d_framebuffers));
    ctx->dk3d_swapchain = NULL;
}

static void frontend_dk3d_destroy_swapchain(ViewContext *ctx)
{
    if (!ctx || !ctx->dk3d_swapchain_ready)
        return;

    if (ctx->dk3d_queue)
        dkQueueWaitIdle(ctx->dk3d_queue);
    if (ctx->dk3d_swapchain)
        dkSwapchainDestroy(ctx->dk3d_swapchain);
    if (ctx->dk3d_framebuffer_mem)
        dkMemBlockDestroy(ctx->dk3d_framebuffer_mem);

    ctx->dk3d_swapchain = NULL;
    ctx->dk3d_framebuffer_mem = NULL;
    memset(ctx->dk3d_framebuffers, 0, sizeof(ctx->dk3d_framebuffers));
    ctx->dk3d_swapchain_ready = false;
}

static void frontend_dk3d_shutdown(ViewContext *ctx)
{
    if (!ctx || !ctx->dk3d_device_ready)
        return;

    frontend_dk3d_destroy_swapchain(ctx);
    player_video_detach();
    if (ctx->dk3d_queue)
    {
        dkQueueWaitIdle(ctx->dk3d_queue);
        dkQueueDestroy(ctx->dk3d_queue);
    }
    if (ctx->dk3d_device)
        dkDeviceDestroy(ctx->dk3d_device);

    frontend_dk3d_reset(ctx);
}

static bool frontend_dk3d_connect(ViewContext *ctx)
{
    DkDeviceMaker device_maker;
    DkQueueMaker queue_maker;
    PlayerVideoDk3dInit init;

    if (!ctx)
        return false;
    if (ctx->dk3d_device_ready)
        return true;
    if (!player_video_supported())
        return false;

    dkDeviceMakerDefaults(&device_maker);
    ctx->dk3d_device = dkDeviceCreate(&device_maker);
    if (!ctx->dk3d_device)
    {
        log_error("[player-view] dkDeviceCreate failed\n");
        frontend_dk3d_reset(ctx);
        return false;
    }

    dkQueueMakerDefaults(&queue_maker, ctx->dk3d_device);
    queue_maker.flags = DkQueueFlags_Graphics;
    ctx->dk3d_queue = dkQueueCreate(&queue_maker);
    if (!ctx->dk3d_queue)
    {
        log_error("[player-view] dkQueueCreate failed\n");
        frontend_dk3d_shutdown(ctx);
        return false;
    }

    memset(&init, 0, sizeof(init));
    init.device = ctx->dk3d_device;
    if (!player_video_attach_dk3d(&init))
    {
        log_warn("[player-view] player_video_attach_dk3d rejected\n");
        frontend_dk3d_shutdown(ctx);
        return false;
    }

    ctx->dk3d_device_ready = true;
    log_info("[player-view] dk3d render context attached early\n");
    return true;
}

static bool frontend_dk3d_create_swapchain(ViewContext *ctx, u32 width, u32 height)
{
    DkImageLayoutMaker image_layout_maker;
    DkImageLayout framebuffer_layout;
    DkMemBlockMaker mem_block_maker;
    DkImage const *swapchain_images[DK3D_FRAMEBUFFER_COUNT];
    DkSwapchainMaker swapchain_maker;
    uint32_t framebuffer_size;
    uint32_t framebuffer_align;
    unsigned i;

    if (!ctx || !ctx->dk3d_device_ready)
        return false;
    if (ctx->dk3d_swapchain_ready)
        return true;

    dkImageLayoutMakerDefaults(&image_layout_maker, ctx->dk3d_device);
    image_layout_maker.flags = DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression;
    image_layout_maker.format = DkImageFormat_RGBA8_Unorm;
    image_layout_maker.dimensions[0] = width;
    image_layout_maker.dimensions[1] = height;
    dkImageLayoutInitialize(&framebuffer_layout, &image_layout_maker);

    framebuffer_size = (uint32_t)dkImageLayoutGetSize(&framebuffer_layout);
    framebuffer_align = dkImageLayoutGetAlignment(&framebuffer_layout);
    framebuffer_size = (framebuffer_size + framebuffer_align - 1U) & ~(framebuffer_align - 1U);

    dkMemBlockMakerDefaults(&mem_block_maker, ctx->dk3d_device, DK3D_FRAMEBUFFER_COUNT * framebuffer_size);
    mem_block_maker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    ctx->dk3d_framebuffer_mem = dkMemBlockCreate(&mem_block_maker);
    if (!ctx->dk3d_framebuffer_mem)
    {
        log_error("[player-view] dkMemBlockCreate failed for dk3d swapchain\n");
        return false;
    }

    for (i = 0; i < DK3D_FRAMEBUFFER_COUNT; ++i)
    {
        dkImageInitialize(&ctx->dk3d_framebuffers[i], &framebuffer_layout, ctx->dk3d_framebuffer_mem, i * framebuffer_size);
        swapchain_images[i] = &ctx->dk3d_framebuffers[i];
    }

    dkSwapchainMakerDefaults(&swapchain_maker, ctx->dk3d_device, nwindowGetDefault(), swapchain_images, DK3D_FRAMEBUFFER_COUNT);
    ctx->dk3d_swapchain = dkSwapchainCreate(&swapchain_maker);
    if (!ctx->dk3d_swapchain)
    {
        log_error("[player-view] dkSwapchainCreate failed\n");
        frontend_dk3d_destroy_swapchain(ctx);
        return false;
    }

    dkSwapchainSetSwapInterval(ctx->dk3d_swapchain, 1);
    ctx->dk3d_swapchain_ready = true;
    return true;
}

static bool frontend_open_dk3d(ViewContext *ctx)
{
    u32 width = VIEW_DEFAULT_WIDTH;
    u32 height = VIEW_DEFAULT_HEIGHT;

    frontend_query_display_size(&width, &height);
    if (!frontend_dk3d_connect(ctx))
        return false;

    consoleExit(NULL);
    if (!frontend_dk3d_create_swapchain(ctx, width, height))
    {
        consoleInit(NULL);
        return false;
    }

    ctx->status.render_api_connected = true;
    ctx->status.foreground_video_active = true;
    ctx->status.display_width = width;
    ctx->status.display_height = height;
    ctx->render_path = FRONTEND_RENDER_DK3D;
    log_info("[player-view] render_api_connected=1 backend=dk3d\n");
    return true;
}
#endif

static bool frontend_open_sw(ViewContext *ctx)
{
    u32 width = VIEW_DEFAULT_WIDTH;
    u32 height = VIEW_DEFAULT_HEIGHT;

    frontend_query_display_size(&width, &height);
    consoleExit(NULL);
    memset(&ctx->framebuffer, 0, sizeof(ctx->framebuffer));

    if (!player_video_attach_sw())
    {
        consoleInit(NULL);
        return false;
    }

    Result rc = framebufferCreate(&ctx->framebuffer,
                                  nwindowGetDefault(),
                                  width,
                                  height,
                                  PIXEL_FORMAT_RGBX_8888,
                                  2);
    if (R_FAILED(rc))
    {
        player_video_detach();
        consoleInit(NULL);
        log_error("[player-view] framebufferCreate failed: 0x%08X\n", rc);
        return false;
    }

    rc = framebufferMakeLinear(&ctx->framebuffer);
    if (R_FAILED(rc))
    {
        framebufferClose(&ctx->framebuffer);
        memset(&ctx->framebuffer, 0, sizeof(ctx->framebuffer));
        player_video_detach();
        consoleInit(NULL);
        log_error("[player-view] framebufferMakeLinear failed: 0x%08X\n", rc);
        return false;
    }

    ctx->framebuffer_ready = true;
    ctx->status.render_api_connected = true;
    ctx->status.foreground_video_active = true;
    ctx->status.display_width = width;
    ctx->status.display_height = height;
    ctx->render_path = FRONTEND_RENDER_SW;
    log_info("[player-view] render_api_connected=1 backend=sw\n");
    log_info("[player-view] framebuffer_ready=1 size=%ux%u\n", width, height);
    return true;
}

static void frontend_query_display_size(u32 *out_width, u32 *out_height)
{
    u32 width = VIEW_DEFAULT_WIDTH;
    u32 height = VIEW_DEFAULT_HEIGHT;

    NWindow *win = nwindowGetDefault();
    if (win)
    {
        u32 queried_width = 0;
        u32 queried_height = 0;
        if (R_SUCCEEDED(nwindowGetDimensions(win, &queried_width, &queried_height)) &&
            queried_width > 0 && queried_height > 0)
        {
            width = queried_width;
            height = queried_height;
        }
    }

    if (out_width)
        *out_width = width;
    if (out_height)
        *out_height = height;
}

bool frontend_connect(ViewContext *ctx)
{
    if (!ctx)
        return false;

#ifdef HAVE_MPV_RENDER_DK3D
    return frontend_dk3d_connect(ctx);
#else
    return player_video_supported();
#endif
}

bool frontend_open(ViewContext *ctx)
{
    if (!ctx)
        return false;
    if (ctx->status.foreground_video_active)
        return true;
    if (!frontend_connect(ctx))
        return false;

#ifdef HAVE_MPV_RENDER_DK3D
    if (frontend_open_dk3d(ctx))
        return true;
    log_warn("[player-view] dk3d path unavailable; fallback to next render\n");
#endif
#if defined(HAVE_SWITCH_EGL_GLES) && !defined(HAVE_MPV_RENDER_DK3D)
    if (frontend_open_gl(ctx))
        return true;
    log_warn("[player-view] gl path unavailable; fallback to software render\n");
#endif
    return frontend_open_sw(ctx);
}

void frontend_close(ViewContext *ctx, bool restore_console)
{
    bool had_video_foreground;
    bool restore_console_now;

    if (!ctx)
        return;

    had_video_foreground = ctx->status.foreground_video_active || ctx->framebuffer_ready;
    restore_console_now = restore_console && had_video_foreground;

    log_info("[player-view] frontend_close begin restore_console=%d had_video=%d render_path=%d render_api_connected=%d framebuffer_ready=%d\n",
             restore_console ? 1 : 0,
             had_video_foreground ? 1 : 0,
             (int)ctx->render_path,
             ctx->status.render_api_connected ? 1 : 0,
             ctx->framebuffer_ready ? 1 : 0);

    if (ctx->render_path == FRONTEND_RENDER_SW && ctx->status.render_api_connected)
    {
        log_info("[player-view] frontend_close step=player_video_detach begin\n");
        player_video_detach();
        log_info("[player-view] frontend_close step=player_video_detach done\n");
    }

#if defined(HAVE_SWITCH_EGL_GLES) && !defined(HAVE_MPV_RENDER_DK3D)
    if (ctx->render_path == FRONTEND_RENDER_GL)
    {
        log_info("[player-view] frontend_close step=player_video_detach begin\n");
        player_video_detach();
        log_info("[player-view] frontend_close step=player_video_detach done\n");
        log_info("[player-view] frontend_close step=frontend_gl_deinit begin\n");
        frontend_gl_deinit(ctx);
        log_info("[player-view] frontend_close step=frontend_gl_deinit done\n");
    }
#endif

#ifdef HAVE_MPV_RENDER_DK3D
    if (ctx->render_path == FRONTEND_RENDER_DK3D)
    {
        log_info("[player-view] frontend_close step=frontend_dk3d_destroy_swapchain begin\n");
        frontend_dk3d_destroy_swapchain(ctx);
        log_info("[player-view] frontend_close step=frontend_dk3d_destroy_swapchain done\n");
    }
#endif

    if (ctx->framebuffer_ready)
    {
        framebufferClose(&ctx->framebuffer);
        memset(&ctx->framebuffer, 0, sizeof(ctx->framebuffer));
        ctx->framebuffer_ready = false;
        log_info("[player-view] frontend_close framebuffer closed\n");
    }

    ctx->status.foreground_video_active = false;
    ctx->status.render_api_connected = false;
    ctx->status.display_width = 0;
    ctx->status.display_height = 0;
    ctx->render_path = FRONTEND_RENDER_NONE;

    if (restore_console_now)
    {
        log_info("[player-view] frontend_close step=console_restore begin\n");
        consoleInit(NULL);
        consoleClear();
        log_info("[player-view] frontend_close step=console_restore done\n");
    }
}

void frontend_shutdown(ViewContext *ctx)
{
#ifdef HAVE_MPV_RENDER_DK3D
    if (!ctx)
        return;
    frontend_dk3d_shutdown(ctx);
#else
    (void)ctx;
#endif
}

bool frontend_render(ViewContext *ctx)
{
    if (!ctx ||
        ctx->status.active_view != PLAYER_VIEW_VIDEO ||
        !ctx->status.foreground_video_active)
    {
        return false;
    }

#ifdef HAVE_MPV_RENDER_DK3D
    if (ctx->render_path == FRONTEND_RENDER_DK3D)
    {
        DkFence ready_fence = {0};
        DkFence done_fence = {0};
        PlayerVideoDk3dFrame frame = {0};
        int slot = -1;
        DkResult wait_result;
        bool rendered;

        if (!ctx->dk3d_swapchain_ready)
            return false;

        dkSwapchainAcquireImage(ctx->dk3d_swapchain, &slot, &ready_fence);
        if (slot < 0 || slot >= DK3D_FRAMEBUFFER_COUNT)
            return false;

        frame.image = &ctx->dk3d_framebuffers[slot];
        frame.ready_fence = &ready_fence;
        frame.done_fence = &done_fence;
        frame.width = (int)ctx->status.display_width;
        frame.height = (int)ctx->status.display_height;
        frame.format = DkImageFormat_RGBA8_Unorm;

        rendered = player_video_render_dk3d(&frame);
        if (!rendered)
            return false;

        wait_result = dkFenceWait(&done_fence, INT64_MAX);
        if (wait_result != DkResult_Success)
        {
            log_warn("[player-view] dkFenceWait failed: %d\n", (int)wait_result);
            return false;
        }

        dkQueuePresentImage(ctx->dk3d_queue, ctx->dk3d_swapchain, slot);
        ++ctx->status.frames_presented;
        return true;
    }
#endif

    if (ctx->render_path == FRONTEND_RENDER_GL)
    {
#if defined(HAVE_SWITCH_EGL_GLES) && !defined(HAVE_MPV_RENDER_DK3D)
        if (ctx->egl_display == EGL_NO_DISPLAY ||
            ctx->egl_surface == EGL_NO_SURFACE ||
            ctx->egl_context == EGL_NO_CONTEXT)
        {
            return false;
        }
        if (eglMakeCurrent(ctx->egl_display, ctx->egl_surface, ctx->egl_surface, ctx->egl_context) != EGL_TRUE)
            return false;

        glViewport(0, 0, (GLsizei)ctx->status.display_width, (GLsizei)ctx->status.display_height);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        bool rendered = player_video_render_gl(0,
                                               (int)ctx->status.display_width,
                                               (int)ctx->status.display_height,
                                               true);
        if (eglSwapBuffers(ctx->egl_display, ctx->egl_surface) != EGL_TRUE)
            return false;
        if (rendered)
            ++ctx->status.frames_presented;
        return rendered;
#else
        return false;
#endif
    }

    if (!ctx->framebuffer_ready)
        return false;

    u32 stride = 0;
    void *pixels = framebufferBegin(&ctx->framebuffer, &stride);
    if (!pixels)
        return false;

    bool rendered = player_video_render_sw(pixels,
                                           (int)ctx->status.display_width,
                                           (int)ctx->status.display_height,
                                           (size_t)stride);
    if (!rendered)
    {
        memset(pixels, 0, (size_t)stride * (size_t)ctx->status.display_height);
    }

    framebufferEnd(&ctx->framebuffer);
    if (rendered)
        ++ctx->status.frames_presented;
    return rendered;
}
