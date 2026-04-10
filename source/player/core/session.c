#include "player/player.h"

#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "log/log.h"
#include "player/backend.h"

#define PLAYER_EVENT_THREAD_STACK_SIZE 0x8000
#define PLAYER_EVENT_POLL_TIMEOUT_MS 100

static const BackendOps *g_backend = NULL;
static PlayerBackendType g_backend_type = PLAYER_BACKEND_AUTO;
static bool g_initialized = false;
static PlayerEventCallback g_event_callback = NULL;
static void *g_event_user = NULL;
static bool g_has_current_media = false;
static PlayerMedia g_current_media;
static PlayerSnapshot g_snapshot;

static Mutex g_player_mutex;
static bool g_player_sync_ready = false;
static Thread g_player_thread;
static bool g_player_thread_started = false;
static bool g_player_stop_requested = false;

static void player_thread_main(void *arg);

static bool backend_available(const BackendOps *backend)
{
    if (!backend)
        return false;
    if (!backend->available)
        return true;
    return backend->available();
}

static const BackendOps *backend_ops_from_type(PlayerBackendType backend_type)
{
    switch (backend_type)
    {
    case PLAYER_BACKEND_MOCK:
        return &g_mock_ops;
    case PLAYER_BACKEND_LIBMPV:
        return &g_libmpv_ops;
    case PLAYER_BACKEND_AUTO:
    default:
        if (backend_available(&g_libmpv_ops))
            return &g_libmpv_ops;
        return &g_mock_ops;
    }
}

static const char *backend_name_from_type(PlayerBackendType backend_type)
{
    const BackendOps *backend = backend_ops_from_type(backend_type);
    if (!backend || !backend->name)
        return "unknown";
    return backend->name;
}

static void player_ensure_sync_primitives(void)
{
    if (g_player_sync_ready)
        return;

    mutexInit(&g_player_mutex);
    g_player_sync_ready = true;
}

static void player_reset_media(PlayerMedia *media)
{
    if (!media)
        return;
    player_media_clear(media);
}

static bool player_sync_snapshot_media_locked(void)
{
    player_media_clear(&g_snapshot.media);
    if (!g_has_current_media)
        return true;
    return player_media_copy(&g_snapshot.media, &g_current_media);
}

static void player_reset_snapshot_locked(void)
{
    player_snapshot_clear(&g_snapshot);
    g_snapshot.state = PLAYER_STATE_IDLE;
    g_snapshot.volume = PLAYER_DEFAULT_VOLUME;
}

static bool player_store_media_locked(bool has_media, const PlayerMedia *media)
{
    PlayerMedia current_media = {0};

    if (has_media && media && !player_media_copy(&current_media, media))
        return false;

    player_media_clear(&g_current_media);
    g_current_media = current_media;
    g_has_current_media = has_media;
    g_snapshot.has_media = has_media;

    if (!player_sync_snapshot_media_locked())
    {
        g_snapshot.has_media = false;
        return false;
    }

    return true;
}

static void player_apply_event_locked(const PlayerEvent *event)
{
    if (!event)
        return;

    g_snapshot.state = event->state;
    g_snapshot.position_ms = event->position_ms;
    g_snapshot.duration_ms = event->duration_ms;
    g_snapshot.volume = event->volume;
    g_snapshot.mute = event->mute;
    g_snapshot.seekable = event->seekable;

    if (event->uri && g_has_current_media)
    {
        if (!player_media_set(&g_current_media, event->uri, g_current_media.metadata))
            return;
    }

    if (g_has_current_media)
    {
        g_snapshot.has_media = true;
        (void)player_sync_snapshot_media_locked();
    }
    else
    {
        g_snapshot.has_media = false;
        player_reset_media(&g_snapshot.media);
        g_snapshot.state = PLAYER_STATE_IDLE;
        g_snapshot.position_ms = 0;
        g_snapshot.duration_ms = 0;
        g_snapshot.seekable = false;
    }
}

static bool player_read_backend_snapshot(PlayerSnapshot *out)
{
    if (!out)
        return false;

    memset(out, 0, sizeof(*out));
    out->state = g_backend && g_backend->get_state ? g_backend->get_state() : PLAYER_STATE_IDLE;
    out->position_ms = g_backend && g_backend->get_position_ms ? g_backend->get_position_ms() : 0;
    out->duration_ms = g_backend && g_backend->get_duration_ms ? g_backend->get_duration_ms() : 0;
    out->volume = g_backend && g_backend->get_volume ? g_backend->get_volume() : PLAYER_DEFAULT_VOLUME;
    out->mute = g_backend && g_backend->get_mute ? g_backend->get_mute() : false;
    out->seekable = g_backend && g_backend->is_seekable ? g_backend->is_seekable() : false;

    mutexLock(&g_player_mutex);
    out->has_media = g_has_current_media;
    if (g_has_current_media && !player_media_copy(&out->media, &g_current_media))
    {
        mutexUnlock(&g_player_mutex);
        player_snapshot_clear(out);
        return false;
    }
    mutexUnlock(&g_player_mutex);

    if (!out->has_media)
    {
        out->state = PLAYER_STATE_IDLE;
        out->position_ms = 0;
        out->duration_ms = 0;
        out->seekable = false;
    }
    return true;
}

