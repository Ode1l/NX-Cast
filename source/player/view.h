#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "types.h"

#define PLAYER_HOME_ERROR_MAX 512
#define PLAYER_HOME_IPTV_STATUS_MAX 192
#define PLAYER_HOME_IPTV_NAME_MAX 128
#define PLAYER_HOME_IPTV_FILTER_MAX 96

typedef enum
{
    PLAYER_VIEW_HOME = 0,
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

typedef struct
{
    bool storage_ready;
    bool network_ready;
    bool dlna_running;
    bool video_ready;
    bool playback_active;
    PlayerState playback_state;
    bool has_error;
    char error_line[PLAYER_HOME_ERROR_MAX];
    bool iptv_ready;
    bool iptv_panel_open;
    bool iptv_sources_open;
    bool iptv_refreshing;
    int iptv_source_count;
    int iptv_channel_count;
    int iptv_visible_count;
    int iptv_favorite_count;
    int iptv_recent_count;
    int iptv_logo_cached_count;
    int iptv_epg_channel_count;
    int iptv_selected_index;
    int iptv_source_selected_index;
    char iptv_active_filter[PLAYER_HOME_IPTV_FILTER_MAX];
    char iptv_search[PLAYER_HOME_IPTV_FILTER_MAX];
    char iptv_status[PLAYER_HOME_IPTV_STATUS_MAX];
    char iptv_last_name[PLAYER_HOME_IPTV_NAME_MAX];
} PlayerHomeViewState;

void player_view_status_clear(PlayerViewStatus *status);
bool player_view_status_copy(PlayerViewStatus *out, const PlayerViewStatus *status);
bool player_view_init(void);
void player_view_deinit(void);
bool player_view_prepare_video(void);
void player_view_set_home_state(const PlayerHomeViewState *state);
void player_view_sync(const PlayerSnapshot *snapshot);
bool player_view_show_home(void);
bool player_view_show_video(void);
void player_view_begin_frame(void);
PlayerViewMode player_view_get_mode(void);
PlayerRenderOwner player_view_get_owner(void);
bool player_view_get_status(PlayerViewStatus *out);
bool player_view_has_foreground_renderer(void);
const char *player_view_mode_name(PlayerViewMode mode);
const char *player_render_owner_name(PlayerRenderOwner owner);
bool player_view_render_frame(void);
