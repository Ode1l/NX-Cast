#include "integration.h"

#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "app/protocol_coordinator.h"
#include "log/log.h"
#include "player/renderer.h"
#include "protocol/airplay/discovery/mdns.h"
#include "protocol/airplay/media/mirror_runtime.h"
#include "protocol/airplay/media/remote_video.h"
#include "protocol/airplay/receiver.h"
#include "protocol/airplay/security/crypto.h"
#include "protocol/airplay/trace.h"

#define AIRPLAY_CONTROL_PORT 7000u
#define AIRPLAY_STREAM_CAPACITY (4u * 1024u * 1024u)
#define AIRPLAY_STORAGE_DIRECTORY "sdmc:/switch/NX-Cast/airplay"
#define AIRPLAY_RESOURCE_TRANSITION_TIMEOUT_MS 6000u

#ifndef NXCAST_AIRPLAY_DISCOVERY_ENABLED
#define NXCAST_AIRPLAY_DISCOVERY_ENABLED 1
#endif

typedef struct
{
    Mutex mutex;
    bool mutex_ready;
    bool running;
    bool starting;
    bool stop_requested;
    bool pin_visible;
    char pin[AIRPLAY_INTEGRATION_PIN_SIZE];
    char status[AIRPLAY_INTEGRATION_STATUS_MAX];
    AirPlayRemoteVideo *remote_video;
    AirPlayMirrorRuntime *mirror_runtime;
    PlayerOwnershipLease remote_lease;
    PlayerOwnershipLease mirror_lease;
} AirPlayIntegrationState;

static AirPlayIntegrationState g_airplay;

static void integration_ensure_mutex(void)
{
    if (g_airplay.mutex_ready)
        return;
    mutexInit(&g_airplay.mutex);
    g_airplay.mutex_ready = true;
}

static void integration_set_status(const char *status)
{
    integration_ensure_mutex();
    mutexLock(&g_airplay.mutex);
    snprintf(g_airplay.status, sizeof(g_airplay.status), "%s",
             status ? status : "");
    mutexUnlock(&g_airplay.mutex);
}

static bool integration_stop_requested(void)
{
    bool requested;

    integration_ensure_mutex();
    mutexLock(&g_airplay.mutex);
    requested = g_airplay.stop_requested;
    mutexUnlock(&g_airplay.mutex);
    return requested;
}

static PlayerOwnershipLease integration_remote_lease(void)
{
    PlayerOwnershipLease lease;

    integration_ensure_mutex();
    mutexLock(&g_airplay.mutex);
    lease = g_airplay.remote_lease;
    mutexUnlock(&g_airplay.mutex);
    return lease;
}

static PlayerOwnershipLease integration_mirror_lease(void)
{
    PlayerOwnershipLease lease;

    integration_ensure_mutex();
    mutexLock(&g_airplay.mutex);
    lease = g_airplay.mirror_lease;
    mutexUnlock(&g_airplay.mutex);
    return lease;
}

static bool integration_submit_player(PlayerCommandSource source,
                                      const PlayerOwnershipLease *lease,
                                      PlayerCommandKind kind,
                                      const char *uri,
                                      const char *metadata,
                                      int value)
{
    PlayerCommandRequest request = {
        .kind = kind,
        .source = source,
        .uri = uri,
        .metadata = metadata,
        .value = value,
    };
    PlayerCommandStatus status;

    if (!lease)
        return false;
    request.lease = *lease;
    status = player_submit_command_async(&request);
    if (!player_command_status_succeeded(status))
    {
        log_warn("[airplay] player command rejected source=%d kind=%d token=%llu generation=%u status=%s\n",
                 (int)source, (int)kind,
                 (unsigned long long)lease->token, lease->generation,
                 player_command_status_name(status));
        return false;
    }
    return true;
}

