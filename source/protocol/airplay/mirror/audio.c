#include "audio.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __SWITCH__
#include <switch.h>
typedef Thread AirPlayAudioThread;
typedef void (*AirPlayAudioThreadEntry)(void *argument);
#define AIRPLAY_AUDIO_THREAD_RETURN void
#define AIRPLAY_AUDIO_THREAD_FINISH() return
#define AIRPLAY_AUDIO_THREAD_STACK_SIZE 0x10000u
#else
#include <pthread.h>
typedef pthread_t AirPlayAudioThread;
typedef void *(*AirPlayAudioThreadEntry)(void *argument);
#define AIRPLAY_AUDIO_THREAD_RETURN void *
#define AIRPLAY_AUDIO_THREAD_FINISH() return NULL
#endif

#include "protocol/airplay/security/crypto.h"
#include "protocol/airplay/trace.h"

#define AIRPLAY_AUDIO_REORDER_THRESHOLD 3u
#define AIRPLAY_AUDIO_POLL_MS 100u

typedef struct
{
    uint8_t *data;
    size_t size;
    uint16_t sequence;
    uint32_t timestamp;
    bool filled;
} AirPlayAudioSlot;

struct AirPlayMirrorAudio
{
    uint64_t session_id;
    uint8_t key[16];
    uint8_t iv[16];
    AirPlayMirrorAudioFormat format;
    AirPlayMirrorAudioCallback callback;
    AirPlayMirrorAudioSyncCallback sync_callback;
    void *callback_user_data;
    AirPlayAudioSlot slots[AIRPLAY_MIRROR_AUDIO_WINDOW];
    size_t buffered;
    uint16_t expected_sequence;
    bool sequence_ready;
    bool discontinuity;
    AirPlayAudioThread thread;
    atomic_bool running;
    atomic_bool recording;
    atomic_int data_fd;
    atomic_int control_fd;
    uint16_t data_port;
    uint16_t control_port;
    bool thread_started;
};

static uint16_t read_be16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] << 8) | data[1];
}

static uint32_t read_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static uint64_t read_be64(const uint8_t *data)
{
    return ((uint64_t)read_be32(data) << 32) | read_be32(data + 4u);
}

bool airplay_mirror_audio_format(uint8_t compression_type,
                                 uint16_t samples_per_frame,
                                 uint32_t sample_rate,
                                 AirPlayMirrorAudioFormat *format_out)
{
    static const uint8_t aac_lc[] = {0x12u, 0x10u};
    static const uint8_t aac_eld[] = {0xf8u, 0xe8u, 0x50u, 0x00u};
    const uint8_t *config;
    size_t config_size;

    if (!format_out)
        return false;
    if (sample_rate == 0u)
        sample_rate = 44100u;
    if (compression_type == AIRPLAY_MIRROR_AUDIO_CT_AAC_LC)
    {
        config = aac_lc;
        config_size = sizeof(aac_lc);
        if (samples_per_frame == 0u)
            samples_per_frame = 1024u;
    }
    else if (compression_type == AIRPLAY_MIRROR_AUDIO_CT_AAC_ELD)
    {
        config = aac_eld;
        config_size = sizeof(aac_eld);
        if (samples_per_frame == 0u)
            samples_per_frame = 480u;
    }
    else
        return false;
    if (sample_rate != 44100u || samples_per_frame == 0u)
        return false;
    memset(format_out, 0, sizeof(*format_out));
    format_out->compression_type = compression_type;
    format_out->sample_rate = sample_rate;
    format_out->channels = 2u;
    format_out->samples_per_frame = samples_per_frame;
    memcpy(format_out->codec_config, config, config_size);
    format_out->codec_config_size = config_size;
    return true;
}

static void audio_slots_clear(AirPlayMirrorAudio *audio)
{
    for (size_t index = 0u; index < AIRPLAY_MIRROR_AUDIO_WINDOW; ++index)
    {
        free(audio->slots[index].data);
        memset(&audio->slots[index], 0, sizeof(audio->slots[index]));
    }
    audio->buffered = 0u;
}

