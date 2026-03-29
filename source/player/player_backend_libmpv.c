#include "player_backend.h"

#include <switch.h>

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "log/log.h"

#ifdef HAVE_LIBMPV
#include <mpv/client.h>

#define PLAYER_LIBMPV_URI_MAX 1024
#define PLAYER_LIBMPV_METADATA_MAX 2048

static void (*g_event_sink)(const PlayerEvent *event) = NULL;

static Mutex g_state_mutex;
static bool g_mutex_ready = false;
static mpv_handle *g_mpv = NULL;
static PlayerState g_state = PLAYER_STATE_IDLE;
static int g_position_ms = 0;
static int g_duration_ms = 0;
static int g_volume = 20;
static bool g_mute = false;
static bool g_media_loaded = false;
static bool g_stop_mode = true;
static char g_uri[PLAYER_LIBMPV_URI_MAX];
static char g_metadata[PLAYER_LIBMPV_METADATA_MAX];

static void libmpv_clip_for_log(const char *input, char *output, size_t output_size);

static const char *libmpv_end_reason_name(mpv_end_file_reason reason)
{
    switch (reason)
    {
    case MPV_END_FILE_REASON_EOF:
        return "eof";
    case MPV_END_FILE_REASON_STOP:
        return "stop";
    case MPV_END_FILE_REASON_QUIT:
        return "quit";
    case MPV_END_FILE_REASON_ERROR:
        return "error";
    case MPV_END_FILE_REASON_REDIRECT:
        return "redirect";
    default:
        return "unknown";
    }
}

static void libmpv_drain_events_locked(void)
{
    if (!g_mpv)
        return;

    for (;;)
    {
        mpv_event *event = mpv_wait_event(g_mpv, 0);
        if (!event || event->event_id == MPV_EVENT_NONE)
            break;

        switch (event->event_id)
        {
        case MPV_EVENT_START_FILE:
            g_media_loaded = false;
            log_debug("[player-libmpv] event START_FILE\n");
            break;
        case MPV_EVENT_FILE_LOADED:
            g_media_loaded = true;
            log_info("[player-libmpv] event FILE_LOADED\n");
            break;
        case MPV_EVENT_END_FILE:
        {
            const mpv_event_end_file *end = (const mpv_event_end_file *)event->data;
            g_media_loaded = false;
            g_position_ms = 0;
            if (end)
            {
                log_info("[player-libmpv] event END_FILE reason=%s error=%s\n",
                         libmpv_end_reason_name(end->reason),
                         end->error != 0 ? mpv_error_string(end->error) : "none");
                if (end->reason == MPV_END_FILE_REASON_ERROR)
                    g_state = PLAYER_STATE_ERROR;
            }
            else
            {
                log_info("[player-libmpv] event END_FILE\n");
            }
            if (g_uri[0] != '\0')
                g_stop_mode = true;
            break;
        }
        case MPV_EVENT_LOG_MESSAGE:
        {
            const mpv_event_log_message *message = (const mpv_event_log_message *)event->data;
            if (!message || !message->text)
                break;
            if (message->log_level <= MPV_LOG_LEVEL_WARN)
            {
                char clipped[192];
                libmpv_clip_for_log(message->text, clipped, sizeof(clipped));
                log_warn("[player-libmpv] mpv[%s/%s] %s\n",
                         message->prefix ? message->prefix : "?",
                         message->level ? message->level : "?",
                         clipped);
            }
            break;
        }
        default:
            break;
        }
    }
}

static void libmpv_ensure_mutex(void)
{
    if (g_mutex_ready)
        return;
    mutexInit(&g_state_mutex);
    g_mutex_ready = true;
}

