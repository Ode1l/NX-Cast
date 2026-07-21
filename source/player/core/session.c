#include "player/player.h"

#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "log/log.h"
#include "player/backend.h"
#include "player/backend/libmpv_airplay.h"
#include "player/core/media_actor.h"
#include "player/core/ownership.h"
#include "player/trace.h"
#include "protocol/airplay/media/stream_bridge.h"

#define PLAYER_ACTOR_CAPACITY 64
#define PLAYER_ACTOR_RESERVED_CAPACITY 4
#define PLAYER_ACTOR_MAX_COMMAND_BURST 8
#define PLAYER_ACTOR_IDLE_POLL_MS 10
#define PLAYER_ACTOR_THREAD_STACK_SIZE 0x8000
#define PLAYER_ACTOR_COMMAND_TIMEOUT_MS 2000
#define PLAYER_TRACE_URL_MAX 160

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
static MediaActor *g_media_actor = NULL;

static void player_log_trace_action(const char *action, const char *phase,
                                    const char *detail);
static bool player_execute_set_media(const PlayerMedia *media);
static bool player_execute_actor_command(void *context,
                                         const MediaActorCommand *command);
static bool player_validate_actor_command(void *context,
                                          const MediaActorCommand *command);
static void player_actor_pump_events(void *context, int timeout_ms);
static void player_actor_wakeup(void *context);
static void player_emit_from_backend(const PlayerEvent *event);
static bool player_actor_initialize(void *context);
static void player_actor_finalize(void *context);

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

static bool player_actor_initialize(void *context)
{
    (void)context;

    if (!g_initialized || !g_backend)
        return false;
    log_info("[player-actor] lifecycle=initialize phase=begin backend=%s\n",
             player_get_backend_name());
    if (g_backend->set_event_sink)
        g_backend->set_event_sink(player_emit_from_backend);
    if (g_backend->init && !g_backend->init())
    {
        if (g_backend->set_event_sink)
            g_backend->set_event_sink(NULL);
        log_error("[player-actor] lifecycle=initialize phase=failed backend=%s\n",
                  player_get_backend_name());
        return false;
    }
    player_refresh_cached_snapshot_from_backend();
    log_info("[player-actor] lifecycle=initialize phase=ready backend=%s\n",
             player_get_backend_name());
    return true;
}

