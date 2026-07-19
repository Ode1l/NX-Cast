#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "protocol/airplay/mirror/clock.h"

static int g_failures;

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

static uint64_t ntp_from_us(uint64_t seconds, uint64_t microseconds)
{
    return (seconds << 32) |
           (microseconds * UINT64_C(4294967296) / 1000000u);
}

static void test_timeline(void)
{
    AirPlayMirrorClock *clock = NULL;
    AirPlayMirrorClockStats stats;
    AirPlayMirrorClockResult result;
    const uint32_t audio_anchor = UINT32_C(0xfffffff0);
    int64_t pts = -1;

    CHECK(airplay_mirror_clock_create(&clock));
    CHECK(airplay_mirror_clock_set_audio_rate(clock, 44100u));
    CHECK(airplay_mirror_clock_map_audio(clock, audio_anchor, 1000000u, &pts) ==
          AIRPLAY_MIRROR_CLOCK_WAITING_FOR_SYNC);
    CHECK(airplay_mirror_clock_map_video(clock, ntp_from_us(100u, 0u),
                                         1000000u, &pts) ==
          AIRPLAY_MIRROR_CLOCK_OK);
    CHECK(pts == 0);
    CHECK(airplay_mirror_clock_update_audio_sync(
              clock, audio_anchor, ntp_from_us(100u, 100000u), 1000000u) ==
          AIRPLAY_MIRROR_CLOCK_OK);
    CHECK(airplay_mirror_clock_map_audio(clock, audio_anchor, 1100000u, &pts) ==
          AIRPLAY_MIRROR_CLOCK_OK);
    CHECK(pts >= 8999 && pts <= 9000);
    CHECK(airplay_mirror_clock_map_audio(clock, audio_anchor + 1024u,
                                         1123000u, &pts) ==
          AIRPLAY_MIRROR_CLOCK_OK);
    CHECK(pts >= 11088 && pts <= 11090);
    CHECK(airplay_mirror_clock_map_video(clock, ntp_from_us(100u, 33333u),
                                         1035000u, &pts) ==
          AIRPLAY_MIRROR_CLOCK_OK);
    CHECK(pts >= 2998 && pts <= 3000);

    for (uint32_t second = 1u; second <= 120u; ++second)
    {
        uint32_t rtp = audio_anchor + second * 44102u;

        result = airplay_mirror_clock_update_audio_sync(
            clock, rtp, ntp_from_us(100u + second, 100000u),
            UINT64_C(1000000) + (uint64_t)second * 1000000u);
        CHECK(result == AIRPLAY_MIRROR_CLOCK_OK);
    }
    CHECK(airplay_mirror_clock_get_stats(clock, &stats));
    CHECK(stats.audio_sync_updates == 121u);
    CHECK(stats.audio_corrections > 0u);
    CHECK(stats.drift_ppm > 0 && stats.drift_ppm < 100);
    CHECK(stats.video_jitter_us > 0u && stats.audio_jitter_us > 0u);

    result = airplay_mirror_clock_update_audio_sync(
        clock, audio_anchor + 121u * 44102u,
        ntp_from_us(223u, 100000u), UINT64_C(122000000));
    CHECK(result == AIRPLAY_MIRROR_CLOCK_DISCONTINUITY);
    CHECK(airplay_mirror_clock_map_video(clock, ntp_from_us(102u, 33333u),
                                         UINT64_C(4000000), &pts) ==
          AIRPLAY_MIRROR_CLOCK_OK);
    CHECK(airplay_mirror_clock_map_video(clock, ntp_from_us(101u, 0u),
                                         UINT64_C(4100000), &pts) ==
          AIRPLAY_MIRROR_CLOCK_DISCONTINUITY);
    CHECK(airplay_mirror_clock_map_audio(clock, 1u, UINT64_C(4100000), &pts) ==
          AIRPLAY_MIRROR_CLOCK_WAITING_FOR_SYNC);

    airplay_mirror_clock_reset(clock);
    CHECK(airplay_mirror_clock_get_stats(clock, &stats));
    CHECK(stats.generation > 1u && stats.audio_sync_updates == 0u);
    airplay_mirror_clock_destroy(clock);
}

static void test_ntp_wrap(void)
{
    AirPlayMirrorClock *clock = NULL;
    int64_t first = -1;
    int64_t second = -1;
    uint64_t before_wrap = (UINT64_C(0xffffffff) << 32) |
                           ntp_from_us(0u, 900000u);
    uint64_t after_wrap = ntp_from_us(0u, 100000u);

    CHECK(airplay_mirror_clock_create(&clock));
    CHECK(airplay_mirror_clock_map_video(clock, before_wrap, 1u, &first) ==
          AIRPLAY_MIRROR_CLOCK_OK);
    CHECK(airplay_mirror_clock_map_video(clock, after_wrap, 200001u, &second) ==
          AIRPLAY_MIRROR_CLOCK_OK);
    CHECK(first == 0 && second >= 17999 && second <= 18001);
    CHECK(airplay_mirror_clock_ntp_to_90k(ntp_from_us(1u, 500000u)) ==
          135000);
    airplay_mirror_clock_destroy(clock);
}

int main(void)
{
    test_timeline();
    test_ntp_wrap();
    if (g_failures)
    {
        fprintf(stderr, "%d AirPlay clock checks failed\n", g_failures);
        return 1;
    }
    puts("AirPlay clock checks passed");
    return 0;
}
