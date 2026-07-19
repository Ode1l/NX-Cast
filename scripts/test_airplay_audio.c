#include <arpa/inet.h>
#include <mbedtls/aes.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "protocol/airplay/media/stream_bridge.h"
#include "protocol/airplay/mirror/audio.h"
#include "protocol/airplay/mirror/video.h"

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

typedef struct
{
    uint8_t payload[16][4096];
    size_t sizes[16];
    uint16_t sequences[16];
    bool discontinuities[16];
    atomic_size_t count;
    atomic_uint sync_count;
    atomic_uint sync_rtp;
    atomic_uint_fast64_t sync_ntp;
} AudioRecorder;

typedef struct
{
    uint8_t data[4096];
    size_t size;
} VideoFixture;

static int hex_value(int character)
{
    if (character >= '0' && character <= '9')
        return character - '0';
    if (character >= 'a' && character <= 'f')
        return character - 'a' + 10;
    if (character >= 'A' && character <= 'F')
        return character - 'A' + 10;
    return -1;
}

static uint8_t *read_hex(const char *path, size_t *size_out)
{
    FILE *file = fopen(path, "rb");
    uint8_t *output = NULL;
    size_t size = 0u;
    size_t capacity = 0u;
    int high = -1;
    int character;

    if (!file || !size_out)
        goto failure;
    while ((character = fgetc(file)) != EOF)
    {
        int value = hex_value(character);
        if (value < 0)
            continue;
        if (high < 0)
            high = value;
        else
        {
            if (size == capacity)
            {
                size_t next_capacity = capacity ? capacity * 2u : 64u;
                uint8_t *next = realloc(output, next_capacity);
                if (!next)
                    goto failure;
                output = next;
                capacity = next_capacity;
            }
            output[size++] = (uint8_t)((high << 4) | value);
            high = -1;
        }
    }
    fclose(file);
    if (high >= 0 || size == 0u)
    {
        free(output);
        return NULL;
    }
    *size_out = size;
    return output;

failure:
    if (file)
        fclose(file);
    free(output);
    return NULL;
}

static void record_audio(const AirPlayMirrorAudioFrame *frame, void *user_data)
{
    AudioRecorder *recorder = user_data;
    size_t index = atomic_load_explicit(&recorder->count, memory_order_relaxed);

    CHECK(index < 16u && frame->size <= sizeof(recorder->payload[index]));
    if (index < 16u && frame->size <= sizeof(recorder->payload[index]))
    {
        memcpy(recorder->payload[index], frame->data, frame->size);
        recorder->sizes[index] = frame->size;
        recorder->sequences[index] = frame->sequence;
        recorder->discontinuities[index] = frame->discontinuity;
        atomic_store_explicit(&recorder->count, index + 1u,
                              memory_order_release);
    }
}

static void record_sync(uint32_t rtp_timestamp, uint64_t ntp_timestamp,
                        void *user_data)
{
    AudioRecorder *recorder = user_data;

    atomic_store_explicit(&recorder->sync_rtp, rtp_timestamp,
                          memory_order_relaxed);
    atomic_store_explicit(&recorder->sync_ntp, ntp_timestamp,
                          memory_order_relaxed);
    atomic_fetch_add_explicit(&recorder->sync_count, 1u, memory_order_release);
}

static bool encrypt_payload(const uint8_t key[16], const uint8_t iv[16],
                            const uint8_t *input, uint8_t *output, size_t size)
{
    mbedtls_aes_context aes;
    uint8_t working_iv[16];
    size_t encrypted_size = size / 16u * 16u;
    int result;

    memcpy(working_iv, iv, sizeof(working_iv));
    mbedtls_aes_init(&aes);
    result = mbedtls_aes_setkey_enc(&aes, key, 128u);
    if (result == 0 && encrypted_size)
        result = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, encrypted_size,
                                       working_iv, input, output);
    mbedtls_aes_free(&aes);
    if (result != 0)
        return false;
    memcpy(output + encrypted_size, input + encrypted_size, size - encrypted_size);
    return true;
}

