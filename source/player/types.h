#pragma once

#include <stdbool.h>

#define PLAYER_DEFAULT_VOLUME 100
#define PLAYER_MEDIA_URI_MAX 1024
#define PLAYER_MEDIA_FORMAT_HINT_MAX 48
#define PLAYER_MEDIA_USER_AGENT_MAX 192
#define PLAYER_MEDIA_REFERRER_MAX 192
#define PLAYER_MEDIA_ORIGIN_MAX 128
#define PLAYER_MEDIA_COOKIE_MAX 768
#define PLAYER_MEDIA_EXTRA_HEADERS_MAX 512

typedef enum
{
    PLAYER_MEDIA_VENDOR_UNKNOWN = 0,
    PLAYER_MEDIA_VENDOR_BILIBILI,
    PLAYER_MEDIA_VENDOR_IQIYI,
    PLAYER_MEDIA_VENDOR_MGTV,
    PLAYER_MEDIA_VENDOR_YOUKU,
    PLAYER_MEDIA_VENDOR_QQ_VIDEO,
    PLAYER_MEDIA_VENDOR_CCTV
} PlayerMediaVendor;

typedef enum
{
    PLAYER_MEDIA_FORMAT_UNKNOWN = 0,
    PLAYER_MEDIA_FORMAT_MP4,
    PLAYER_MEDIA_FORMAT_FLV,
    PLAYER_MEDIA_FORMAT_HLS,
    PLAYER_MEDIA_FORMAT_DASH,
    PLAYER_MEDIA_FORMAT_MPEG_TS
} PlayerMediaFormat;

typedef enum
{
    PLAYER_MEDIA_TRANSPORT_UNKNOWN = 0,
    PLAYER_MEDIA_TRANSPORT_HTTP_FILE,
    PLAYER_MEDIA_TRANSPORT_HLS_DIRECT,
    PLAYER_MEDIA_TRANSPORT_HLS_LOCAL_PROXY,
    PLAYER_MEDIA_TRANSPORT_HLS_GATEWAY
} PlayerMediaTransport;

/*
 * Request context comes from the control point HTTP request. It is a narrow
 * public input surface; detailed media policy and classification stay inside
 * ingress.
 */
typedef struct
{
    char sender_user_agent[PLAYER_MEDIA_USER_AGENT_MAX];
    char referrer[PLAYER_MEDIA_REFERRER_MAX];
    char origin[PLAYER_MEDIA_ORIGIN_MAX];
    char cookie[PLAYER_MEDIA_COOKIE_MAX];
    char extra_headers[PLAYER_MEDIA_EXTRA_HEADERS_MAX];
} PlayerOpenContext;

/*
 * Public media view exposed to protocol/render layers. This intentionally
 * omits transport-policy internals such as flags, headers, probe results, and
 * load options.
 */
typedef struct
{
    char uri[PLAYER_MEDIA_URI_MAX];
    PlayerMediaVendor vendor;
    PlayerMediaFormat format;
    PlayerMediaTransport transport;
    char format_hint[PLAYER_MEDIA_FORMAT_HINT_MAX];
} PlayerMediaSummary;

typedef enum
{
    PLAYER_STATE_IDLE = 0,
    PLAYER_STATE_STOPPED,
    PLAYER_STATE_LOADING,
    PLAYER_STATE_BUFFERING,
    PLAYER_STATE_SEEKING,
    PLAYER_STATE_PLAYING,
    PLAYER_STATE_PAUSED,
    PLAYER_STATE_ERROR
} PlayerState;

typedef enum
{
    PLAYER_EVENT_STATE_CHANGED = 0,
    PLAYER_EVENT_POSITION_CHANGED,
    PLAYER_EVENT_DURATION_CHANGED,
    PLAYER_EVENT_VOLUME_CHANGED,
    PLAYER_EVENT_MUTE_CHANGED,
    PLAYER_EVENT_URI_CHANGED,
    PLAYER_EVENT_ERROR
} PlayerEventType;

typedef struct
{
    PlayerEventType type;
    PlayerState state;
    int position_ms;
    int duration_ms;
    int volume;
    bool mute;
    bool seekable;
    int error_code;
    const char *uri;
} PlayerEvent;

typedef void (*PlayerEventCallback)(const PlayerEvent *event, void *user);

typedef struct
{
    bool has_media;
    PlayerMediaSummary media;
    PlayerState state;
    int position_ms;
    int duration_ms;
    int volume;
    bool mute;
    bool seekable;
} PlayerSnapshot;