static void player_refresh_cached_snapshot_from_backend(void)
{
    PlayerSnapshot snapshot;

    if (!player_read_backend_snapshot(&snapshot))
        return;

    mutexLock(&g_player_mutex);
    player_snapshot_clear(&g_snapshot);
    g_snapshot = snapshot;
    mutexUnlock(&g_player_mutex);
}

static void player_emit_from_backend(const PlayerEvent *event)
{
    PlayerEvent forwarded = {0};
    PlayerEventCallback callback = NULL;
    void *callback_user = NULL;

    if (!event)
        return;

    mutexLock(&g_player_mutex);
    player_apply_event_locked(event);
    forwarded = *event;
    forwarded.uri = NULL;
    if (event->uri && !player_event_copy(&forwarded, event))
    {
        forwarded = *event;
        forwarded.uri = NULL;
    }
    callback = g_event_callback;
    callback_user = g_event_user;
    mutexUnlock(&g_player_mutex);

    if (callback)
        callback(&forwarded, callback_user);

    player_event_clear(&forwarded);
}

static bool player_run_backend_bool(bool (*fn)(void))
{
    bool ok;

    if (!g_initialized || !g_backend || !fn)
        return false;

    ok = fn();
    if (ok)
        player_refresh_cached_snapshot_from_backend();
    if (ok && g_backend->wakeup)
        g_backend->wakeup();
    return ok;
}

static bool player_run_backend_int(bool (*fn)(int), int value)
{
    bool ok;

    if (!g_initialized || !g_backend || !fn)
        return false;

    ok = fn(value);
    if (ok)
        player_refresh_cached_snapshot_from_backend();
    if (ok && g_backend->wakeup)
        g_backend->wakeup();
    return ok;
}

static bool player_run_backend_string(bool (*fn)(const char *), const char *value)
{
    bool ok;

    if (!g_initialized || !g_backend || !fn || !value)
        return false;

    ok = fn(value);
    if (ok)
        player_refresh_cached_snapshot_from_backend();
    if (ok && g_backend->wakeup)
        g_backend->wakeup();
    return ok;
}

static bool player_run_backend_flag(bool (*fn)(bool), bool value)
{
    bool ok;

    if (!g_initialized || !g_backend || !fn)
        return false;

    ok = fn(value);
    if (ok)
        player_refresh_cached_snapshot_from_backend();
    if (ok && g_backend->wakeup)
        g_backend->wakeup();
    return ok;
}

static void player_thread_main(void *arg)
{
    (void)arg;

    while (true)
    {
        bool should_stop;

        mutexLock(&g_player_mutex);
        should_stop = g_player_stop_requested;
        mutexUnlock(&g_player_mutex);
        if (should_stop)
            break;

        if (g_backend && g_backend->pump_events)
            g_backend->pump_events(PLAYER_EVENT_POLL_TIMEOUT_MS);
        else
            svcSleepThread((int64_t)PLAYER_EVENT_POLL_TIMEOUT_MS * 1000000LL);
    }
}

bool player_set_backend(PlayerBackendType backend)
{
    if (g_initialized)
    {
        log_warn("[player] backend change rejected while initialized current=%s requested=%s\n",
                 player_get_backend_name(),
                 backend_name_from_type(backend));
        return false;
    }

    g_backend_type = backend;
    g_backend = NULL;
    log_info("[player] backend configured=%s\n", backend_name_from_type(backend));
    return true;
}

PlayerBackendType player_get_backend(void)
{
    return g_backend_type;
}

const char *player_get_backend_name(void)
{
    if (g_backend && g_backend->name)
        return g_backend->name;
    return backend_name_from_type(g_backend_type);
}

void player_set_event_callback(PlayerEventCallback callback, void *user)
{
    player_ensure_sync_primitives();
    mutexLock(&g_player_mutex);
    g_event_callback = callback;
    g_event_user = user;
    mutexUnlock(&g_player_mutex);
}