static bool integration_remote_claim(uint64_t session_id, void *user_data)
{
    ProtocolMediaTransaction transaction;

    (void)user_data;
    if (!protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_AIRPLAY_VIDEO,
                                           session_id, &transaction))
        return false;
    if (!protocol_coordinator_media_wait_resources(
            &transaction.lease, AIRPLAY_RESOURCE_TRANSITION_TIMEOUT_MS))
    {
        protocol_coordinator_media_abort(&transaction);
        integration_set_status("AirPlay resource handoff failed");
        return false;
    }
    mutexLock(&g_airplay.mutex);
    g_airplay.remote_lease = transaction.lease;
    mutexUnlock(&g_airplay.mutex);
    integration_set_status("AirPlay video connected");
    protocol_coordinator_media_end(&transaction);
    return true;
}

static void integration_remote_release(uint64_t session_id, void *user_data)
{
    PlayerOwnershipLease lease;

    (void)user_data;
    lease = integration_remote_lease();
    if (lease.owner == PLAYER_MEDIA_OWNER_AIRPLAY_VIDEO &&
        lease.token == session_id)
    {
        (void)integration_submit_player(PLAYER_COMMAND_SOURCE_AIRPLAY_VIDEO,
                                        &lease, PLAYER_COMMAND_STOP, NULL,
                                        NULL, 0);
        (void)integration_submit_player(PLAYER_COMMAND_SOURCE_AIRPLAY_VIDEO,
                                        &lease,
                                        PLAYER_COMMAND_RELEASE_LEASE, NULL,
                                        NULL, 0);
        mutexLock(&g_airplay.mutex);
        memset(&g_airplay.remote_lease, 0, sizeof(g_airplay.remote_lease));
        mutexUnlock(&g_airplay.mutex);
        integration_set_status("Ready for AirPlay video");
    }
}

static bool integration_remote_load(const char *url, const char *metadata,
                                    void *user_data)
{
    PlayerOwnershipLease lease;

    (void)user_data;
    lease = integration_remote_lease();
    integration_set_status("Loading AirPlay video");
    return integration_submit_player(PLAYER_COMMAND_SOURCE_AIRPLAY_VIDEO,
                                     &lease, PLAYER_COMMAND_OPEN, url,
                                     metadata ? metadata : "AirPlay Video", 0);
}

static bool integration_remote_play(void *user_data)
{
    PlayerOwnershipLease lease;

    (void)user_data;
    lease = integration_remote_lease();
    return integration_submit_player(PLAYER_COMMAND_SOURCE_AIRPLAY_VIDEO,
                                     &lease, PLAYER_COMMAND_PLAY, NULL,
                                     NULL, 0);
}

static bool integration_remote_pause(void *user_data)
{
    PlayerOwnershipLease lease;

    (void)user_data;
    lease = integration_remote_lease();
    return integration_submit_player(PLAYER_COMMAND_SOURCE_AIRPLAY_VIDEO,
                                     &lease, PLAYER_COMMAND_PAUSE, NULL,
                                     NULL, 0);
}

static bool integration_remote_stop(void *user_data)
{
    PlayerOwnershipLease lease;

    (void)user_data;
    lease = integration_remote_lease();
    if (lease.owner == PLAYER_MEDIA_OWNER_NONE)
        return true;
    return integration_submit_player(PLAYER_COMMAND_SOURCE_AIRPLAY_VIDEO,
                                     &lease, PLAYER_COMMAND_STOP, NULL,
                                     NULL, 0);
}

static bool integration_remote_seek(int position_ms, void *user_data)
{
    PlayerOwnershipLease lease;

    (void)user_data;
    lease = integration_remote_lease();
    return integration_submit_player(PLAYER_COMMAND_SOURCE_AIRPLAY_VIDEO,
                                     &lease, PLAYER_COMMAND_SEEK_MS, NULL,
                                     NULL, position_ms);
}

