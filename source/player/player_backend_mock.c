#include "player_backend.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "log/log.h"

static void (*g_event_sink)(const PlayerEvent *event) = NULL;

static PlayerState g_state = PLAYER_STATE_IDLE;
static int g_position_ms = 0;
static int g_duration_ms = 0;
static int g_volume = 20;
static bool g_mute = false;
static bool g_has_source = false;
static PlayerResolvedSource g_source;
static char g_uri[1024];
static char g_metadata[2048];
static int g_play_anchor_ms = 0;
static int64_t g_play_anchor_monotonic_ms = 0;

static int64_t monotonic_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000LL + (int64_t)(ts.tv_nsec / 1000000LL);
}

static void emit_event(PlayerEventType type)
{
    if (!g_event_sink)
        return;

    PlayerEvent event = {
        .type = type,
        .state = g_state,
        .position_ms = g_position_ms,
        .duration_ms = g_duration_ms,
        .volume = g_volume,
        .mute = g_mute,
        .error_code = 0,
        .uri = g_uri,
        .source_profile = g_has_source ? g_source.profile : PLAYER_SOURCE_PROFILE_UNKNOWN
    };
    g_event_sink(&event);
}

static void refresh_position(bool emit_events)
{
    if (g_state != PLAYER_STATE_PLAYING)
        return;

    int64_t now_ms = monotonic_time_ms();
    if (g_play_anchor_monotonic_ms <= 0)
        g_play_anchor_monotonic_ms = now_ms;

    int64_t elapsed_ms = now_ms - g_play_anchor_monotonic_ms;
    if (elapsed_ms < 0)
        elapsed_ms = 0;

    int new_position = g_play_anchor_ms + (int)elapsed_ms;
    bool finished = false;
    if (g_duration_ms > 0 && new_position >= g_duration_ms)
    {
        new_position = g_duration_ms;
        finished = true;
    }

    if (new_position != g_position_ms)
    {
        g_position_ms = new_position;
        if (emit_events)
            emit_event(PLAYER_EVENT_POSITION_CHANGED);
    }

    if (finished)
    {
        g_state = PLAYER_STATE_STOPPED;
        g_play_anchor_ms = g_position_ms;
        g_play_anchor_monotonic_ms = now_ms;
        if (emit_events)
            emit_event(PLAYER_EVENT_STATE_CHANGED);
    }
}

static bool mock_init(void)
{
    memset(g_uri, 0, sizeof(g_uri));
    memset(g_metadata, 0, sizeof(g_metadata));
    player_source_reset(&g_source);
    g_has_source = false;
    g_state = PLAYER_STATE_STOPPED;
    g_position_ms = 0;
    g_duration_ms = 0;
    g_volume = 20;
    g_mute = false;
    g_play_anchor_ms = 0;
    g_play_anchor_monotonic_ms = monotonic_time_ms();
    log_info("[player-mock] init\n");
    emit_event(PLAYER_EVENT_STATE_CHANGED);
    emit_event(PLAYER_EVENT_VOLUME_CHANGED);
    emit_event(PLAYER_EVENT_MUTE_CHANGED);
    return true;
}

static void mock_deinit(void)
{
    log_info("[player-mock] deinit\n");
}

static void mock_set_event_sink(void (*sink)(const PlayerEvent *event))
{
    g_event_sink = sink;
}

static bool mock_set_source(const PlayerResolvedSource *source)
{
    if (!source || source->uri[0] == '\0')
        return false;

    g_source = *source;
    g_has_source = true;
    snprintf(g_uri, sizeof(g_uri), "%s", g_source.uri);
    snprintf(g_metadata, sizeof(g_metadata), "%s", g_source.metadata);

    g_state = PLAYER_STATE_STOPPED;
    g_position_ms = 0;
    if (g_duration_ms <= 0)
        g_duration_ms = 5 * 60 * 1000;
    g_play_anchor_ms = 0;
    g_play_anchor_monotonic_ms = monotonic_time_ms();

    log_info("[player-mock] set_source profile=%s uri=%s metadata_len=%zu\n",
             player_source_profile_name(g_source.profile),
             g_uri,
             strlen(g_metadata));
    emit_event(PLAYER_EVENT_URI_CHANGED);
    emit_event(PLAYER_EVENT_DURATION_CHANGED);
    emit_event(PLAYER_EVENT_POSITION_CHANGED);
    emit_event(PLAYER_EVENT_STATE_CHANGED);
    return true;
}

