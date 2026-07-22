#ifndef NXCAST_AIRPLAY_MIRROR_TRANSPORT_SESSION_H
#define NXCAST_AIRPLAY_MIRROR_TRANSPORT_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "video.h"

#define AIRPLAY_MIRROR_HEADER_SIZE 128u
#define AIRPLAY_MIRROR_MAX_PAYLOAD AIRPLAY_MIRROR_VIDEO_MAX_ACCESS_UNIT
#define AIRPLAY_MIRROR_PACKET_VIDEO 0u
#define AIRPLAY_MIRROR_PACKET_CODEC 1u
#define AIRPLAY_MIRROR_PACKET_HEARTBEAT 2u
#define AIRPLAY_MIRROR_PACKET_REPORT 5u

typedef struct
{
    uint32_t payload_size;
    uint8_t type;
    uint8_t flags;
    uint16_t options;
    uint64_t timestamp;
} AirPlayMirrorPacketHeader;

typedef struct
{
    uint64_t session_id;
    const uint8_t *session_key;
    uint64_t stream_connection_id;
    AirPlayMirrorVideoCallback video_callback;
    void *video_user_data;
} AirPlayMirrorSessionConfig;

typedef struct
{
    uint64_t connections_accepted;
    uint64_t encrypted_packets;
    uint64_t encrypted_bytes;
    uint64_t decrypt_ok;
    uint64_t decrypt_failures;
    uint64_t invalid_headers;
    AirPlayMirrorVideoStats video;
} AirPlayMirrorSessionStats;

typedef struct AirPlayMirrorSession AirPlayMirrorSession;

bool airplay_mirror_packet_header_parse(
    const uint8_t data[AIRPLAY_MIRROR_HEADER_SIZE],
    AirPlayMirrorPacketHeader *header_out);
bool airplay_mirror_session_derive_crypto(
    const uint8_t session_key[16], uint64_t stream_connection_id,
    uint8_t key_out[16], uint8_t iv_out[16]);
bool airplay_mirror_session_create(const AirPlayMirrorSessionConfig *config,
                                   AirPlayMirrorSession **session_out);
void airplay_mirror_session_set_recording(AirPlayMirrorSession *session,
                                          bool recording);
void airplay_mirror_session_destroy(AirPlayMirrorSession *session);
uint16_t airplay_mirror_session_port(const AirPlayMirrorSession *session);
bool airplay_mirror_session_is_running(const AirPlayMirrorSession *session);
bool airplay_mirror_session_get_stats(const AirPlayMirrorSession *session,
                                      AirPlayMirrorSessionStats *stats_out);

#endif