static AirPlayRemoteVideoState integration_remote_state(PlayerState state)
{
    switch (state)
    {
    case PLAYER_STATE_LOADING:
        return AIRPLAY_REMOTE_VIDEO_LOADING;
    case PLAYER_STATE_BUFFERING:
    case PLAYER_STATE_SEEKING:
        return AIRPLAY_REMOTE_VIDEO_BUFFERING;
    case PLAYER_STATE_PLAYING:
        return AIRPLAY_REMOTE_VIDEO_PLAYING;
    case PLAYER_STATE_PAUSED:
        return AIRPLAY_REMOTE_VIDEO_PAUSED;
    case PLAYER_STATE_STOPPED:
    case PLAYER_STATE_IDLE:
        return AIRPLAY_REMOTE_VIDEO_STOPPED;
    case PLAYER_STATE_ERROR:
    default:
        return AIRPLAY_REMOTE_VIDEO_ERROR;
    }
}

static bool integration_remote_snapshot(AirPlayRemoteVideoSnapshot *snapshot_out,
                                        void *user_data)
{
    PlayerOwnershipLease lease;
    RendererSnapshot snapshot = {0};
    bool have_snapshot;

    (void)user_data;
    if (!snapshot_out)
        return false;
    lease = integration_remote_lease();
    if (!protocol_coordinator_media_validate(&lease))
        return false;
    have_snapshot = renderer_get_snapshot(&snapshot);
    if (!have_snapshot)
        return false;
    snapshot_out->has_media = snapshot.has_media;
    snapshot_out->seekable = snapshot.seekable;
    snapshot_out->position_ms = snapshot.position_ms;
    snapshot_out->duration_ms = snapshot.duration_ms;
    snapshot_out->state = integration_remote_state(snapshot.state);
    renderer_snapshot_clear(&snapshot);
    return true;
}

static bool integration_mirror_bind(AirPlayStreamBridge *bridge,
                                    void *user_data)
{
    PlayerOwnershipLease lease;
    PlayerCommandStatus status;

    (void)user_data;
    lease = integration_mirror_lease();
    status = player_submit_airplay_stream_bridge(bridge, &lease);
    if (!player_command_status_succeeded(status))
        log_warn("[airplay] stream bind rejected token=%llu generation=%u status=%s\n",
                 (unsigned long long)lease.token, lease.generation,
                 player_command_status_name(status));
    return player_command_status_succeeded(status);
}

static bool integration_mirror_set_uri(const char *uri, const char *metadata,
                                       void *user_data)
{
    PlayerOwnershipLease lease;

    (void)user_data;
    lease = integration_mirror_lease();
    return integration_submit_player(PLAYER_COMMAND_SOURCE_AIRPLAY_MIRROR,
                                     &lease, PLAYER_COMMAND_OPEN, uri,
                                     metadata, 0);
}

static bool integration_mirror_play(void *user_data)
{
    PlayerOwnershipLease lease;

    (void)user_data;
    lease = integration_mirror_lease();
    return integration_submit_player(PLAYER_COMMAND_SOURCE_AIRPLAY_MIRROR,
                                     &lease, PLAYER_COMMAND_PLAY, NULL,
                                     NULL, 0);
}

static bool integration_mirror_stop_player(void *user_data)
{
    PlayerOwnershipLease lease;

    (void)user_data;
    lease = integration_mirror_lease();
    if (lease.owner == PLAYER_MEDIA_OWNER_NONE)
        return true;
    return integration_submit_player(PLAYER_COMMAND_SOURCE_AIRPLAY_MIRROR,
                                     &lease, PLAYER_COMMAND_STOP, NULL,
                                     NULL, 0);
}