static size_t make_packet(uint8_t *packet, size_t capacity, uint16_t sequence,
                          uint32_t timestamp, const uint8_t *payload,
                          size_t payload_size, const uint8_t key[16],
                          const uint8_t iv[16])
{
    if (capacity < 12u + payload_size)
        return 0u;
    memset(packet, 0, 12u + payload_size);
    packet[0] = 0x80u;
    packet[1] = 0x60u;
    packet[2] = (uint8_t)(sequence >> 8);
    packet[3] = (uint8_t)sequence;
    packet[4] = (uint8_t)(timestamp >> 24);
    packet[5] = (uint8_t)(timestamp >> 16);
    packet[6] = (uint8_t)(timestamp >> 8);
    packet[7] = (uint8_t)timestamp;
    if (!encrypt_payload(key, iv, payload, packet + 12u, payload_size))
        return 0u;
    return 12u + payload_size;
}

static void sleep_milliseconds(unsigned milliseconds)
{
    struct timespec duration = {
        .tv_sec = (time_t)(milliseconds / 1000u),
        .tv_nsec = (long)(milliseconds % 1000u) * 1000000L};
    nanosleep(&duration, NULL);
}

static void test_audio_packets(const uint8_t *payload, size_t payload_size)
{
    AirPlayMirrorAudioConfig config = {0};
    AirPlayMirrorAudio *audio = NULL;
    AudioRecorder recorder = {0};
    uint8_t key[16];
    uint8_t iv[16];
    uint8_t packet[AIRPLAY_MIRROR_AUDIO_MAX_PACKET];
    size_t packet_size;
    int socket_fd;
    struct sockaddr_in address;

    for (size_t index = 0u; index < 16u; ++index)
    {
        key[index] = (uint8_t)index;
        iv[index] = (uint8_t)(0xf0u + index);
    }
    config.session_id = 1u;
    config.aes_key = key;
    config.aes_iv = iv;
    config.compression_type = AIRPLAY_MIRROR_AUDIO_CT_AAC_LC;
    config.samples_per_frame = 1024u;
    config.sample_rate = 44100u;
    config.callback = record_audio;
    config.sync_callback = record_sync;
    config.callback_user_data = &recorder;
    CHECK(airplay_mirror_audio_create(&config, &audio));
    airplay_mirror_audio_set_recording(audio, true);
    packet_size = make_packet(packet, sizeof(packet), 100u, 44100u, payload,
                              payload_size, key, iv);
    CHECK(packet_size != 0u &&
          airplay_mirror_audio_process_packet(audio, packet, packet_size));
    CHECK(atomic_load_explicit(&recorder.count, memory_order_acquire) == 1u &&
          recorder.sequences[0] == 100u);
    CHECK(recorder.sizes[0] == payload_size &&
          memcmp(recorder.payload[0], payload, payload_size) == 0);
    CHECK(airplay_mirror_audio_process_packet(audio, packet, packet_size));
    CHECK(atomic_load_explicit(&recorder.count, memory_order_acquire) == 1u);
    for (uint16_t sequence = 102u; sequence <= 104u; ++sequence)
    {
        packet_size = make_packet(packet, sizeof(packet), sequence,
                                  44100u + (sequence - 100u) * 1024u,
                                  payload, payload_size, key, iv);
        CHECK(airplay_mirror_audio_process_packet(audio, packet, packet_size));
    }
    CHECK(atomic_load_explicit(&recorder.count, memory_order_acquire) == 4u &&
          recorder.sequences[1] == 102u &&
          recorder.discontinuities[1]);
    packet[0] = 0u;
    CHECK(!airplay_mirror_audio_process_packet(audio, packet, packet_size));

    packet_size = make_packet(packet, sizeof(packet), 105u, 49220u, payload,
                              payload_size, key, iv);
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    CHECK(socket_fd >= 0);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(airplay_mirror_audio_data_port(audio));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (socket_fd >= 0)
    {
        CHECK(sendto(socket_fd, packet, packet_size, 0,
                     (struct sockaddr *)&address, sizeof(address)) ==
              (ssize_t)packet_size);
        close(socket_fd);
    }
    for (unsigned attempt = 0u;
         atomic_load_explicit(&recorder.count, memory_order_acquire) < 5u &&
         attempt < 100u;
         ++attempt)
        sleep_milliseconds(5u);
    CHECK(atomic_load_explicit(&recorder.count, memory_order_acquire) == 5u &&
          recorder.sequences[4] == 105u);
    airplay_mirror_audio_set_recording(audio, false);
    packet_size = make_packet(packet, sizeof(packet), 106u, 50244u, payload,
                              payload_size, key, iv);
    CHECK(airplay_mirror_audio_process_packet(audio, packet, packet_size));
    CHECK(atomic_load_explicit(&recorder.count, memory_order_acquire) == 5u);
    airplay_mirror_audio_set_recording(audio, true);
    packet_size = make_packet(packet, sizeof(packet), 107u, 51268u, payload,
                              payload_size, key, iv);
    CHECK(airplay_mirror_audio_process_packet(audio, packet, packet_size));
    CHECK(atomic_load_explicit(&recorder.count, memory_order_acquire) == 6u &&
          recorder.sequences[5] == 107u);
    memset(packet, 0, 20u);
    packet[0] = 0x80u;
    packet[1] = 0xd4u;
    packet[4] = 0x12u;
    packet[5] = 0x34u;
    packet[6] = 0x56u;
    packet[7] = 0x78u;
    packet[8] = 0x00u;
    packet[9] = 0x00u;
    packet[10] = 0x00u;
    packet[11] = 0x64u;
    packet[12] = 0x80u;
    CHECK(airplay_mirror_audio_process_control_packet(audio, packet, 20u));
    CHECK(atomic_load_explicit(&recorder.sync_count, memory_order_acquire) == 1u);
    CHECK(atomic_load_explicit(&recorder.sync_rtp, memory_order_relaxed) ==
          UINT32_C(0x12345678));
    CHECK(atomic_load_explicit(&recorder.sync_ntp, memory_order_relaxed) ==
          UINT64_C(0x0000006480000000));
    packet[1] = 0u;
    CHECK(!airplay_mirror_audio_process_control_packet(audio, packet, 20u));
    packet_size = make_packet(packet + 4u, sizeof(packet) - 4u, 108u, 52292u,
                              payload, payload_size, key, iv);
    packet[0] = 0x80u;
    packet[1] = 0xd6u;
    packet[2] = 0u;
    packet[3] = 0u;
    CHECK(airplay_mirror_audio_process_control_packet(audio, packet,
                                                      packet_size + 4u));
    CHECK(atomic_load_explicit(&recorder.count, memory_order_acquire) == 7u &&
          recorder.sequences[6] == 108u);
    airplay_mirror_audio_destroy(audio);

    audio = NULL;
    config.session_id = 2u;
    CHECK(airplay_mirror_audio_create(&config, &audio));
    airplay_mirror_audio_set_recording(audio, true);
    packet_size = make_packet(packet, sizeof(packet), 200u, 44100u, payload,
                              payload_size, key, iv);
    CHECK(airplay_mirror_audio_process_packet(audio, packet, packet_size));
    CHECK(atomic_load_explicit(&recorder.count, memory_order_acquire) == 8u &&
          recorder.sequences[7] == 200u);
    airplay_mirror_audio_destroy(audio);
}