bool player_init(void)
{
    Result rc;

    if (g_initialized)
        return true;

    player_ensure_sync_primitives();
    mutexLock(&g_player_mutex);
    g_player_stop_requested = false;
    g_has_current_media = false;
    player_reset_media(&g_current_media);
    player_reset_snapshot_locked();
    mutexUnlock(&g_player_mutex);

    if (g_backend_type == PLAYER_BACKEND_AUTO)
    {
        log_info("[player] auto resolve libmpv_available=%d mock_available=%d\n",
                 backend_available(&g_libmpv_ops) ? 1 : 0,
                 backend_available(&g_mock_ops) ? 1 : 0);
    }

    g_backend = backend_ops_from_type(g_backend_type);
    if (!g_backend || !backend_available(g_backend))
    {
        log_error("[player] backend unavailable name=%s\n", backend_name_from_type(g_backend_type));
        g_backend = NULL;
        return false;
    }

    if (g_backend->set_event_sink)
        g_backend->set_event_sink(player_emit_from_backend);

    if (g_backend->init && !g_backend->init())
    {
        log_error("[player] backend init failed name=%s\n", player_get_backend_name());
        g_backend = NULL;
        return false;
    }

    player_refresh_cached_snapshot_from_backend();

    rc = threadCreate(&g_player_thread,
                      player_thread_main,
                      NULL,
                      NULL,
                      PLAYER_EVENT_THREAD_STACK_SIZE,
                      0x2B,
                      -2);
    if (R_FAILED(rc))
    {
        log_error("[player] threadCreate failed: 0x%x\n", rc);
        if (g_backend && g_backend->deinit)
            g_backend->deinit();
        g_backend = NULL;
        return false;
    }

    rc = threadStart(&g_player_thread);
    if (R_FAILED(rc))
    {
        log_error("[player] threadStart failed: 0x%x\n", rc);
        threadClose(&g_player_thread);
        if (g_backend && g_backend->deinit)
            g_backend->deinit();
        g_backend = NULL;
        return false;
    }

    g_player_thread_started = true;
    g_initialized = true;
    log_info("[player] init backend=%s renderer_mode=direct\n", player_get_backend_name());
    return true;
}

void player_deinit(void)
{
    if (!g_initialized)
        return;

    log_info("[player] deinit begin backend=%s thread_started=%d\n",
             player_get_backend_name(),
             g_player_thread_started ? 1 : 0);
    mutexLock(&g_player_mutex);
    g_player_stop_requested = true;
    mutexUnlock(&g_player_mutex);

    if (g_backend && g_backend->wakeup)
    {
        log_info("[player] deinit step=backend_wakeup\n");
        g_backend->wakeup();
    }

    if (g_player_thread_started)
    {
        log_info("[player] deinit waiting for event thread exit\n");
        threadWaitForExit(&g_player_thread);
        threadClose(&g_player_thread);
        g_player_thread_started = false;
        log_info("[player] deinit event thread closed\n");
    }

    if (g_backend && g_backend->deinit)
    {
        log_info("[player] deinit step=backend_deinit begin\n");
        g_backend->deinit();
        log_info("[player] deinit step=backend_deinit done\n");
    }

    mutexLock(&g_player_mutex);
    (void)player_store_media_locked(false, NULL);
    player_reset_snapshot_locked();
    mutexUnlock(&g_player_mutex);

    log_info("[player] deinit backend=%s\n", player_get_backend_name());
    g_backend = NULL;
    g_initialized = false;
}

bool player_set_uri(const char *uri, const char *metadata)
{
    PlayerMedia media;
    bool ok;

    if (!uri || uri[0] == '\0')
        return false;

    memset(&media, 0, sizeof(media));
    if (!player_media_set(&media, uri, metadata))
        return false;

    ok = player_set_media(&media);
    player_media_clear(&media);
    return ok;
}

bool player_set_media(const PlayerMedia *media)
{
    PlayerMedia previous_media = {0};
    bool previous_has_media;
    bool ok;

    if (!g_initialized || !g_backend || !g_backend->set_media || !media || !media->uri || media->uri[0] == '\0')
        return false;

    mutexLock(&g_player_mutex);
    previous_has_media = g_has_current_media;
    if (previous_has_media && !player_media_copy(&previous_media, &g_current_media))
    {
        mutexUnlock(&g_player_mutex);
        return false;
    }
    if (!player_store_media_locked(true, media))
    {
        player_media_clear(&previous_media);
        mutexUnlock(&g_player_mutex);
        return false;
    }
    g_snapshot.state = PLAYER_STATE_LOADING;
    g_snapshot.position_ms = 0;
    g_snapshot.duration_ms = 0;
    g_snapshot.seekable = false;
    mutexUnlock(&g_player_mutex);

    ok = g_backend->set_media(media);
    if (!ok)
    {
        mutexLock(&g_player_mutex);
        (void)player_store_media_locked(previous_has_media, previous_has_media ? &previous_media : NULL);
        mutexUnlock(&g_player_mutex);
        player_media_clear(&previous_media);
        return false;
    }

    player_media_clear(&previous_media);
    player_refresh_cached_snapshot_from_backend();
    if (g_backend->wakeup)
        g_backend->wakeup();
    return true;
}

