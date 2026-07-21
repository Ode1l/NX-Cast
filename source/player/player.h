#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <deko3d.h>

#include "player/core/ownership.h"
#include "types.h"

typedef enum
{
    PLAYER_BACKEND_AUTO = 0,
    PLAYER_BACKEND_MOCK,
    PLAYER_BACKEND_LIBMPV
} PlayerBackendType;

typedef struct
{
    DkDevice device;
} PlayerVideoDk3dInit;

typedef struct
{
    DkImage *image;
    DkFence *ready_fence;
    DkFence *done_fence;
    int width;
    int height;
    DkImageFormat format;
} PlayerVideoDk3dFrame;

typedef enum
{
    PLAYER_COMMAND_OPEN = 1,
    PLAYER_COMMAND_PLAY,
    PLAYER_COMMAND_PAUSE,
    PLAYER_COMMAND_STOP,
    PLAYER_COMMAND_STOP_ANY,
    PLAYER_COMMAND_SEEK_TARGET,
    PLAYER_COMMAND_SEEK_MS,
    PLAYER_COMMAND_SET_VOLUME,
    PLAYER_COMMAND_SET_MUTE,
    PLAYER_COMMAND_SHOW_OSD,
    PLAYER_COMMAND_RELEASE_LEASE
} PlayerCommandKind;

typedef enum
{
    PLAYER_COMMAND_SOURCE_APP = 0,
    PLAYER_COMMAND_SOURCE_UI,
    PLAYER_COMMAND_SOURCE_DLNA,
    PLAYER_COMMAND_SOURCE_IPTV,
    PLAYER_COMMAND_SOURCE_AIRPLAY_VIDEO,
    PLAYER_COMMAND_SOURCE_AIRPLAY_MIRROR
} PlayerCommandSource;

typedef struct
{
    PlayerCommandKind kind;
    PlayerCommandSource source;
    PlayerOwnershipLease lease;
    const char *uri;
    const char *metadata;
    const char *text;
    int value;
    bool flag;
} PlayerCommandRequest;

typedef enum
{
    PLAYER_COMMAND_STATUS_ACCEPTED = 0,
    PLAYER_COMMAND_STATUS_EXECUTED,
    PLAYER_COMMAND_STATUS_EXECUTION_FAILED,
    PLAYER_COMMAND_STATUS_INVALID,
    PLAYER_COMMAND_STATUS_NOT_RUNNING,
    PLAYER_COMMAND_STATUS_QUEUE_FULL,
    PLAYER_COMMAND_STATUS_NO_MEMORY,
    PLAYER_COMMAND_STATUS_TIMEOUT,
    PLAYER_COMMAND_STATUS_SHUTTING_DOWN,
    PLAYER_COMMAND_STATUS_STALE
} PlayerCommandStatus;

typedef struct
{
    bool running;
    bool stopping;
    bool accepting;
    bool initializing;
    bool backend_ready;
    bool initialization_failed;
    bool dispatch_enabled;
    size_t queue_depth;
    size_t queue_high_watermark;
    uint64_t submitted;
    uint64_t executed;
    uint64_t execution_failed;
    uint64_t rejected_full;
    uint64_t rejected_stopping;
    uint64_t rejected_stale;
    uint64_t coalesced;
    uint64_t timed_out;
    uint64_t current_command_id;
    int current_command_kind;
    int current_command_producer;
    uint64_t current_session_token;
    uint32_t current_generation;
    uint64_t last_completed_command_id;
    uint64_t current_command_age_ms;
    uint64_t heartbeat_age_ms;
} PlayerRuntimeHealth;

bool player_set_backend(PlayerBackendType backend);
PlayerBackendType player_get_backend(void);
const char *player_get_backend_name(void);

bool player_init(void);
bool player_activate(void);
void player_deinit(void);

void player_set_event_callback(PlayerEventCallback callback, void *user);

PlayerCommandStatus player_submit_command_async(
    const PlayerCommandRequest *request);
PlayerCommandStatus player_submit_command_wait(
    const PlayerCommandRequest *request, uint32_t timeout_ms);
bool player_command_status_succeeded(PlayerCommandStatus status);
const char *player_command_status_name(PlayerCommandStatus status);
bool player_get_runtime_health(PlayerRuntimeHealth *health_out);
void player_quiesce(void);
bool player_wait_idle(uint32_t timeout_ms);

typedef struct AirPlayStreamBridge AirPlayStreamBridge;
PlayerCommandStatus player_submit_airplay_stream_bridge(
    AirPlayStreamBridge *bridge, const PlayerOwnershipLease *lease);

bool player_set_media(const PlayerMedia *media);
bool player_set_uri(const char *uri, const char *metadata);
bool player_play(void);
bool player_pause(void);
bool player_stop(void);
bool player_seek_target(const char *target);
bool player_seek_ms(int position_ms);
bool player_set_volume(int volume_0_100);
bool player_set_mute(bool mute);
bool player_show_osd(const char *text, int duration_ms);
bool player_video_supported(void);
bool player_video_attach_gl(void *(*get_proc_address)(void *ctx, const char *name), void *get_proc_address_ctx);
bool player_video_attach_sw(void);
bool player_video_attach_dk3d(const PlayerVideoDk3dInit *init);
void player_video_detach(void);
bool player_video_render_gl(int fbo, int width, int height, bool flip_y);
bool player_video_render_sw(void *pixels, int width, int height, size_t stride);
bool player_video_render_dk3d(const PlayerVideoDk3dFrame *frame);

int player_get_position_ms(void);
int player_get_duration_ms(void);
int player_get_volume(void);
bool player_get_mute(void);
bool player_is_seekable(void);
PlayerState player_get_state(void);
bool player_get_current_media(PlayerMedia *out);
bool player_get_snapshot(PlayerSnapshot *out);
