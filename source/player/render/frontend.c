#include "player/render/internal.h"

#include <stdint.h>
#include <string.h>

#if defined(HAVE_SWITCH_EGL_GLES) && !defined(HAVE_MPV_RENDER_DK3D)
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#endif

#include "log/log.h"
#include "player/player.h"
#include "player/ui/overlay.h"

#define VIEW_DEFAULT_WIDTH 1280
#define VIEW_DEFAULT_HEIGHT 720
#define DK3D_FRAMEBUFFER_COUNT 2
#define DK3D_OVERLAY_CMDMEM_SIZE (128 * 1024)

typedef struct
{
    int x;
    int y;
    int width;
    int height;
    uint8_t gray;
} FrontendOverlayRect;

typedef void (*FrontendOverlayRectFillFn)(void *userdata, const FrontendOverlayRect *rect);

static void frontend_query_display_size(u32 *out_width, u32 *out_height);
static bool frontend_overlay_build_rects(const ViewContext *ctx, FrontendOverlayRect *rects, size_t rect_capacity, size_t *out_count);
static void frontend_overlay_render_sw(void *pixels, size_t stride, u32 width, u32 height, const ViewContext *ctx);
static void frontend_overlay_render_text_generic(const ViewContext *ctx, FrontendOverlayRectFillFn fill_rect, void *userdata);

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

