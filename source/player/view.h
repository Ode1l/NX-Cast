#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "types.h"

typedef enum
{
    PLAYER_VIEW_LOG = 0,
    PLAYER_VIEW_VIDEO
} PlayerViewMode;

typedef enum
{
    PLAYER_RENDER_OWNER_MAIN_THREAD = 0,
    PLAYER_RENDER_OWNER_BACKEND
} PlayerRenderOwner;

typedef struct
{
    bool initialized;
    bool has_media;
    bool session_active;
    bool render_api_connected;
    bool foreground_video_active;
    PlayerState player_state;
    PlayerViewMode active_view;
    PlayerViewMode desired_view;
    PlayerRenderOwner render_owner;
    uint64_t frame_counter;
    uint64_t frames_presented;
    uint32_t display_width;
    uint32_t display_height;
    char *media_uri;
} PlayerViewStatus;

void player_view_status_clear(PlayerViewStatus *status);
bool player_view_status_copy(PlayerViewStatus *out, const PlayerViewStatus *status);
bool player_view_init(void);
void player_view_deinit(void);
bool player_view_prepare_video(void);
void player_view_sync(const PlayerSnapshot *snapshot);
void player_view_begin_frame(void);
PlayerViewMode player_view_get_mode(void);
PlayerRenderOwner player_view_get_owner(void);
bool player_view_get_status(PlayerViewStatus *out);
const char *player_view_mode_name(PlayerViewMode mode);
const char *player_render_owner_name(PlayerRenderOwner owner);
bool player_view_render_frame(void);
