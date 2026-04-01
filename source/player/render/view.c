#include "player/view.h"

#include <stdio.h>
#include <string.h>

#include "log/log.h"
#include "player/player.h"
#include "player/render/internal.h"

static ViewContext g_view;

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

void player_view_deinit(void)
{
    if (!g_view.status.initialized)
        return;

    frontend_close(&g_view, true);
    if (g_view.status.render_api_connected)
        player_video_detach();

    log_info("[player-view] deinit frame_counter=%llu frames_presented=%llu active_view=%s\n",
             (unsigned long long)g_view.status.frame_counter,
             (unsigned long long)g_view.status.frames_presented,
             player_view_mode_name(g_view.status.active_view));
    memset(&g_view, 0, sizeof(g_view));
}

void player_view_sync(const PlayerSnapshot *snapshot)
{
    if (!g_view.status.initialized || !snapshot)
        return;

    PlayerViewMode previous_desired = g_view.status.desired_view;

    g_view.status.has_media = snapshot->has_media;
    g_view.status.session_active = should_show_video_view(snapshot);
    g_view.status.player_state = snapshot->state;
    g_view.status.desired_view = g_view.status.session_active ? PLAYER_VIEW_VIDEO
                                                              : PLAYER_VIEW_LOG;

    if (snapshot->has_media)
    {
        snprintf(g_view.status.media_uri, sizeof(g_view.status.media_uri), "%s", snapshot->media.uri);
        snprintf(g_view.status.media_hint, sizeof(g_view.status.media_hint), "%s",
                 snapshot->media.format_hint[0] != '\0' ? snapshot->media.format_hint : "unknown");
    }
    else
    {
        g_view.status.media_uri[0] = '\0';
        g_view.status.media_hint[0] = '\0';
    }

    if (previous_desired != g_view.status.desired_view)
    {
        log_info("[player-view] desired_view=%s state=%s has_media=%d hint=%s\n",
                 player_view_mode_name(g_view.status.desired_view),
                 player_state_name(g_view.status.player_state),
                 g_view.status.has_media ? 1 : 0,
                 g_view.status.media_hint[0] != '\0' ? g_view.status.media_hint : "none");
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

    *out = g_view.status;
    return true;
}

bool player_view_render_frame(void)
{
    if (!g_view.status.initialized)
        return false;
    return frontend_render(&g_view);
}
