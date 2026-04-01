#include "player/render/internal.h"

#include <string.h>

#include "log/log.h"
#include "player/player.h"

#define VIEW_DEFAULT_WIDTH 1280
#define VIEW_DEFAULT_HEIGHT 720

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
    if (ctx->status.render_api_connected)
        return true;
    if (!player_video_supported())
        return false;
    if (!player_video_attach_sw())
        return false;

    ctx->status.render_api_connected = true;
    log_info("[player-view] render_api_connected=1 backend=sw\n");
    return true;
}

bool frontend_open(ViewContext *ctx)
{
    if (!ctx)
        return false;
    if (ctx->status.foreground_video_active)
        return true;
    if (!frontend_connect(ctx))
        return false;

    u32 width = VIEW_DEFAULT_WIDTH;
    u32 height = VIEW_DEFAULT_HEIGHT;
    frontend_query_display_size(&width, &height);

    consoleExit(NULL);
    memset(&ctx->framebuffer, 0, sizeof(ctx->framebuffer));

    Result rc = framebufferCreate(&ctx->framebuffer,
                                  nwindowGetDefault(),
                                  width,
                                  height,
                                  PIXEL_FORMAT_RGBX_8888,
                                  2);
    if (R_FAILED(rc))
    {
        consoleInit(NULL);
        log_error("[player-view] framebufferCreate failed: 0x%08X\n", rc);
        return false;
    }

    rc = framebufferMakeLinear(&ctx->framebuffer);
    if (R_FAILED(rc))
    {
        framebufferClose(&ctx->framebuffer);
        memset(&ctx->framebuffer, 0, sizeof(ctx->framebuffer));
        consoleInit(NULL);
        log_error("[player-view] framebufferMakeLinear failed: 0x%08X\n", rc);
        return false;
    }

    ctx->framebuffer_ready = true;
    ctx->status.foreground_video_active = true;
    ctx->status.display_width = width;
    ctx->status.display_height = height;
    log_info("[player-view] framebuffer_ready=1 size=%ux%u\n", width, height);
    return true;
}

void frontend_close(ViewContext *ctx, bool restore_console)
{
    if (!ctx)
        return;

    bool had_video_foreground = ctx->status.foreground_video_active || ctx->framebuffer_ready;

    if (ctx->framebuffer_ready)
    {
        framebufferClose(&ctx->framebuffer);
        memset(&ctx->framebuffer, 0, sizeof(ctx->framebuffer));
        ctx->framebuffer_ready = false;
    }

    ctx->status.foreground_video_active = false;
    ctx->status.display_width = 0;
    ctx->status.display_height = 0;

    if (restore_console && had_video_foreground)
    {
        consoleInit(NULL);
        consoleClear();
    }
}

bool frontend_render(ViewContext *ctx)
{
    if (!ctx ||
        ctx->status.active_view != PLAYER_VIEW_VIDEO ||
        !ctx->framebuffer_ready)
    {
        return false;
    }

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