static void integration_mirror_status(AirPlayMirrorRuntimeStatus status,
                                      uint32_t generation, void *user_data)
{
    PlayerOwnershipLease lease;

    (void)generation;
    (void)user_data;
    switch (status)
    {
    case AIRPLAY_MIRROR_RUNTIME_PREPARING:
        integration_set_status("Preparing AirPlay mirroring");
        break;
    case AIRPLAY_MIRROR_RUNTIME_WAITING_KEYFRAME:
        integration_set_status("Waiting for AirPlay video");
        break;
    case AIRPLAY_MIRROR_RUNTIME_PLAYING:
        integration_set_status("AirPlay mirroring active");
        break;
    case AIRPLAY_MIRROR_RUNTIME_ERROR:
#if defined(NXCAST_EXCLUSIVE_MEDIA_RESOURCES) && \
    NXCAST_EXCLUSIVE_MEDIA_RESOURCES
        lease = integration_mirror_lease();
        if (protocol_coordinator_media_validate(&lease))
        {
            airplay_mirror_runtime_stop(lease.token, user_data);
            (void)integration_submit_player(
                PLAYER_COMMAND_SOURCE_AIRPLAY_MIRROR, &lease,
                PLAYER_COMMAND_STOP, NULL, NULL, 0);
            (void)protocol_coordinator_media_release(&lease);
        }
        mutexLock(&g_airplay.mutex);
        if (g_airplay.mirror_lease.generation == lease.generation)
            memset(&g_airplay.mirror_lease, 0,
                   sizeof(g_airplay.mirror_lease));
        mutexUnlock(&g_airplay.mutex);
#endif
        integration_set_status("AirPlay mirroring error");
        break;
    case AIRPLAY_MIRROR_RUNTIME_DISCONNECTED:
        lease = integration_mirror_lease();
        (void)integration_submit_player(PLAYER_COMMAND_SOURCE_AIRPLAY_MIRROR,
                                        &lease,
                                        PLAYER_COMMAND_RELEASE_LEASE, NULL,
                                        NULL, 0);
        mutexLock(&g_airplay.mutex);
        memset(&g_airplay.mirror_lease, 0, sizeof(g_airplay.mirror_lease));
        mutexUnlock(&g_airplay.mutex);
        integration_set_status("Ready for AirPlay video");
        break;
    case AIRPLAY_MIRROR_RUNTIME_IDLE:
    default:
        break;
    }
}

static bool integration_mirror_prepare(const AirPlayTransportSetup *setup,
                                       uint16_t *timing_port_out,
                                       void *user_data)
{
    if (!setup)
        return false;
    return airplay_mirror_runtime_transport_prepare(
        setup->session_id, setup->key, setup->iv, setup->peer_ipv4_address,
        setup->peer_timing_port, setup->uses_ntp_timing, timing_port_out,
        user_data);
}

static bool integration_mirror_open(uint64_t session_id, const uint8_t key[16],
                                    uint64_t connection_id,
                                    uint16_t *data_port_out, void *user_data)
{
    return airplay_mirror_runtime_open(session_id, key, connection_id,
                                       data_port_out, user_data);
}

static void integration_mirror_record(uint64_t session_id, void *user_data)
{
    ProtocolMediaTransaction transaction;
    AirPlayMirrorRuntimeStatus status;

    if (!protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_AIRPLAY_MIRROR,
                                           session_id, &transaction))
    {
        airplay_mirror_runtime_stop(session_id, user_data);
        integration_set_status("AirPlay mirroring error");
        return;
    }
    if (!protocol_coordinator_media_wait_resources(
            &transaction.lease, AIRPLAY_RESOURCE_TRANSITION_TIMEOUT_MS))
    {
        airplay_mirror_runtime_stop(session_id, user_data);
        integration_set_status("AirPlay resource handoff failed");
        protocol_coordinator_media_abort(&transaction);
        return;
    }
    mutexLock(&g_airplay.mutex);
    g_airplay.mirror_lease = transaction.lease;
    mutexUnlock(&g_airplay.mutex);
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu mirror record claim session=%llu previous=%s\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS(),
                       (unsigned long long)session_id,
                       player_media_owner_name(transaction.previous.owner));

    airplay_mirror_runtime_record(session_id, user_data);
    status = airplay_mirror_runtime_status(user_data, NULL);
    if (status != AIRPLAY_MIRROR_RUNTIME_WAITING_KEYFRAME)
    {
        mutexLock(&g_airplay.mutex);
        if (g_airplay.mirror_lease.generation ==
            transaction.lease.generation)
            memset(&g_airplay.mirror_lease, 0, sizeof(g_airplay.mirror_lease));
        mutexUnlock(&g_airplay.mutex);
        integration_set_status("AirPlay mirroring error");
        protocol_coordinator_media_abort(&transaction);
        return;
    }
    protocol_coordinator_media_end(&transaction);
}

