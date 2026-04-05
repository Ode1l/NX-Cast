#include "player/player.h"

#include <switch.h>

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "log/log.h"
#include "player/backend.h"
#include "player/ingress.h"

#define PLAYER_COMMAND_SLOT_COUNT 16
#define PLAYER_COMMAND_QUEUE_CAPACITY PLAYER_COMMAND_SLOT_COUNT
#define PLAYER_THREAD_STACK_SIZE 0x8000
#define PLAYER_THREAD_POLL_TIMEOUT_MS 50
#define PLAYER_HLS_STARTUP_SEEK_GUARD_MS 5000
#define PLAYER_HLS_GATEWAY_STARTUP_SEEK_GUARD_MS 8000
#define PLAYER_HLS_LOCAL_PROXY_STARTUP_SEEK_GUARD_MS 12000

typedef enum
{
    PLAYER_BACKEND_AUTO = 0,
    PLAYER_BACKEND_MOCK,
    PLAYER_BACKEND_LIBMPV
} PlayerBackendType;

typedef enum
{
    PLAYER_COMMAND_SET_MEDIA = 0,
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
    PlayerMedia media;
    int int_value;
    bool bool_value;
} PlayerCommandSlot;

static const BackendOps *g_backend = NULL;
static PlayerBackendType g_backend_type = PLAYER_BACKEND_AUTO;
static bool g_initialized = false;
static PlayerEventCallback g_event_callback = NULL;
static void *g_event_user = NULL;
static bool g_has_current_media = false;
static PlayerMedia g_current_media;
static PlayerSnapshot g_snapshot;

static Mutex g_player_mutex;
static CondVar g_player_cond;
static bool g_player_sync_ready = false;
static Thread g_player_thread;
static bool g_player_thread_started = false;
static bool g_player_thread_running = false;
static bool g_player_stop_requested = false;
static bool g_pending_seek_active = false;
static int g_pending_seek_ms = 0;

static PlayerCommandSlot g_command_slots[PLAYER_COMMAND_SLOT_COUNT];
static size_t g_command_queue[PLAYER_COMMAND_QUEUE_CAPACITY];
static size_t g_command_head = 0;
static size_t g_command_tail = 0;
static size_t g_command_count = 0;

static void player_thread_main(void *arg);
static bool player_set_media(const PlayerMedia *media);

static void player_reset_media_summary(PlayerMediaSummary *summary)
{
    if (!summary)
        return;

    memset(summary, 0, sizeof(*summary));
    summary->vendor = PLAYER_MEDIA_VENDOR_UNKNOWN;
    summary->format = PLAYER_MEDIA_FORMAT_UNKNOWN;
    summary->transport = PLAYER_MEDIA_TRANSPORT_UNKNOWN;
}

static void player_fill_media_summary(PlayerMediaSummary *summary, const PlayerMedia *media)
{
    if (!summary)
        return;

    player_reset_media_summary(summary);
    if (!media)
        return;

    snprintf(summary->uri, sizeof(summary->uri), "%s", media->uri);
    snprintf(summary->format_hint, sizeof(summary->format_hint), "%s", media->format_hint);
    summary->vendor = media->vendor;
    summary->format = media->format;
    summary->transport = media->transport;
}

static int player_seek_guard_ms_for_media(const PlayerMedia *media)
{
    if (!media || !media->flags.is_hls)
        return 0;

    switch (media->transport)
    {
    case PLAYER_MEDIA_TRANSPORT_HLS_LOCAL_PROXY:
        return PLAYER_HLS_LOCAL_PROXY_STARTUP_SEEK_GUARD_MS;
    case PLAYER_MEDIA_TRANSPORT_HLS_GATEWAY:
        return PLAYER_HLS_GATEWAY_STARTUP_SEEK_GUARD_MS;
    case PLAYER_MEDIA_TRANSPORT_HLS_DIRECT:
    default:
        return PLAYER_HLS_STARTUP_SEEK_GUARD_MS;
    }
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
    condvarInit(&g_player_cond);
    g_player_sync_ready = true;
}

static void player_reset_snapshot_locked(void)
{
    memset(&g_snapshot, 0, sizeof(g_snapshot));
    g_snapshot.state = PLAYER_STATE_IDLE;
    player_reset_media_summary(&g_snapshot.media);
}

static void player_reset_command_queue_locked(void)
{
    memset(g_command_slots, 0, sizeof(g_command_slots));
    memset(g_command_queue, 0, sizeof(g_command_queue));
    g_command_head = 0;
    g_command_tail = 0;
    g_command_count = 0;
    g_pending_seek_active = false;
    g_pending_seek_ms = 0;
}