static void libmpv_clip_for_log(const char *input, char *output, size_t output_size)
{
    if (!output || output_size == 0)
        return;

    if (!input)
    {
        output[0] = '\0';
        return;
    }

    size_t len = strlen(input);
    if (len + 1 <= output_size)
    {
        snprintf(output, output_size, "%s", input);
        return;
    }

    if (output_size <= 4)
    {
        output[0] = '\0';
        return;
    }

    size_t copy_len = output_size - 4;
    memcpy(output, input, copy_len);
    output[copy_len] = '.';
    output[copy_len + 1] = '.';
    output[copy_len + 2] = '.';
    output[copy_len + 3] = '\0';
}

static int libmpv_ms_from_seconds(double seconds)
{
    if (!(seconds >= 0.0))
        return 0;

    double ms = seconds * 1000.0;
    if (ms >= (double)INT_MAX)
        return INT_MAX;
    return (int)(ms + 0.5);
}

static void libmpv_emit_event_locked(PlayerEventType type)
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
        .uri = g_uri
    };
    g_event_sink(&event);
}

static bool libmpv_get_flag_locked(const char *name, bool *out)
{
    int value = 0;
    int rc = mpv_get_property(g_mpv, name, MPV_FORMAT_FLAG, &value);
    if (rc < 0)
        return false;

    if (out)
        *out = value != 0;
    return true;
}

static bool libmpv_get_double_locked(const char *name, double *out)
{
    double value = 0.0;
    int rc = mpv_get_property(g_mpv, name, MPV_FORMAT_DOUBLE, &value);
    if (rc < 0)
        return false;

    if (out)
        *out = value;
    return true;
}

static bool libmpv_set_flag_locked(const char *name, bool value)
{
    int flag = value ? 1 : 0;
    int rc = mpv_set_property(g_mpv, name, MPV_FORMAT_FLAG, &flag);
    if (rc < 0)
    {
        log_warn("[player-libmpv] set_property %s=%d failed: %s\n",
                 name,
                 flag,
                 mpv_error_string(rc));
        return false;
    }
    return true;
}

static bool libmpv_set_double_locked(const char *name, double value)
{
    int rc = mpv_set_property(g_mpv, name, MPV_FORMAT_DOUBLE, &value);
    if (rc < 0)
    {
        log_warn("[player-libmpv] set_property %s=%.3f failed: %s\n",
                 name,
                 value,
                 mpv_error_string(rc));
        return false;
    }
    return true;
}

static bool libmpv_command_locked(const char *action, const char **args)
{
    int rc = mpv_command(g_mpv, args);
    if (rc < 0)
    {
        log_warn("[player-libmpv] command %s failed: %s\n", action, mpv_error_string(rc));
        return false;
    }
    return true;
}

static bool libmpv_load_uri_locked(const char *uri, bool paused)
{
    const char *options = paused ? "pause=yes" : "pause=no";
    // mpv 0.38+ inserts an optional playlist index as the 3rd argument of
    // loadfile. If we want to pass per-file options, we must explicitly pass
    // "-1" as the index placeholder and move options to the 4th argument.
    const char *cmd[] = {"loadfile", uri, "replace", "-1", options, NULL};
    return libmpv_command_locked(paused ? "loadfile(pause=yes)" : "loadfile(pause=no)", cmd);
}

static bool libmpv_seek_absolute_locked(int position_ms)
{
    char seconds_buffer[32];
    double seconds = (double)position_ms / 1000.0;
    snprintf(seconds_buffer, sizeof(seconds_buffer), "%.3f", seconds);

    const char *cmd[] = {"seek", seconds_buffer, "absolute", "exact", NULL};
    return libmpv_command_locked("seek", cmd);
}

static bool libmpv_stop_command_locked(void)
{
    const char *cmd[] = {"stop", NULL};
    return libmpv_command_locked("stop", cmd);
}