static void integration_pin_display(const char pin[5], void *user_data)
{
    (void)user_data;
    mutexLock(&g_airplay.mutex);
    memcpy(g_airplay.pin, pin, sizeof(g_airplay.pin));
    g_airplay.pin_visible = true;
    snprintf(g_airplay.status, sizeof(g_airplay.status), "Enter the PIN on your iPhone");
    mutexUnlock(&g_airplay.mutex);
}

static void integration_pin_dismiss(void *user_data)
{
    (void)user_data;
    mutexLock(&g_airplay.mutex);
    memset(g_airplay.pin, 0, sizeof(g_airplay.pin));
    g_airplay.pin_visible = false;
    snprintf(g_airplay.status, sizeof(g_airplay.status), "Ready for AirPlay video");
    mutexUnlock(&g_airplay.mutex);
}

static bool integration_start_sync(void)
{
    AirPlayMirrorRuntimeConfig mirror_config = {0};
    AirPlayRemoteVideoOps remote_ops = {0};
    AirPlayReceiverConfig receiver_config = {0};
    const char *failure_stage = "ed25519";

    integration_ensure_mutex();
    mutexLock(&g_airplay.mutex);
    if (g_airplay.running)
    {
        mutexUnlock(&g_airplay.mutex);
        return true;
    }
    mutexUnlock(&g_airplay.mutex);

    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu integration stage=ed25519 begin\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS());
    if (!airplay_crypto_ed25519_available())
        goto failure;
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu integration stage=ed25519 done\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS());
    if (integration_stop_requested())
    {
        failure_stage = "cancelled";
        goto failure;
    }

    failure_stage = "mirror-runtime";
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu integration stage=%s begin\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS(), failure_stage);
    mirror_config.stream_capacity = AIRPLAY_STREAM_CAPACITY;
    mirror_config.player.bind_stream = integration_mirror_bind;
    mirror_config.player.set_uri = integration_mirror_set_uri;
    mirror_config.player.play = integration_mirror_play;
    mirror_config.player.stop = integration_mirror_stop_player;
    mirror_config.player.status_changed = integration_mirror_status;
    if (!airplay_mirror_runtime_create(&mirror_config,
                                       &g_airplay.mirror_runtime))
        goto failure;
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu integration stage=%s done\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS(), failure_stage);
    if (integration_stop_requested())
    {
        failure_stage = "cancelled";
        goto failure;
    }

    failure_stage = "remote-video";
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu integration stage=%s begin\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS(), failure_stage);
    remote_ops.claim_owner = integration_remote_claim;
    remote_ops.release_owner = integration_remote_release;
    remote_ops.load = integration_remote_load;
    remote_ops.play = integration_remote_play;
    remote_ops.pause = integration_remote_pause;
    remote_ops.stop = integration_remote_stop;
    remote_ops.seek_ms = integration_remote_seek;
    remote_ops.snapshot = integration_remote_snapshot;
    if (!airplay_remote_video_create(&remote_ops, &g_airplay.remote_video))
        goto failure;
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu integration stage=%s done\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS(), failure_stage);
    if (integration_stop_requested())
    {
        failure_stage = "cancelled";
        goto failure;
    }

    failure_stage = "receiver";
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu integration stage=%s begin\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS(), failure_stage);
    receiver_config.friendly_name = "NX-Cast";
    receiver_config.storage_directory = AIRPLAY_STORAGE_DIRECTORY;
    receiver_config.control_port = AIRPLAY_CONTROL_PORT;
    receiver_config.features = AIRPLAY_MDNS_FEATURES_MIRROR_COMPAT;
    receiver_config.enable_discovery = NXCAST_AIRPLAY_DISCOVERY_ENABLED != 0;
    receiver_config.pin_display_callback = integration_pin_display;
    receiver_config.pin_dismiss_callback = integration_pin_dismiss;
    receiver_config.transport_prepare_callback = integration_mirror_prepare;
    receiver_config.mirror_open_callback = integration_mirror_open;
    receiver_config.audio_open_callback = airplay_mirror_runtime_audio_open;
    receiver_config.mirror_record_callback = integration_mirror_record;
    receiver_config.mirror_stop_callback = airplay_mirror_runtime_stop;
    receiver_config.remote_video = g_airplay.remote_video;
    receiver_config.media_user_data = g_airplay.mirror_runtime;
    if (!airplay_receiver_start(&receiver_config))
        goto failure;
    if (integration_stop_requested())
    {
        failure_stage = "cancelled";
        goto failure;
    }

    mutexLock(&g_airplay.mutex);
    g_airplay.running = true;
    snprintf(g_airplay.status, sizeof(g_airplay.status),
             "Ready for AirPlay video");
    mutexUnlock(&g_airplay.mutex);
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu integration stage=%s done\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS(), failure_stage);
    log_info("[airplay] integration started port=%u discovery=%d\n",
             airplay_receiver_port(),
             receiver_config.enable_discovery ? 1 : 0);
    return true;

