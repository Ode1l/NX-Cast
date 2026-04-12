#include "player/backend.h"

#include <switch.h>

#include <ctype.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "log/log.h"
#include "player/seek_target.h"

static void (*g_event_sink)(const PlayerEvent *event) = NULL;

static PlayerState g_state = PLAYER_STATE_IDLE;
static int g_position_ms = 0;
static int g_duration_ms = 0;
static int g_volume = PLAYER_DEFAULT_VOLUME;
static bool g_mute = false;
static bool g_seekable = false;
static bool g_has_media = false;
static PlayerMedia g_media;
static int g_play_anchor_ms = 0;
static int64_t g_play_anchor_monotonic_ms = 0;

static int64_t monotonic_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000LL + (int64_t)(ts.tv_nsec / 1000000LL);
}

static void emit_event(PlayerEventType type, int error_code)
{
    PlayerEvent event = {0};

    if (!g_event_sink)
        return;

    event.type = type;
    event.state = g_state;
    event.position_ms = g_position_ms;
    event.duration_ms = g_duration_ms;
    event.volume = g_volume;
    event.mute = g_mute;
    event.seekable = g_seekable;
    event.error_code = error_code;
    if (g_has_media && g_media.uri)
        event.uri = strdup(g_media.uri);
    g_event_sink(&event);
    player_event_clear(&event);
}

static void refresh_position(bool emit_events)
{
    int new_position;
    bool finished = false;
    int64_t now_ms;
    int64_t elapsed_ms;

    if (g_state != PLAYER_STATE_PLAYING)
        return;

    now_ms = monotonic_time_ms();
    if (g_play_anchor_monotonic_ms <= 0)
        g_play_anchor_monotonic_ms = now_ms;

    elapsed_ms = now_ms - g_play_anchor_monotonic_ms;
    if (elapsed_ms < 0)
        elapsed_ms = 0;

    new_position = g_play_anchor_ms + (int)elapsed_ms;
    if (g_duration_ms > 0 && new_position >= g_duration_ms)
    {
        new_position = g_duration_ms;
        finished = true;
    }

    if (new_position != g_position_ms)
    {
        g_position_ms = new_position;
        if (emit_events)
            emit_event(PLAYER_EVENT_POSITION_CHANGED, 0);
    }

    if (finished)
    {
        g_state = PLAYER_STATE_STOPPED;
        g_play_anchor_ms = g_position_ms;
        g_play_anchor_monotonic_ms = now_ms;
        if (emit_events)
            emit_event(PLAYER_EVENT_STATE_CHANGED, 0);
    }
}

static bool mock_init(void)
{
    memset(&g_media, 0, sizeof(g_media));
    g_has_media = false;
    g_state = PLAYER_STATE_IDLE;
    g_position_ms = 0;
    g_duration_ms = 0;
    g_volume = PLAYER_DEFAULT_VOLUME;
    g_mute = false;
    g_seekable = false;
    g_play_anchor_ms = 0;
    g_play_anchor_monotonic_ms = monotonic_time_ms();
    log_info("[player-mock] init\n");
    emit_event(PLAYER_EVENT_STATE_CHANGED, 0);
    emit_event(PLAYER_EVENT_VOLUME_CHANGED, 0);
    emit_event(PLAYER_EVENT_MUTE_CHANGED, 0);
    return true;
}

static void mock_deinit(void)
{
    player_media_clear(&g_media);
    log_info("[player-mock] deinit\n");
}

static void mock_set_event_sink(void (*sink)(const PlayerEvent *event))
{
    g_event_sink = sink;
}

static bool mock_set_media(const PlayerMedia *media)
{
    PlayerMedia copy = {0};

    if (!media || !media->uri || media->uri[0] == '\0')
        return false;

    if (!player_media_copy(&copy, media))
        return false;

    player_media_clear(&g_media);
    g_media = copy;
    g_has_media = true;
    g_state = PLAYER_STATE_PAUSED;
    g_position_ms = 0;
    g_duration_ms = 5 * 60 * 1000;
    g_seekable = true;
    g_play_anchor_ms = 0;
    g_play_anchor_monotonic_ms = monotonic_time_ms();

    log_info("[player-mock] set_media uri=%s metadata_len=%zu\n",
             g_media.uri,
             g_media.metadata ? strlen(g_media.metadata) : 0u);
    emit_event(PLAYER_EVENT_URI_CHANGED, 0);
    emit_event(PLAYER_EVENT_DURATION_CHANGED, 0);
    emit_event(PLAYER_EVENT_POSITION_CHANGED, 0);
    emit_event(PLAYER_EVENT_STATE_CHANGED, 0);
    return true;
}

static bool mock_play(void)
{
    if (!g_has_media)
        return false;

    refresh_position(false);
    g_state = PLAYER_STATE_PLAYING;
    g_play_anchor_ms = g_position_ms;
    g_play_anchor_monotonic_ms = monotonic_time_ms();
    emit_event(PLAYER_EVENT_STATE_CHANGED, 0);
    return true;
}