static void libmpv_refresh_snapshot_locked(void)
{
    if (!g_mpv)
        return;

    libmpv_drain_events_locked();

    bool paused = true;
    bool idle_active = false;
    bool eof_reached = false;
    double playback_seconds = 0.0;
    double duration_seconds = 0.0;
    double volume = 0.0;
    bool mute = false;

    if (libmpv_get_flag_locked("pause", &paused))
    {
        // keep local paused value only when property is available
    }
    libmpv_get_flag_locked("idle-active", &idle_active);

    if (libmpv_get_flag_locked("eof-reached", &eof_reached) && eof_reached)
    {
        g_stop_mode = true;
        g_media_loaded = false;
    }

    if (libmpv_get_double_locked("playback-time", &playback_seconds))
        g_position_ms = libmpv_ms_from_seconds(playback_seconds);
    else if (g_stop_mode)
        g_position_ms = 0;

    if (libmpv_get_double_locked("duration", &duration_seconds))
        g_duration_ms = libmpv_ms_from_seconds(duration_seconds);

    if (libmpv_get_double_locked("volume", &volume))
    {
        if (volume < 0.0)
            volume = 0.0;
        if (volume > 100.0)
            volume = 100.0;
        g_volume = (int)(volume + 0.5);
    }

    if (libmpv_get_flag_locked("mute", &mute))
        g_mute = mute;

    if (g_uri[0] == '\0')
    {
        g_state = PLAYER_STATE_STOPPED;
        return;
    }

    if (g_state == PLAYER_STATE_ERROR)
        return;

    if (g_stop_mode || (!g_media_loaded && idle_active))
        g_state = PLAYER_STATE_STOPPED;
    else if (paused)
        g_state = PLAYER_STATE_PAUSED;
    else
        g_state = PLAYER_STATE_PLAYING;
}

static bool libmpv_available(void)
{
    return true;
}

static bool libmpv_init(void)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);

    memset(g_uri, 0, sizeof(g_uri));
    memset(g_metadata, 0, sizeof(g_metadata));
    g_state = PLAYER_STATE_STOPPED;
    g_position_ms = 0;
    g_duration_ms = 0;
    g_volume = 20;
    g_mute = false;
    g_media_loaded = false;
    g_stop_mode = true;

    g_mpv = mpv_create();
    if (!g_mpv)
    {
        mutexUnlock(&g_state_mutex);
        log_error("[player-libmpv] mpv_create failed\n");
        return false;
    }

    mpv_set_option_string(g_mpv, "config", "no");
    mpv_set_option_string(g_mpv, "terminal", "no");
    mpv_set_option_string(g_mpv, "idle", "yes");
    mpv_set_option_string(g_mpv, "input-default-bindings", "no");
    mpv_set_option_string(g_mpv, "input-vo-keyboard", "no");
    mpv_set_option_string(g_mpv, "osc", "no");
    mpv_set_option_string(g_mpv, "audio-display", "no");
    mpv_set_option_string(g_mpv, "ao", "null");
    // Step 1 only binds DLNA control to a real playback core. Video rendering
    // stays disabled until the later render/deko3d design is in place.
    mpv_set_option_string(g_mpv, "vo", "null");

    int rc = mpv_initialize(g_mpv);
    if (rc < 0)
    {
        mpv_terminate_destroy(g_mpv);
        g_mpv = NULL;
        mutexUnlock(&g_state_mutex);
        log_error("[player-libmpv] mpv_initialize failed: %s\n", mpv_error_string(rc));
        return false;
    }

    mpv_request_log_messages(g_mpv, "warn");

    libmpv_set_double_locked("volume", (double)g_volume);
    libmpv_set_flag_locked("mute", g_mute);
    libmpv_refresh_snapshot_locked();
    libmpv_emit_event_locked(PLAYER_EVENT_STATE_CHANGED);
    libmpv_emit_event_locked(PLAYER_EVENT_VOLUME_CHANGED);
    libmpv_emit_event_locked(PLAYER_EVENT_MUTE_CHANGED);
    mutexUnlock(&g_state_mutex);

    log_info("[player-libmpv] init mode=control-only ao=null vo=null\n");
    return true;
}

