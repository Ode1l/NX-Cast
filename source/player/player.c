#include "player.h"

#include <switch.h>

#include <stddef.h>
#include <string.h>
#include <sys/types.h>

#include "log/log.h"
#include "player_backend.h"

#define PLAYER_COMMAND_SLOT_COUNT 16
#define PLAYER_COMMAND_QUEUE_CAPACITY PLAYER_COMMAND_SLOT_COUNT
#define PLAYER_THREAD_STACK_SIZE 0x8000
#define PLAYER_THREAD_POLL_TIMEOUT_MS 50

typedef enum
{
    PLAYER_COMMAND_SET_SOURCE = 0,
    PLAYER_COMMAND_PLAY,
    PLAYER_COMMAND_PAUSE,
    PLAYER_COMMAND_STOP,
    PLAYER_COMMAND_SEEK_MS,
    PLAYER_COMMAND_SET_VOLUME,
    PLAYER_COMMAND_SET_MUTE
} PlayerCommandType;

typedef struct
{
    bool in_use;
    bool queued;
    bool completed;
    bool result;
    PlayerCommandType type;
    PlayerResolvedSource source;
    int int_value;
    bool bool_value;
} PlayerCommandSlot;

static const PlayerBackendOps *g_backend = NULL;
static PlayerBackendType g_backend_type = PLAYER_BACKEND_AUTO;
static bool g_initialized = false;
static PlayerEventCallback g_event_callback = NULL;
static void *g_event_user = NULL;
static bool g_has_current_source = false;
static PlayerResolvedSource g_current_source;
static PlayerSnapshot g_snapshot;

static Mutex g_player_mutex;
static CondVar g_player_cond;
static bool g_player_sync_ready = false;
static Thread g_player_thread;
static bool g_player_thread_started = false;
static bool g_player_thread_running = false;
static bool g_player_stop_requested = false;

static PlayerCommandSlot g_command_slots[PLAYER_COMMAND_SLOT_COUNT];
static size_t g_command_queue[PLAYER_COMMAND_QUEUE_CAPACITY];
static size_t g_command_head = 0;
static size_t g_command_tail = 0;
static size_t g_command_count = 0;

static void player_thread_main(void *arg);

static bool backend_available(const PlayerBackendOps *backend)
{
    if (!backend)
        return false;
    if (!backend->available)
        return true;
    return backend->available();
}

static const PlayerBackendOps *backend_ops_from_type(PlayerBackendType backend_type)
{
    switch (backend_type)
    {
    case PLAYER_BACKEND_MOCK:
        return &g_player_backend_mock;
    case PLAYER_BACKEND_LIBMPV:
        return &g_player_backend_libmpv;
    case PLAYER_BACKEND_AUTO:
    default:
        if (backend_available(&g_player_backend_libmpv))
            return &g_player_backend_libmpv;
        return &g_player_backend_mock;
    }
}

static const char *backend_name_from_type(PlayerBackendType backend_type)
{
    const PlayerBackendOps *backend = backend_ops_from_type(backend_type);
    if (!backend || !backend->name)
        return "unknown";
    return backend->name;
}

static void player_ensure_sync_primitives(void)
{
    if (g_player_sync_ready)
        return;

    mutexInit(&g_player_mutex);
    condvarInit(&g_player_cond);
    g_player_sync_ready = true;
}

static void player_reset_snapshot_locked(void)
{
    memset(&g_snapshot, 0, sizeof(g_snapshot));
    g_snapshot.state = PLAYER_STATE_IDLE;
}

static void player_reset_command_queue_locked(void)
{
    memset(g_command_slots, 0, sizeof(g_command_slots));
    memset(g_command_queue, 0, sizeof(g_command_queue));
    g_command_head = 0;
    g_command_tail = 0;
    g_command_count = 0;
}

static void player_store_source_locked(bool has_source, const PlayerResolvedSource *source)
{
    g_has_current_source = has_source;
    if (has_source && source)
    {
        g_current_source = *source;
        g_snapshot.has_source = true;
        g_snapshot.source = *source;
    }
    else
    {
        player_source_reset(&g_current_source);
        g_snapshot.has_source = false;
        player_source_reset(&g_snapshot.source);
    }
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

    if (g_has_current_source)
    {
        g_snapshot.has_source = true;
        g_snapshot.source = g_current_source;
    }

}