bool player_play(void)
{
    return player_run_backend_bool(g_backend ? g_backend->play : NULL);
}

bool player_pause(void)
{
    return player_run_backend_bool(g_backend ? g_backend->pause : NULL);
}

bool player_stop(void)
{
    return player_run_backend_bool(g_backend ? g_backend->stop : NULL);
}

bool player_seek_target(const char *target)
{
    return player_run_backend_string(g_backend ? g_backend->seek_target : NULL, target);
}

bool player_seek_ms(int position_ms)
{
    return player_run_backend_int(g_backend ? g_backend->seek_ms : NULL, position_ms);
}

bool player_set_volume(int volume_0_100)
{
    return player_run_backend_int(g_backend ? g_backend->set_volume : NULL, volume_0_100);
}

bool player_set_mute(bool mute)
{
    return player_run_backend_flag(g_backend ? g_backend->set_mute : NULL, mute);
}

bool player_video_supported(void)
{
    if (!g_initialized || !g_backend)
        return false;
    if ((!g_backend->render_attach_gl || !g_backend->render_frame_gl) &&
        (!g_backend->render_attach_sw || !g_backend->render_frame_sw))
    {
        return false;
    }
    if (g_backend->render_supported)
        return g_backend->render_supported();
    return true;
}

bool player_video_attach_gl(void *(*get_proc_address)(void *ctx, const char *name), void *get_proc_address_ctx)
{
    if (!player_video_supported() || !g_backend->render_attach_gl)
        return false;
    return g_backend->render_attach_gl(get_proc_address, get_proc_address_ctx);
}

bool player_video_attach_sw(void)
{
    if (!player_video_supported() || !g_backend->render_attach_sw)
        return false;
    return g_backend->render_attach_sw();
}

void player_video_detach(void)
{
    if (!g_initialized || !g_backend || !g_backend->render_detach)
        return;
    g_backend->render_detach();
}

bool player_video_render_gl(int fbo, int width, int height, bool flip_y)
{
    if (!player_video_supported() || !g_backend->render_frame_gl)
        return false;
    return g_backend->render_frame_gl(fbo, width, height, flip_y);
}

bool player_video_render_sw(void *pixels, int width, int height, size_t stride)
{
    if (!player_video_supported() || !g_backend->render_frame_sw)
        return false;
    return g_backend->render_frame_sw(pixels, width, height, stride);
}

int player_get_position_ms(void)
{
    int value;

    mutexLock(&g_player_mutex);
    value = g_snapshot.position_ms;
    mutexUnlock(&g_player_mutex);
    return value;
}

int player_get_duration_ms(void)
{
    int value;

    mutexLock(&g_player_mutex);
    value = g_snapshot.duration_ms;
    mutexUnlock(&g_player_mutex);
    return value;
}

int player_get_volume(void)
{
    int value;

    mutexLock(&g_player_mutex);
    value = g_snapshot.volume;
    mutexUnlock(&g_player_mutex);
    return value;
}

bool player_get_mute(void)
{
    bool value;

    mutexLock(&g_player_mutex);
    value = g_snapshot.mute;
    mutexUnlock(&g_player_mutex);
    return value;
}

bool player_is_seekable(void)
{
    bool value;

    mutexLock(&g_player_mutex);
    value = g_snapshot.seekable;
    mutexUnlock(&g_player_mutex);
    return value;
}

PlayerState player_get_state(void)
{
    PlayerState value;

    mutexLock(&g_player_mutex);
    value = g_snapshot.state;
    mutexUnlock(&g_player_mutex);
    return value;
}

bool player_get_current_media(PlayerMedia *out)
{
    bool has_media;

    if (!out)
        return false;

    memset(out, 0, sizeof(*out));
    mutexLock(&g_player_mutex);
    has_media = g_has_current_media;
    if (has_media && !player_media_copy(out, &g_current_media))
    {
        mutexUnlock(&g_player_mutex);
        return false;
    }
    mutexUnlock(&g_player_mutex);
    return has_media;
}

bool player_get_snapshot(PlayerSnapshot *out)
{
    bool ok;

    if (!out)
        return false;

    memset(out, 0, sizeof(*out));
    mutexLock(&g_player_mutex);
    ok = player_snapshot_copy(out, &g_snapshot);
    mutexUnlock(&g_player_mutex);
    return ok;
}