static void libmpv_deinit(void)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);

    if (g_mpv)
    {
        mpv_terminate_destroy(g_mpv);
        g_mpv = NULL;
    }

    memset(g_uri, 0, sizeof(g_uri));
    memset(g_metadata, 0, sizeof(g_metadata));
    g_state = PLAYER_STATE_STOPPED;
    g_position_ms = 0;
    g_duration_ms = 0;
    g_media_loaded = false;
    g_stop_mode = true;
    g_event_sink = NULL;
    mutexUnlock(&g_state_mutex);

    log_info("[player-libmpv] deinit\n");
}

static void libmpv_set_event_sink(void (*sink)(const PlayerEvent *event))
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);
    g_event_sink = sink;
    mutexUnlock(&g_state_mutex);
}

static bool libmpv_set_uri(const char *uri, const char *metadata)
{
    if (!uri || uri[0] == '\0')
        return false;

    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);

    if (!g_mpv)
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    char clipped_uri[160];
    libmpv_clip_for_log(uri, clipped_uri, sizeof(clipped_uri));

    if (!libmpv_load_uri_locked(uri, true))
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    snprintf(g_uri, sizeof(g_uri), "%s", uri);
    if (metadata)
        snprintf(g_metadata, sizeof(g_metadata), "%s", metadata);
    else
        g_metadata[0] = '\0';

    g_position_ms = 0;
    g_duration_ms = 0;
    g_stop_mode = true;
    g_media_loaded = false;
    g_state = PLAYER_STATE_STOPPED;
    libmpv_refresh_snapshot_locked();

    log_info("[player-libmpv] set_uri uri=%s metadata_len=%zu\n", clipped_uri, strlen(g_metadata));
    libmpv_emit_event_locked(PLAYER_EVENT_URI_CHANGED);
    libmpv_emit_event_locked(PLAYER_EVENT_DURATION_CHANGED);
    libmpv_emit_event_locked(PLAYER_EVENT_POSITION_CHANGED);
    libmpv_emit_event_locked(PLAYER_EVENT_STATE_CHANGED);
    mutexUnlock(&g_state_mutex);
    return true;
}

static bool libmpv_play(void)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);

    if (!g_mpv || g_uri[0] == '\0')
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    libmpv_refresh_snapshot_locked();

    bool reloaded = false;
    if (!g_media_loaded)
    {
        if (!libmpv_load_uri_locked(g_uri, false))
        {
            mutexUnlock(&g_state_mutex);
            return false;
        }
        g_position_ms = 0;
        g_duration_ms = 0;
        g_media_loaded = false;
        reloaded = true;
    }
    else if (!libmpv_set_flag_locked("pause", false))
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    g_stop_mode = false;
    libmpv_refresh_snapshot_locked();
    g_state = PLAYER_STATE_PLAYING;

    log_info("[player-libmpv] play reloaded=%d\n", reloaded ? 1 : 0);
    if (reloaded)
        libmpv_emit_event_locked(PLAYER_EVENT_POSITION_CHANGED);
    libmpv_emit_event_locked(PLAYER_EVENT_STATE_CHANGED);
    mutexUnlock(&g_state_mutex);
    return true;
}

static bool libmpv_pause(void)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);

    if (!g_mpv)
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    libmpv_refresh_snapshot_locked();
    if (g_state != PLAYER_STATE_PLAYING)
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    if (!libmpv_set_flag_locked("pause", true))
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    g_stop_mode = false;
    libmpv_refresh_snapshot_locked();
    g_state = PLAYER_STATE_PAUSED;

    log_info("[player-libmpv] pause\n");
    libmpv_emit_event_locked(PLAYER_EVENT_POSITION_CHANGED);
    libmpv_emit_event_locked(PLAYER_EVENT_STATE_CHANGED);
    mutexUnlock(&g_state_mutex);
    return true;
}

