#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "player.h"

typedef enum
{
    PLAYER_PLATFORM_VIEW_LOG = 0,
    PLAYER_PLATFORM_VIEW_VIDEO
} PlayerPlatformViewMode;

typedef enum
{
    PLAYER_PLATFORM_RENDER_OWNER_MAIN_THREAD = 0,
    PLAYER_PLATFORM_RENDER_OWNER_BACKEND
} PlayerPlatformRenderOwner;

typedef struct
{
    bool initialized;
    bool has_source;
    bool session_active;
    PlayerState player_state;
    PlayerPlatformViewMode active_view;
    PlayerPlatformViewMode desired_view;
    PlayerPlatformRenderOwner render_owner;
    uint64_t frame_counter;
    char source_uri[PLAYER_SOURCE_URI_MAX];
    char source_hint[PLAYER_SOURCE_FORMAT_HINT_MAX];
} PlayerPlatformVideoStatus;

bool player_platform_video_init(void);
void player_platform_video_deinit(void);
void player_platform_video_sync_snapshot(const PlayerSnapshot *snapshot);
void player_platform_video_begin_frame(void);
PlayerPlatformViewMode player_platform_video_get_active_view(void);
PlayerPlatformRenderOwner player_platform_video_get_render_owner(void);
bool player_platform_video_get_status(PlayerPlatformVideoStatus *out);
const char *player_platform_view_mode_name(PlayerPlatformViewMode mode);
const char *player_platform_render_owner_name(PlayerPlatformRenderOwner owner);
void player_platform_video_render_placeholder(void);
