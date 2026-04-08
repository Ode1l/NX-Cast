#include "player/backend.h"

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "log/log.h"

#ifdef HAVE_LIBMPV
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>

typedef enum
{
    LIBMPV_OBS_VOLUME = 1,
    LIBMPV_OBS_TIME_POS,
    LIBMPV_OBS_PAUSE,
    LIBMPV_OBS_MUTE,
    LIBMPV_OBS_DURATION,
    LIBMPV_OBS_SEEKABLE,
    LIBMPV_OBS_IDLE_ACTIVE,
    LIBMPV_OBS_PAUSED_FOR_CACHE,
    LIBMPV_OBS_SEEKING
} LibmpvObservedProperty;

typedef enum
{
    LIBMPV_REPLY_LOADFILE = 100,
    LIBMPV_REPLY_PLAY,
    LIBMPV_REPLY_PAUSE,
    LIBMPV_REPLY_STOP,
    LIBMPV_REPLY_SEEK,
    LIBMPV_REPLY_SET_VOLUME,
    LIBMPV_REPLY_SET_MUTE
} LibmpvReplyId;

typedef struct
{
    PlayerEvent events[4];
    size_t count;
} LibmpvPendingEvents;

static void (*g_event_sink)(const PlayerEvent *event) = NULL;
static Mutex g_mutex;
static bool g_sync_ready = false;

static mpv_handle *g_mpv = NULL;
static mpv_render_context *g_render_ctx = NULL;
static bool g_render_update_pending = false;

static bool g_has_media = false;
static char *g_uri = NULL;
static PlayerState g_state = PLAYER_STATE_IDLE;
static int g_position_ms = 0;
static int g_duration_ms = 0;
static int g_volume = PLAYER_DEFAULT_VOLUME;
static bool g_mute = false;
static bool g_seekable = false;
static bool g_pause = true;
static bool g_paused_for_cache = false;
static bool g_seeking = false;
static bool g_idle_active = true;
static bool g_file_loaded = false;
static int g_last_error = 0;

static void libmpv_ensure_sync(void)
{
    if (g_sync_ready)
        return;
    mutexInit(&g_mutex);
    g_sync_ready = true;
}

static int libmpv_double_to_ms(double value)
{
    if (value <= 0.0)
        return 0;
    return (int)(value * 1000.0 + 0.5);
}

static char *libmpv_format_seek_target(int position_ms)
{
    int needed;
    char *target;

    needed = snprintf(NULL, 0, "%.3f", position_ms / 1000.0);
    if (needed < 0)
        return NULL;

    target = malloc((size_t)needed + 1);
    if (!target)
        return NULL;

    snprintf(target, (size_t)needed + 1, "%.3f", position_ms / 1000.0);
    return target;
}

static bool libmpv_set_uri_locked(const char *uri)
{
    char *copy = NULL;

    if (uri)
    {
        copy = strdup(uri);
        if (!copy)
            return false;
    }

    free(g_uri);
    g_uri = copy;
    return true;
}

static void libmpv_queue_event_locked(LibmpvPendingEvents *pending, PlayerEventType type)
{
    PlayerEvent *event = NULL;
    const char *uri = NULL;

    if (!pending || pending->count >= (sizeof(pending->events) / sizeof(pending->events[0])))
        return;

    event = &pending->events[pending->count++];
    memset(event, 0, sizeof(*event));
    event->type = type;
    event->state = g_state;
    event->position_ms = g_position_ms;
    event->duration_ms = g_duration_ms;
    event->volume = g_volume;
    event->mute = g_mute;
    event->seekable = g_seekable;
    event->error_code = g_last_error;
    uri = g_has_media ? g_uri : NULL;
    if (uri)
        event->uri = strdup(uri);
}

static void libmpv_flush_events(LibmpvPendingEvents *pending)
{
    if (!pending)
        return;

    for (size_t i = 0; i < pending->count; ++i)
    {
        if (g_event_sink)
            g_event_sink(&pending->events[i]);
        player_event_clear(&pending->events[i]);
    }
}

