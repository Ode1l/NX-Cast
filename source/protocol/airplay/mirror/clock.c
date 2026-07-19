#include "clock.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define AIRPLAY_CLOCK_CORRECTION_DEADBAND_TICKS 90
#define AIRPLAY_CLOCK_MAX_CORRECTION_TICKS 450
#define AIRPLAY_CLOCK_SYNC_JUMP_TICKS 22500
#define AIRPLAY_CLOCK_VIDEO_BACKWARD_TICKS 9000
#define AIRPLAY_CLOCK_VIDEO_FORWARD_TICKS 900000

typedef struct
{
    int64_t source_pts;
    uint64_t local_time_us;
    uint64_t jitter_us;
    bool ready;
} AirPlayClockArrival;

struct AirPlayMirrorClock
{
    uint32_t sample_rate;
    uint64_t video_anchor_ntp;
    int64_t video_anchor_pts;
    uint64_t last_video_ntp;
    int64_t last_video_pts;
    int64_t last_audio_pts;
    uint32_t audio_sync_rtp;
    uint64_t audio_sync_ntp;
    int64_t audio_adjust_ticks;
    uint32_t previous_sync_rtp;
    uint64_t previous_sync_ntp;
    AirPlayClockArrival video_arrival;
    AirPlayClockArrival audio_arrival;
    AirPlayMirrorClockStats stats;
    bool video_ready;
    bool audio_sync_ready;
    bool audio_ready;
    bool previous_sync_ready;
};

static uint64_t absolute_i64(int64_t value)
{
    if (value >= 0)
        return (uint64_t)value;
    return (uint64_t)(-(value + 1)) + 1u;
}

static int64_t ntp_delta_to_90k(uint64_t timestamp, uint64_t anchor)
{
    int64_t delta = (int64_t)(timestamp - anchor);
    int64_t seconds = delta / INT64_C(4294967296);
    int64_t fraction = delta % INT64_C(4294967296);

    return seconds * AIRPLAY_MIRROR_CLOCK_TIME_BASE +
           fraction * AIRPLAY_MIRROR_CLOCK_TIME_BASE /
               INT64_C(4294967296);
}

int64_t airplay_mirror_clock_ntp_to_90k(uint64_t ntp_timestamp)
{
    uint64_t seconds = ntp_timestamp >> 32;
    uint64_t fraction = ntp_timestamp & UINT32_MAX;

    if (seconds > (uint64_t)INT64_MAX / AIRPLAY_MIRROR_CLOCK_TIME_BASE)
        return INT64_MAX;
    return (int64_t)(seconds * AIRPLAY_MIRROR_CLOCK_TIME_BASE +
                     fraction * AIRPLAY_MIRROR_CLOCK_TIME_BASE /
                         UINT64_C(4294967296));
}

static void update_jitter(AirPlayClockArrival *arrival, int64_t source_pts,
                          uint64_t local_time_us)
{
    if (arrival->ready && local_time_us >= arrival->local_time_us)
    {
        int64_t source_delta = source_pts - arrival->source_pts;
        uint64_t local_delta = local_time_us - arrival->local_time_us;
        int64_t source_us = source_delta * 1000000 /
                            AIRPLAY_MIRROR_CLOCK_TIME_BASE;
        int64_t variation = (int64_t)local_delta - source_us;
        uint64_t absolute = absolute_i64(variation);

        arrival->jitter_us = (arrival->jitter_us * 15u + absolute + 8u) / 16u;
    }
    arrival->source_pts = source_pts;
    arrival->local_time_us = local_time_us;
    arrival->ready = true;
}

static int64_t map_video_ntp(const AirPlayMirrorClock *clock,
                             uint64_t ntp_timestamp)
{
    return clock->video_anchor_pts +
           ntp_delta_to_90k(ntp_timestamp, clock->video_anchor_ntp);
}

static void update_skew(AirPlayMirrorClock *clock)
{
    uint64_t absolute;

    if (!clock->video_ready || !clock->audio_ready)
        return;
    clock->stats.av_skew_ticks = clock->last_audio_pts - clock->last_video_pts;
    absolute = absolute_i64(clock->stats.av_skew_ticks);
    if (absolute > (uint64_t)clock->stats.max_abs_av_skew_ticks)
        clock->stats.max_abs_av_skew_ticks =
            absolute > INT64_MAX ? INT64_MAX : (int64_t)absolute;
}

