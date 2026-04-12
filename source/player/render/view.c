#include "player/view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log/log.h"
#include "player/player.h"
#include "player/render/internal.h"

#define PLAYER_VIEW_STOP_HOLD_MS 1500ULL

static ViewContext g_view;

static bool player_view_set_media_uri(PlayerViewStatus *status, const char *uri)
{
    char *copy = NULL;

    if (!status)
        return false;

    if (uri)
    {
        copy = strdup(uri);
        if (!copy)
            return false;
    }

    free(status->media_uri);
    status->media_uri = copy;
    return true;
}

void player_view_status_clear(PlayerViewStatus *status)
{
    if (!status)
        return;

    free(status->media_uri);
    status->media_uri = NULL;
    memset(status, 0, sizeof(*status));
}

bool player_view_status_copy(PlayerViewStatus *out, const PlayerViewStatus *status)
{
    PlayerViewStatus copy;

    if (!out || !status)
        return false;

    memset(&copy, 0, sizeof(copy));
    copy = *status;
    copy.media_uri = NULL;
    if (status->media_uri)
    {
        copy.media_uri = strdup(status->media_uri);
        if (!copy.media_uri)
            return false;
    }

    player_view_status_clear(out);
    *out = copy;
    return true;
}

static uint64_t monotonic_time_ms(void)
{
    return armTicksToNs(armGetSystemTick()) / 1000000ULL;
}

