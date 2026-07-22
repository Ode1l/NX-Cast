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
    uint64_t config_ok;
    uint64_t config_failures;
    uint64_t access_units_ok;
    uint64_t access_units_dropped;
    uint64_t access_units_invalid;
    uint64_t access_units_no_memory;
    uint64_t access_unit_bytes;
    uint64_t keyframes;
    uint32_t config_generation;
    bool waiting_for_keyframe;
} AirPlayMirrorVideoStats;

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
bool airplay_mirror_video_get_stats(const AirPlayMirrorVideo *video,
                                    AirPlayMirrorVideoStats *stats_out);
const char *airplay_mirror_video_result_name(AirPlayMirrorVideoResult result);

#endif