failure:
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu integration failed stage=%s\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS(), failure_stage);
    airplay_receiver_stop();
    airplay_remote_video_destroy(g_airplay.remote_video);
    g_airplay.remote_video = NULL;
    airplay_mirror_runtime_destroy(g_airplay.mirror_runtime);
    g_airplay.mirror_runtime = NULL;
    mutexLock(&g_airplay.mutex);
    if (strcmp(failure_stage, "cancelled") == 0)
        snprintf(g_airplay.status, sizeof(g_airplay.status), "AirPlay stopped");
    else
        snprintf(g_airplay.status, sizeof(g_airplay.status),
                 "AirPlay unavailable (%s)", failure_stage);
    mutexUnlock(&g_airplay.mutex);
    if (strcmp(failure_stage, "cancelled") == 0)
        log_info("[airplay] integration start cancelled\n");
    else if (strcmp(failure_stage, "ed25519") == 0)
        log_error("[airplay] integration start failed stage=ed25519; "
                  "install switch-libsodium and rebuild\n");
    else
        log_error("[airplay] integration start failed stage=%s; "
                  "DLNA/IPTV remain active\n", failure_stage);
    return false;
}

bool airplay_integration_start(void)
{
    bool started;

    integration_ensure_mutex();
    mutexLock(&g_airplay.mutex);
    if (g_airplay.running)
    {
        mutexUnlock(&g_airplay.mutex);
        return true;
    }
    if (g_airplay.starting)
    {
        mutexUnlock(&g_airplay.mutex);
        return false;
    }
    g_airplay.starting = true;
    g_airplay.stop_requested = false;
    snprintf(g_airplay.status, sizeof(g_airplay.status), "Starting AirPlay");
    mutexUnlock(&g_airplay.mutex);

    started = integration_start_sync();
    mutexLock(&g_airplay.mutex);
    g_airplay.starting = false;
    mutexUnlock(&g_airplay.mutex);
    return started;
}

void airplay_integration_request_stop(void)
{
    integration_ensure_mutex();
    mutexLock(&g_airplay.mutex);
    g_airplay.stop_requested = true;
    mutexUnlock(&g_airplay.mutex);
}