static const char *player_state_name(PlayerState state)
{
    switch (state)
    {
    case PLAYER_STATE_IDLE:
        return "IDLE";
    case PLAYER_STATE_STOPPED:
        return "STOPPED";
    case PLAYER_STATE_LOADING:
        return "LOADING";
    case PLAYER_STATE_BUFFERING:
        return "BUFFERING";
    case PLAYER_STATE_SEEKING:
        return "SEEKING";
    case PLAYER_STATE_PLAYING:
        return "PLAYING";
    case PLAYER_STATE_PAUSED:
        return "PAUSED";
    case PLAYER_STATE_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

const char *player_view_mode_name(PlayerViewMode mode)
{
    switch (mode)
    {
    case PLAYER_VIEW_VIDEO:
        return "video";
    case PLAYER_VIEW_LOG:
    default:
        return "log";
    }
}

const char *player_render_owner_name(PlayerRenderOwner owner)
{
    switch (owner)
    {
    case PLAYER_RENDER_OWNER_BACKEND:
        return "backend";
    case PLAYER_RENDER_OWNER_MAIN_THREAD:
    default:
        return "main-thread";
    }
}

static bool should_show_video_view(const PlayerSnapshot *snapshot)
{
    if (!snapshot || !snapshot->has_media)
        return false;

    switch (snapshot->state)
    {
    case PLAYER_STATE_LOADING:
    case PLAYER_STATE_BUFFERING:
    case PLAYER_STATE_SEEKING:
    case PLAYER_STATE_PLAYING:
    case PLAYER_STATE_PAUSED:
        return true;
    case PLAYER_STATE_IDLE:
    case PLAYER_STATE_STOPPED:
    case PLAYER_STATE_ERROR:
    default:
        return false;
    }
}

bool player_view_init(void)
{
    memset(&g_view, 0, sizeof(g_view));
    g_view.status.initialized = true;
    g_view.status.active_view = PLAYER_VIEW_LOG;
    g_view.status.desired_view = PLAYER_VIEW_LOG;
    g_view.status.render_owner = PLAYER_RENDER_OWNER_MAIN_THREAD;

    log_info("[player-view] init render_owner=%s active_view=%s\n",
             player_render_owner_name(g_view.status.render_owner),
             player_view_mode_name(g_view.status.active_view));
    return true;
}

bool player_view_prepare_video(void)
{
    bool ok;

    if (!g_view.status.initialized)
        return false;

    ok = frontend_connect(&g_view);
    log_info("[player-view] prepare_video ok=%d render_api_connected=%d render_path=%d\n",
             ok ? 1 : 0,
             g_view.status.render_api_connected ? 1 : 0,
             (int)g_view.render_path);
    return ok;
}

void player_view_deinit(void)
{
    bool restore_console = false;

    if (!g_view.status.initialized)
        return;

    restore_console = g_view.status.foreground_video_active ||
                      g_view.framebuffer_ready ||
                      g_view.render_path != FRONTEND_RENDER_NONE;

    // Tear down the active frontend first, but delay console restoration
    // until after any render backend/device objects are fully destroyed.
    frontend_close(&g_view, false);
    frontend_shutdown(&g_view);
    if (restore_console)
    {
        consoleInit(NULL);
        consoleClear();
    }

    log_info("[player-view] deinit frame_counter=%llu frames_presented=%llu active_view=%s\n",
             (unsigned long long)g_view.status.frame_counter,
             (unsigned long long)g_view.status.frames_presented,
             player_view_mode_name(g_view.status.active_view));
    player_view_status_clear(&g_view.status);
    memset(&g_view, 0, sizeof(g_view));
}

void player_view_sync(const PlayerSnapshot *snapshot)
{
    uint64_t now_ms;
    bool keep_video_hold = false;

    if (!g_view.status.initialized || !snapshot)
        return;

    now_ms = monotonic_time_ms();
    PlayerViewMode previous_desired = g_view.status.desired_view;

    g_view.status.has_media = snapshot->has_media;
    g_view.status.session_active = should_show_video_view(snapshot);
    g_view.status.player_state = snapshot->state;

    if (g_view.status.session_active)
    {
        g_view.last_video_state_ms = now_ms;
        g_view.stop_hold_until_ms = 0;
        g_view.status.desired_view = PLAYER_VIEW_VIDEO;
    }
    else
    {
        if (snapshot->has_media &&
            (snapshot->state == PLAYER_STATE_STOPPED || snapshot->state == PLAYER_STATE_IDLE) &&
            g_view.status.active_view == PLAYER_VIEW_VIDEO &&
            g_view.last_video_state_ms > 0)
        {
            if (g_view.stop_hold_until_ms == 0)
                g_view.stop_hold_until_ms = g_view.last_video_state_ms + PLAYER_VIEW_STOP_HOLD_MS;

            keep_video_hold = now_ms < g_view.stop_hold_until_ms;
        }

        if (keep_video_hold)
            g_view.status.desired_view = PLAYER_VIEW_VIDEO;
        else
        {
            g_view.stop_hold_until_ms = 0;
            g_view.status.desired_view = PLAYER_VIEW_LOG;
        }
    }

    if (!player_view_set_media_uri(&g_view.status, snapshot->has_media ? snapshot->media.uri : NULL))
        log_warn("[player-view] failed to update media uri copy\n");

    if (previous_desired != g_view.status.desired_view)
    {
        log_info("[player-view] desired_view=%s state=%s has_media=%d uri=%s\n",
                 player_view_mode_name(g_view.status.desired_view),
                 player_state_name(g_view.status.player_state),
                 g_view.status.has_media ? 1 : 0,
                 g_view.status.media_uri ? g_view.status.media_uri : "none");
    }
}

void player_view_begin_frame(void)
{
    if (!g_view.status.initialized)
        return;

    ++g_view.status.frame_counter;
    (void)frontend_connect(&g_view);

    if (g_view.status.active_view == g_view.status.desired_view)
        return;

    if (g_view.status.desired_view == PLAYER_VIEW_VIDEO)
    {
        if (!frontend_open(&g_view))
        {
            g_view.status.desired_view = PLAYER_VIEW_LOG;
            log_warn("[player-view] video view rejected render_api=%d\n",
                     g_view.status.render_api_connected ? 1 : 0);
            return;
        }
    }
    else
    {
        frontend_close(&g_view, true);
    }

    log_info("[player-view] active_view=%s frame=%llu\n",
             player_view_mode_name(g_view.status.desired_view),
             (unsigned long long)g_view.status.frame_counter);
    g_view.status.active_view = g_view.status.desired_view;
}

PlayerViewMode player_view_get_mode(void)
{
    return g_view.status.active_view;
}

PlayerRenderOwner player_view_get_owner(void)
{
    return g_view.status.render_owner;
}

bool player_view_get_status(PlayerViewStatus *out)
{
    if (!out || !g_view.status.initialized)
        return false;

    return player_view_status_copy(out, &g_view.status);
}

bool player_view_render_frame(void)
{
    if (!g_view.status.initialized)
        return false;
    return frontend_render(&g_view);
}