static void frontend_gl_fill_rect(u32 display_height, const FrontendOverlayRect *rect)
{
    GLint y;
    float shade;

    if (!rect || rect->width <= 0 || rect->height <= 0)
        return;

    y = (GLint)display_height - (GLint)(rect->y + rect->height);
    shade = (float)rect->gray / 255.0f;
    glScissor(rect->x, y, rect->width, rect->height);
    glClearColor(shade, shade, shade, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

static void frontend_overlay_fill_rect_gl_cb(void *userdata, const FrontendOverlayRect *rect)
{
    u32 *display_height = (u32 *)userdata;

    if (!display_height)
        return;
    frontend_gl_fill_rect(*display_height, rect);
}

static void frontend_overlay_render_gl(ViewContext *ctx)
{
    FrontendOverlayRect rects[8];
    size_t rect_count = 0;
    size_t i;

    if (!ctx || !frontend_overlay_build_rects(ctx, rects, sizeof(rects) / sizeof(rects[0]), &rect_count) || rect_count == 0)
        return;

    glEnable(GL_SCISSOR_TEST);
    for (i = 0; i < rect_count; ++i)
        frontend_gl_fill_rect(ctx->status.display_height, &rects[i]);
    frontend_overlay_render_text_generic(ctx, frontend_overlay_fill_rect_gl_cb, &ctx->status.display_height);
    glDisable(GL_SCISSOR_TEST);
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
static bool frontend_dk3d_init_overlay(ViewContext *ctx)
{
    DkMemBlockMaker mem_block_maker;
    DkCmdBufMaker cmdbuf_maker;

    if (!ctx || ctx->dk3d_overlay_cmd_mem || ctx->dk3d_overlay_cmdbuf)
        return true;

    dkMemBlockMakerDefaults(&mem_block_maker, ctx->dk3d_device, DK3D_OVERLAY_CMDMEM_SIZE);
    mem_block_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    ctx->dk3d_overlay_cmd_mem = dkMemBlockCreate(&mem_block_maker);
    if (!ctx->dk3d_overlay_cmd_mem)
    {
        log_error("[player-view] dkMemBlockCreate failed for dk3d overlay commands\n");
        return false;
    }

    dkCmdBufMakerDefaults(&cmdbuf_maker, ctx->dk3d_device);
    ctx->dk3d_overlay_cmdbuf = dkCmdBufCreate(&cmdbuf_maker);
    if (!ctx->dk3d_overlay_cmdbuf)
    {
        log_error("[player-view] dkCmdBufCreate failed for dk3d overlay\n");
        dkMemBlockDestroy(ctx->dk3d_overlay_cmd_mem);
        ctx->dk3d_overlay_cmd_mem = NULL;
        return false;
    }

    dkCmdBufAddMemory(ctx->dk3d_overlay_cmdbuf, ctx->dk3d_overlay_cmd_mem, 0, DK3D_OVERLAY_CMDMEM_SIZE);
    return true;
}

static void frontend_dk3d_reset(ViewContext *ctx)
{
    if (!ctx)
        return;

    ctx->dk3d_device_ready = false;
    ctx->dk3d_swapchain_ready = false;
    ctx->dk3d_device = NULL;
    ctx->dk3d_queue = NULL;
    ctx->dk3d_overlay_cmd_mem = NULL;
    ctx->dk3d_overlay_cmdbuf = NULL;
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
        if (ctx->dk3d_overlay_cmdbuf)
            dkCmdBufDestroy(ctx->dk3d_overlay_cmdbuf);
        if (ctx->dk3d_overlay_cmd_mem)
            dkMemBlockDestroy(ctx->dk3d_overlay_cmd_mem);
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
    if (!frontend_dk3d_init_overlay(ctx))
    {
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

static void frontend_dk3d_fill_rect(DkCmdBuf cmdbuf, const FrontendOverlayRect *rect)
{
    DkScissor scissor;
    float shade;

    if (!rect || rect->width <= 0 || rect->height <= 0)
        return;

    scissor.x = (uint32_t)rect->x;
    scissor.y = (uint32_t)rect->y;
    scissor.width = (uint32_t)rect->width;
    scissor.height = (uint32_t)rect->height;
    shade = (float)rect->gray / 255.0f;
    dkCmdBufSetScissors(cmdbuf, 0, &scissor, 1);
    dkCmdBufClearColorFloat(cmdbuf, 0, DkColorMask_RGBA, shade, shade, shade, 1.0f);
}

static void frontend_overlay_fill_rect_dk3d_cb(void *userdata, const FrontendOverlayRect *rect)
{
    DkCmdBuf cmdbuf = userdata ? *(DkCmdBuf *)userdata : NULL;

    if (!cmdbuf)
        return;
    frontend_dk3d_fill_rect(cmdbuf, rect);
}

static void frontend_overlay_render_dk3d(ViewContext *ctx, int slot)
{
    FrontendOverlayRect rects[8];
    size_t rect_count = 0;
    size_t i;
    DkImageView image_view;
    DkViewport viewport;

    if (!ctx || !ctx->dk3d_overlay_cmdbuf || slot < 0 || slot >= DK3D_FRAMEBUFFER_COUNT)
        return;
    if (!frontend_overlay_build_rects(ctx, rects, sizeof(rects) / sizeof(rects[0]), &rect_count) || rect_count == 0)
        return;

    memset(&viewport, 0, sizeof(viewport));
    viewport.width = (float)ctx->status.display_width;
    viewport.height = (float)ctx->status.display_height;
    viewport.near = 0.0f;
    viewport.far = 1.0f;

    dkCmdBufClear(ctx->dk3d_overlay_cmdbuf);
    dkImageViewDefaults(&image_view, &ctx->dk3d_framebuffers[slot]);
    dkCmdBufBindRenderTarget(ctx->dk3d_overlay_cmdbuf, &image_view, NULL);
    dkCmdBufSetViewports(ctx->dk3d_overlay_cmdbuf, 0, &viewport, 1);

    for (i = 0; i < rect_count; ++i)
        frontend_dk3d_fill_rect(ctx->dk3d_overlay_cmdbuf, &rects[i]);
    frontend_overlay_render_text_generic(ctx, frontend_overlay_fill_rect_dk3d_cb, &ctx->dk3d_overlay_cmdbuf);

    dkQueueSubmitCommands(ctx->dk3d_queue, dkCmdBufFinishList(ctx->dk3d_overlay_cmdbuf));
}
#endif

static int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

static char frontend_font_normalize_char(char c)
{
    if (c >= 'a' && c <= 'z')
        return (char)(c - 'a' + 'A');
    return c;
}

static const uint8_t *frontend_font_glyph_rows(char c)
{
    static const uint8_t glyph_space[7] = {0, 0, 0, 0, 0, 0, 0};
    static const uint8_t glyph_plus[7] = {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00};
    static const uint8_t glyph_minus[7] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
    static const uint8_t glyph_dot[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06};
    static const uint8_t glyph_slash[7] = {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10};
    static const uint8_t glyph_colon[7] = {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00};
    static const uint8_t glyph_percent[7] = {0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13};
    static const uint8_t digits[10][7] = {
        {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
        {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
        {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
        {0x1E, 0x01, 0x01, 0x06, 0x01, 0x01, 0x1E},
        {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
        {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E},
        {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E},
        {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
        {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
        {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E},
    };
    static const uint8_t letters[26][7] = {
        {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
        {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
        {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},
        {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},
        {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F},
        {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
        {0x0E, 0x11, 0x10, 0x10, 0x13, 0x11, 0x0F},
        {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
        {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F},
        {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C},
        {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
        {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
        {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11},
        {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
        {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
        {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
        {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D},
        {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
        {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E},
        {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
        {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
        {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
        {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A},
        {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
        {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04},
        {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},
    };

    c = frontend_font_normalize_char(c);
    if (c >= '0' && c <= '9')
        return digits[c - '0'];
    if (c >= 'A' && c <= 'Z')
        return letters[c - 'A'];

    switch (c)
    {
    case '+':
        return glyph_plus;
    case '-':
        return glyph_minus;
    case '.':
        return glyph_dot;
    case '/':
        return glyph_slash;
    case ':':
        return glyph_colon;
    case '%':
        return glyph_percent;
    case ' ':
    default:
        return glyph_space;
    }
}

static int frontend_font_measure_text(const char *text, int scale)
{
    int width = 0;

    if (!text || scale <= 0)
        return 0;

    while (*text)
    {
        width += 6 * scale;
        ++text;
    }
    if (width > 0)
        width -= scale;
    return width;
}

static void frontend_overlay_draw_text(const char *text,
                                       int x,
                                       int y,
                                       int scale,
                                       int max_width,
                                       uint8_t gray,
                                       FrontendOverlayRectFillFn fill_rect,
                                       void *userdata)
{
    int pen_x = x;

    if (!text || !text[0] || scale <= 0 || max_width <= 0 || !fill_rect)
        return;

    while (*text)
    {
        const uint8_t *glyph = frontend_font_glyph_rows(*text);
        int glyph_width = 5 * scale;
        int row;

        if (pen_x + glyph_width > x + max_width)
            break;

        for (row = 0; row < 7; ++row)
        {
            uint8_t bits = glyph[row];
            int col = 0;

            while (col < 5)
            {
                int run_start;
                int run_length = 0;
                FrontendOverlayRect rect;

                while (col < 5 && ((bits >> (4 - col)) & 1U) == 0U)
                    ++col;
                if (col >= 5)
                    break;

                run_start = col;
                while (col < 5 && ((bits >> (4 - col)) & 1U) != 0U)
                {
                    ++run_length;
                    ++col;
                }

                rect.x = pen_x + run_start * scale;
                rect.y = y + row * scale;
                rect.width = run_length * scale;
                rect.height = scale;
                rect.gray = gray;
                fill_rect(userdata, &rect);
            }
        }

        pen_x += 6 * scale;
        ++text;
    }
}

static void frontend_overlay_push_rect(FrontendOverlayRect *rects, size_t rect_capacity, size_t *count, int x, int y, int width, int height, uint8_t gray)
{
    if (!rects || !count || *count >= rect_capacity || width <= 0 || height <= 0)
        return;

    rects[*count].x = x;
    rects[*count].y = y;
    rects[*count].width = width;
    rects[*count].height = height;
    rects[*count].gray = gray;
    ++(*count);
}

static bool frontend_overlay_build_rects(const ViewContext *ctx, FrontendOverlayRect *rects, size_t rect_capacity, size_t *out_count)
{
    PlayerUiOverlaySnapshot overlay;
    int width;
    int height;
    int pad_x;
    int pad_y;
    int bar_x;
    int bar_y;
    int bar_width;
    int bar_height;
    int inner_pad;
    int slot_y;
    int progress_height;
    int progress_y;
    int progress_width;
    int progress_fill_width;
    size_t count = 0;

    if (out_count)
        *out_count = 0;
    if (!ctx || !player_ui_overlay_get_snapshot(&overlay) || overlay.kind != PLAYER_UI_OVERLAY_BAR)
        return false;

    width = (int)ctx->status.display_width;
    height = (int)ctx->status.display_height;
    if (width <= 0 || height <= 0)
        return false;

    pad_x = clamp_int(width / 30, 24, 56);
    pad_y = clamp_int(height / 28, 18, 40);
    bar_x = pad_x;
    bar_width = width - pad_x * 2;
    bar_height = clamp_int(height / 9, 76, 118);
    bar_y = height - pad_y - bar_height;
    inner_pad = clamp_int(bar_height / 7, 8, 16);
    slot_y = bar_y + inner_pad;
    progress_height = clamp_int(bar_height / 10, 8, 14);
    progress_y = bar_y + bar_height - inner_pad - progress_height;
    progress_width = bar_width - inner_pad * 2;
    progress_fill_width = clamp_int((overlay.bar.progress_permille * progress_width + 500) / 1000, 0, progress_width);

    frontend_overlay_push_rect(rects, rect_capacity, &count, bar_x, bar_y, bar_width, bar_height, 28);
    frontend_overlay_push_rect(rects, rect_capacity, &count, bar_x + inner_pad, slot_y - 2, bar_width - inner_pad * 2, 1, 50);
    frontend_overlay_push_rect(rects, rect_capacity, &count, bar_x + inner_pad, progress_y, progress_width, progress_height, 58);
    if (progress_fill_width > 0)
        frontend_overlay_push_rect(rects, rect_capacity, &count, bar_x + inner_pad, progress_y, progress_fill_width, progress_height, 188);

    if (out_count)
        *out_count = count;
    return count > 0;
}

static void frontend_overlay_fill_rect_sw(void *pixels, size_t stride, u32 width, u32 height, const FrontendOverlayRect *rect)
{
    uint8_t *base = (uint8_t *)pixels;
    int x0;
    int y0;
    int x1;
    int y1;
    int y;

    if (!base || !rect || rect->width <= 0 || rect->height <= 0)
        return;

    x0 = clamp_int(rect->x, 0, (int)width);
    y0 = clamp_int(rect->y, 0, (int)height);
    x1 = clamp_int(rect->x + rect->width, 0, (int)width);
    y1 = clamp_int(rect->y + rect->height, 0, (int)height);
    if (x1 <= x0 || y1 <= y0)
        return;

    for (y = y0; y < y1; ++y)
    {
        uint8_t *row = base + (size_t)y * stride + (size_t)x0 * 4U;
        int x;

        for (x = x0; x < x1; ++x)
        {
            row[0] = rect->gray;
            row[1] = rect->gray;
            row[2] = rect->gray;
            row[3] = 0xFF;
            row += 4;
        }
    }
}

typedef struct
{
    void *pixels;
    size_t stride;
    u32 width;
    u32 height;
} FrontendOverlaySwTarget;

static void frontend_overlay_fill_rect_sw_cb(void *userdata, const FrontendOverlayRect *rect)
{
    FrontendOverlaySwTarget *target = (FrontendOverlaySwTarget *)userdata;

    if (!target)
        return;
    frontend_overlay_fill_rect_sw(target->pixels, target->stride, target->width, target->height, rect);
}

static void frontend_overlay_render_text_generic(const ViewContext *ctx, FrontendOverlayRectFillFn fill_rect, void *userdata)
{
    PlayerUiOverlaySnapshot overlay;
    int width;
    int height;
    int pad_x;
    int pad_y;
    int bar_x;
    int bar_y;
    int bar_width;
    int bar_height;
    int inner_pad;
    int title_scale;
    int slot_scale;
    int title_y;
    int slot_y;
    int title_max_width;
    int left_zone_width;
    int right_zone_width;
    int center_zone_x;
    int center_zone_width;
    int right_text_width;
    int center_text_width;

    if (!ctx || !fill_rect || !player_ui_overlay_get_snapshot(&overlay) || overlay.kind != PLAYER_UI_OVERLAY_BAR)
        return;

    width = (int)ctx->status.display_width;
    height = (int)ctx->status.display_height;
    if (width <= 0 || height <= 0)
        return;

    pad_x = clamp_int(width / 30, 24, 56);
    pad_y = clamp_int(height / 28, 18, 40);
    bar_x = pad_x;
    bar_width = width - pad_x * 2;
    bar_height = clamp_int(height / 9, 76, 118);
    bar_y = height - pad_y - bar_height;
    inner_pad = clamp_int(bar_height / 7, 8, 16);
    title_scale = clamp_int(bar_height / 26, 2, 4);
    slot_scale = clamp_int(bar_height / 34, 2, 3);
    title_y = bar_y + inner_pad;
    slot_y = bar_y + clamp_int((bar_height * 42) / 100, 28, 48);
    title_max_width = bar_width - inner_pad * 2;
    left_zone_width = clamp_int((bar_width * 28) / 100, 160, bar_width / 3);
    right_zone_width = clamp_int((bar_width * 20) / 100, 110, bar_width / 4);
    center_zone_x = bar_x + left_zone_width;
    center_zone_width = bar_width - left_zone_width - right_zone_width;

    frontend_overlay_draw_text(overlay.bar.title,
                               bar_x + inner_pad,
                               title_y,
                               title_scale,
                               title_max_width,
                               230,
                               fill_rect,
                               userdata);

    frontend_overlay_draw_text(overlay.bar.left,
                               bar_x + inner_pad,
                               slot_y,
                               slot_scale,
                               left_zone_width - inner_pad * 2,
                               190,
                               fill_rect,
                               userdata);

    center_text_width = frontend_font_measure_text(overlay.bar.center, slot_scale);
    frontend_overlay_draw_text(overlay.bar.center,
                               center_zone_x + (center_zone_width - center_text_width) / 2,
                               slot_y,
                               slot_scale,
                               center_zone_width,
                               214,
                               fill_rect,
                               userdata);

    right_text_width = frontend_font_measure_text(overlay.bar.right, slot_scale);
    frontend_overlay_draw_text(overlay.bar.right,
                               bar_x + bar_width - inner_pad - right_text_width,
                               slot_y,
                               slot_scale,
                               right_zone_width - inner_pad,
                               190,
                               fill_rect,
                               userdata);
}

static void frontend_overlay_render_sw(void *pixels, size_t stride, u32 width, u32 height, const ViewContext *ctx)
{
    FrontendOverlayRect rects[8];
    size_t rect_count = 0;
    size_t i;
    FrontendOverlaySwTarget target;

    if (!pixels || !ctx || !frontend_overlay_build_rects(ctx, rects, sizeof(rects) / sizeof(rects[0]), &rect_count) || rect_count == 0)
        return;

    for (i = 0; i < rect_count; ++i)
        frontend_overlay_fill_rect_sw(pixels, stride, width, height, &rects[i]);

    target.pixels = pixels;
    target.stride = stride;
    target.width = width;
    target.height = height;
    frontend_overlay_render_text_generic(ctx, frontend_overlay_fill_rect_sw_cb, &target);
}

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

        frontend_overlay_render_dk3d(ctx, slot);
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
        if (rendered)
            frontend_overlay_render_gl(ctx);
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
    frontend_overlay_render_sw(pixels, (size_t)stride, ctx->status.display_width, ctx->status.display_height, ctx);

    framebufferEnd(&ctx->framebuffer);
    if (rendered)
        ++ctx->status.frames_presented;
    return rendered;
}
