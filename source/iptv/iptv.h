#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define IPTV_ROOT_DIR "sdmc:/switch/NX-Cast/iptv"
#define IPTV_PREINSTALLED_SOURCES_FILE IPTV_ROOT_DIR "/sources.txt"
#define IPTV_MAX_CHANNELS 1024
#define IPTV_MAX_SOURCES 32
#define IPTV_MAX_GROUPS 64
#define IPTV_MAX_RECENT 32
#define IPTV_NAME_MAX 128
#define IPTV_GROUP_MAX 96
#define IPTV_SOURCE_MAX 128
#define IPTV_TVG_ID_MAX 128
#define IPTV_LOGO_URL_MAX 512
#define IPTV_PATH_MAX 512
#define IPTV_URL_MAX 1024
#define IPTV_STATUS_MAX 192
#define IPTV_SEARCH_MAX 96
#define IPTV_EPG_TITLE_MAX 160

typedef enum
{
    IPTV_PLAYLIST_UNKNOWN = 0,
    IPTV_PLAYLIST_CHANNEL_LIST,
    IPTV_PLAYLIST_HLS_STREAM
} IptvPlaylistKind;

typedef struct
{
    uint32_t id;
    uint32_t source_id;
    bool favorite;
    bool recent;
    bool logo_cached;
    char name[IPTV_NAME_MAX];
    char group[IPTV_GROUP_MAX];
    char source[IPTV_SOURCE_MAX];
    char tvg_id[IPTV_TVG_ID_MAX];
    char logo_url[IPTV_LOGO_URL_MAX];
    char logo_path[IPTV_PATH_MAX];
    char url[IPTV_URL_MAX];
    char now_title[IPTV_EPG_TITLE_MAX];
    char next_title[IPTV_EPG_TITLE_MAX];
    time_t now_start;
    time_t now_stop;
    time_t next_start;
    time_t next_stop;
} IptvChannel;

typedef struct
{
    uint32_t id;
    bool local;
    bool enabled;
    bool refreshing;
    bool cache_ready;
    int channel_count;
    time_t refreshed_at;
    char name[IPTV_SOURCE_MAX];
    char url[IPTV_URL_MAX];
    char epg_url[IPTV_URL_MAX];
    char cache_path[IPTV_PATH_MAX];
    char status[IPTV_STATUS_MAX];
} IptvSource;

typedef struct
{
    bool initialized;
    bool loaded;
    bool refreshing;
    bool background_suspended;
    bool background_worker_busy;
    int source_count;
    int hls_stream_count;
    int channel_count;
    int visible_count;
    int favorite_count;
    int recent_count;
    int group_count;
    int logo_cached_count;
    int epg_channel_count;
    int selected_index;
    int source_selected_index;
    char active_filter[IPTV_GROUP_MAX];
    char search[IPTV_SEARCH_MAX];
    char status[IPTV_STATUS_MAX];
    char last_name[IPTV_NAME_MAX];
    char last_url[IPTV_URL_MAX];
} IptvState;

bool iptv_init(void);
void iptv_deinit(void);
bool iptv_set_background_network_suspended(bool suspended);
bool iptv_reload(void);
IptvPlaylistKind iptv_classify_playlist_file(const char *path);
const char *iptv_playlist_kind_name(IptvPlaylistKind kind);

bool iptv_get_state(IptvState *out);
int iptv_get_channel_count(void);
int iptv_get_selected_index(void);
void iptv_set_selected_index(int index);
void iptv_select_delta(int delta);
bool iptv_get_channel(int index, IptvChannel *out);

int iptv_get_source_count(void);
int iptv_get_source_selected_index(void);
void iptv_set_source_selected_index(int index);
void iptv_select_source_delta(int delta);
bool iptv_get_source(int index, IptvSource *out);
bool iptv_add_source_url(const char *url);
bool iptv_url_looks_like_playlist(const char *url);
bool iptv_prompt_add_source(void);
bool iptv_prompt_set_source_epg(void);
bool iptv_remove_selected_source(void);
bool iptv_refresh_selected_source_async(void);
bool iptv_refresh_all_async(void);

void iptv_cycle_filter(int delta);
bool iptv_prompt_search(void);
void iptv_clear_search(void);
bool iptv_toggle_selected_favorite(void);

bool iptv_play_channel(int index);
bool iptv_play_url(const char *url);
bool iptv_prompt_url(char *out_url, size_t out_url_size);
void iptv_set_status(const char *status);