void airplay_integration_stop_active_media(void)
{
    PlayerOwnershipLease remote;
    PlayerOwnershipLease mirror;
    AirPlayMirrorRuntime *mirror_runtime;

    integration_ensure_mutex();
    mutexLock(&g_airplay.mutex);
    remote = g_airplay.remote_lease;
    mirror = g_airplay.mirror_lease;
    mirror_runtime = g_airplay.mirror_runtime;
    mutexUnlock(&g_airplay.mutex);

    if (mirror.owner == PLAYER_MEDIA_OWNER_AIRPLAY_MIRROR &&
        protocol_coordinator_media_validate(&mirror) && mirror_runtime)
        airplay_mirror_runtime_stop(mirror.token, mirror_runtime);
    if (remote.owner == PLAYER_MEDIA_OWNER_AIRPLAY_VIDEO &&
        protocol_coordinator_media_validate(&remote))
        integration_remote_release(remote.token, NULL);
}

void airplay_integration_stop(void)
{
    PlayerOwnershipLease remote;
    PlayerOwnershipLease mirror;

    airplay_integration_request_stop();
    airplay_receiver_stop();
    airplay_remote_video_destroy(g_airplay.remote_video);
    g_airplay.remote_video = NULL;
    airplay_mirror_runtime_destroy(g_airplay.mirror_runtime);
    g_airplay.mirror_runtime = NULL;
    remote = integration_remote_lease();
    mirror = integration_mirror_lease();
    if (protocol_coordinator_media_validate(&remote))
    {
        (void)integration_submit_player(PLAYER_COMMAND_SOURCE_AIRPLAY_VIDEO,
                                        &remote, PLAYER_COMMAND_STOP, NULL,
                                        NULL, 0);
        (void)integration_submit_player(PLAYER_COMMAND_SOURCE_AIRPLAY_VIDEO,
                                        &remote,
                                        PLAYER_COMMAND_RELEASE_LEASE, NULL,
                                        NULL, 0);
    }
    if (protocol_coordinator_media_validate(&mirror))
    {
        (void)integration_submit_player(PLAYER_COMMAND_SOURCE_AIRPLAY_MIRROR,
                                        &mirror, PLAYER_COMMAND_STOP, NULL,
                                        NULL, 0);
        (void)player_submit_airplay_stream_bridge(NULL, &mirror);
        (void)integration_submit_player(PLAYER_COMMAND_SOURCE_AIRPLAY_MIRROR,
                                        &mirror,
                                        PLAYER_COMMAND_RELEASE_LEASE, NULL,
                                        NULL, 0);
    }
    mutexLock(&g_airplay.mutex);
    g_airplay.running = false;
    g_airplay.starting = false;
    g_airplay.stop_requested = false;
    g_airplay.pin_visible = false;
    memset(g_airplay.pin, 0, sizeof(g_airplay.pin));
    memset(&g_airplay.remote_lease, 0, sizeof(g_airplay.remote_lease));
    memset(&g_airplay.mirror_lease, 0, sizeof(g_airplay.mirror_lease));
    snprintf(g_airplay.status, sizeof(g_airplay.status), "AirPlay stopped");
    mutexUnlock(&g_airplay.mutex);
}

bool airplay_integration_get_status(AirPlayIntegrationStatus *status_out)
{
    if (!status_out)
        return false;
    integration_ensure_mutex();
    if (!mutexTryLock(&g_airplay.mutex))
        return false;
    status_out->running = g_airplay.running;
    status_out->starting = g_airplay.starting;
    status_out->pin_visible = g_airplay.pin_visible;
    memcpy(status_out->pin, g_airplay.pin, sizeof(status_out->pin));
    snprintf(status_out->status, sizeof(status_out->status), "%s",
             g_airplay.status);
    mutexUnlock(&g_airplay.mutex);
    return true;
}