static void audio_emit_ready(AirPlayMirrorAudio *audio)
{
    while (audio->sequence_ready)
    {
        AirPlayAudioSlot *slot = &audio->slots[
            audio->expected_sequence % AIRPLAY_MIRROR_AUDIO_WINDOW];
        AirPlayMirrorAudioFrame frame;

        if (!slot->filled || slot->sequence != audio->expected_sequence)
            break;
        frame.data = slot->data;
        frame.size = slot->size;
        frame.sequence = slot->sequence;
        frame.rtp_timestamp = slot->timestamp;
        frame.discontinuity = audio->discontinuity;
        audio->discontinuity = false;
        if (audio->callback && atomic_load(&audio->recording))
            audio->callback(&frame, audio->callback_user_data);
        free(slot->data);
        memset(slot, 0, sizeof(*slot));
        audio->buffered--;
        audio->expected_sequence++;
    }
}

static void audio_skip_gap_if_needed(AirPlayMirrorAudio *audio)
{
    uint16_t nearest = 0u;
    uint16_t nearest_distance = UINT16_MAX;

    if (audio->buffered < AIRPLAY_AUDIO_REORDER_THRESHOLD)
        return;
    for (size_t index = 0u; index < AIRPLAY_MIRROR_AUDIO_WINDOW; ++index)
    {
        AirPlayAudioSlot *slot = &audio->slots[index];
        uint16_t distance;

        if (!slot->filled)
            continue;
        distance = (uint16_t)(slot->sequence - audio->expected_sequence);
        if (distance < nearest_distance)
        {
            nearest = slot->sequence;
            nearest_distance = distance;
        }
    }
    if (nearest_distance != UINT16_MAX && nearest_distance != 0u)
    {
        audio->expected_sequence = nearest;
        audio->discontinuity = true;
    }
}

bool airplay_mirror_audio_process_packet(AirPlayMirrorAudio *audio,
                                         const uint8_t *packet,
                                         size_t packet_size)
{
    static const uint8_t empty_marker[] = {0x00u, 0x68u, 0x34u, 0x00u};
    AirPlayAudioSlot *slot;
    uint8_t *payload;
    size_t payload_size;
    size_t encrypted_size;
    uint16_t sequence;
    uint32_t timestamp;
    int16_t distance;

    if (!audio || !packet || packet_size < AIRPLAY_MIRROR_AUDIO_RTP_HEADER_SIZE ||
        packet_size > AIRPLAY_MIRROR_AUDIO_MAX_PACKET ||
        packet[0] >> 6 != 2u || (packet[1] & 0x7fu) != 96u)
        return false;
    payload_size = packet_size - AIRPLAY_MIRROR_AUDIO_RTP_HEADER_SIZE;
    if (payload_size == 0u ||
        (payload_size == sizeof(empty_marker) &&
         memcmp(packet + AIRPLAY_MIRROR_AUDIO_RTP_HEADER_SIZE,
                empty_marker, sizeof(empty_marker)) == 0))
        return true;
    sequence = read_be16(packet + 2u);
    timestamp = read_be32(packet + 4u);
    if (!audio->sequence_ready)
    {
        audio->expected_sequence = sequence;
        audio->sequence_ready = true;
    }
    distance = (int16_t)(sequence - audio->expected_sequence);
    if (distance < 0)
        return true;
    if ((uint16_t)distance >= AIRPLAY_MIRROR_AUDIO_WINDOW)
    {
        audio_slots_clear(audio);
        audio->expected_sequence = sequence;
        audio->discontinuity = true;
    }
    slot = &audio->slots[sequence % AIRPLAY_MIRROR_AUDIO_WINDOW];
    if (slot->filled && slot->sequence == sequence)
        return true;
    if (slot->filled)
    {
        free(slot->data);
        memset(slot, 0, sizeof(*slot));
        audio->buffered--;
        audio->discontinuity = true;
    }
    payload = malloc(payload_size);
    if (!payload)
        return false;
    encrypted_size = payload_size / AIRPLAY_CRYPTO_AES_BLOCK_SIZE *
                     AIRPLAY_CRYPTO_AES_BLOCK_SIZE;
    if (!airplay_crypto_aes_cbc_decrypt(audio->key, audio->iv,
                                        packet + AIRPLAY_MIRROR_AUDIO_RTP_HEADER_SIZE,
                                        payload, encrypted_size))
    {
        free(payload);
        return false;
    }
    memcpy(payload + encrypted_size,
           packet + AIRPLAY_MIRROR_AUDIO_RTP_HEADER_SIZE + encrypted_size,
           payload_size - encrypted_size);
    slot->data = payload;
    slot->size = payload_size;
    slot->sequence = sequence;
    slot->timestamp = timestamp;
    slot->filled = true;
    audio->buffered++;
    audio_emit_ready(audio);
    audio_skip_gap_if_needed(audio);
    audio_emit_ready(audio);
    return true;
}

