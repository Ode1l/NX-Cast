#ifndef NXCAST_AIRPLAY_MIRROR_VIDEO_H
#define NXCAST_AIRPLAY_MIRROR_VIDEO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AIRPLAY_MIRROR_VIDEO_MAX_CONFIG (64u * 1024u)
#define AIRPLAY_MIRROR_VIDEO_MAX_ACCESS_UNIT (8u * 1024u * 1024u)

typedef enum
{
    AIRPLAY_MIRROR_VIDEO_OK = 0,
    AIRPLAY_MIRROR_VIDEO_DROPPED,
    AIRPLAY_MIRROR_VIDEO_INVALID,
    AIRPLAY_MIRROR_VIDEO_NO_MEMORY
} AirPlayMirrorVideoResult;

typedef struct
{
    const uint8_t *data;
    size_t size;
    uint64_t timestamp;
    uint32_t config_generation;
    bool keyframe;
} AirPlayMirrorAccessUnit;

typedef void (*AirPlayMirrorVideoCallback)(const AirPlayMirrorAccessUnit *access_unit,
                                           void *user_data);

typedef struct AirPlayMirrorVideo AirPlayMirrorVideo;

bool airplay_mirror_video_create(AirPlayMirrorVideoCallback callback,
                                 void *user_data,
                                 AirPlayMirrorVideo **video_out);
void airplay_mirror_video_destroy(AirPlayMirrorVideo *video);
void airplay_mirror_video_reset(AirPlayMirrorVideo *video);
AirPlayMirrorVideoResult airplay_mirror_video_process_config(
    AirPlayMirrorVideo *video, const uint8_t *avcc, size_t avcc_size,
    uint64_t timestamp);
AirPlayMirrorVideoResult airplay_mirror_video_process_access_unit(
    AirPlayMirrorVideo *video, const uint8_t *payload, size_t payload_size,
    uint64_t timestamp);
uint32_t airplay_mirror_video_config_generation(const AirPlayMirrorVideo *video);
bool airplay_mirror_video_waiting_for_keyframe(const AirPlayMirrorVideo *video);
const char *airplay_mirror_video_result_name(AirPlayMirrorVideoResult result);

#endif