static PlayerState libmpv_derive_state_locked(void)
{
    if (g_last_error != 0)
        return PLAYER_STATE_ERROR;
    if (!g_has_media || !g_uri || g_uri[0] == '\0')
        return PLAYER_STATE_IDLE;
    if (g_idle_active)
        return PLAYER_STATE_STOPPED;
    if (!g_file_loaded)
        return PLAYER_STATE_LOADING;
    if (g_seeking)
        return PLAYER_STATE_SEEKING;
    if (g_paused_for_cache && !g_pause)
        return PLAYER_STATE_BUFFERING;
    return g_pause ? PLAYER_STATE_PAUSED : PLAYER_STATE_PLAYING;
}

static void libmpv_refresh_state_locked(LibmpvPendingEvents *pending)
{
    PlayerState next_state = libmpv_derive_state_locked();
    if (next_state != g_state)
    {
        g_state = next_state;
        libmpv_queue_event_locked(pending, PLAYER_EVENT_STATE_CHANGED);
    }
}

static void libmpv_set_position_locked(int value, LibmpvPendingEvents *pending)
{
    if (value < 0)
        value = 0;
    if (value != g_position_ms)
    {
        g_position_ms = value;
        libmpv_queue_event_locked(pending, PLAYER_EVENT_POSITION_CHANGED);
    }
}

static void libmpv_set_duration_locked(int value, LibmpvPendingEvents *pending)
{
    if (value < 0)
        value = 0;
    if (value != g_duration_ms)
    {
        g_duration_ms = value;
        libmpv_queue_event_locked(pending, PLAYER_EVENT_DURATION_CHANGED);
    }
}

static void libmpv_set_volume_locked(int value, LibmpvPendingEvents *pending)
{
    if (value < 0)
        value = 0;
    if (value > 100)
        value = 100;
    if (value != g_volume)
    {
        g_volume = value;
        libmpv_queue_event_locked(pending, PLAYER_EVENT_VOLUME_CHANGED);
    }
}

static void libmpv_set_mute_locked(bool value, LibmpvPendingEvents *pending)
{
    if (value != g_mute)
    {
        g_mute = value;
        libmpv_queue_event_locked(pending, PLAYER_EVENT_MUTE_CHANGED);
    }
}

static void libmpv_set_seekable_locked(bool value)
{
    g_seekable = value;
}

static void libmpv_reset_locked(void)
{
    free(g_uri);
    g_uri = NULL;
    g_has_media = false;
    g_state = PLAYER_STATE_IDLE;
    g_position_ms = 0;
    g_duration_ms = 0;
    g_volume = PLAYER_DEFAULT_VOLUME;
    g_mute = false;
    g_seekable = false;
    g_pause = true;
    g_paused_for_cache = false;
    g_seeking = false;
    g_idle_active = true;
    g_file_loaded = false;
    g_last_error = 0;
    g_render_update_pending = false;
}

static bool libmpv_async_load_current(bool paused)
{
    const char *args[6];
    const char *options = paused ? "pause=yes" : "pause=no";
    int rc;
    LibmpvPendingEvents pending = {0};

    mutexLock(&g_mutex);
    if (!g_mpv || !g_has_media || !g_uri || g_uri[0] == '\0')
    {
        mutexUnlock(&g_mutex);
        return false;
    }

    args[0] = "loadfile";
    args[1] = g_uri;
    args[2] = "replace";
    args[3] = "-1";
    args[4] = options;
    args[5] = NULL;

    rc = mpv_command_async(g_mpv, LIBMPV_REPLY_LOADFILE, args);
    if (rc < 0)
    {
        log_error("[player-libmpv] loadfile failed: %s\n", mpv_error_string(rc));
        mutexUnlock(&g_mutex);
        return false;
    }

    g_last_error = 0;
    g_pause = paused;
    g_idle_active = false;
    g_file_loaded = false;
    g_seeking = false;
    g_paused_for_cache = false;
    libmpv_set_position_locked(0, &pending);
    libmpv_set_duration_locked(0, &pending);
    libmpv_refresh_state_locked(&pending);
    libmpv_queue_event_locked(&pending, PLAYER_EVENT_URI_CHANGED);
    mutexUnlock(&g_mutex);

    libmpv_flush_events(&pending);
    return true;
}