static bool libmpv_stop(void)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);

    if (!g_mpv || g_uri[0] == '\0')
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    if (g_stop_mode || !g_media_loaded)
    {
        g_position_ms = 0;
        g_stop_mode = true;
        g_media_loaded = false;
        g_state = PLAYER_STATE_STOPPED;
        log_info("[player-libmpv] stop already-stopped=1\n");
        libmpv_emit_event_locked(PLAYER_EVENT_POSITION_CHANGED);
        libmpv_emit_event_locked(PLAYER_EVENT_STATE_CHANGED);
        mutexUnlock(&g_state_mutex);
        return true;
    }

    if (!libmpv_stop_command_locked())
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    g_position_ms = 0;
    g_stop_mode = true;
    g_media_loaded = false;
    g_state = PLAYER_STATE_STOPPED;

    log_info("[player-libmpv] stop\n");
    libmpv_emit_event_locked(PLAYER_EVENT_POSITION_CHANGED);
    libmpv_emit_event_locked(PLAYER_EVENT_STATE_CHANGED);
    mutexUnlock(&g_state_mutex);
    return true;
}

static bool libmpv_seek_ms(int position_ms)
{
    if (position_ms < 0)
        return false;

    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);

    if (!g_mpv || g_uri[0] == '\0')
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    if (!g_media_loaded)
    {
        if (!libmpv_load_uri_locked(g_uri, true))
        {
            mutexUnlock(&g_state_mutex);
            return false;
        }
        g_media_loaded = false;
        g_stop_mode = true;
    }

    if (g_duration_ms > 0 && position_ms > g_duration_ms)
        position_ms = g_duration_ms;

    if (!libmpv_seek_absolute_locked(position_ms))
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    g_position_ms = position_ms;
    libmpv_refresh_snapshot_locked();
    if (g_stop_mode)
        g_state = PLAYER_STATE_STOPPED;

    log_info("[player-libmpv] seek_ms=%d\n", g_position_ms);
    libmpv_emit_event_locked(PLAYER_EVENT_POSITION_CHANGED);
    mutexUnlock(&g_state_mutex);
    return true;
}

static bool libmpv_set_volume(int volume_0_100)
{
    if (volume_0_100 < 0)
        volume_0_100 = 0;
    if (volume_0_100 > 100)
        volume_0_100 = 100;

    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);

    if (!g_mpv)
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    if (!libmpv_set_double_locked("volume", (double)volume_0_100))
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    g_volume = volume_0_100;
    libmpv_refresh_snapshot_locked();
    log_info("[player-libmpv] set_volume=%d\n", g_volume);
    libmpv_emit_event_locked(PLAYER_EVENT_VOLUME_CHANGED);
    mutexUnlock(&g_state_mutex);
    return true;
}

static bool libmpv_set_mute(bool mute)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);

    if (!g_mpv)
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    if (!libmpv_set_flag_locked("mute", mute))
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    g_mute = mute;
    libmpv_refresh_snapshot_locked();
    log_info("[player-libmpv] set_mute=%d\n", g_mute ? 1 : 0);
    libmpv_emit_event_locked(PLAYER_EVENT_MUTE_CHANGED);
    mutexUnlock(&g_state_mutex);
    return true;
}

static int libmpv_get_position_ms(void)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);
    libmpv_refresh_snapshot_locked();
    int position_ms = g_position_ms;
    mutexUnlock(&g_state_mutex);
    return position_ms;
}

static int libmpv_get_duration_ms(void)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);
    libmpv_refresh_snapshot_locked();
    int duration_ms = g_duration_ms;
    mutexUnlock(&g_state_mutex);
    return duration_ms;
}

static int libmpv_get_volume(void)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);
    libmpv_refresh_snapshot_locked();
    int volume = g_volume;
    mutexUnlock(&g_state_mutex);
    return volume;
}

static bool libmpv_get_mute(void)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);
    libmpv_refresh_snapshot_locked();
    bool mute = g_mute;
    mutexUnlock(&g_state_mutex);
    return mute;
}

static PlayerState libmpv_get_state(void)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);
    libmpv_refresh_snapshot_locked();
    PlayerState state = g_state;
    mutexUnlock(&g_state_mutex);
    return state;
}

#else

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

#endif

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