bool airplay_mirror_audio_process_control_packet(AirPlayMirrorAudio *audio,
                                                 const uint8_t *packet,
                                                 size_t packet_size)
{
    uint8_t packet_type;
    uint32_t rtp_timestamp;
    uint64_t ntp_timestamp;

    if (!audio || !packet || packet_size < 4u || packet[0] >> 6 != 2u)
        return false;
    packet_type = packet[1] & 0x7fu;
    if (packet_type == 0x56u)
        return packet_size >= 4u + AIRPLAY_MIRROR_AUDIO_RTP_HEADER_SIZE &&
               airplay_mirror_audio_process_packet(audio, packet + 4u,
                                                    packet_size - 4u);
    if (packet_type != 0x54u || packet_size < 20u)
        return false;
    rtp_timestamp = read_be32(packet + 4u);
    ntp_timestamp = read_be64(packet + 8u);
    if (!ntp_timestamp)
        return false;
    if (audio->sync_callback)
        audio->sync_callback(rtp_timestamp, ntp_timestamp,
                             audio->callback_user_data);
    return true;
}

static bool audio_thread_start(AirPlayAudioThread *thread,
                               AirPlayAudioThreadEntry entry, void *argument)
{
#ifdef __SWITCH__
    Result result = threadCreate(thread, entry, argument, NULL,
                                 AIRPLAY_AUDIO_THREAD_STACK_SIZE, 0x2b, -2);
    if (R_FAILED(result))
        return false;
    result = threadStart(thread);
    if (R_FAILED(result))
    {
        threadClose(thread);
        return false;
    }
    return true;
#else
    return pthread_create(thread, NULL, entry, argument) == 0;
#endif
}

static void audio_thread_join(AirPlayAudioThread *thread)
{
#ifdef __SWITCH__
    threadWaitForExit(thread);
    threadClose(thread);
#else
    pthread_join(*thread, NULL);
#endif
}

static void close_owned_socket(atomic_int *owner)
{
    int socket_fd = atomic_exchange(owner, -1);

    if (socket_fd >= 0)
    {
        shutdown(socket_fd, SHUT_RDWR);
        close(socket_fd);
    }
}

static AIRPLAY_AUDIO_THREAD_RETURN audio_thread(void *argument)
{
    AirPlayMirrorAudio *audio = argument;
    uint8_t packet[AIRPLAY_MIRROR_AUDIO_MAX_PACKET];

    while (atomic_load(&audio->running))
    {
        int data_fd = atomic_load(&audio->data_fd);
        int control_fd = atomic_load(&audio->control_fd);
        int maximum = data_fd > control_fd ? data_fd : control_fd;
        fd_set read_set;
        struct timeval timeout = {0, AIRPLAY_AUDIO_POLL_MS * 1000u};
        int result;

        if (maximum < 0)
            break;
        FD_ZERO(&read_set);
        if (data_fd >= 0)
            FD_SET(data_fd, &read_set);
        if (control_fd >= 0)
            FD_SET(control_fd, &read_set);
        result = select(maximum + 1, &read_set, NULL, NULL, &timeout);
        if (result < 0 && errno != EINTR)
            break;
        if (result <= 0)
            continue;
        if (data_fd >= 0 && FD_ISSET(data_fd, &read_set))
        {
            ssize_t received = recvfrom(data_fd, packet, sizeof(packet), 0, NULL, NULL);
            if (received > 0)
                (void)airplay_mirror_audio_process_packet(audio, packet,
                                                          (size_t)received);
        }
        if (control_fd >= 0 && FD_ISSET(control_fd, &read_set))
        {
            ssize_t received = recvfrom(control_fd, packet, sizeof(packet), 0,
                                        NULL, NULL);
            if (received > 0)
                (void)airplay_mirror_audio_process_control_packet(
                    audio, packet, (size_t)received);
        }
    }
    AIRPLAY_AUDIO_THREAD_FINISH();
}

