#ifndef NXCAST_AIRPLAY_MIRROR_TIMING_H
#define NXCAST_AIRPLAY_MIRROR_TIMING_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint64_t requests_sent;
    uint64_t responses_received;
} AirPlayMirrorTimingStats;

typedef struct AirPlayMirrorTiming AirPlayMirrorTiming;

bool airplay_mirror_timing_create(uint32_t peer_ipv4_address,
                                  uint16_t peer_port,
                                  AirPlayMirrorTiming **timing_out);
void airplay_mirror_timing_destroy(AirPlayMirrorTiming *timing);
uint16_t airplay_mirror_timing_port(const AirPlayMirrorTiming *timing);
bool airplay_mirror_timing_get_stats(const AirPlayMirrorTiming *timing,
                                     AirPlayMirrorTimingStats *stats_out);

#endif