static void libmpv_on_render_update(void *ctx)
{
    (void)ctx;
    mutexLock(&g_mutex);
    g_render_update_pending = true;
    mutexUnlock(&g_mutex);
}

static void libmpv_log_reply_error(uint64_t reply_userdata, int error)
{
    const char *name = "unknown";

    switch (reply_userdata)
    {
    case LIBMPV_REPLY_LOADFILE:
        name = "loadfile";
        break;
    case LIBMPV_REPLY_PLAY:
        name = "play";
        break;
    case LIBMPV_REPLY_PAUSE:
        name = "pause";
        break;
    case LIBMPV_REPLY_STOP:
        name = "stop";
        break;
    case LIBMPV_REPLY_SEEK:
        name = "seek";
        break;
    case LIBMPV_REPLY_SET_VOLUME:
        name = "set-volume";
        break;
    case LIBMPV_REPLY_SET_MUTE:
        name = "set-mute";
        break;
    default:
        break;
    }

    log_warn("[player-libmpv] async reply failed op=%s error=%s\n",
             name,
             mpv_error_string(error));
}

static void libmpv_handle_property_change(const mpv_event_property *prop, uint64_t reply_userdata)
{
    LibmpvPendingEvents pending = {0};

    if (!prop)
        return;

    mutexLock(&g_mutex);
    switch (reply_userdata)
    {
    case LIBMPV_OBS_VOLUME:
        if (prop->format == MPV_FORMAT_DOUBLE && prop->data)
            libmpv_set_volume_locked((int)(*(double *)prop->data + 0.5), &pending);
        break;
    case LIBMPV_OBS_TIME_POS:
        if (prop->format == MPV_FORMAT_DOUBLE && prop->data)
            libmpv_set_position_locked(libmpv_double_to_ms(*(double *)prop->data), &pending);
        else if (prop->format == MPV_FORMAT_NONE)
            libmpv_set_position_locked(0, &pending);
        break;
    case LIBMPV_OBS_PAUSE:
        if (prop->format == MPV_FORMAT_FLAG && prop->data)
            g_pause = (*(int *)prop->data) != 0;
        libmpv_refresh_state_locked(&pending);
        break;
    case LIBMPV_OBS_MUTE:
        if (prop->format == MPV_FORMAT_FLAG && prop->data)
            libmpv_set_mute_locked((*(int *)prop->data) != 0, &pending);
        break;
    case LIBMPV_OBS_DURATION:
        if (prop->format == MPV_FORMAT_DOUBLE && prop->data)
            libmpv_set_duration_locked(libmpv_double_to_ms(*(double *)prop->data), &pending);
        else if (prop->format == MPV_FORMAT_NONE)
            libmpv_set_duration_locked(0, &pending);
        break;
    case LIBMPV_OBS_SEEKABLE:
        if (prop->format == MPV_FORMAT_FLAG && prop->data)
            libmpv_set_seekable_locked((*(int *)prop->data) != 0);
        break;
    case LIBMPV_OBS_IDLE_ACTIVE:
        if (prop->format == MPV_FORMAT_FLAG && prop->data)
            g_idle_active = (*(int *)prop->data) != 0;
        libmpv_refresh_state_locked(&pending);
        break;
    case LIBMPV_OBS_PAUSED_FOR_CACHE:
        if (prop->format == MPV_FORMAT_FLAG && prop->data)
            g_paused_for_cache = (*(int *)prop->data) != 0;
        libmpv_refresh_state_locked(&pending);
        break;
    case LIBMPV_OBS_SEEKING:
        if (prop->format == MPV_FORMAT_FLAG && prop->data)
            g_seeking = (*(int *)prop->data) != 0;
        libmpv_refresh_state_locked(&pending);
        break;
    default:
        break;
    }
    mutexUnlock(&g_mutex);

    libmpv_flush_events(&pending);
}

