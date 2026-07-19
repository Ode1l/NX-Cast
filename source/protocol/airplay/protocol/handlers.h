#ifndef NXCAST_AIRPLAY_PROTOCOL_HANDLERS_H
#define NXCAST_AIRPLAY_PROTOCOL_HANDLERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol/airplay/discovery/dns.h"
#include "protocol/airplay/protocol/rtsp.h"
#include "protocol/airplay/security/fairplay.h"

#define AIRPLAY_HANDLER_NAME_MAX 63u
#define AIRPLAY_HANDLER_DEVICE_ID_STRING_SIZE 18u
#define AIRPLAY_HANDLER_PAIRING_ID_SIZE 37u

typedef bool (*AirPlayKeyUnwrapCallback)(
    const uint8_t wrapped_key[AIRPLAY_FAIRPLAY_WRAPPED_KEY_SIZE],
    uint8_t key_out[AIRPLAY_FAIRPLAY_AES_KEY_SIZE],
    void *user_data);
typedef bool (*AirPlaySharedSecretCallback)(const AirPlayRtspSession *session,
                                            uint8_t secret_out[32],
                                            void *user_data);
typedef bool (*AirPlayTransportPrepareCallback)(uint64_t session_id,
                                                const uint8_t key[16],
                                                const uint8_t iv[16],
                                                uint16_t *timing_port_out,
                                                void *user_data);
typedef bool (*AirPlayMirrorOpenCallback)(uint64_t session_id,
                                         const uint8_t key[16],
                                         uint64_t stream_connection_id,
                                         uint16_t *data_port_out,
                                         void *user_data);
typedef bool (*AirPlayAudioOpenCallback)(
    uint64_t session_id, const uint8_t key[16], const uint8_t iv[16],
    uint8_t compression_type, uint16_t samples_per_frame,
    uint32_t sample_rate, uint16_t *data_port_out,
    uint16_t *control_port_out, void *user_data);
typedef void (*AirPlayMirrorRecordCallback)(uint64_t session_id, void *user_data);
typedef void (*AirPlayMirrorStopCallback)(uint64_t session_id, void *user_data);
typedef struct AirPlayRemoteVideo AirPlayRemoteVideo;

typedef struct
{
    const char *friendly_name;
    const char *device_id;
    const char *pairing_id;
    const uint8_t *public_key;
    uint64_t features;
    const uint8_t *airplay_txt;
    size_t airplay_txt_size;
    AirPlayKeyUnwrapCallback unwrap_key_callback;
    AirPlaySharedSecretCallback shared_secret_callback;
    AirPlayTransportPrepareCallback transport_prepare_callback;
    AirPlayMirrorOpenCallback mirror_open_callback;
    AirPlayAudioOpenCallback audio_open_callback;
    AirPlayMirrorRecordCallback mirror_record_callback;
    AirPlayMirrorStopCallback mirror_stop_callback;
    AirPlayRemoteVideo *remote_video;
    void *callback_user_data;
} AirPlayHandlersConfig;

typedef struct AirPlayHandlers AirPlayHandlers;

bool airplay_handlers_create(const AirPlayHandlersConfig *config,
                             AirPlayHandlers **handlers_out);
void airplay_handlers_destroy(AirPlayHandlers *handlers);
bool airplay_handlers_route(AirPlayRtspSession *session,
                            const AirPlayRtspRequest *request,
                            AirPlayRtspResponse *response,
                            void *user_data);
void airplay_handlers_session_closed(AirPlayRtspSession *session, void *user_data);

#endif
