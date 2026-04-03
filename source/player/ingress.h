#pragma once

#include <stdbool.h>

#define PLAYER_MEDIA_URI_MAX 1024
#define PLAYER_MEDIA_METADATA_MAX 2048
#define PLAYER_MEDIA_USER_AGENT_MAX 192
#define PLAYER_MEDIA_REFERRER_MAX 192
#define PLAYER_MEDIA_ORIGIN_MAX 128
#define PLAYER_MEDIA_HEADER_FIELDS_MAX 512
#define PLAYER_MEDIA_PROBE_INFO_MAX 32
#define PLAYER_MEDIA_MIME_TYPE_MAX 96
#define PLAYER_MEDIA_PROTOCOL_INFO_MAX 192
#define PLAYER_MEDIA_FORMAT_HINT_MAX 48
#define PLAYER_MEDIA_MPV_LOAD_OPTIONS_MAX 320

typedef enum
{
    PLAYER_MEDIA_PROFILE_UNKNOWN = 0,
    PLAYER_MEDIA_PROFILE_DIRECT_HTTP_FILE,
    PLAYER_MEDIA_PROFILE_GENERIC_HLS,
    PLAYER_MEDIA_PROFILE_HEADER_SENSITIVE_HTTP,
    PLAYER_MEDIA_PROFILE_SIGNED_EPHEMERAL_URL,
    PLAYER_MEDIA_PROFILE_VENDOR_SENSITIVE_URL
} PlayerMediaProfile;

typedef enum
{
    PLAYER_MEDIA_FORMAT_UNKNOWN = 0,
    PLAYER_MEDIA_FORMAT_MP4,
    PLAYER_MEDIA_FORMAT_FLV,
    PLAYER_MEDIA_FORMAT_HLS,
    PLAYER_MEDIA_FORMAT_DASH,
    PLAYER_MEDIA_FORMAT_MPEG_TS
} PlayerMediaFormat;

typedef struct
{
    bool is_http;
    bool is_https;
    bool is_hls;
    bool likely_live;
    bool is_signed;
    bool is_bilibili;
    bool is_dash;
    bool is_flv;
    bool is_mp4;
    bool is_mpeg_ts;
    bool likely_segmented;
    bool likely_video_only;
} PlayerMediaFlags;

typedef struct
{
    char uri[PLAYER_MEDIA_URI_MAX];
    char original_uri[PLAYER_MEDIA_URI_MAX];
    char metadata[PLAYER_MEDIA_METADATA_MAX];
    PlayerMediaProfile profile;
    PlayerMediaFormat format;
    PlayerMediaFlags flags;
    bool selected_from_metadata;
    int metadata_candidate_count;
    int network_timeout_seconds;
    int demuxer_readahead_seconds;
    char user_agent[PLAYER_MEDIA_USER_AGENT_MAX];
    char referrer[PLAYER_MEDIA_REFERRER_MAX];
    char origin[PLAYER_MEDIA_ORIGIN_MAX];
    char header_fields[PLAYER_MEDIA_HEADER_FIELDS_MAX];
    char probe_info[PLAYER_MEDIA_PROBE_INFO_MAX];
    char mime_type[PLAYER_MEDIA_MIME_TYPE_MAX];
    char protocol_info[PLAYER_MEDIA_PROTOCOL_INFO_MAX];
    char format_hint[PLAYER_MEDIA_FORMAT_HINT_MAX];
    char mpv_load_options[PLAYER_MEDIA_MPV_LOAD_OPTIONS_MAX];
} PlayerMedia;

void ingress_reset(PlayerMedia *media);
bool ingress_resolve(const char *uri, const char *metadata, PlayerMedia *out);
const char *ingress_profile_name(PlayerMediaProfile profile);
const char *ingress_format_name(PlayerMediaFormat format);