static void libmpv_handle_event(mpv_event *event)
{
    LibmpvPendingEvents pending = {0};

    if (!event)
        return;

    switch (event->event_id)
    {
    case MPV_EVENT_START_FILE:
        mutexLock(&g_mutex);
        g_last_error = 0;
        g_idle_active = false;
        g_file_loaded = false;
        g_seeking = false;
        libmpv_refresh_state_locked(&pending);
        mutexUnlock(&g_mutex);
        libmpv_flush_events(&pending);
        break;
    case MPV_EVENT_FILE_LOADED:
        mutexLock(&g_mutex);
        g_last_error = 0;
        g_idle_active = false;
        g_file_loaded = true;
        libmpv_refresh_state_locked(&pending);
        mutexUnlock(&g_mutex);
        libmpv_flush_events(&pending);
        break;
    case MPV_EVENT_SEEK:
        mutexLock(&g_mutex);
        g_seeking = true;
        libmpv_refresh_state_locked(&pending);
        mutexUnlock(&g_mutex);
        libmpv_flush_events(&pending);
        break;
    case MPV_EVENT_PLAYBACK_RESTART:
        mutexLock(&g_mutex);
        g_file_loaded = true;
        g_idle_active = false;
        g_seeking = false;
        libmpv_refresh_state_locked(&pending);
        mutexUnlock(&g_mutex);
        libmpv_flush_events(&pending);
        break;
    case MPV_EVENT_END_FILE:
    {
        mpv_event_end_file *end = (mpv_event_end_file *)event->data;

        mutexLock(&g_mutex);
        g_idle_active = true;
        g_file_loaded = false;
        g_seeking = false;
        g_paused_for_cache = false;
        g_pause = true;

        if (end && end->reason == MPV_END_FILE_REASON_ERROR)
        {
            g_last_error = end->error;
            libmpv_refresh_state_locked(&pending);
            libmpv_queue_event_locked(&pending, PLAYER_EVENT_ERROR);
        }
        else
        {
            g_last_error = 0;
            if (end && end->reason == MPV_END_FILE_REASON_EOF && g_duration_ms > 0)
                libmpv_set_position_locked(g_duration_ms, &pending);
            else
                libmpv_set_position_locked(0, &pending);
            libmpv_refresh_state_locked(&pending);
        }
        mutexUnlock(&g_mutex);

        libmpv_flush_events(&pending);
        break;
    }
    case MPV_EVENT_PROPERTY_CHANGE:
        libmpv_handle_property_change((mpv_event_property *)event->data, event->reply_userdata);
        break;
    case MPV_EVENT_COMMAND_REPLY:
    case MPV_EVENT_SET_PROPERTY_REPLY:
        if (event->error < 0)
        {
            mutexLock(&g_mutex);
            g_last_error = event->error;
            libmpv_refresh_state_locked(&pending);
            libmpv_queue_event_locked(&pending, PLAYER_EVENT_ERROR);
            mutexUnlock(&g_mutex);
            libmpv_log_reply_error(event->reply_userdata, event->error);
            libmpv_flush_events(&pending);
        }
        break;
    case MPV_EVENT_LOG_MESSAGE:
        break;
    case MPV_EVENT_SHUTDOWN:
        mutexLock(&g_mutex);
        g_idle_active = true;
        g_file_loaded = false;
        g_seeking = false;
        libmpv_refresh_state_locked(&pending);
        mutexUnlock(&g_mutex);
        libmpv_flush_events(&pending);
        break;
    default:
        break;
    }
}

static bool libmpv_available(void)
{
    return true;
}

