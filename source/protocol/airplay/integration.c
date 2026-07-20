#include "integration.h"

#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "log/log.h"
#include "player/backend/libmpv_airplay.h"
#include "player/renderer.h"
#include "protocol/airplay/discovery/mdns.h"
#include "protocol/airplay/media/mirror_runtime.h"
#include "protocol/airplay/media/remote_video.h"
#include "protocol/airplay/receiver.h"
#include "protocol/airplay/security/crypto.h"

#define AIRPLAY_CONTROL_PORT 7000u
#define AIRPLAY_STREAM_CAPACITY (4u * 1024u * 1024u)
#define AIRPLAY_STORAGE_DIRECTORY "sdmc:/switch/NX-Cast/airplay"

typedef struct
{
    Mutex mutex;
    bool mutex_ready;
    bool running;
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

static void integration_stop_previous(const PlayerOwnershipLease *previous,
                                      const PlayerOwnershipLease *current)
{
    if (!previous || !current || previous->owner == PLAYER_MEDIA_OWNER_NONE ||
        (previous->owner == current->owner && previous->token == current->token))
        return;
    if (renderer_get_state() != PLAYER_STATE_IDLE &&
        renderer_get_state() != PLAYER_STATE_STOPPED)
        (void)renderer_stop();
}

static bool integration_remote_claim(uint64_t session_id, void *user_data)
{
    PlayerOwnershipLease lease = {0};
    PlayerOwnershipLease previous = {0};

    (void)user_data;
    if (!player_ownership_claim(PLAYER_MEDIA_OWNER_AIRPLAY_VIDEO, session_id,
                                &lease, &previous))
        return false;
    integration_stop_previous(&previous, &lease);
    if (previous.owner == PLAYER_MEDIA_OWNER_AIRPLAY_VIDEO &&
        previous.token == session_id &&
        renderer_get_state() != PLAYER_STATE_IDLE &&
        renderer_get_state() != PLAYER_STATE_STOPPED)
        (void)renderer_stop();
    mutexLock(&g_airplay.mutex);
    g_airplay.remote_lease = lease;
    mutexUnlock(&g_airplay.mutex);
    integration_set_status("AirPlay video connected");
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
        (void)player_ownership_release(&lease);
        mutexLock(&g_airplay.mutex);
        memset(&g_airplay.remote_lease, 0, sizeof(g_airplay.remote_lease));
        mutexUnlock(&g_airplay.mutex);
        integration_set_status("Ready for AirPlay video");
    }
}

static bool integration_remote_active(void)
{
    PlayerOwnershipLease lease = integration_remote_lease();
    return player_ownership_validate(&lease);
}

static bool integration_remote_load(const char *url, const char *metadata,
                                    void *user_data)
{
    (void)user_data;
    if (!integration_remote_active())
        return false;
    integration_set_status("Loading AirPlay video");
    return renderer_set_uri(url, metadata ? metadata : "AirPlay Video");
}

static bool integration_remote_play(void *user_data)
{
    (void)user_data;
    return integration_remote_active() && renderer_play();
}

static bool integration_remote_pause(void *user_data)
{
    (void)user_data;
    return integration_remote_active() && renderer_pause();
}

static bool integration_remote_stop(void *user_data)
{
    (void)user_data;
    if (!integration_remote_active())
        return true;
    return renderer_stop();
}

static bool integration_remote_seek(int position_ms, void *user_data)
{
    (void)user_data;
    return integration_remote_active() && renderer_seek_ms(position_ms);
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
    RendererSnapshot snapshot = {0};

    (void)user_data;
    if (!snapshot_out || !integration_remote_active() ||
        !renderer_get_snapshot(&snapshot))
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

    (void)user_data;
    lease = integration_mirror_lease();
    if (bridge && !player_ownership_validate(&lease))
        return false;
    return player_libmpv_set_airplay_stream_bridge(bridge);
}

static bool integration_mirror_active(void)
{
    PlayerOwnershipLease lease = integration_mirror_lease();
    return player_ownership_validate(&lease);
}

static bool integration_mirror_set_uri(const char *uri, const char *metadata,
                                       void *user_data)
{
    (void)user_data;
    return integration_mirror_active() && renderer_set_uri(uri, metadata);
}

static bool integration_mirror_play(void *user_data)
{
    (void)user_data;
    return integration_mirror_active() && renderer_play();
}

