#pragma once

#include <stdbool.h>

#define PLAYER_SOURCE_URI_MAX 1024
#define PLAYER_SOURCE_METADATA_MAX 2048
#define PLAYER_SOURCE_USER_AGENT_MAX 192
#define PLAYER_SOURCE_REFERRER_MAX 192
#define PLAYER_SOURCE_ORIGIN_MAX 128
#define PLAYER_SOURCE_HEADER_FIELDS_MAX 512
#define PLAYER_SOURCE_PROBE_INFO_MAX 32
#define PLAYER_SOURCE_MIME_TYPE_MAX 96
#define PLAYER_SOURCE_PROTOCOL_INFO_MAX 192
#define PLAYER_SOURCE_FORMAT_HINT_MAX 48
#define PLAYER_SOURCE_MPV_LOAD_OPTIONS_MAX 320

typedef enum
{
    PLAYER_SOURCE_PROFILE_UNKNOWN = 0,
    PLAYER_SOURCE_PROFILE_DIRECT_HTTP_FILE,
    PLAYER_SOURCE_PROFILE_GENERIC_HLS,
    PLAYER_SOURCE_PROFILE_HEADER_SENSITIVE_HTTP,
    PLAYER_SOURCE_PROFILE_SIGNED_EPHEMERAL_URL,
    PLAYER_SOURCE_PROFILE_VENDOR_SENSITIVE_URL
} PlayerSourceProfile;

typedef enum
{
    PLAYER_SOURCE_FORMAT_UNKNOWN = 0,
    PLAYER_SOURCE_FORMAT_MP4,
    PLAYER_SOURCE_FORMAT_FLV,
    PLAYER_SOURCE_FORMAT_HLS,
    PLAYER_SOURCE_FORMAT_DASH,
    PLAYER_SOURCE_FORMAT_MPEG_TS
} PlayerSourceFormat;

typedef struct
{
    bool is_http;
    bool is_https;
    bool is_hls;
    bool is_signed;
    bool is_bilibili;
    bool is_dash;
    bool is_flv;
    bool is_mp4;
    bool is_mpeg_ts;
    bool likely_segmented;
    bool likely_video_only;
} PlayerSourceFlags;

typedef struct
{
    char uri[PLAYER_SOURCE_URI_MAX];
    char original_uri[PLAYER_SOURCE_URI_MAX];
    char metadata[PLAYER_SOURCE_METADATA_MAX];
    PlayerSourceProfile profile;
    PlayerSourceFormat format;
    PlayerSourceFlags flags;
    bool selected_from_metadata;
    int metadata_candidate_count;
    int network_timeout_seconds;
    char user_agent[PLAYER_SOURCE_USER_AGENT_MAX];
    char referrer[PLAYER_SOURCE_REFERRER_MAX];
    char origin[PLAYER_SOURCE_ORIGIN_MAX];
    char header_fields[PLAYER_SOURCE_HEADER_FIELDS_MAX];
    char probe_info[PLAYER_SOURCE_PROBE_INFO_MAX];
    char mime_type[PLAYER_SOURCE_MIME_TYPE_MAX];
    char protocol_info[PLAYER_SOURCE_PROTOCOL_INFO_MAX];
    char format_hint[PLAYER_SOURCE_FORMAT_HINT_MAX];
    char mpv_load_options[PLAYER_SOURCE_MPV_LOAD_OPTIONS_MAX];
} PlayerResolvedSource;

void player_source_reset(PlayerResolvedSource *source);
bool player_source_resolve(const char *uri, const char *metadata, PlayerResolvedSource *out);
const char *player_source_profile_name(PlayerSourceProfile profile);
const char *player_source_format_name(PlayerSourceFormat format);
