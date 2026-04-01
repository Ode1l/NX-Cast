#include "player_platform.h"

#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "log/log.h"

static PlayerPlatformVideoStatus g_video_status;

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

const char *player_platform_view_mode_name(PlayerPlatformViewMode mode)
{
    switch (mode)
    {
    case PLAYER_PLATFORM_VIEW_VIDEO:
        return "video";
    case PLAYER_PLATFORM_VIEW_LOG:
    default:
        return "log";
    }
}

const char *player_platform_render_owner_name(PlayerPlatformRenderOwner owner)
{
    switch (owner)
    {
    case PLAYER_PLATFORM_RENDER_OWNER_BACKEND:
        return "backend";
    case PLAYER_PLATFORM_RENDER_OWNER_MAIN_THREAD:
    default:
        return "main-thread";
    }
}

static bool should_show_video_view(const PlayerSnapshot *snapshot)
{
    if (!snapshot || !snapshot->has_source)
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

bool player_platform_video_init(void)
{
    memset(&g_video_status, 0, sizeof(g_video_status));
    g_video_status.initialized = true;
    g_video_status.active_view = PLAYER_PLATFORM_VIEW_LOG;
    g_video_status.desired_view = PLAYER_PLATFORM_VIEW_LOG;
    g_video_status.render_owner = PLAYER_PLATFORM_RENDER_OWNER_MAIN_THREAD;

    log_info("[player-platform-video] init render_owner=%s active_view=%s\n",
             player_platform_render_owner_name(g_video_status.render_owner),
             player_platform_view_mode_name(g_video_status.active_view));
    return true;
}

void player_platform_video_deinit(void)
{
    if (!g_video_status.initialized)
        return;

    log_info("[player-platform-video] deinit frame_counter=%llu active_view=%s\n",
             (unsigned long long)g_video_status.frame_counter,
             player_platform_view_mode_name(g_video_status.active_view));
    memset(&g_video_status, 0, sizeof(g_video_status));
}

void player_platform_video_sync_snapshot(const PlayerSnapshot *snapshot)
{
    if (!g_video_status.initialized || !snapshot)
        return;

    PlayerPlatformViewMode previous_desired = g_video_status.desired_view;

    g_video_status.has_source = snapshot->has_source;
    g_video_status.session_active = should_show_video_view(snapshot);
    g_video_status.player_state = snapshot->state;
    g_video_status.desired_view = g_video_status.session_active ? PLAYER_PLATFORM_VIEW_VIDEO
                                                                : PLAYER_PLATFORM_VIEW_LOG;

    if (snapshot->has_source)
    {
        snprintf(g_video_status.source_uri, sizeof(g_video_status.source_uri), "%s", snapshot->source.uri);
        snprintf(g_video_status.source_hint, sizeof(g_video_status.source_hint), "%s",
                 snapshot->source.format_hint[0] != '\0' ? snapshot->source.format_hint : "unknown");
    }
    else
    {
        g_video_status.source_uri[0] = '\0';
        g_video_status.source_hint[0] = '\0';
    }

    if (previous_desired != g_video_status.desired_view)
    {
        log_info("[player-platform-video] desired_view=%s state=%s has_source=%d hint=%s\n",
                 player_platform_view_mode_name(g_video_status.desired_view),
                 player_state_name(g_video_status.player_state),
                 g_video_status.has_source ? 1 : 0,
                 g_video_status.source_hint[0] != '\0' ? g_video_status.source_hint : "none");
    }
}

void player_platform_video_begin_frame(void)
{
    if (!g_video_status.initialized)
        return;

    ++g_video_status.frame_counter;
    if (g_video_status.active_view != g_video_status.desired_view)
    {
        log_info("[player-platform-video] active_view=%s frame=%llu\n",
                 player_platform_view_mode_name(g_video_status.desired_view),
                 (unsigned long long)g_video_status.frame_counter);
        g_video_status.active_view = g_video_status.desired_view;
    }
}

PlayerPlatformViewMode player_platform_video_get_active_view(void)
{
    return g_video_status.active_view;
}

PlayerPlatformRenderOwner player_platform_video_get_render_owner(void)
{
    return g_video_status.render_owner;
}

bool player_platform_video_get_status(PlayerPlatformVideoStatus *out)
{
    if (!out || !g_video_status.initialized)
        return false;

    *out = g_video_status;
    return true;
}

void player_platform_video_render_placeholder(void)
{
    const char *hint = g_video_status.source_hint[0] != '\0' ? g_video_status.source_hint : "none";
    const char *uri = g_video_status.source_uri[0] != '\0' ? g_video_status.source_uri : "<none>";

    consoleClear();
    printf("NX-Cast Video View (Skeleton)\n");
    printf("Owner %s  View %s  Frame %llu\n",
           player_platform_render_owner_name(g_video_status.render_owner),
           player_platform_view_mode_name(g_video_status.active_view),
           (unsigned long long)g_video_status.frame_counter);
    printf("Player %s  Source %s\n", player_state_name(g_video_status.player_state), hint);
    printf("Main thread owns the foreground and render loop.\n");
    printf("libmpv render API is not connected in Step 2.1.\n");
    printf("Stop playback to return to logs.\n");
    printf("\n");
    printf("URI:\n");
    printf("%s\n", uri);
}
