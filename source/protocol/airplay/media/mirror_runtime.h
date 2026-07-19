#ifndef NXCAST_AIRPLAY_MEDIA_MIRROR_RUNTIME_H
#define NXCAST_AIRPLAY_MEDIA_MIRROR_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "protocol/airplay/media/stream_bridge.h"

typedef enum
{
    AIRPLAY_MIRROR_RUNTIME_IDLE = 0,
    AIRPLAY_MIRROR_RUNTIME_PREPARING,
    AIRPLAY_MIRROR_RUNTIME_WAITING_KEYFRAME,
    AIRPLAY_MIRROR_RUNTIME_PLAYING,
    AIRPLAY_MIRROR_RUNTIME_DISCONNECTED,
    AIRPLAY_MIRROR_RUNTIME_ERROR
} AirPlayMirrorRuntimeStatus;

typedef struct
{
    bool (*bind_stream)(AirPlayStreamBridge *bridge, void *user_data);
    bool (*set_uri)(const char *uri, const char *metadata, void *user_data);
    bool (*play)(void *user_data);
    bool (*stop)(void *user_data);
    void (*status_changed)(AirPlayMirrorRuntimeStatus status, uint32_t generation,
                           void *user_data);
    void *user_data;
} AirPlayMirrorRuntimePlayerOps;

typedef struct
{
    AirPlayMirrorRuntimePlayerOps player;
    size_t stream_capacity;
} AirPlayMirrorRuntimeConfig;

typedef struct AirPlayMirrorRuntime AirPlayMirrorRuntime;

bool airplay_mirror_runtime_create(const AirPlayMirrorRuntimeConfig *config,
                                   AirPlayMirrorRuntime **runtime_out);
void airplay_mirror_runtime_destroy(AirPlayMirrorRuntime *runtime);

bool airplay_mirror_runtime_transport_prepare(uint64_t session_id,
                                              const uint8_t key[16],
                                              const uint8_t iv[16],
                                              uint16_t *timing_port_out,
                                              void *user_data);
bool airplay_mirror_runtime_open(uint64_t session_id, const uint8_t key[16],
                                 uint64_t stream_connection_id,
                                 uint16_t *data_port_out, void *user_data);
bool airplay_mirror_runtime_audio_open(
    uint64_t session_id, const uint8_t key[16], const uint8_t iv[16],
    uint8_t compression_type, uint16_t samples_per_frame,
    uint32_t sample_rate, uint16_t *data_port_out,
    uint16_t *control_port_out, void *user_data);
void airplay_mirror_runtime_record(uint64_t session_id, void *user_data);
void airplay_mirror_runtime_stop(uint64_t session_id, void *user_data);

AirPlayMirrorRuntimeStatus airplay_mirror_runtime_status(
    AirPlayMirrorRuntime *runtime, uint32_t *generation_out);
const char *airplay_mirror_runtime_status_name(AirPlayMirrorRuntimeStatus status);

#endif