static void reset_audio_mapping(AirPlayMirrorClock *clock)
{
    clock->audio_sync_ready = false;
    clock->audio_ready = false;
    clock->previous_sync_ready = false;
    clock->audio_adjust_ticks = 0;
    memset(&clock->audio_arrival, 0, sizeof(clock->audio_arrival));
}

bool airplay_mirror_clock_create(AirPlayMirrorClock **clock_out)
{
    AirPlayMirrorClock *clock;

    if (!clock_out || *clock_out)
        return false;
    clock = calloc(1, sizeof(*clock));
    if (!clock)
        return false;
    clock->stats.generation = 1u;
    *clock_out = clock;
    return true;
}

void airplay_mirror_clock_destroy(AirPlayMirrorClock *clock)
{
    if (!clock)
        return;
    memset(clock, 0, sizeof(*clock));
    free(clock);
}

void airplay_mirror_clock_reset(AirPlayMirrorClock *clock)
{
    uint32_t sample_rate;
    uint32_t generation;

    if (!clock)
        return;
    sample_rate = clock->sample_rate;
    generation = clock->stats.generation + 1u;
    memset(clock, 0, sizeof(*clock));
    clock->sample_rate = sample_rate;
    clock->stats.generation = generation ? generation : 1u;
}

bool airplay_mirror_clock_set_audio_rate(AirPlayMirrorClock *clock,
                                         uint32_t sample_rate)
{
    if (!clock || sample_rate < 8000u || sample_rate > 192000u)
        return false;
    if (clock->sample_rate && clock->sample_rate != sample_rate)
        reset_audio_mapping(clock);
    clock->sample_rate = sample_rate;
    return true;
}

AirPlayMirrorClockResult airplay_mirror_clock_update_audio_sync(
    AirPlayMirrorClock *clock, uint32_t rtp_timestamp, uint64_t ntp_timestamp,
    uint64_t local_time_us)
{
    AirPlayMirrorClockResult result = AIRPLAY_MIRROR_CLOCK_OK;
    int64_t correction = 0;

    (void)local_time_us;
    if (!clock || !clock->sample_rate || !ntp_timestamp)
        return AIRPLAY_MIRROR_CLOCK_OUT_OF_RANGE;
    if (clock->audio_sync_ready && clock->video_ready)
    {
        int32_t rtp_delta = (int32_t)(rtp_timestamp - clock->audio_sync_rtp);
        int64_t predicted = map_video_ntp(clock, clock->audio_sync_ntp) +
                            clock->audio_adjust_ticks +
                            (int64_t)rtp_delta *
                                AIRPLAY_MIRROR_CLOCK_TIME_BASE /
                                clock->sample_rate;
        int64_t observed = map_video_ntp(clock, ntp_timestamp);

        correction = observed - predicted;
        if (absolute_i64(correction) > AIRPLAY_CLOCK_SYNC_JUMP_TICKS)
        {
            clock->stats.discontinuities++;
            clock->stats.generation++;
            clock->audio_adjust_ticks = 0;
            clock->audio_ready = false;
            memset(&clock->audio_arrival, 0, sizeof(clock->audio_arrival));
            result = AIRPLAY_MIRROR_CLOCK_DISCONTINUITY;
        }
        else if (absolute_i64(correction) >
                 AIRPLAY_CLOCK_CORRECTION_DEADBAND_TICKS)
        {
            int64_t applied = correction;

            if (applied > AIRPLAY_CLOCK_MAX_CORRECTION_TICKS)
                applied = AIRPLAY_CLOCK_MAX_CORRECTION_TICKS;
            else if (applied < -AIRPLAY_CLOCK_MAX_CORRECTION_TICKS)
                applied = -AIRPLAY_CLOCK_MAX_CORRECTION_TICKS;
            clock->audio_adjust_ticks = predicted + applied - observed;
            clock->stats.audio_corrections++;
        }
        else
            clock->audio_adjust_ticks = predicted - observed;
    }
    if (clock->previous_sync_ready)
    {
        int32_t rtp_delta = (int32_t)(rtp_timestamp - clock->previous_sync_rtp);
        int64_t ntp_delta = ntp_delta_to_90k(ntp_timestamp,
                                             clock->previous_sync_ntp);

        if (rtp_delta > 0 && ntp_delta > 0 && ntp_delta <
            60 * AIRPLAY_MIRROR_CLOCK_TIME_BASE)
        {
            int64_t expected = ntp_delta * clock->sample_rate /
                               AIRPLAY_MIRROR_CLOCK_TIME_BASE;
            if (expected > 0)
            {
                int64_t observed_ppm = ((int64_t)rtp_delta - expected) *
                                       1000000 / expected;
                clock->stats.drift_ppm =
                    (clock->stats.drift_ppm * 7 + observed_ppm) / 8;
            }
        }
    }
    clock->previous_sync_rtp = rtp_timestamp;
    clock->previous_sync_ntp = ntp_timestamp;
    clock->previous_sync_ready = true;
    clock->audio_sync_rtp = rtp_timestamp;
    clock->audio_sync_ntp = ntp_timestamp;
    clock->audio_sync_ready = true;
    clock->stats.audio_sync_updates++;
    return result;
}