static bool player_read_backend_snapshot(PlayerSnapshot *out)
{
    if (!out)
        return false;

    memset(out, 0, sizeof(*out));
    if (g_backend && g_backend->get_state)
        out->state = g_backend->get_state();
    else
        out->state = PLAYER_STATE_IDLE;

    if (g_backend && g_backend->get_position_ms)
        out->position_ms = g_backend->get_position_ms();
    if (g_backend && g_backend->get_duration_ms)
        out->duration_ms = g_backend->get_duration_ms();
    if (g_backend && g_backend->get_volume)
        out->volume = g_backend->get_volume();
    if (g_backend && g_backend->get_mute)
        out->mute = g_backend->get_mute();
    if (g_backend && g_backend->is_seekable)
        out->seekable = g_backend->is_seekable();

    mutexLock(&g_player_mutex);
    out->has_source = g_has_current_source;
    if (g_has_current_source)
        out->source = g_current_source;
    mutexUnlock(&g_player_mutex);
    return true;
}

static void player_refresh_cached_snapshot_from_backend(void)
{
    PlayerSnapshot snapshot;

    if (!player_read_backend_snapshot(&snapshot))
        return;

    mutexLock(&g_player_mutex);
    g_snapshot = snapshot;
    condvarWakeAll(&g_player_cond);
    mutexUnlock(&g_player_mutex);
}

static void player_emit_from_backend(const PlayerEvent *event)
{
    PlayerEvent forwarded;
    PlayerEventCallback callback = NULL;
    void *callback_user = NULL;

    if (!event)
        return;

    mutexLock(&g_player_mutex);
    player_apply_event_locked(event);
    callback = g_event_callback;
    callback_user = g_event_user;
    forwarded = *event;
    condvarWakeAll(&g_player_cond);
    mutexUnlock(&g_player_mutex);

    if (callback)
        callback(&forwarded, callback_user);
}

static ssize_t player_find_free_slot_locked(void)
{
    for (size_t i = 0; i < PLAYER_COMMAND_SLOT_COUNT; ++i)
    {
        if (!g_command_slots[i].in_use)
            return (ssize_t)i;
    }
    return -1;
}

static ssize_t player_acquire_slot_locked(void)
{
    while (!g_player_stop_requested)
    {
        ssize_t slot_index = player_find_free_slot_locked();
        if (slot_index >= 0)
        {
            memset(&g_command_slots[slot_index], 0, sizeof(g_command_slots[slot_index]));
            g_command_slots[slot_index].in_use = true;
            return slot_index;
        }
        condvarWait(&g_player_cond, &g_player_mutex);
    }

    return -1;
}

static void player_enqueue_slot_locked(size_t slot_index)
{
    g_command_slots[slot_index].queued = true;
    g_command_queue[g_command_tail] = slot_index;
    g_command_tail = (g_command_tail + 1) % PLAYER_COMMAND_QUEUE_CAPACITY;
    g_command_count++;
}

static ssize_t player_dequeue_slot_locked(void)
{
    if (g_command_count == 0)
        return -1;

    size_t slot_index = g_command_queue[g_command_head];
    g_command_head = (g_command_head + 1) % PLAYER_COMMAND_QUEUE_CAPACITY;
    g_command_count--;
    g_command_slots[slot_index].queued = false;
    return (ssize_t)slot_index;
}

static bool player_execute_command(PlayerCommandSlot *slot)
{
    bool result = false;

    if (!slot || !g_backend)
        return false;

    switch (slot->type)
    {
    case PLAYER_COMMAND_SET_SOURCE:
    {
        bool old_has_source;
        PlayerResolvedSource old_source;

        mutexLock(&g_player_mutex);
        old_has_source = g_has_current_source;
        old_source = g_current_source;
        player_store_source_locked(true, &slot->source);
        mutexUnlock(&g_player_mutex);

        result = g_backend->set_source && g_backend->set_source(&slot->source);
        if (!result)
        {
            mutexLock(&g_player_mutex);
            player_store_source_locked(old_has_source, old_has_source ? &old_source : NULL);
            mutexUnlock(&g_player_mutex);
        }
        break;
    }
    case PLAYER_COMMAND_PLAY:
        result = g_backend->play && g_backend->play();
        break;
    case PLAYER_COMMAND_PAUSE:
        result = g_backend->pause && g_backend->pause();
        break;
    case PLAYER_COMMAND_STOP:
        result = g_backend->stop && g_backend->stop();
        break;
    case PLAYER_COMMAND_SEEK_MS:
        result = g_backend->seek_ms && g_backend->seek_ms(slot->int_value);
        break;
    case PLAYER_COMMAND_SET_VOLUME:
        result = g_backend->set_volume && g_backend->set_volume(slot->int_value);
        break;
    case PLAYER_COMMAND_SET_MUTE:
        result = g_backend->set_mute && g_backend->set_mute(slot->bool_value);
        break;
    default:
        break;
    }

    player_refresh_cached_snapshot_from_backend();
    return result;
}