static bool libmpv_init(void)
{
    int rc;

    libmpv_ensure_sync();
    mutexLock(&g_mutex);
    libmpv_reset_locked();
    mutexUnlock(&g_mutex);

    g_mpv = mpv_create();
    if (!g_mpv)
    {
        log_error("[player-libmpv] mpv_create failed\n");
        return false;
    }

    mpv_set_option_string(g_mpv, "config", "no");
    mpv_set_option_string(g_mpv, "terminal", "no");
    mpv_set_option_string(g_mpv, "input-default-bindings", "no");
    mpv_set_option_string(g_mpv, "input-vo-keyboard", "no");
    mpv_set_option_string(g_mpv, "osc", "no");
    mpv_set_option_string(g_mpv, "osd-level", "0");
    mpv_set_option_string(g_mpv, "audio-display", "no");
    mpv_set_option_string(g_mpv, "idle", "yes");
    mpv_set_option_string(g_mpv, "vo", "libmpv");
    mpv_set_option_string(g_mpv, "ao", "hos");

    rc = mpv_initialize(g_mpv);
    if (rc < 0)
    {
        log_error("[player-libmpv] mpv_initialize failed: %s\n", mpv_error_string(rc));
        mpv_terminate_destroy(g_mpv);
        g_mpv = NULL;
        return false;
    }

    mpv_request_log_messages(g_mpv, "warn");
    mpv_observe_property(g_mpv, LIBMPV_OBS_VOLUME, "volume", MPV_FORMAT_DOUBLE);
    mpv_observe_property(g_mpv, LIBMPV_OBS_TIME_POS, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(g_mpv, LIBMPV_OBS_PAUSE, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(g_mpv, LIBMPV_OBS_MUTE, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(g_mpv, LIBMPV_OBS_DURATION, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(g_mpv, LIBMPV_OBS_SEEKABLE, "seekable", MPV_FORMAT_FLAG);
    mpv_observe_property(g_mpv, LIBMPV_OBS_IDLE_ACTIVE, "idle-active", MPV_FORMAT_FLAG);
    mpv_observe_property(g_mpv, LIBMPV_OBS_PAUSED_FOR_CACHE, "paused-for-cache", MPV_FORMAT_FLAG);
    mpv_observe_property(g_mpv, LIBMPV_OBS_SEEKING, "seeking", MPV_FORMAT_FLAG);

    log_info("[player-libmpv] init\n");
    return true;
}

static void libmpv_deinit(void)
{
    mpv_render_context *render_ctx = NULL;
    mpv_handle *mpv = NULL;

    mutexLock(&g_mutex);
    render_ctx = g_render_ctx;
    g_render_ctx = NULL;
    mpv = g_mpv;
    g_mpv = NULL;
    libmpv_reset_locked();
    mutexUnlock(&g_mutex);

    if (render_ctx)
        mpv_render_context_free(render_ctx);
    if (mpv)
        mpv_terminate_destroy(mpv);

    log_info("[player-libmpv] deinit\n");
}

static void libmpv_set_event_sink(void (*sink)(const PlayerEvent *event))
{
    g_event_sink = sink;
}

static bool libmpv_set_media(const PlayerMedia *media)
{
    char *previous_uri = NULL;
    bool previous_has_media;
    int previous_last_error;

    if (!media || !media->uri || media->uri[0] == '\0')
        return false;

    mutexLock(&g_mutex);
    previous_uri = g_uri;
    previous_has_media = g_has_media;
    previous_last_error = g_last_error;

    g_uri = NULL;
    if (!libmpv_set_uri_locked(media->uri))
    {
        g_uri = previous_uri;
        mutexUnlock(&g_mutex);
        return false;
    }
    g_has_media = true;
    g_last_error = 0;
    mutexUnlock(&g_mutex);

    if (libmpv_async_load_current(true))
    {
        free(previous_uri);
        return true;
    }

    mutexLock(&g_mutex);
    free(g_uri);
    g_uri = previous_uri;
    g_has_media = previous_has_media;
    g_last_error = previous_last_error;
    mutexUnlock(&g_mutex);
    return false;
}

static bool libmpv_play(void)
{
    int pause_flag = 0;
    int rc;
    LibmpvPendingEvents pending = {0};

    mutexLock(&g_mutex);
    if (!g_mpv || !g_has_media || !g_uri || g_uri[0] == '\0')
    {
        mutexUnlock(&g_mutex);
        return false;
    }

    if (g_state == PLAYER_STATE_STOPPED || g_idle_active)
    {
        mutexUnlock(&g_mutex);
        return libmpv_async_load_current(false);
    }

    rc = mpv_set_property_async(g_mpv, LIBMPV_REPLY_PLAY, "pause", MPV_FORMAT_FLAG, &pause_flag);
    if (rc < 0)
    {
        log_warn("[player-libmpv] play failed: %s\n", mpv_error_string(rc));
        mutexUnlock(&g_mutex);
        return false;
    }

    g_last_error = 0;
    g_pause = false;
    libmpv_refresh_state_locked(&pending);
    mutexUnlock(&g_mutex);

    libmpv_flush_events(&pending);
    return true;
}

static bool libmpv_pause(void)
{
    int pause_flag = 1;
    int rc;
    LibmpvPendingEvents pending = {0};

    mutexLock(&g_mutex);
    if (!g_mpv || !g_has_media || !g_uri || g_uri[0] == '\0' || g_state == PLAYER_STATE_STOPPED || g_idle_active)
    {
        mutexUnlock(&g_mutex);
        return false;
    }

    rc = mpv_set_property_async(g_mpv, LIBMPV_REPLY_PAUSE, "pause", MPV_FORMAT_FLAG, &pause_flag);
    if (rc < 0)
    {
        log_warn("[player-libmpv] pause failed: %s\n", mpv_error_string(rc));
        mutexUnlock(&g_mutex);
        return false;
    }

    g_last_error = 0;
    g_pause = true;
    libmpv_refresh_state_locked(&pending);
    mutexUnlock(&g_mutex);

    libmpv_flush_events(&pending);
    return true;
}

static bool libmpv_stop(void)
{
    const char *args[] = {"stop", NULL};
    int rc;
    LibmpvPendingEvents pending = {0};

    mutexLock(&g_mutex);
    if (!g_mpv || !g_has_media || !g_uri || g_uri[0] == '\0')
    {
        mutexUnlock(&g_mutex);
        return false;
    }

    rc = mpv_command_async(g_mpv, LIBMPV_REPLY_STOP, args);
    if (rc < 0)
    {
        log_warn("[player-libmpv] stop failed: %s\n", mpv_error_string(rc));
        mutexUnlock(&g_mutex);
        return false;
    }

    g_last_error = 0;
    g_idle_active = true;
    g_file_loaded = false;
    g_seeking = false;
    g_paused_for_cache = false;
    g_pause = true;
    libmpv_set_position_locked(0, &pending);
    libmpv_refresh_state_locked(&pending);
    mutexUnlock(&g_mutex);

    libmpv_flush_events(&pending);
    return true;
}

static bool libmpv_seek_ms(int position_ms)
{
    const char *args[4];
    char *target = NULL;
    int rc;
    LibmpvPendingEvents pending = {0};

    if (position_ms < 0)
        position_ms = 0;

    target = libmpv_format_seek_target(position_ms);
    if (!target)
        return false;

    mutexLock(&g_mutex);
    if (!g_mpv || !g_has_media || !g_uri || g_uri[0] == '\0' || g_state == PLAYER_STATE_STOPPED || g_idle_active)
    {
        mutexUnlock(&g_mutex);
        free(target);
        return false;
    }

    args[0] = "seek";
    args[1] = target;
    args[2] = "absolute+exact";
    args[3] = NULL;

    rc = mpv_command_async(g_mpv, LIBMPV_REPLY_SEEK, args);
    if (rc < 0)
    {
        log_warn("[player-libmpv] seek failed: %s\n", mpv_error_string(rc));
        mutexUnlock(&g_mutex);
        free(target);
        return false;
    }

    g_last_error = 0;
    g_seeking = true;
    libmpv_set_position_locked(position_ms, &pending);
    libmpv_refresh_state_locked(&pending);
    mutexUnlock(&g_mutex);

    libmpv_flush_events(&pending);
    free(target);
    return true;
}

static bool libmpv_set_volume(int volume_0_100)
{
    double volume;
    int rc;
    LibmpvPendingEvents pending = {0};

    if (volume_0_100 < 0)
        volume_0_100 = 0;
    if (volume_0_100 > 100)
        volume_0_100 = 100;
    volume = (double)volume_0_100;

    mutexLock(&g_mutex);
    if (!g_mpv)
    {
        mutexUnlock(&g_mutex);
        return false;
    }

    rc = mpv_set_property_async(g_mpv, LIBMPV_REPLY_SET_VOLUME, "volume", MPV_FORMAT_DOUBLE, &volume);
    if (rc < 0)
    {
        log_warn("[player-libmpv] set_volume failed: %s\n", mpv_error_string(rc));
        mutexUnlock(&g_mutex);
        return false;
    }

    libmpv_set_volume_locked(volume_0_100, &pending);
    mutexUnlock(&g_mutex);

    libmpv_flush_events(&pending);
    return true;
}

static bool libmpv_set_mute(bool mute)
{
    int mute_flag = mute ? 1 : 0;
    int rc;
    LibmpvPendingEvents pending = {0};

    mutexLock(&g_mutex);
    if (!g_mpv)
    {
        mutexUnlock(&g_mutex);
        return false;
    }

    rc = mpv_set_property_async(g_mpv, LIBMPV_REPLY_SET_MUTE, "mute", MPV_FORMAT_FLAG, &mute_flag);
    if (rc < 0)
    {
        log_warn("[player-libmpv] set_mute failed: %s\n", mpv_error_string(rc));
        mutexUnlock(&g_mutex);
        return false;
    }

    libmpv_set_mute_locked(mute, &pending);
    mutexUnlock(&g_mutex);

    libmpv_flush_events(&pending);
    return true;
}

static bool libmpv_pump_events(int timeout_ms)
{
    mpv_event *event;
    double timeout_s = timeout_ms <= 0 ? 0.0 : (double)timeout_ms / 1000.0;
    bool handled = false;

    mutexLock(&g_mutex);
    if (!g_mpv)
    {
        mutexUnlock(&g_mutex);
        return false;
    }
    mutexUnlock(&g_mutex);

    event = mpv_wait_event(g_mpv, timeout_s);
    while (event && event->event_id != MPV_EVENT_NONE)
    {
        handled = true;
        libmpv_handle_event(event);
        event = mpv_wait_event(g_mpv, 0.0);
    }

    return handled;
}

static void libmpv_wakeup(void)
{
    mutexLock(&g_mutex);
    if (g_mpv)
        mpv_wakeup(g_mpv);
    mutexUnlock(&g_mutex);
}

static bool libmpv_render_supported(void)
{
    bool supported;

    mutexLock(&g_mutex);
    supported = g_mpv != NULL;
    mutexUnlock(&g_mutex);
    return supported;
}

static bool libmpv_render_attach_gl(void *(*get_proc_address)(void *ctx, const char *name), void *get_proc_address_ctx)
{
    mpv_opengl_init_params gl_init_params;
    mpv_render_param params[4];
    mpv_render_context *new_ctx = NULL;
    mpv_handle *mpv = NULL;
    int rc;
    int advanced_control = 0;

    if (!get_proc_address)
        return false;

    mutexLock(&g_mutex);
    if (g_render_ctx)
    {
        mutexUnlock(&g_mutex);
        return true;
    }
    mpv = g_mpv;
    mutexUnlock(&g_mutex);
    if (!mpv)
        return false;

    gl_init_params.get_proc_address = get_proc_address;
    gl_init_params.get_proc_address_ctx = get_proc_address_ctx;

    params[0].type = MPV_RENDER_PARAM_API_TYPE;
    params[0].data = (void *)MPV_RENDER_API_TYPE_OPENGL;
    params[1].type = MPV_RENDER_PARAM_OPENGL_INIT_PARAMS;
    params[1].data = &gl_init_params;
    params[2].type = MPV_RENDER_PARAM_ADVANCED_CONTROL;
    params[2].data = &advanced_control;
    params[3].type = MPV_RENDER_PARAM_INVALID;
    params[3].data = NULL;

    rc = mpv_render_context_create(&new_ctx, mpv, params);
    if (rc < 0)
    {
        log_warn("[player-libmpv] render_context_create failed: %s\n", mpv_error_string(rc));
        return false;
    }

    mpv_render_context_set_update_callback(new_ctx, libmpv_on_render_update, NULL);

    mutexLock(&g_mutex);
    if (!g_render_ctx)
    {
        g_render_ctx = new_ctx;
        g_render_update_pending = true;
        new_ctx = NULL;
    }
    mutexUnlock(&g_mutex);

    if (new_ctx)
        mpv_render_context_free(new_ctx);
    return true;
}

static bool libmpv_render_attach_sw(void)
{
    return false;
}

static void libmpv_render_detach(void)
{
    mpv_render_context *render_ctx = NULL;

    mutexLock(&g_mutex);
    render_ctx = g_render_ctx;
    g_render_ctx = NULL;
    g_render_update_pending = false;
    mutexUnlock(&g_mutex);

    if (render_ctx)
        mpv_render_context_free(render_ctx);
}

static bool libmpv_render_frame_gl(int fbo, int width, int height, bool flip_y)
{
    mpv_render_context *render_ctx = NULL;
    mpv_opengl_fbo mpv_fbo;
    mpv_render_param params[4];
    int flip = flip_y ? 1 : 0;
    uint64_t flags;
    int rc;

    if (width <= 0 || height <= 0)
        return false;

    mutexLock(&g_mutex);
    render_ctx = g_render_ctx;
    g_render_update_pending = false;
    mutexUnlock(&g_mutex);
    if (!render_ctx)
        return false;

    flags = mpv_render_context_update(render_ctx);
    (void)flags;

    mpv_fbo.fbo = fbo;
    mpv_fbo.w = width;
    mpv_fbo.h = height;
    mpv_fbo.internal_format = 0;

    params[0].type = MPV_RENDER_PARAM_OPENGL_FBO;
    params[0].data = &mpv_fbo;
    params[1].type = MPV_RENDER_PARAM_FLIP_Y;
    params[1].data = &flip;
    params[2].type = MPV_RENDER_PARAM_INVALID;
    params[2].data = NULL;
    params[3].type = MPV_RENDER_PARAM_INVALID;
    params[3].data = NULL;

    rc = mpv_render_context_render(render_ctx, params);
    return rc >= 0;
}

static bool libmpv_render_frame_sw(void *pixels, int width, int height, size_t stride)
{
    (void)pixels;
    (void)width;
    (void)height;
    (void)stride;
    return false;
}

static int libmpv_get_position_ms(void)
{
    int value;

    mutexLock(&g_mutex);
    value = g_position_ms;
    mutexUnlock(&g_mutex);
    return value;
}

static int libmpv_get_duration_ms(void)
{
    int value;

    mutexLock(&g_mutex);
    value = g_duration_ms;
    mutexUnlock(&g_mutex);
    return value;
}

static int libmpv_get_volume(void)
{
    int value;

    mutexLock(&g_mutex);
    value = g_volume;
    mutexUnlock(&g_mutex);
    return value;
}

static bool libmpv_get_mute(void)
{
    bool value;

    mutexLock(&g_mutex);
    value = g_mute;
    mutexUnlock(&g_mutex);
    return value;
}

static bool libmpv_is_seekable(void)
{
    bool value;

    mutexLock(&g_mutex);
    value = g_seekable;
    mutexUnlock(&g_mutex);
    return value;
}

static PlayerState libmpv_get_state(void)
{
    PlayerState value;

    mutexLock(&g_mutex);
    value = g_state;
    mutexUnlock(&g_mutex);
    return value;
}

const BackendOps g_libmpv_ops = {
    .name = "libmpv",
    .available = libmpv_available,
    .init = libmpv_init,
    .deinit = libmpv_deinit,
    .set_event_sink = libmpv_set_event_sink,
    .set_media = libmpv_set_media,
    .play = libmpv_play,
    .pause = libmpv_pause,
    .stop = libmpv_stop,
    .seek_ms = libmpv_seek_ms,
    .set_volume = libmpv_set_volume,
    .set_mute = libmpv_set_mute,
    .pump_events = libmpv_pump_events,
    .wakeup = libmpv_wakeup,
    .render_supported = libmpv_render_supported,
    .render_attach_gl = libmpv_render_attach_gl,
    .render_attach_sw = libmpv_render_attach_sw,
    .render_detach = libmpv_render_detach,
    .render_frame_gl = libmpv_render_frame_gl,
    .render_frame_sw = libmpv_render_frame_sw,
    .get_position_ms = libmpv_get_position_ms,
    .get_duration_ms = libmpv_get_duration_ms,
    .get_volume = libmpv_get_volume,
    .get_mute = libmpv_get_mute,
    .is_seekable = libmpv_is_seekable,
    .get_state = libmpv_get_state
};

#else

static bool libmpv_unavailable(void)
{
    return false;
}

const BackendOps g_libmpv_ops = {
    .name = "libmpv",
    .available = libmpv_unavailable
};

#endif