static void player_store_media_locked(bool has_media, const PlayerMedia *media)
{
    g_has_current_media = has_media;
    if (has_media && media)
    {
        g_current_media = *media;
        g_snapshot.has_media = true;
        player_fill_media_summary(&g_snapshot.media, media);
    }
    else
    {
        ingress_reset(&g_current_media);
        g_snapshot.has_media = false;
        player_reset_media_summary(&g_snapshot.media);
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

    if (g_has_current_media)
    {
        g_snapshot.has_media = true;
        player_fill_media_summary(&g_snapshot.media, &g_current_media);
    }
    else
    {
        g_snapshot.has_media = false;
        player_reset_media_summary(&g_snapshot.media);
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
    out->has_media = g_has_current_media;
    if (g_has_current_media)
        player_fill_media_summary(&out->media, &g_current_media);
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
    case PLAYER_COMMAND_SET_MEDIA:
    {
        bool old_has_media;
        PlayerMedia old_media;

        mutexLock(&g_player_mutex);
        old_has_media = g_has_current_media;
        old_media = g_current_media;
        player_store_media_locked(true, &slot->media);
        mutexUnlock(&g_player_mutex);

        result = g_backend->set_media && g_backend->set_media(&slot->media);
        if (!result)
        {
            mutexLock(&g_player_mutex);
            player_store_media_locked(old_has_media, old_has_media ? &old_media : NULL);
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

static bool player_should_coalesce_seek_locked(int position_ms)
{
    int guard_ms;

    (void)position_ms;

    if (!g_has_current_media)
        return false;

    if (g_pending_seek_active)
        return true;

    guard_ms = player_seek_guard_ms_for_media(&g_current_media);

    if (g_has_current_media &&
        guard_ms > 0 &&
        g_current_media.flags.is_hls &&
        g_snapshot.state == PLAYER_STATE_PLAYING &&
        g_snapshot.position_ms >= 0 &&
        g_snapshot.position_ms < guard_ms)
    {
        return true;
    }

    switch (g_snapshot.state)
    {
    case PLAYER_STATE_LOADING:
    case PLAYER_STATE_BUFFERING:
    case PLAYER_STATE_SEEKING:
        return true;
    default:
        return false;
    }
}

static bool player_maybe_dispatch_pending_seek(void)
{
    PlayerCommandSlot slot;
    PlayerState state = PLAYER_STATE_IDLE;
    int target_ms = 0;
    int guard_ms = 0;

    memset(&slot, 0, sizeof(slot));

    mutexLock(&g_player_mutex);
    if (!g_pending_seek_active || !g_has_current_media)
    {
        if (!g_has_current_media)
        {
            g_pending_seek_active = false;
            g_pending_seek_ms = 0;
        }
        mutexUnlock(&g_player_mutex);
        return false;
    }

    state = g_snapshot.state;
    switch (state)
    {
    case PLAYER_STATE_LOADING:
    case PLAYER_STATE_BUFFERING:
    case PLAYER_STATE_SEEKING:
        mutexUnlock(&g_player_mutex);
        return false;
    case PLAYER_STATE_IDLE:
    case PLAYER_STATE_STOPPED:
    case PLAYER_STATE_ERROR:
        g_pending_seek_active = false;
        g_pending_seek_ms = 0;
        mutexUnlock(&g_player_mutex);
        return false;
    default:
        break;
    }

    guard_ms = player_seek_guard_ms_for_media(&g_current_media);

    if (guard_ms > 0 &&
        g_current_media.flags.is_hls &&
        state == PLAYER_STATE_PLAYING &&
        g_snapshot.position_ms >= 0 &&
        g_snapshot.position_ms < guard_ms)
    {
        mutexUnlock(&g_player_mutex);
        return false;
    }

    if (!g_snapshot.seekable)
    {
        mutexUnlock(&g_player_mutex);
        return false;
    }

    target_ms = g_pending_seek_ms;
    g_pending_seek_active = false;
    g_pending_seek_ms = 0;
    mutexUnlock(&g_player_mutex);

    slot.type = PLAYER_COMMAND_SEEK_MS;
    slot.int_value = target_ms;
    log_info("[player] applying coalesced seek_ms=%d state=%s transport=%s\n",
             target_ms,
             player_state_name(state),
             ingress_transport_name(g_current_media.transport));
    return player_execute_command(&slot);
}

static bool player_submit_command(PlayerCommandType type,
                                  const PlayerMedia *media,
                                  int int_value,
                                  bool bool_value)
{
    bool result = false;
    ssize_t slot_index;

    if (!g_initialized || !g_backend || !g_player_thread_running)
        return false;

    player_ensure_sync_primitives();
    mutexLock(&g_player_mutex);

    if (type == PLAYER_COMMAND_SET_MEDIA || type == PLAYER_COMMAND_STOP)
    {
        g_pending_seek_active = false;
        g_pending_seek_ms = 0;
    }

    if (type == PLAYER_COMMAND_SEEK_MS && player_should_coalesce_seek_locked(int_value))
    {
        bool replaced = g_pending_seek_active;
        g_pending_seek_active = true;
        g_pending_seek_ms = int_value;
        log_info("[player] coalesced seek_ms=%d state=%s transport=%s replaced=%d\n",
                 int_value,
                 player_state_name(g_snapshot.state),
                 ingress_transport_name(g_current_media.transport),
                 replaced ? 1 : 0);
        mutexUnlock(&g_player_mutex);
        return true;
    }

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
    if (media)
        slot->media = *media;

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

        if (player_maybe_dispatch_pending_seek())
            continue;
    }

    mutexLock(&g_player_mutex);
    g_player_thread_running = false;
    condvarWakeAll(&g_player_cond);
    mutexUnlock(&g_player_mutex);
}

static const char *player_get_backend_name(void)
{
    if (g_backend && g_backend->name)
        return g_backend->name;
    return backend_name_from_type(g_backend_type);
}

const char *player_media_vendor_name(PlayerMediaVendor vendor)
{
    return ingress_vendor_name(vendor);
}

const char *player_media_format_name(PlayerMediaFormat format)
{
    return ingress_format_name(format);
}

const char *player_media_transport_name(PlayerMediaTransport transport)
{
    return ingress_transport_name(transport);
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
    ingress_reset(&g_current_media);
    g_has_current_media = false;
    player_reset_snapshot_locked();
    player_reset_command_queue_locked();
    g_player_stop_requested = false;
    mutexUnlock(&g_player_mutex);

    if (g_backend_type == PLAYER_BACKEND_AUTO)
    {
        log_info("[player] auto resolve libmpv_available=%d mock_available=%d\n",
                 backend_available(&g_libmpv_ops) ? 1 : 0,
                 backend_available(&g_mock_ops) ? 1 : 0);
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
    player_store_media_locked(false, NULL);
    mutexUnlock(&g_player_mutex);

    if (g_backend && g_backend->deinit)
        g_backend->deinit();

    log_info("[player] deinit backend=%s\n", player_get_backend_name());
    g_backend = NULL;
    g_initialized = false;
}

bool player_set_uri(const char *uri, const char *metadata)
{
    return player_set_uri_with_context(uri, metadata, NULL);
}

bool player_set_uri_with_context(const char *uri, const char *metadata, const PlayerOpenContext *ctx)
{
    PlayerMedia resolved;

    if (!ingress_resolve_with_context(uri, metadata, ctx, &resolved))
        return false;

    log_info("[player] resolve_media profile=%s vendor=%s format=%s transport=%s hint=%s mime=%s selected_from_metadata=%d candidates=%d hls=%d local_proxy=%d live_hint=%d dash=%d flv=%d mp4=%d ts=%d signed=%d bilibili=%d segmented=%d video_only=%d timeout=%d readahead_s=%d\n",
             ingress_profile_name(resolved.profile),
             ingress_vendor_name(resolved.vendor),
             ingress_format_name(resolved.format),
             ingress_transport_name(resolved.transport),
             resolved.format_hint[0] != '\0' ? resolved.format_hint : "unknown",
             resolved.mime_type[0] != '\0' ? resolved.mime_type : "unknown",
             resolved.selected_from_metadata ? 1 : 0,
             resolved.metadata_candidate_count,
             resolved.flags.is_hls ? 1 : 0,
             resolved.flags.is_local_proxy ? 1 : 0,
             resolved.flags.likely_live ? 1 : 0,
             resolved.flags.is_dash ? 1 : 0,
             resolved.flags.is_flv ? 1 : 0,
             resolved.flags.is_mp4 ? 1 : 0,
             resolved.flags.is_mpeg_ts ? 1 : 0,
             resolved.flags.is_signed ? 1 : 0,
             resolved.flags.is_bilibili ? 1 : 0,
             resolved.flags.likely_segmented ? 1 : 0,
             resolved.flags.likely_video_only ? 1 : 0,
             resolved.network_timeout_seconds,
             resolved.demuxer_readahead_seconds);

    if (resolved.selected_from_metadata)
    {
        log_info("[player] selected_resource original_uri=%s selected_uri=%s protocol_info=%s\n",
                 resolved.original_uri[0] != '\0' ? resolved.original_uri : "<empty>",
                 resolved.uri[0] != '\0' ? resolved.uri : "<empty>",
                 resolved.protocol_info[0] != '\0' ? resolved.protocol_info : "<none>");
    }

    if (resolved.flags.likely_video_only)
    {
        log_warn("[player] %s media looks like segmented DASH/fMP4 video. Generic DMR may not have the companion audio track.\n",
                 ingress_vendor_name(resolved.vendor));
    }

    return player_set_media(&resolved);
}

static bool player_set_media(const PlayerMedia *media)
{
    if (!media)
        return false;
    return player_submit_command(PLAYER_COMMAND_SET_MEDIA, media, 0, false);
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

bool player_get_snapshot(PlayerSnapshot *out)
{
    if (!out)
        return false;

    mutexLock(&g_player_mutex);
    *out = g_snapshot;
    mutexUnlock(&g_player_mutex);
    return true;
}
