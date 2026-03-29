#include "player_backend.h"

#include "log/log.h"

static void (*g_event_sink)(const PlayerEvent *event) = NULL;

static bool libmpv_available(void)
{
    return false;
}

static bool libmpv_init(void)
{
    log_warn("[player-libmpv] libmpv backend requested but libmpv is not available in current toolchain\n");
    return false;
}

static void libmpv_deinit(void)
{
    g_event_sink = NULL;
}

static void libmpv_set_event_sink(void (*sink)(const PlayerEvent *event))
{
    g_event_sink = sink;
}

static bool libmpv_not_ready(const char *action)
{
    log_warn("[player-libmpv] action=%s rejected backend not built\n", action);
    return false;
}

static bool libmpv_set_uri(const char *uri, const char *metadata)
{
    (void)uri;
    (void)metadata;
    return libmpv_not_ready("set_uri");
}

static bool libmpv_play(void)
{
    return libmpv_not_ready("play");
}

static bool libmpv_pause(void)
{
    return libmpv_not_ready("pause");
}

static bool libmpv_stop(void)
{
    return libmpv_not_ready("stop");
}

static bool libmpv_seek_ms(int position_ms)
{
    (void)position_ms;
    return libmpv_not_ready("seek_ms");
}

static bool libmpv_set_volume(int volume_0_100)
{
    (void)volume_0_100;
    return libmpv_not_ready("set_volume");
}

static bool libmpv_set_mute(bool mute)
{
    (void)mute;
    return libmpv_not_ready("set_mute");
}

static int libmpv_get_position_ms(void)
{
    return 0;
}

static int libmpv_get_duration_ms(void)
{
    return 0;
}

static int libmpv_get_volume(void)
{
    return 0;
}

static bool libmpv_get_mute(void)
{
    return false;
}

static PlayerState libmpv_get_state(void)
{
    return PLAYER_STATE_IDLE;
}

const PlayerBackendOps g_player_backend_libmpv = {
    .name = "libmpv",
    .available = libmpv_available,
    .init = libmpv_init,
    .deinit = libmpv_deinit,
    .set_event_sink = libmpv_set_event_sink,
    .set_uri = libmpv_set_uri,
    .play = libmpv_play,
    .pause = libmpv_pause,
    .stop = libmpv_stop,
    .seek_ms = libmpv_seek_ms,
    .set_volume = libmpv_set_volume,
    .set_mute = libmpv_set_mute,
    .get_position_ms = libmpv_get_position_ms,
    .get_duration_ms = libmpv_get_duration_ms,
    .get_volume = libmpv_get_volume,
    .get_mute = libmpv_get_mute,
    .get_state = libmpv_get_state,
};