static bool mock_play(void)
{
    if (g_uri[0] == '\0')
        return false;

    refresh_position(false);
    g_state = PLAYER_STATE_PLAYING;
    g_play_anchor_ms = g_position_ms;
    g_play_anchor_monotonic_ms = monotonic_time_ms();
    log_info("[player-mock] play\n");
    emit_event(PLAYER_EVENT_STATE_CHANGED);
    return true;
}

static bool mock_pause(void)
{
    if (g_state != PLAYER_STATE_PLAYING)
        return false;

    refresh_position(true);
    g_state = PLAYER_STATE_PAUSED;
    g_play_anchor_ms = g_position_ms;
    g_play_anchor_monotonic_ms = monotonic_time_ms();
    log_info("[player-mock] pause\n");
    emit_event(PLAYER_EVENT_STATE_CHANGED);
    return true;
}

static bool mock_stop(void)
{
    if (g_uri[0] == '\0')
        return false;

    refresh_position(false);
    g_state = PLAYER_STATE_STOPPED;
    g_position_ms = 0;
    g_play_anchor_ms = 0;
    g_play_anchor_monotonic_ms = monotonic_time_ms();
    log_info("[player-mock] stop\n");
    emit_event(PLAYER_EVENT_POSITION_CHANGED);
    emit_event(PLAYER_EVENT_STATE_CHANGED);
    return true;
}

static bool mock_seek_ms(int position_ms)
{
    if (g_uri[0] == '\0' || position_ms < 0)
        return false;

    refresh_position(false);
    if (g_duration_ms > 0 && position_ms > g_duration_ms)
        position_ms = g_duration_ms;

    g_position_ms = position_ms;
    g_play_anchor_ms = g_position_ms;
    g_play_anchor_monotonic_ms = monotonic_time_ms();
    log_info("[player-mock] seek_ms=%d\n", g_position_ms);
    emit_event(PLAYER_EVENT_POSITION_CHANGED);
    return true;
}

static bool mock_set_volume(int volume_0_100)
{
    if (volume_0_100 < 0)
        volume_0_100 = 0;
    if (volume_0_100 > 100)
        volume_0_100 = 100;

    g_volume = volume_0_100;
    log_info("[player-mock] set_volume=%d\n", g_volume);
    emit_event(PLAYER_EVENT_VOLUME_CHANGED);
    return true;
}

static bool mock_set_mute(bool mute)
{
    g_mute = mute;
    log_info("[player-mock] set_mute=%d\n", g_mute ? 1 : 0);
    emit_event(PLAYER_EVENT_MUTE_CHANGED);
    return true;
}

static int mock_get_position_ms(void)
{
    refresh_position(false);
    return g_position_ms;
}

static int mock_get_duration_ms(void)
{
    refresh_position(false);
    return g_duration_ms;
}

static int mock_get_volume(void)
{
    return g_volume;
}

static bool mock_get_mute(void)
{
    return g_mute;
}

static bool mock_is_seekable(void)
{
    return g_uri[0] != '\0' && g_duration_ms > 0;
}

static PlayerState mock_get_state(void)
{
    refresh_position(false);
    return g_state;
}

const PlayerBackendOps g_player_backend_mock = {
    .name = "mock",
    .available = NULL,
    .init = mock_init,
    .deinit = mock_deinit,
    .set_event_sink = mock_set_event_sink,
    .set_source = mock_set_source,
    .play = mock_play,
    .pause = mock_pause,
    .stop = mock_stop,
    .seek_ms = mock_seek_ms,
    .set_volume = mock_set_volume,
    .set_mute = mock_set_mute,
    .get_position_ms = mock_get_position_ms,
    .get_duration_ms = mock_get_duration_ms,
    .get_volume = mock_get_volume,
    .get_mute = mock_get_mute,
    .is_seekable = mock_is_seekable,
    .get_state = mock_get_state
};
