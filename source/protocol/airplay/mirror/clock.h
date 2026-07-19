#ifndef NXCAST_AIRPLAY_MIRROR_CLOCK_H
#define NXCAST_AIRPLAY_MIRROR_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

#define AIRPLAY_MIRROR_CLOCK_TIME_BASE 90000
#define AIRPLAY_MIRROR_CLOCK_MAX_SKEW_TICKS \
    (2 * AIRPLAY_MIRROR_CLOCK_TIME_BASE)

typedef enum
{
    AIRPLAY_MIRROR_CLOCK_OK = 0,
    AIRPLAY_MIRROR_CLOCK_WAITING_FOR_SYNC,
    AIRPLAY_MIRROR_CLOCK_DISCONTINUITY,
    AIRPLAY_MIRROR_CLOCK_OUT_OF_RANGE
} AirPlayMirrorClockResult;

typedef struct
{
    uint32_t generation;
    uint64_t audio_sync_updates;
    uint64_t audio_corrections;
    uint64_t discontinuities;
    uint64_t unsynced_audio_drops;
    uint64_t skew_audio_drops;
    uint64_t video_jitter_us;
    uint64_t audio_jitter_us;
    int64_t av_skew_ticks;
    int64_t max_abs_av_skew_ticks;
    int64_t drift_ppm;
} AirPlayMirrorClockStats;

typedef struct AirPlayMirrorClock AirPlayMirrorClock;

bool airplay_mirror_clock_create(AirPlayMirrorClock **clock_out);
void airplay_mirror_clock_destroy(AirPlayMirrorClock *clock);
void airplay_mirror_clock_reset(AirPlayMirrorClock *clock);
bool airplay_mirror_clock_set_audio_rate(AirPlayMirrorClock *clock,
                                         uint32_t sample_rate);
AirPlayMirrorClockResult airplay_mirror_clock_update_audio_sync(
    AirPlayMirrorClock *clock, uint32_t rtp_timestamp, uint64_t ntp_timestamp,
    uint64_t local_time_us);
AirPlayMirrorClockResult airplay_mirror_clock_map_video(
    AirPlayMirrorClock *clock, uint64_t ntp_timestamp, uint64_t local_time_us,
    int64_t *pts_out);
AirPlayMirrorClockResult airplay_mirror_clock_map_audio(
    AirPlayMirrorClock *clock, uint32_t rtp_timestamp, uint64_t local_time_us,
    int64_t *pts_out);
bool airplay_mirror_clock_get_stats(const AirPlayMirrorClock *clock,
                                    AirPlayMirrorClockStats *stats_out);
int64_t airplay_mirror_clock_ntp_to_90k(uint64_t ntp_timestamp);

#endif