static void player_actor_finalize(void *context)
{
    (void)context;

    if (!g_backend)
        return;
    log_info("[player-actor] lifecycle=finalize phase=begin backend=%s\n",
             player_get_backend_name());
    if (g_backend->set_event_sink)
        g_backend->set_event_sink(NULL);
    if (g_backend->deinit)
        g_backend->deinit();
    log_info("[player-actor] lifecycle=finalize phase=done backend=%s\n",
             player_get_backend_name());
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

static bool player_run_backend_bool(const char *action, bool (*fn)(void))
{
    bool ok;

    if (!g_initialized || !g_backend || !fn)
        return false;

    player_log_trace_action(action, "backend-call-begin", "-");
    ok = fn();
    player_log_trace_action(action, "backend-call-done", ok ? "ok" : "failed");
    if (ok)
    {
        player_log_trace_action(action, "snapshot-refresh-begin", "-");
        player_refresh_cached_snapshot_from_backend();
        player_log_trace_action(action, "snapshot-refresh-done", "-");
    }
    if (ok && g_backend->wakeup)
    {
        player_log_trace_action(action, "backend-wakeup-begin", "-");
        g_backend->wakeup();
        player_log_trace_action(action, "backend-wakeup-done", "-");
    }
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

static bool player_run_backend_string_int(bool (*fn)(const char *, int), const char *text, int value)
{
    bool ok;

    if (!g_initialized || !g_backend || !fn || !text)
        return false;

    ok = fn(text, value);
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

static bool player_actor_submit_wait(const MediaActorCommand *command)
{
    MediaActorSubmitStatus status;

    if (!g_initialized || !g_media_actor || !command)
        return false;

    status = media_actor_submit_wait(g_media_actor, command,
                                     PLAYER_ACTOR_COMMAND_TIMEOUT_MS);
    if (status == MEDIA_ACTOR_SUBMIT_EXECUTED)
        return true;

    log_warn("[player-actor] command=%d producer=%d token=%llu generation=%u status=%s\n",
             (int)command->kind,
             (int)command->producer,
             (unsigned long long)command->session_token,
             command->generation,
             media_actor_submit_status_name(status));
    return false;
}

static PlayerMediaOwner player_owner_from_actor_producer(
    MediaActorProducer producer)
{
    switch (producer)
    {
    case MEDIA_ACTOR_PRODUCER_DLNA:
        return PLAYER_MEDIA_OWNER_DLNA;
    case MEDIA_ACTOR_PRODUCER_IPTV:
        return PLAYER_MEDIA_OWNER_IPTV;
    case MEDIA_ACTOR_PRODUCER_AIRPLAY_VIDEO:
        return PLAYER_MEDIA_OWNER_AIRPLAY_VIDEO;
    case MEDIA_ACTOR_PRODUCER_AIRPLAY_MIRROR:
        return PLAYER_MEDIA_OWNER_AIRPLAY_MIRROR;
    default:
        return PLAYER_MEDIA_OWNER_NONE;
    }
}

static bool player_validate_actor_command(void *context,
                                          const MediaActorCommand *command)
{
    PlayerMediaOwner owner;
    PlayerOwnershipLease lease;

    (void)context;
    if (!command)
        return false;
    owner = player_owner_from_actor_producer(command->producer);
    if (owner == PLAYER_MEDIA_OWNER_NONE)
        return true;
    lease.owner = owner;
    lease.token = command->session_token;
    lease.generation = command->generation;
    return player_ownership_validate(&lease);
}

static bool player_execute_actor_command(void *context,
                                         const MediaActorCommand *command)
{
    PlayerMedia media = {0};

    (void)context;
    if (!command || !g_backend)
        return false;

    switch (command->kind)
    {
    case MEDIA_ACTOR_COMMAND_OPEN:
        media.uri = (char *)command->text;
        media.metadata = (char *)command->metadata;
        if (!player_execute_set_media(&media))
            return false;
        return !command->flag ||
               player_run_backend_bool("Play", g_backend->play);
    case MEDIA_ACTOR_COMMAND_PLAY:
        return player_run_backend_bool("Play", g_backend->play);
    case MEDIA_ACTOR_COMMAND_PAUSE:
        return player_run_backend_bool("Pause", g_backend->pause);
    case MEDIA_ACTOR_COMMAND_STOP:
    case MEDIA_ACTOR_COMMAND_STOP_ANY:
    case MEDIA_ACTOR_COMMAND_QUIESCE:
        return player_run_backend_bool("Stop", g_backend->stop);
    case MEDIA_ACTOR_COMMAND_SEEK_TARGET:
        return player_run_backend_string(g_backend->seek_target, command->text);
    case MEDIA_ACTOR_COMMAND_SEEK_MS:
        return player_run_backend_int(g_backend->seek_ms, command->value);
    case MEDIA_ACTOR_COMMAND_SET_VOLUME:
        return player_run_backend_int(g_backend->set_volume, command->value);
    case MEDIA_ACTOR_COMMAND_SET_MUTE:
        return player_run_backend_flag(g_backend->set_mute, command->flag);
    case MEDIA_ACTOR_COMMAND_SHOW_OSD:
        return player_run_backend_string_int(g_backend->show_osd,
                                             command->text,
                                             command->value);
    case MEDIA_ACTOR_COMMAND_BIND_STREAM:
        return player_libmpv_set_airplay_stream_bridge(command->opaque);
    case MEDIA_ACTOR_COMMAND_RELEASE_LEASE:
    {
        PlayerOwnershipLease lease = {
            .owner = player_owner_from_actor_producer(command->producer),
            .token = command->session_token,
            .generation = command->generation,
        };
        return player_ownership_release(&lease);
    }
    case MEDIA_ACTOR_COMMAND_SHUTDOWN:
        return true;
    default:
        return false;
    }
}

static void player_actor_pump_events(void *context, int timeout_ms)
{
    (void)context;
    if (g_backend && g_backend->pump_events)
        (void)g_backend->pump_events(timeout_ms);
}

static void player_actor_wakeup(void *context)
{
    (void)context;
    if (g_backend && g_backend->wakeup)
        g_backend->wakeup();
}

static void player_log_trace_action(const char *action, const char *phase, const char *detail)
{
    player_trace_log("[media-trace] seq=%u t_ms=%llu layer=player action=%s phase=%s url_hash=%08x detail=%s\n",
                     player_trace_current_media_seq(),
                     (unsigned long long)player_trace_elapsed_ms(),
                     action ? action : "(unknown)",
                     phase ? phase : "(unknown)",
                     player_trace_current_media_hash(),
                     detail ? detail : "-");
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

static MediaActorCommandKind player_actor_kind(PlayerCommandKind kind)
{
    switch (kind)
    {
    case PLAYER_COMMAND_OPEN:
        return MEDIA_ACTOR_COMMAND_OPEN;
    case PLAYER_COMMAND_PLAY:
        return MEDIA_ACTOR_COMMAND_PLAY;
    case PLAYER_COMMAND_PAUSE:
        return MEDIA_ACTOR_COMMAND_PAUSE;
    case PLAYER_COMMAND_STOP:
        return MEDIA_ACTOR_COMMAND_STOP;
    case PLAYER_COMMAND_STOP_ANY:
        return MEDIA_ACTOR_COMMAND_STOP_ANY;
    case PLAYER_COMMAND_SEEK_TARGET:
        return MEDIA_ACTOR_COMMAND_SEEK_TARGET;
    case PLAYER_COMMAND_SEEK_MS:
        return MEDIA_ACTOR_COMMAND_SEEK_MS;
    case PLAYER_COMMAND_SET_VOLUME:
        return MEDIA_ACTOR_COMMAND_SET_VOLUME;
    case PLAYER_COMMAND_SET_MUTE:
        return MEDIA_ACTOR_COMMAND_SET_MUTE;
    case PLAYER_COMMAND_SHOW_OSD:
        return MEDIA_ACTOR_COMMAND_SHOW_OSD;
    case PLAYER_COMMAND_RELEASE_LEASE:
        return MEDIA_ACTOR_COMMAND_RELEASE_LEASE;
    default:
        return 0;
    }
}

static MediaActorProducer player_actor_producer(PlayerCommandSource source)
{
    switch (source)
    {
    case PLAYER_COMMAND_SOURCE_APP:
        return MEDIA_ACTOR_PRODUCER_APP;
    case PLAYER_COMMAND_SOURCE_UI:
        return MEDIA_ACTOR_PRODUCER_UI;
    case PLAYER_COMMAND_SOURCE_DLNA:
        return MEDIA_ACTOR_PRODUCER_DLNA;
    case PLAYER_COMMAND_SOURCE_IPTV:
        return MEDIA_ACTOR_PRODUCER_IPTV;
    case PLAYER_COMMAND_SOURCE_AIRPLAY_VIDEO:
        return MEDIA_ACTOR_PRODUCER_AIRPLAY_VIDEO;
    case PLAYER_COMMAND_SOURCE_AIRPLAY_MIRROR:
        return MEDIA_ACTOR_PRODUCER_AIRPLAY_MIRROR;
    default:
        return MEDIA_ACTOR_PRODUCER_UNKNOWN;
    }
}

static PlayerMediaOwner player_source_owner(PlayerCommandSource source)
{
    switch (source)
    {
    case PLAYER_COMMAND_SOURCE_DLNA:
        return PLAYER_MEDIA_OWNER_DLNA;
    case PLAYER_COMMAND_SOURCE_IPTV:
        return PLAYER_MEDIA_OWNER_IPTV;
    case PLAYER_COMMAND_SOURCE_AIRPLAY_VIDEO:
        return PLAYER_MEDIA_OWNER_AIRPLAY_VIDEO;
    case PLAYER_COMMAND_SOURCE_AIRPLAY_MIRROR:
        return PLAYER_MEDIA_OWNER_AIRPLAY_MIRROR;
    default:
        return PLAYER_MEDIA_OWNER_NONE;
    }
}

static PlayerCommandStatus player_command_status_from_actor(
    MediaActorSubmitStatus status)
{
    switch (status)
    {
    case MEDIA_ACTOR_SUBMIT_ACCEPTED:
        return PLAYER_COMMAND_STATUS_ACCEPTED;
    case MEDIA_ACTOR_SUBMIT_EXECUTED:
        return PLAYER_COMMAND_STATUS_EXECUTED;
    case MEDIA_ACTOR_SUBMIT_EXECUTION_FAILED:
        return PLAYER_COMMAND_STATUS_EXECUTION_FAILED;
    case MEDIA_ACTOR_SUBMIT_INVALID:
        return PLAYER_COMMAND_STATUS_INVALID;
    case MEDIA_ACTOR_SUBMIT_NOT_RUNNING:
        return PLAYER_COMMAND_STATUS_NOT_RUNNING;
    case MEDIA_ACTOR_SUBMIT_QUEUE_FULL:
        return PLAYER_COMMAND_STATUS_QUEUE_FULL;
    case MEDIA_ACTOR_SUBMIT_NO_MEMORY:
        return PLAYER_COMMAND_STATUS_NO_MEMORY;
    case MEDIA_ACTOR_SUBMIT_TIMEOUT:
        return PLAYER_COMMAND_STATUS_TIMEOUT;
    case MEDIA_ACTOR_SUBMIT_SHUTTING_DOWN:
        return PLAYER_COMMAND_STATUS_SHUTTING_DOWN;
    case MEDIA_ACTOR_SUBMIT_STALE:
        return PLAYER_COMMAND_STATUS_STALE;
    default:
        return PLAYER_COMMAND_STATUS_INVALID;
    }
}

static bool player_command_request_valid(const PlayerCommandRequest *request)
{
    PlayerMediaOwner expected_owner;

    if (!request || player_actor_kind(request->kind) == 0 ||
        player_actor_producer(request->source) == MEDIA_ACTOR_PRODUCER_UNKNOWN)
    {
        return false;
    }
    if (request->kind == PLAYER_COMMAND_OPEN &&
        (!request->uri || request->uri[0] == '\0'))
        return false;
    if ((request->kind == PLAYER_COMMAND_SEEK_TARGET ||
         request->kind == PLAYER_COMMAND_SHOW_OSD) && !request->text)
        return false;

    expected_owner = player_source_owner(request->source);
    if (expected_owner == PLAYER_MEDIA_OWNER_NONE)
        return request->kind != PLAYER_COMMAND_STOP ||
               request->source == PLAYER_COMMAND_SOURCE_APP ||
               request->source == PLAYER_COMMAND_SOURCE_UI;
    if (request->kind == PLAYER_COMMAND_STOP_ANY)
        return false;
    return request->lease.owner == expected_owner &&
           request->lease.generation != 0u;
}

static PlayerCommandStatus player_submit_command(
    const PlayerCommandRequest *request, bool wait, uint32_t timeout_ms)
{
    MediaActorCommand command;
    MediaActorSubmitStatus status;

    if (!player_command_request_valid(request))
        return PLAYER_COMMAND_STATUS_INVALID;
    if (player_source_owner(request->source) != PLAYER_MEDIA_OWNER_NONE &&
        !player_ownership_validate(&request->lease))
        return PLAYER_COMMAND_STATUS_STALE;
    if (!g_initialized || !g_media_actor)
        return PLAYER_COMMAND_STATUS_NOT_RUNNING;

    memset(&command, 0, sizeof(command));
    command.kind = player_actor_kind(request->kind);
    command.producer = player_actor_producer(request->source);
    command.session_token = request->lease.token;
    command.generation = request->lease.generation;
    command.text = request->kind == PLAYER_COMMAND_OPEN ? request->uri
                                                        : request->text;
    command.metadata = request->metadata;
    command.value = request->value;
    command.flag = request->flag;

    status = wait ? media_actor_submit_wait(g_media_actor, &command, timeout_ms)
                  : media_actor_submit_async(g_media_actor, &command);
    if (status != MEDIA_ACTOR_SUBMIT_ACCEPTED &&
        status != MEDIA_ACTOR_SUBMIT_EXECUTED)
    {
        log_warn("[player-actor] event=reject command=%d source=%d owner=%s token=%llu generation=%u status=%s\n",
                 (int)request->kind,
                 (int)request->source,
                 player_media_owner_name(request->lease.owner),
                 (unsigned long long)request->lease.token,
                 request->lease.generation,
                 media_actor_submit_status_name(status));
    }
    return player_command_status_from_actor(status);
}

PlayerCommandStatus player_submit_command_async(
    const PlayerCommandRequest *request)
{
    return player_submit_command(request, false, 0);
}

PlayerCommandStatus player_submit_command_wait(
    const PlayerCommandRequest *request, uint32_t timeout_ms)
{
    return player_submit_command(request, true, timeout_ms);
}

bool player_command_status_succeeded(PlayerCommandStatus status)
{
    return status == PLAYER_COMMAND_STATUS_ACCEPTED ||
           status == PLAYER_COMMAND_STATUS_EXECUTED;
}

const char *player_command_status_name(PlayerCommandStatus status)
{
    switch (status)
    {
    case PLAYER_COMMAND_STATUS_ACCEPTED:
        return "accepted";
    case PLAYER_COMMAND_STATUS_EXECUTED:
        return "executed";
    case PLAYER_COMMAND_STATUS_EXECUTION_FAILED:
        return "execution-failed";
    case PLAYER_COMMAND_STATUS_INVALID:
        return "invalid";
    case PLAYER_COMMAND_STATUS_NOT_RUNNING:
        return "not-running";
    case PLAYER_COMMAND_STATUS_QUEUE_FULL:
        return "queue-full";
    case PLAYER_COMMAND_STATUS_NO_MEMORY:
        return "no-memory";
    case PLAYER_COMMAND_STATUS_TIMEOUT:
        return "timeout";
    case PLAYER_COMMAND_STATUS_SHUTTING_DOWN:
        return "shutting-down";
    case PLAYER_COMMAND_STATUS_STALE:
        return "stale";
    default:
        return "unknown";
    }
}

bool player_get_runtime_health(PlayerRuntimeHealth *health_out)
{
    MediaActorHealth actor_health;

    if (!health_out || !g_media_actor ||
        !media_actor_get_health(g_media_actor, &actor_health))
        return false;
    memset(health_out, 0, sizeof(*health_out));
    health_out->running = actor_health.running;
    health_out->stopping = actor_health.stopping;
    health_out->accepting = actor_health.accepting;
    health_out->initializing = actor_health.initializing;
    health_out->backend_ready = actor_health.ready;
    health_out->initialization_failed = actor_health.initialization_failed;
    health_out->dispatch_enabled = actor_health.dispatch_enabled;
    health_out->queue_depth = actor_health.pending;
    health_out->queue_high_watermark = actor_health.max_depth;
    health_out->submitted = actor_health.submitted;
    health_out->executed = actor_health.executed;
    health_out->execution_failed = actor_health.execution_failed;
    health_out->rejected_full = actor_health.rejected_full;
    health_out->rejected_stopping = actor_health.rejected_stopping;
    health_out->rejected_stale = actor_health.rejected_stale;
    health_out->coalesced = actor_health.coalesced;
    health_out->timed_out = actor_health.timed_out;
    health_out->current_command_id = actor_health.current_command_id;
    health_out->current_command_kind = (int)actor_health.current_command_kind;
    health_out->current_command_producer =
        (int)actor_health.current_command_producer;
    health_out->current_session_token = actor_health.current_session_token;
    health_out->current_generation = actor_health.current_generation;
    health_out->last_completed_command_id =
        actor_health.last_completed_command_id;
    health_out->current_command_age_ms = actor_health.current_command_age_ms;
    health_out->heartbeat_age_ms = actor_health.heartbeat_age_ms;
    return true;
}

bool player_activate(void)
{
    return g_media_actor && media_actor_activate(g_media_actor);
}

void player_quiesce(void)
{
    if (g_media_actor)
        media_actor_quiesce(g_media_actor);
}

bool player_wait_idle(uint32_t timeout_ms)
{
    return g_media_actor && media_actor_wait_idle(g_media_actor, timeout_ms);
}

static void player_airplay_bridge_retain(void *opaque)
{
    airplay_stream_bridge_retain(opaque);
}

static void player_airplay_bridge_release(void *opaque)
{
    airplay_stream_bridge_release(opaque);
}

PlayerCommandStatus player_submit_airplay_stream_bridge(
    AirPlayStreamBridge *bridge, const PlayerOwnershipLease *lease)
{
    MediaActorCommand command = {
        .kind = MEDIA_ACTOR_COMMAND_BIND_STREAM,
        .producer = MEDIA_ACTOR_PRODUCER_AIRPLAY_MIRROR,
        .opaque = bridge,
        .opaque_retain = player_airplay_bridge_retain,
        .opaque_release = player_airplay_bridge_release,
    };

    if (!lease || lease->owner != PLAYER_MEDIA_OWNER_AIRPLAY_MIRROR ||
        lease->generation == 0u)
        return PLAYER_COMMAND_STATUS_INVALID;
    if (!player_ownership_validate(lease))
        return PLAYER_COMMAND_STATUS_STALE;
    if (!g_initialized || !g_media_actor)
        return PLAYER_COMMAND_STATUS_NOT_RUNNING;
    command.session_token = lease->token;
    command.generation = lease->generation;
    return player_command_status_from_actor(
        media_actor_submit_async(g_media_actor, &command));
}

bool player_init(void)
{
    MediaActorConfig actor_config = {
        .capacity = PLAYER_ACTOR_CAPACITY,
        .reserved_capacity = PLAYER_ACTOR_RESERVED_CAPACITY,
        .max_command_burst = PLAYER_ACTOR_MAX_COMMAND_BURST,
        .idle_poll_ms = PLAYER_ACTOR_IDLE_POLL_MS,
        .thread_stack_size = PLAYER_ACTOR_THREAD_STACK_SIZE,
        .thread_priority = 0x2B,
        .thread_core = -2,
        .start_suspended = true,
        .initialize = player_actor_initialize,
        .finalize = player_actor_finalize,
        .validate = player_validate_actor_command,
        .execute = player_execute_actor_command,
        .pump_events = player_actor_pump_events,
        .wakeup = player_actor_wakeup,
        .context = NULL,
    };

    if (g_initialized)
        return true;

    player_ensure_sync_primitives();
    mutexLock(&g_player_mutex);
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

    g_media_actor = media_actor_create(&actor_config);
    if (!g_media_actor)
    {
        log_error("[player] media actor create failed\n");
        g_backend = NULL;
        return false;
    }

    g_initialized = true;
    if (!media_actor_start(g_media_actor))
    {
        log_error("[player] media actor start failed\n");
        g_initialized = false;
        media_actor_destroy(g_media_actor);
        g_media_actor = NULL;
        g_backend = NULL;
        return false;
    }

    log_info("[player] init accepted backend=%s lifecycle=async-suspended actor_capacity=%d reserved=%d\n",
             player_get_backend_name(),
             PLAYER_ACTOR_CAPACITY,
             PLAYER_ACTOR_RESERVED_CAPACITY);
    return true;
}

void player_deinit(void)
{
    MediaActorHealth actor_health = {0};

    if (!g_initialized)
    {
        player_ownership_reset();
        return;
    }

    log_info("[player] deinit begin backend=%s actor_started=%d\n",
             player_get_backend_name(),
             g_media_actor ? 1 : 0);

    if (g_media_actor)
    {
        log_info("[player] deinit step=actor_stop begin\n");
        media_actor_stop(g_media_actor);
        (void)media_actor_get_health(g_media_actor, &actor_health);
        log_info("[player] deinit step=actor_stop done submitted=%llu executed=%llu failed=%llu rejected_full=%llu rejected_stopping=%llu rejected_stale=%llu timeout=%llu max_depth=%u\n",
                 (unsigned long long)actor_health.submitted,
                 (unsigned long long)actor_health.executed,
                 (unsigned long long)actor_health.execution_failed,
                 (unsigned long long)actor_health.rejected_full,
                 (unsigned long long)actor_health.rejected_stopping,
                 (unsigned long long)actor_health.rejected_stale,
                 (unsigned long long)actor_health.timed_out,
                 (unsigned int)actor_health.max_depth);
        media_actor_destroy(g_media_actor);
        g_media_actor = NULL;
    }

    mutexLock(&g_player_mutex);
    (void)player_store_media_locked(false, NULL);
    player_reset_snapshot_locked();
    mutexUnlock(&g_player_mutex);

    log_info("[player] deinit end backend=%s\n", player_get_backend_name());
    g_backend = NULL;
    g_initialized = false;
    player_ownership_reset();
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

static bool player_execute_set_media(const PlayerMedia *media)
{
    PlayerMedia previous_media = {0};
    bool previous_has_media;
    bool ok;
    uint32_t seq;
    uint32_t hash;
    char summary[PLAYER_TRACE_URL_MAX];

    if (!g_initialized || !g_backend || !g_backend->set_media || !media || !media->uri || media->uri[0] == '\0')
        return false;

    hash = player_trace_uri_hash(media->uri);
    seq = player_trace_current_media_seq();
    if (seq == 0 || player_trace_current_media_hash() != hash)
        seq = player_trace_begin_media("PlayerSetMedia", media->uri, media->metadata);
    player_trace_log("[media-trace] seq=%u t_ms=%llu layer=player action=set_media phase=begin url_hash=%08x url=%s\n",
                     seq,
                     (unsigned long long)player_trace_elapsed_ms(),
                     hash,
                     player_trace_uri_summary(media->uri, summary, sizeof(summary)));

    mutexLock(&g_player_mutex);
    previous_has_media = g_has_current_media;
    if (previous_has_media && !player_media_copy(&previous_media, &g_current_media))
    {
        mutexUnlock(&g_player_mutex);
        player_trace_warn("[media-trace] seq=%u t_ms=%llu layer=player action=set_media phase=failed reason=copy-previous url_hash=%08x url=%s\n",
                          seq,
                          (unsigned long long)player_trace_elapsed_ms(),
                          hash,
                          player_trace_uri_summary(media->uri, summary, sizeof(summary)));
        return false;
    }
    if (!player_store_media_locked(true, media))
    {
        player_media_clear(&previous_media);
        mutexUnlock(&g_player_mutex);
        player_trace_warn("[media-trace] seq=%u t_ms=%llu layer=player action=set_media phase=failed reason=store-media url_hash=%08x url=%s\n",
                          seq,
                          (unsigned long long)player_trace_elapsed_ms(),
                          hash,
                          player_trace_uri_summary(media->uri, summary, sizeof(summary)));
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
        player_trace_warn("[media-trace] seq=%u t_ms=%llu layer=player action=set_media phase=failed reason=backend url_hash=%08x url=%s\n",
                          seq,
                          (unsigned long long)player_trace_elapsed_ms(),
                          hash,
                          player_trace_uri_summary(media->uri, summary, sizeof(summary)));
        return false;
    }

    player_media_clear(&previous_media);
    player_refresh_cached_snapshot_from_backend();
    if (g_backend->wakeup)
        g_backend->wakeup();
    player_trace_log("[media-trace] seq=%u t_ms=%llu layer=player action=set_media phase=done url_hash=%08x url=%s\n",
                     seq,
                     (unsigned long long)player_trace_elapsed_ms(),
                     hash,
                     player_trace_uri_summary(media->uri, summary, sizeof(summary)));
    return true;
}

bool player_set_media(const PlayerMedia *media)
{
    MediaActorCommand command = {
        .kind = MEDIA_ACTOR_COMMAND_OPEN,
        .producer = MEDIA_ACTOR_PRODUCER_APP,
    };

    if (!media || !media->uri || media->uri[0] == '\0')
        return false;
    command.text = media->uri;
    command.metadata = media->metadata;
    return player_actor_submit_wait(&command);
}

bool player_play(void)
{
    MediaActorCommand command = {
        .kind = MEDIA_ACTOR_COMMAND_PLAY,
        .producer = MEDIA_ACTOR_PRODUCER_APP,
    };
    bool ok;

    player_log_trace_action("Play", "begin", "-");
    ok = player_actor_submit_wait(&command);
    player_log_trace_action("Play", ok ? "done" : "failed", ok ? "-" : "backend");
    return ok;
}

bool player_pause(void)
{
    MediaActorCommand command = {
        .kind = MEDIA_ACTOR_COMMAND_PAUSE,
        .producer = MEDIA_ACTOR_PRODUCER_APP,
    };
    bool ok;

    player_log_trace_action("Pause", "begin", "-");
    ok = player_actor_submit_wait(&command);
    player_log_trace_action("Pause", ok ? "done" : "failed", ok ? "-" : "backend");
    return ok;
}

bool player_stop(void)
{
    MediaActorCommand command = {
        .kind = MEDIA_ACTOR_COMMAND_STOP_ANY,
        .producer = MEDIA_ACTOR_PRODUCER_APP,
    };
    bool ok;

    player_log_trace_action("Stop", "begin", "-");
    ok = player_actor_submit_wait(&command);
    player_log_trace_action("Stop", ok ? "done" : "failed", ok ? "-" : "backend");
    return ok;
}

bool player_seek_target(const char *target)
{
    MediaActorCommand command = {
        .kind = MEDIA_ACTOR_COMMAND_SEEK_TARGET,
        .producer = MEDIA_ACTOR_PRODUCER_APP,
        .text = target,
    };
    bool ok;

    if (!target)
        return false;
    player_log_trace_action("Seek", "begin", target);
    ok = player_actor_submit_wait(&command);
    player_log_trace_action("Seek", ok ? "done" : "failed", ok ? target : "backend");
    return ok;
}

bool player_seek_ms(int position_ms)
{
    MediaActorCommand command = {
        .kind = MEDIA_ACTOR_COMMAND_SEEK_MS,
        .producer = MEDIA_ACTOR_PRODUCER_APP,
        .value = position_ms,
    };
    bool ok;
    char detail[32];

    snprintf(detail, sizeof(detail), "%d", position_ms);
    player_log_trace_action("SeekMs", "begin", detail);
    ok = player_actor_submit_wait(&command);
    player_log_trace_action("SeekMs", ok ? "done" : "failed", ok ? detail : "backend");
    return ok;
}

bool player_set_volume(int volume_0_100)
{
    MediaActorCommand command = {
        .kind = MEDIA_ACTOR_COMMAND_SET_VOLUME,
        .producer = MEDIA_ACTOR_PRODUCER_APP,
        .value = volume_0_100,
    };

    return player_actor_submit_wait(&command);
}

bool player_set_mute(bool mute)
{
    MediaActorCommand command = {
        .kind = MEDIA_ACTOR_COMMAND_SET_MUTE,
        .producer = MEDIA_ACTOR_PRODUCER_APP,
        .flag = mute,
    };

    return player_actor_submit_wait(&command);
}

bool player_show_osd(const char *text, int duration_ms)
{
    MediaActorCommand command = {
        .kind = MEDIA_ACTOR_COMMAND_SHOW_OSD,
        .producer = MEDIA_ACTOR_PRODUCER_APP,
        .text = text,
        .value = duration_ms,
    };

    if (!text)
        return false;
    return player_actor_submit_wait(&command);
}

bool player_video_supported(void)
{
    MediaActorHealth health;

    if (!g_initialized || !g_backend || !g_media_actor ||
        !media_actor_get_health(g_media_actor, &health) || !health.ready)
        return false;
    if ((!g_backend->render_attach_gl || !g_backend->render_frame_gl) &&
        (!g_backend->render_attach_sw || !g_backend->render_frame_sw) &&
        (!g_backend->render_attach_dk3d || !g_backend->render_frame_dk3d))
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

bool player_video_attach_dk3d(const PlayerVideoDk3dInit *init)
{
    if (!player_video_supported() || !g_backend->render_attach_dk3d)
        return false;
    return g_backend->render_attach_dk3d(init);
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

bool player_video_render_dk3d(const PlayerVideoDk3dFrame *frame)
{
    if (!player_video_supported() || !g_backend->render_frame_dk3d)
        return false;
    return g_backend->render_frame_dk3d(frame);
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