AirPlayMirrorClockResult airplay_mirror_clock_map_video(
    AirPlayMirrorClock *clock, uint64_t ntp_timestamp, uint64_t local_time_us,
    int64_t *pts_out)
{
    AirPlayMirrorClockResult result = AIRPLAY_MIRROR_CLOCK_OK;
    int64_t pts;

    if (!clock || !pts_out || !ntp_timestamp)
        return AIRPLAY_MIRROR_CLOCK_OUT_OF_RANGE;
    if (!clock->video_ready)
    {
        clock->video_anchor_ntp = ntp_timestamp;
        clock->video_anchor_pts = 0;
        pts = 0;
        clock->video_ready = true;
    }
    else
    {
        int64_t delta = ntp_delta_to_90k(ntp_timestamp,
                                         clock->last_video_ntp);

        if (delta < -AIRPLAY_CLOCK_VIDEO_BACKWARD_TICKS ||
            delta > AIRPLAY_CLOCK_VIDEO_FORWARD_TICKS)
        {
            clock->video_anchor_ntp = ntp_timestamp;
            clock->video_anchor_pts = clock->last_video_pts + 1;
            pts = clock->video_anchor_pts;
            reset_audio_mapping(clock);
            clock->stats.discontinuities++;
            clock->stats.generation++;
            result = AIRPLAY_MIRROR_CLOCK_DISCONTINUITY;
        }
        else
        {
            pts = map_video_ntp(clock, ntp_timestamp);
            if (pts <= clock->last_video_pts)
                pts = clock->last_video_pts + 1;
        }
    }
    clock->last_video_ntp = ntp_timestamp;
    clock->last_video_pts = pts;
    update_jitter(&clock->video_arrival, pts, local_time_us);
    clock->stats.video_jitter_us = clock->video_arrival.jitter_us;
    update_skew(clock);
    *pts_out = pts;
    return result;
}

AirPlayMirrorClockResult airplay_mirror_clock_map_audio(
    AirPlayMirrorClock *clock, uint32_t rtp_timestamp, uint64_t local_time_us,
    int64_t *pts_out)
{
    int32_t rtp_delta;
    int64_t pts;

    if (!clock || !pts_out || !clock->sample_rate)
        return AIRPLAY_MIRROR_CLOCK_OUT_OF_RANGE;
    if (!clock->video_ready || !clock->audio_sync_ready)
    {
        clock->stats.unsynced_audio_drops++;
        return AIRPLAY_MIRROR_CLOCK_WAITING_FOR_SYNC;
    }
    rtp_delta = (int32_t)(rtp_timestamp - clock->audio_sync_rtp);
    pts = map_video_ntp(clock, clock->audio_sync_ntp) +
          clock->audio_adjust_ticks +
          (int64_t)rtp_delta * AIRPLAY_MIRROR_CLOCK_TIME_BASE /
              clock->sample_rate;
    if (pts < 0 ||
        (clock->video_ready &&
         absolute_i64(pts - clock->last_video_pts) >
             AIRPLAY_MIRROR_CLOCK_MAX_SKEW_TICKS))
    {
        clock->stats.skew_audio_drops++;
        return AIRPLAY_MIRROR_CLOCK_OUT_OF_RANGE;
    }
    if (clock->audio_ready && pts <= clock->last_audio_pts)
        pts = clock->last_audio_pts + 1;
    clock->last_audio_pts = pts;
    clock->audio_ready = true;
    update_jitter(&clock->audio_arrival, pts, local_time_us);
    clock->stats.audio_jitter_us = clock->audio_arrival.jitter_us;
    update_skew(clock);
    *pts_out = pts;
    return AIRPLAY_MIRROR_CLOCK_OK;
}

bool airplay_mirror_clock_get_stats(const AirPlayMirrorClock *clock,
                                    AirPlayMirrorClockStats *stats_out)
{
    if (!clock || !stats_out)
        return false;
    *stats_out = clock->stats;
    return true;
}