static bool player_submit_command(PlayerCommandType type,
                                  const PlayerResolvedSource *source,
                                  int int_value,
                                  bool bool_value)
{
    bool result = false;
    ssize_t slot_index;

    if (!g_initialized || !g_backend || !g_player_thread_running)
        return false;

    player_ensure_sync_primitives();
    mutexLock(&g_player_mutex);
    slot_index = player_acquire_slot_locked();
    if (slot_index < 0)
    {
        mutexUnlock(&g_player_mutex);
        return false;
    }

    PlayerCommandSlot *slot = &g_command_slots[slot_index];
    slot->type = type;
    slot->int_value = int_value;
    slot->bool_value = bool_value;
    if (source)
        slot->source = *source;

    player_enqueue_slot_locked((size_t)slot_index);
    condvarWakeAll(&g_player_cond);
    mutexUnlock(&g_player_mutex);

    if (g_backend->wakeup)
        g_backend->wakeup();

    mutexLock(&g_player_mutex);
    while (slot->in_use && !slot->completed && !g_player_stop_requested)
        condvarWait(&g_player_cond, &g_player_mutex);

    if (slot->completed)
        result = slot->result;

    memset(slot, 0, sizeof(*slot));
    condvarWakeAll(&g_player_cond);
    mutexUnlock(&g_player_mutex);
    return result;
}