static void capture_video(const AirPlayMirrorAccessUnit *access_unit, void *user_data)
{
    VideoFixture *fixture = user_data;

    CHECK(access_unit->size <= sizeof(fixture->data));
    if (access_unit->size <= sizeof(fixture->data))
    {
        memcpy(fixture->data, access_unit->data, access_unit->size);
        fixture->size = access_unit->size;
    }
}

static void test_dual_stream(const uint8_t *aac, size_t aac_size)
{
    AirPlayMirrorVideo *video = NULL;
    AirPlayStreamBridge *bridge = NULL;
    AirPlayMirrorAudioFormat format;
    AirPlayMirrorAccessUnit video_frame = {0};
    AirPlayMirrorAudioFrame audio_frame = {0};
    AirPlayStreamBridgeStats stats = {0};
    VideoFixture fixture = {0};
    uint8_t *avcc = NULL;
    uint8_t *idr = NULL;
    size_t avcc_size = 0u;
    size_t idr_size = 0u;
    uint8_t buffer[1024];
    int64_t amount;
    FILE *output;

    avcc = read_hex("scripts/fixtures/airplay/mirror/h264-avcc.hex", &avcc_size);
    idr = read_hex("scripts/fixtures/airplay/mirror/h264-idr-au.hex", &idr_size);
    CHECK(avcc && idr &&
          airplay_mirror_video_create(capture_video, &fixture, &video));
    CHECK(airplay_mirror_video_process_config(video, avcc, avcc_size, 0u) ==
          AIRPLAY_MIRROR_VIDEO_OK);
    CHECK(airplay_mirror_video_process_access_unit(video, idr, idr_size,
                                                    UINT64_C(1) << 32) ==
          AIRPLAY_MIRROR_VIDEO_OK);
    CHECK(airplay_stream_bridge_create(0u, &bridge));
    CHECK(airplay_mirror_audio_format(AIRPLAY_MIRROR_AUDIO_CT_AAC_LC, 1024u,
                                      44100u, &format));
    CHECK(airplay_stream_bridge_configure_audio(bridge, &format));
    CHECK(airplay_stream_bridge_update_audio_sync(
        bridge, 44100u, UINT64_C(1) << 32));
    video_frame.data = fixture.data;
    video_frame.size = fixture.size;
    video_frame.timestamp = UINT64_C(1) << 32;
    video_frame.config_generation = 1u;
    video_frame.keyframe = true;
    audio_frame.data = aac;
    audio_frame.size = aac_size;
    audio_frame.sequence = 1u;
    audio_frame.rtp_timestamp = 44100u;
    for (unsigned index = 0u; index < 180u; ++index)
    {
        video_frame.timestamp = (UINT64_C(1) << 32) +
                                ((uint64_t)index << 32) / 30u;
        audio_frame.rtp_timestamp = 44100u + index * 1024u;
        CHECK(airplay_stream_bridge_push_video(bridge, &video_frame));
        CHECK(airplay_stream_bridge_push_audio(bridge, &audio_frame));
    }
    CHECK(airplay_stream_bridge_finish(bridge));
    CHECK(airplay_stream_bridge_claim_reader(bridge));
    output = fopen("build/tests/airplay-mirror-av.ts", "wb");
    CHECK(output != NULL);
    while ((amount = airplay_stream_bridge_read(bridge, buffer, sizeof(buffer))) > 0)
    {
        if (output)
            CHECK(fwrite(buffer, 1u, (size_t)amount, output) == (size_t)amount);
    }
    CHECK(amount == 0);
    if (output)
        fclose(output);
    CHECK(airplay_stream_bridge_get_stats(bridge, &stats));
    CHECK(stats.video_packets == 180u && stats.audio_packets == 180u);
    airplay_stream_bridge_release_reader(bridge);
    airplay_stream_bridge_release(bridge);
    airplay_mirror_video_destroy(video);
    free(avcc);
    free(idr);
}

int main(void)
{
    AirPlayMirrorAudioFormat format;
    uint8_t *aac;
    size_t aac_size;

    CHECK(airplay_mirror_audio_format(AIRPLAY_MIRROR_AUDIO_CT_AAC_ELD, 0u, 0u,
                                      &format));
    CHECK(format.codec_config_size == 4u && format.samples_per_frame == 480u);
    CHECK(!airplay_mirror_audio_format(2u, 0u, 0u, &format));
    aac = read_hex("scripts/fixtures/airplay/mirror/aac-lc-frame.hex", &aac_size);
    CHECK(aac != NULL);
    if (aac)
    {
        test_audio_packets(aac, aac_size);
        test_dual_stream(aac, aac_size);
    }
    free(aac);
    if (g_failures)
    {
        fprintf(stderr, "%d AirPlay audio checks failed\n", g_failures);
        return 1;
    }
    puts("AirPlay audio checks passed");
    return 0;
}
