#ifndef NXCAST_AIRPLAY_MEDIA_STREAM_BRIDGE_H
#define NXCAST_AIRPLAY_MEDIA_STREAM_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol/airplay/mirror/audio.h"
#include "protocol/airplay/mirror/clock.h"
#include "protocol/airplay/mirror/video.h"

#define AIRPLAY_STREAM_BRIDGE_DEFAULT_CAPACITY (2u * 1024u * 1024u)
#define AIRPLAY_STREAM_BRIDGE_MIN_CAPACITY (16u * 1024u)

typedef struct
{
    size_t capacity;
    size_t buffered;
    uint64_t bytes_written;
    uint64_t bytes_read;
    uint64_t video_packets;
    uint64_t audio_packets;
    uint32_t video_config_generation;
    AirPlayMirrorClockStats clock;
    bool eof;
    bool cancelled;
    bool reader_claimed;
} AirPlayStreamBridgeStats;

typedef struct AirPlayStreamBridge AirPlayStreamBridge;

bool airplay_stream_bridge_create(size_t capacity,
                                  AirPlayStreamBridge **bridge_out);
void airplay_stream_bridge_retain(AirPlayStreamBridge *bridge);
void airplay_stream_bridge_release(AirPlayStreamBridge *bridge);
bool airplay_stream_bridge_claim_reader(AirPlayStreamBridge *bridge);
void airplay_stream_bridge_release_reader(AirPlayStreamBridge *bridge);
bool airplay_stream_bridge_push_video(AirPlayStreamBridge *bridge,
                                      const AirPlayMirrorAccessUnit *access_unit);
bool airplay_stream_bridge_configure_audio(
    AirPlayStreamBridge *bridge, const AirPlayMirrorAudioFormat *format);
bool airplay_stream_bridge_push_audio(AirPlayStreamBridge *bridge,
                                      const AirPlayMirrorAudioFrame *frame);
bool airplay_stream_bridge_update_audio_sync(AirPlayStreamBridge *bridge,
                                             uint32_t rtp_timestamp,
                                             uint64_t ntp_timestamp);
bool airplay_stream_bridge_finish(AirPlayStreamBridge *bridge);
void airplay_stream_bridge_cancel(AirPlayStreamBridge *bridge);
int64_t airplay_stream_bridge_read(AirPlayStreamBridge *bridge,
                                   uint8_t *output, size_t output_size);
bool airplay_stream_bridge_get_stats(AirPlayStreamBridge *bridge,
                                     AirPlayStreamBridgeStats *stats_out);
int64_t airplay_stream_bridge_ntp_to_90k(uint64_t ntp_timestamp);

#endif