static bool create_udp_socket(atomic_int *owner, uint16_t *port_out)
{
    struct sockaddr_in address;
    socklen_t address_size = sizeof(address);
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (socket_fd < 0)
        return false;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(0u);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        getsockname(socket_fd, (struct sockaddr *)&address, &address_size) != 0)
    {
        close(socket_fd);
        return false;
    }
    *port_out = ntohs(address.sin_port);
    atomic_store(owner, socket_fd);
    return *port_out != 0u;
}

bool airplay_mirror_audio_create(const AirPlayMirrorAudioConfig *config,
                                 AirPlayMirrorAudio **audio_out)
{
    AirPlayMirrorAudio *audio;

    if (!config || !audio_out || *audio_out || config->session_id == 0u ||
        !config->aes_key || !config->aes_iv || !config->callback)
        return false;
    audio = calloc(1, sizeof(*audio));
    if (!audio)
        return false;
    audio->session_id = config->session_id;
    memcpy(audio->key, config->aes_key, sizeof(audio->key));
    memcpy(audio->iv, config->aes_iv, sizeof(audio->iv));
    audio->callback = config->callback;
    audio->sync_callback = config->sync_callback;
    audio->callback_user_data = config->callback_user_data;
    atomic_init(&audio->running, false);
    atomic_init(&audio->recording, false);
    atomic_init(&audio->data_fd, -1);
    atomic_init(&audio->control_fd, -1);
    if (!airplay_mirror_audio_format(config->compression_type,
                                     config->samples_per_frame,
                                     config->sample_rate, &audio->format) ||
        !create_udp_socket(&audio->data_fd, &audio->data_port) ||
        !create_udp_socket(&audio->control_fd, &audio->control_port))
    {
        airplay_mirror_audio_destroy(audio);
        return false;
    }
    atomic_store(&audio->running, true);
    if (!audio_thread_start(&audio->thread, audio_thread, audio))
    {
        atomic_store(&audio->running, false);
        airplay_mirror_audio_destroy(audio);
        return false;
    }
    audio->thread_started = true;
    *audio_out = audio;
    return true;
}

void airplay_mirror_audio_set_recording(AirPlayMirrorAudio *audio, bool recording)
{
    if (audio)
        atomic_store(&audio->recording, recording);
}

void airplay_mirror_audio_destroy(AirPlayMirrorAudio *audio)
{
    if (!audio)
        return;
    atomic_store(&audio->running, false);
    close_owned_socket(&audio->data_fd);
    close_owned_socket(&audio->control_fd);
    if (audio->thread_started)
        audio_thread_join(&audio->thread);
    audio_slots_clear(audio);
    airplay_crypto_secure_zero(audio->key, sizeof(audio->key));
    airplay_crypto_secure_zero(audio->iv, sizeof(audio->iv));
    free(audio);
}

uint16_t airplay_mirror_audio_data_port(const AirPlayMirrorAudio *audio)
{
    return audio && atomic_load(&audio->running) ? audio->data_port : 0u;
}

uint16_t airplay_mirror_audio_control_port(const AirPlayMirrorAudio *audio)
{
    return audio && atomic_load(&audio->running) ? audio->control_port : 0u;
}