static bool mock_pause(void)
{
    if (!g_has_media || g_state == PLAYER_STATE_STOPPED || g_state == PLAYER_STATE_IDLE)
        return false;

    refresh_position(true);
    g_state = PLAYER_STATE_PAUSED;
    g_play_anchor_ms = g_position_ms;
    g_play_anchor_monotonic_ms = monotonic_time_ms();
    emit_event(PLAYER_EVENT_STATE_CHANGED, 0);
    return true;
}

static bool mock_stop(void)
{
    if (!g_has_media)
        return false;

    g_state = PLAYER_STATE_STOPPED;
    g_position_ms = 0;
    g_play_anchor_ms = 0;
    g_play_anchor_monotonic_ms = monotonic_time_ms();
    emit_event(PLAYER_EVENT_POSITION_CHANGED, 0);
    emit_event(PLAYER_EVENT_STATE_CHANGED, 0);
    return true;
}

static bool mock_parse_seek_target_ms(const char *target, int *out_ms)
{
    return player_seek_target_parse_ms(target, out_ms);
}

static bool mock_seek_ms(int position_ms)
{
    if (!g_has_media || position_ms < 0)
        return false;

    if (g_duration_ms > 0 && position_ms > g_duration_ms)
        position_ms = g_duration_ms;

    g_position_ms = position_ms;
    g_play_anchor_ms = g_position_ms;
    g_play_anchor_monotonic_ms = monotonic_time_ms();
    emit_event(PLAYER_EVENT_POSITION_CHANGED, 0);
    return true;
}

static bool mock_seek_target(const char *target)
{
    int position_ms = 0;

    if (!mock_parse_seek_target_ms(target, &position_ms))
        return false;
    return mock_seek_ms(position_ms);
}

static bool mock_set_volume(int volume_0_100)
{
    if (volume_0_100 < 0)
        volume_0_100 = 0;
    if (volume_0_100 > 100)
        volume_0_100 = 100;

    g_volume = volume_0_100;
    emit_event(PLAYER_EVENT_VOLUME_CHANGED, 0);
    return true;
}

static bool mock_set_mute(bool mute)
{
    g_mute = mute;
    emit_event(PLAYER_EVENT_MUTE_CHANGED, 0);
    return true;
}

static bool mock_pump_events(int timeout_ms)
{
    int before_position = g_position_ms;
    PlayerState before_state = g_state;

    if (timeout_ms > 0)
        svcSleepThread((int64_t)timeout_ms * 1000000LL);

    refresh_position(true);
    return before_position != g_position_ms || before_state != g_state;
}

static void mock_wakeup(void)
{
}

static bool mock_render_supported(void)
{
    return false;
}

static bool mock_render_attach_gl(void *(*get_proc_address)(void *ctx, const char *name), void *get_proc_address_ctx)
{
    (void)get_proc_address;
    (void)get_proc_address_ctx;
    return false;
}

static bool mock_render_attach_sw(void)
{
    return false;
}

static bool mock_render_attach_dk3d(void)
{
    return false;
}

static void mock_render_detach(void)
{
}

static bool mock_render_frame_gl(int fbo, int width, int height, bool flip_y)
{
    (void)fbo;
    (void)width;
    (void)height;
    (void)flip_y;
    return false;
}

static bool mock_render_frame_sw(void *pixels, int width, int height, size_t stride)
{
    (void)pixels;
    (void)width;
    (void)height;
    (void)stride;
    return false;
}

static bool mock_render_frame_dk3d(int width, int height)
{
    (void)width;
    (void)height;
    return false;
}

static int mock_get_position_ms(void)
{
    refresh_position(false);
    return g_position_ms;
}

static int mock_get_duration_ms(void)
{
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
    return g_seekable;
}

static PlayerState mock_get_state(void)
{
    refresh_position(false);
    return g_state;
}

const BackendOps g_mock_ops = {
    .name = "mock",
    .available = NULL,
    .init = mock_init,
    .deinit = mock_deinit,
    .set_event_sink = mock_set_event_sink,
    .set_media = mock_set_media,
    .play = mock_play,
    .pause = mock_pause,
    .stop = mock_stop,
    .seek_target = mock_seek_target,
    .seek_ms = mock_seek_ms,
    .set_volume = mock_set_volume,
    .set_mute = mock_set_mute,
    .pump_events = mock_pump_events,
    .wakeup = mock_wakeup,
    .render_supported = mock_render_supported,
    .render_attach_gl = mock_render_attach_gl,
    .render_attach_sw = mock_render_attach_sw,
    .render_attach_dk3d = mock_render_attach_dk3d,
    .render_detach = mock_render_detach,
    .render_frame_gl = mock_render_frame_gl,
    .render_frame_sw = mock_render_frame_sw,
    .render_frame_dk3d = mock_render_frame_dk3d,
    .get_position_ms = mock_get_position_ms,
    .get_duration_ms = mock_get_duration_ms,
    .get_volume = mock_get_volume,
    .get_mute = mock_get_mute,
    .is_seekable = mock_is_seekable,
    .get_state = mock_get_state
};
