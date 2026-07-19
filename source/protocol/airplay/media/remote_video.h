#ifndef NXCAST_AIRPLAY_MEDIA_REMOTE_VIDEO_H
#define NXCAST_AIRPLAY_MEDIA_REMOTE_VIDEO_H

#include <stdbool.h>
#include <stdint.h>

#include "protocol/airplay/protocol/rtsp.h"

#define AIRPLAY_REMOTE_VIDEO_URL_MAX 4095u
#define AIRPLAY_REMOTE_VIDEO_METADATA_MAX 255u

typedef enum
{
    AIRPLAY_REMOTE_VIDEO_IDLE = 0,
    AIRPLAY_REMOTE_VIDEO_LOADING,
    AIRPLAY_REMOTE_VIDEO_BUFFERING,
    AIRPLAY_REMOTE_VIDEO_PLAYING,
    AIRPLAY_REMOTE_VIDEO_PAUSED,
    AIRPLAY_REMOTE_VIDEO_STOPPED,
    AIRPLAY_REMOTE_VIDEO_ERROR
} AirPlayRemoteVideoState;

typedef struct
{
    bool has_media;
    bool seekable;
    int position_ms;
    int duration_ms;
    AirPlayRemoteVideoState state;
} AirPlayRemoteVideoSnapshot;

typedef struct
{
    bool (*load)(const char *url, const char *metadata, void *user_data);
    bool (*play)(void *user_data);
    bool (*pause)(void *user_data);
    bool (*stop)(void *user_data);
    bool (*seek_ms)(int position_ms, void *user_data);
    bool (*snapshot)(AirPlayRemoteVideoSnapshot *snapshot_out, void *user_data);
    void *user_data;
} AirPlayRemoteVideoOps;

typedef struct AirPlayRemoteVideo AirPlayRemoteVideo;

bool airplay_remote_video_create(const AirPlayRemoteVideoOps *ops,
                                 AirPlayRemoteVideo **remote_out);
void airplay_remote_video_destroy(AirPlayRemoteVideo *remote);
bool airplay_remote_video_route(AirPlayRemoteVideo *remote, uint64_t session_id,
                                const AirPlayRtspRequest *request,
                                AirPlayRtspResponse *response,
                                bool *handled_out);
void airplay_remote_video_session_closed(AirPlayRemoteVideo *remote,
                                         uint64_t session_id);
bool airplay_remote_video_url_supported(const char *url);

#endif