static void player_thread_main(void *arg)
{
    (void)arg;

    while (true)
    {
        ssize_t slot_index;

        mutexLock(&g_player_mutex);
        slot_index = player_dequeue_slot_locked();
        bool should_stop = g_player_stop_requested && slot_index < 0;
        mutexUnlock(&g_player_mutex);

        if (should_stop)
            break;

        if (slot_index >= 0)
        {
            bool result = player_execute_command(&g_command_slots[slot_index]);

            mutexLock(&g_player_mutex);
            g_command_slots[slot_index].result = result;
            g_command_slots[slot_index].completed = true;
            condvarWakeAll(&g_player_cond);
            mutexUnlock(&g_player_mutex);
            continue;
        }

        if (g_backend && g_backend->pump_events)
            g_backend->pump_events(PLAYER_THREAD_POLL_TIMEOUT_MS);
        else
            svcSleepThread((int64_t)PLAYER_THREAD_POLL_TIMEOUT_MS * 1000000LL);
    }

    mutexLock(&g_player_mutex);
    g_player_thread_running = false;
    condvarWakeAll(&g_player_cond);
    mutexUnlock(&g_player_mutex);
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
    if (g_initialized)
        return true;

    player_ensure_sync_primitives();
    mutexLock(&g_player_mutex);
    player_source_reset(&g_current_source);
    g_has_current_source = false;
    player_reset_snapshot_locked();
    player_reset_command_queue_locked();
    g_player_stop_requested = false;
    mutexUnlock(&g_player_mutex);

    if (g_backend_type == PLAYER_BACKEND_AUTO)
    {
        log_info("[player] auto resolve libmpv_available=%d mock_available=%d\n",
                 backend_available(&g_player_backend_libmpv) ? 1 : 0,
                 backend_available(&g_player_backend_mock) ? 1 : 0);
    }

    g_backend = backend_ops_from_type(g_backend_type);
    if (!g_backend)
    {
        log_error("[player] no backend selected\n");
        return false;
    }

    if (g_backend_type != PLAYER_BACKEND_AUTO && !backend_available(g_backend))
    {
        log_error("[player] backend unavailable name=%s\n", player_get_backend_name());
        g_backend = NULL;
        return false;
    }

    if (!backend_available(g_backend))
    {
        log_error("[player] auto backend resolution failed\n");
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

    Result rc = threadCreate(&g_player_thread,
                             player_thread_main,
                             NULL,
                             NULL,
                             PLAYER_THREAD_STACK_SIZE,
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

    mutexLock(&g_player_mutex);
    g_player_thread_started = true;
    g_player_thread_running = true;
    mutexUnlock(&g_player_mutex);

    g_initialized = true;
    log_info("[player] init backend=%s owner_thread=1 queue=%d\n",
             player_get_backend_name(),
             PLAYER_COMMAND_SLOT_COUNT);
    return true;
}

void player_deinit(void)
{
    if (!g_initialized)
        return;

    mutexLock(&g_player_mutex);
    g_player_stop_requested = true;
    condvarWakeAll(&g_player_cond);
    mutexUnlock(&g_player_mutex);

    if (g_backend && g_backend->wakeup)
        g_backend->wakeup();

    if (g_player_thread_started)
    {
        threadWaitForExit(&g_player_thread);
        threadClose(&g_player_thread);
    }

    mutexLock(&g_player_mutex);
    g_player_thread_started = false;
    g_player_thread_running = false;
    player_reset_command_queue_locked();
    player_reset_snapshot_locked();
    player_store_source_locked(false, NULL);
    mutexUnlock(&g_player_mutex);

    if (g_backend && g_backend->deinit)
        g_backend->deinit();

    log_info("[player] deinit backend=%s\n", player_get_backend_name());
    g_backend = NULL;
    g_initialized = false;
}

bool player_set_uri(const char *uri, const char *metadata)
{
    PlayerResolvedSource resolved;

    if (!player_source_resolve(uri, metadata, &resolved))
        return false;

    log_info("[player] resolve_source profile=%s hls=%d signed=%d bilibili=%d timeout=%d\n",
             player_source_profile_name(resolved.profile),
             resolved.flags.is_hls ? 1 : 0,
             resolved.flags.is_signed ? 1 : 0,
             resolved.flags.is_bilibili ? 1 : 0,
             resolved.network_timeout_seconds);

    return player_set_source(&resolved);
}

bool player_set_source(const PlayerResolvedSource *source)
{
    if (!source)
        return false;
    return player_submit_command(PLAYER_COMMAND_SET_SOURCE, source, 0, false);
}

bool player_play(void)
{
    return player_submit_command(PLAYER_COMMAND_PLAY, NULL, 0, false);
}

bool player_pause(void)
{
    return player_submit_command(PLAYER_COMMAND_PAUSE, NULL, 0, false);
}

bool player_stop(void)
{
    return player_submit_command(PLAYER_COMMAND_STOP, NULL, 0, false);
}

bool player_seek_ms(int position_ms)
{
    return player_submit_command(PLAYER_COMMAND_SEEK_MS, NULL, position_ms, false);
}

bool player_set_volume(int volume_0_100)
{
    return player_submit_command(PLAYER_COMMAND_SET_VOLUME, NULL, volume_0_100, false);
}

bool player_set_mute(bool mute)
{
    return player_submit_command(PLAYER_COMMAND_SET_MUTE, NULL, 0, mute);
}

int player_get_position_ms(void)
{
    mutexLock(&g_player_mutex);
    int position_ms = g_snapshot.position_ms;
    mutexUnlock(&g_player_mutex);
    return position_ms;
}

int player_get_duration_ms(void)
{
    mutexLock(&g_player_mutex);
    int duration_ms = g_snapshot.duration_ms;
    mutexUnlock(&g_player_mutex);
    return duration_ms;
}

int player_get_volume(void)
{
    mutexLock(&g_player_mutex);
    int volume = g_snapshot.volume;
    mutexUnlock(&g_player_mutex);
    return volume;
}

bool player_get_mute(void)
{
    mutexLock(&g_player_mutex);
    bool mute = g_snapshot.mute;
    mutexUnlock(&g_player_mutex);
    return mute;
}

bool player_is_seekable(void)
{
    mutexLock(&g_player_mutex);
    bool seekable = g_snapshot.seekable;
    mutexUnlock(&g_player_mutex);
    return seekable;
}

PlayerState player_get_state(void)
{
    mutexLock(&g_player_mutex);
    PlayerState state = g_snapshot.state;
    mutexUnlock(&g_player_mutex);
    return state;
}

bool player_get_current_source(PlayerResolvedSource *out)
{
    if (!out)
        return false;

    mutexLock(&g_player_mutex);
    bool has_source = g_has_current_source;
    if (has_source)
        *out = g_current_source;
    mutexUnlock(&g_player_mutex);
    return has_source;
}

bool player_get_snapshot(PlayerSnapshot *out)
{
    if (!out)
        return false;

    mutexLock(&g_player_mutex);
    *out = g_snapshot;
    mutexUnlock(&g_player_mutex);
    return true;
}
