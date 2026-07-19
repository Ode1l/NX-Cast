#ifndef NXCAST_AIRPLAY_MIRROR_AUDIO_H
#define NXCAST_AIRPLAY_MIRROR_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AIRPLAY_MIRROR_AUDIO_RTP_HEADER_SIZE 12u
#define AIRPLAY_MIRROR_AUDIO_MAX_PACKET (32u * 1024u)
#define AIRPLAY_MIRROR_AUDIO_WINDOW 64u
#define AIRPLAY_MIRROR_AUDIO_CT_AAC_LC 4u
#define AIRPLAY_MIRROR_AUDIO_CT_AAC_ELD 8u

typedef struct
{
    uint8_t compression_type;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t samples_per_frame;
    uint8_t codec_config[4];
    size_t codec_config_size;
} AirPlayMirrorAudioFormat;

typedef struct
{
    const uint8_t *data;
    size_t size;
    uint16_t sequence;
    uint32_t rtp_timestamp;
    bool discontinuity;
} AirPlayMirrorAudioFrame;

typedef void (*AirPlayMirrorAudioCallback)(const AirPlayMirrorAudioFrame *frame,
                                           void *user_data);
typedef void (*AirPlayMirrorAudioSyncCallback)(uint32_t rtp_timestamp,
                                               uint64_t ntp_timestamp,
                                               void *user_data);

typedef struct
{
    uint64_t session_id;
    const uint8_t *aes_key;
    const uint8_t *aes_iv;
    uint8_t compression_type;
    uint16_t samples_per_frame;
    uint32_t sample_rate;
    AirPlayMirrorAudioCallback callback;
    AirPlayMirrorAudioSyncCallback sync_callback;
    void *callback_user_data;
} AirPlayMirrorAudioConfig;

typedef struct AirPlayMirrorAudio AirPlayMirrorAudio;

bool airplay_mirror_audio_format(uint8_t compression_type,
                                 uint16_t samples_per_frame,
                                 uint32_t sample_rate,
                                 AirPlayMirrorAudioFormat *format_out);
bool airplay_mirror_audio_create(const AirPlayMirrorAudioConfig *config,
                                 AirPlayMirrorAudio **audio_out);
void airplay_mirror_audio_set_recording(AirPlayMirrorAudio *audio, bool recording);
void airplay_mirror_audio_destroy(AirPlayMirrorAudio *audio);
uint16_t airplay_mirror_audio_data_port(const AirPlayMirrorAudio *audio);
uint16_t airplay_mirror_audio_control_port(const AirPlayMirrorAudio *audio);
bool airplay_mirror_audio_process_packet(AirPlayMirrorAudio *audio,
                                         const uint8_t *packet,
                                         size_t packet_size);
bool airplay_mirror_audio_process_control_packet(AirPlayMirrorAudio *audio,
                                                 const uint8_t *packet,
                                                 size_t packet_size);

#endif