static bool integration_mirror_stop_player(void *user_data)
{
    (void)user_data;
    if (!integration_mirror_active())
        return true;
    return renderer_stop();
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
        integration_set_status("AirPlay mirroring error");
        break;
    case AIRPLAY_MIRROR_RUNTIME_DISCONNECTED:
        lease = integration_mirror_lease();
        (void)player_ownership_release(&lease);
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

static bool integration_mirror_prepare(uint64_t session_id,
                                       const uint8_t key[16],
                                       const uint8_t iv[16],
                                       uint16_t *timing_port_out,
                                       void *user_data)
{
    return airplay_mirror_runtime_transport_prepare(session_id, key, iv,
                                                    timing_port_out, user_data);
}

static bool integration_mirror_open(uint64_t session_id, const uint8_t key[16],
                                    uint64_t connection_id,
                                    uint16_t *data_port_out, void *user_data)
{
    PlayerOwnershipLease lease = {0};
    PlayerOwnershipLease previous = {0};

    if (!player_ownership_claim(PLAYER_MEDIA_OWNER_AIRPLAY_MIRROR, session_id,
                                &lease, &previous))
        return false;
    integration_stop_previous(&previous, &lease);
    mutexLock(&g_airplay.mutex);
    g_airplay.mirror_lease = lease;
    mutexUnlock(&g_airplay.mutex);
    if (airplay_mirror_runtime_open(session_id, key, connection_id,
                                    data_port_out, user_data))
        return true;
    (void)player_ownership_release(&lease);
    mutexLock(&g_airplay.mutex);
    memset(&g_airplay.mirror_lease, 0, sizeof(g_airplay.mirror_lease));
    mutexUnlock(&g_airplay.mutex);
    return false;
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

bool airplay_integration_start(void)
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

    if (!airplay_crypto_ed25519_available())
        goto failure;

    failure_stage = "mirror-runtime";
    mirror_config.stream_capacity = AIRPLAY_STREAM_CAPACITY;
    mirror_config.player.bind_stream = integration_mirror_bind;
    mirror_config.player.set_uri = integration_mirror_set_uri;
    mirror_config.player.play = integration_mirror_play;
    mirror_config.player.stop = integration_mirror_stop_player;
    mirror_config.player.status_changed = integration_mirror_status;
    if (!airplay_mirror_runtime_create(&mirror_config,
                                       &g_airplay.mirror_runtime))
        goto failure;

    failure_stage = "remote-video";
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

    failure_stage = "receiver";
    receiver_config.friendly_name = "NX-Cast";
    receiver_config.storage_directory = AIRPLAY_STORAGE_DIRECTORY;
    receiver_config.control_port = AIRPLAY_CONTROL_PORT;
    receiver_config.features = AIRPLAY_MDNS_FEATURE_VIDEO |
                               AIRPLAY_MDNS_FEATURE_HLS |
                               AIRPLAY_MDNS_FEATURE_SCREEN_MIRROR |
                               AIRPLAY_MDNS_FEATURE_SCREEN_ROTATE;
    receiver_config.enable_discovery = true;
    receiver_config.pin_display_callback = integration_pin_display;
    receiver_config.pin_dismiss_callback = integration_pin_dismiss;
    receiver_config.transport_prepare_callback = integration_mirror_prepare;
    receiver_config.mirror_open_callback = integration_mirror_open;
    receiver_config.audio_open_callback = airplay_mirror_runtime_audio_open;
    receiver_config.mirror_record_callback = airplay_mirror_runtime_record;
    receiver_config.mirror_stop_callback = airplay_mirror_runtime_stop;
    receiver_config.remote_video = g_airplay.remote_video;
    receiver_config.media_user_data = g_airplay.mirror_runtime;
    if (!airplay_receiver_start(&receiver_config))
        goto failure;

    mutexLock(&g_airplay.mutex);
    g_airplay.running = true;
    snprintf(g_airplay.status, sizeof(g_airplay.status),
             "Ready for AirPlay video");
    mutexUnlock(&g_airplay.mutex);
    log_info("[airplay] integration started port=%u mirror_advertised=1\n",
             airplay_receiver_port());
    return true;

failure:
    airplay_receiver_stop();
    airplay_remote_video_destroy(g_airplay.remote_video);
    g_airplay.remote_video = NULL;
    airplay_mirror_runtime_destroy(g_airplay.mirror_runtime);
    g_airplay.mirror_runtime = NULL;
    mutexLock(&g_airplay.mutex);
    snprintf(g_airplay.status, sizeof(g_airplay.status),
             "AirPlay unavailable (%s)", failure_stage);
    mutexUnlock(&g_airplay.mutex);
    if (strcmp(failure_stage, "ed25519") == 0)
        log_error("[airplay] integration start failed stage=ed25519; "
                  "install switch-libsodium and rebuild\n");
    else
        log_error("[airplay] integration start failed stage=%s; "
                  "DLNA/IPTV remain active\n", failure_stage);
    return false;
}

void airplay_integration_stop(void)
{
    PlayerOwnershipLease remote;
    PlayerOwnershipLease mirror;

    integration_ensure_mutex();
    airplay_receiver_stop();
    airplay_remote_video_destroy(g_airplay.remote_video);
    g_airplay.remote_video = NULL;
    airplay_mirror_runtime_destroy(g_airplay.mirror_runtime);
    g_airplay.mirror_runtime = NULL;
    remote = integration_remote_lease();
    mirror = integration_mirror_lease();
    (void)player_ownership_release(&remote);
    (void)player_ownership_release(&mirror);
    (void)player_libmpv_set_airplay_stream_bridge(NULL);
    mutexLock(&g_airplay.mutex);
    g_airplay.running = false;
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
    mutexLock(&g_airplay.mutex);
    status_out->running = g_airplay.running;
    status_out->pin_visible = g_airplay.pin_visible;
    memcpy(status_out->pin, g_airplay.pin, sizeof(status_out->pin));
    snprintf(status_out->status, sizeof(status_out->status), "%s",
             g_airplay.status);
    mutexUnlock(&g_airplay.mutex);
    return true;
}
