#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "protocol/airplay/mirror/mirror_session.h"
#include "protocol/airplay/security/crypto.h"

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
    atomic_uint count;
    uint8_t data[4096];
    size_t size;
    uint64_t timestamp;
    uint32_t generation;
    bool keyframe;
} Recorder;

static void record_video(const AirPlayMirrorAccessUnit *access_unit, void *user_data)
{
    Recorder *recorder = user_data;

    recorder->size = access_unit->size;
    recorder->timestamp = access_unit->timestamp;
    recorder->generation = access_unit->config_generation;
    recorder->keyframe = access_unit->keyframe;
    CHECK(access_unit->size <= sizeof(recorder->data));
    if (access_unit->size <= sizeof(recorder->data))
        memcpy(recorder->data, access_unit->data, access_unit->size);
    atomic_fetch_add_explicit(&recorder->count, 1u, memory_order_release);
}

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
    size_t output_size = 0u;
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
            if (output_size == capacity)
            {
                size_t next_capacity = capacity ? capacity * 2u : 64u;
                uint8_t *next = realloc(output, next_capacity);
                if (!next)
                    goto failure;
                output = next;
                capacity = next_capacity;
            }
            output[output_size++] = (uint8_t)((high << 4) | value);
            high = -1;
        }
    }
    fclose(file);
    if (high >= 0 || output_size == 0u)
    {
        free(output);
        return NULL;
    }
    *size_out = output_size;
    return output;

failure:
    if (file)
        fclose(file);
    free(output);
    return NULL;
}

static void test_header_and_crypto(void)
{
    static const uint8_t expected_key[16] = {
        0x1f, 0x66, 0x70, 0x33, 0xbe, 0x1b, 0x5a, 0xa9,
        0x3a, 0x44, 0xb4, 0x73, 0x76, 0x13, 0x5e, 0x7d};
    static const uint8_t expected_iv[16] = {
        0xa3, 0x54, 0xab, 0xb0, 0xa5, 0x6b, 0x76, 0xaa,
        0xac, 0xfd, 0xbc, 0x96, 0xa1, 0x0e, 0x7b, 0xc8};
    uint8_t session_key[16];
    uint8_t key[16];
    uint8_t iv[16];
    uint8_t raw[AIRPLAY_MIRROR_HEADER_SIZE] = {0};
    AirPlayMirrorPacketHeader header;

    for (size_t index = 0u; index < sizeof(session_key); ++index)
        session_key[index] = (uint8_t)index;
    raw[0] = 0x34u;
    raw[1] = 0x12u;
    raw[4] = AIRPLAY_MIRROR_PACKET_VIDEO;
    raw[5] = 0x10u;
    raw[6] = 0x16u;
    raw[7] = 0x01u;
    raw[8] = 0x88u;
    raw[15] = 0x11u;
    CHECK(airplay_mirror_packet_header_parse(raw, &header));
    CHECK(header.payload_size == 0x1234u && header.flags == 0x10u);
    CHECK(header.options == 0x0116u && header.timestamp == UINT64_C(0x1100000000000088));
    raw[0] = 1u;
    raw[3] = 1u;
    CHECK(!airplay_mirror_packet_header_parse(raw, &header));
    CHECK(airplay_mirror_session_derive_crypto(session_key, 123456u, key, iv));
    CHECK(memcmp(key, expected_key, sizeof(key)) == 0);
    CHECK(memcmp(iv, expected_iv, sizeof(iv)) == 0);
}

static void test_video_parser(void)
{
    static const uint8_t p_frame[] = {0u, 0u, 0u, 2u, 0x41u, 0x80u};
    Recorder recorder = {0};
    AirPlayMirrorVideo *video = NULL;
    AirPlayMirrorVideoStats stats = {0};
    uint8_t *config;
    uint8_t *idr;
    uint8_t *truncated;
    size_t config_size;
    size_t idr_size;
    size_t truncated_size;
    FILE *dump;

    config = read_hex("scripts/fixtures/airplay/mirror/h264-avcc.hex", &config_size);
    idr = read_hex("scripts/fixtures/airplay/mirror/h264-idr-au.hex", &idr_size);
    truncated = read_hex("scripts/fixtures/airplay/mirror/h264-truncated-au.hex",
                         &truncated_size);
    CHECK(config && idr && truncated);
    CHECK(airplay_mirror_video_create(record_video, &recorder, &video));
    CHECK(airplay_mirror_video_process_access_unit(video, idr, idr_size, 100u) ==
          AIRPLAY_MIRROR_VIDEO_DROPPED);
    CHECK(airplay_mirror_video_process_config(video, config, config_size, 200u) ==
          AIRPLAY_MIRROR_VIDEO_OK);
    CHECK(airplay_mirror_video_waiting_for_keyframe(video));
    CHECK(airplay_mirror_video_process_access_unit(video, p_frame, sizeof(p_frame), 200u) ==
          AIRPLAY_MIRROR_VIDEO_DROPPED);
    CHECK(airplay_mirror_video_process_access_unit(video, idr, idr_size, 200u) ==
          AIRPLAY_MIRROR_VIDEO_OK);
    CHECK(atomic_load(&recorder.count) == 1u && recorder.keyframe &&
          recorder.timestamp == 200u);
    CHECK(recorder.generation == 1u && recorder.size > idr_size);
    CHECK(memcmp(recorder.data, "\0\0\0\1\x67", 5u) == 0);
    dump = fopen("build/tests/airplay-mirror.h264", "wb");
    CHECK(dump != NULL);
    if (dump)
    {
        CHECK(fwrite(recorder.data, 1u, recorder.size, dump) == recorder.size);
        fclose(dump);
    }
    CHECK(airplay_mirror_video_process_access_unit(video, p_frame, sizeof(p_frame), 201u) ==
          AIRPLAY_MIRROR_VIDEO_OK);
    CHECK(atomic_load(&recorder.count) == 2u && !recorder.keyframe && recorder.size == 6u);
    CHECK(airplay_mirror_video_process_access_unit(video, p_frame, sizeof(p_frame), 199u) ==
          AIRPLAY_MIRROR_VIDEO_DROPPED);
    CHECK(airplay_mirror_video_process_access_unit(video, truncated, truncated_size, 202u) ==
          AIRPLAY_MIRROR_VIDEO_INVALID);
    config[10] ^= 1u;
    CHECK(airplay_mirror_video_process_config(video, config, config_size, 203u) ==
          AIRPLAY_MIRROR_VIDEO_OK);
    CHECK(airplay_mirror_video_config_generation(video) == 2u);
    CHECK(airplay_mirror_video_waiting_for_keyframe(video));
    config[config_size - 1u] = 0u;
    CHECK(airplay_mirror_video_process_config(video, config, config_size - 1u, 204u) ==
          AIRPLAY_MIRROR_VIDEO_INVALID);
    CHECK(airplay_mirror_video_get_stats(video, &stats));
    CHECK(stats.config_ok == 2u && stats.config_failures == 1u);
    CHECK(stats.access_units_ok == 2u && stats.access_units_dropped == 3u &&
          stats.access_units_invalid == 1u && stats.keyframes == 1u);
    CHECK(stats.config_generation == 2u);
    airplay_mirror_video_destroy(video);
    free(config);
    free(idr);
    free(truncated);
}

static void write_le32(uint8_t output[4], uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

static void write_le64(uint8_t output[8], uint64_t value)
{
    for (unsigned index = 0u; index < 8u; ++index)
        output[index] = (uint8_t)(value >> (index * 8u));
}

static bool send_all(int socket_fd, const uint8_t *data, size_t size)
{
    size_t offset = 0u;
    while (offset < size)
    {
        ssize_t sent = send(socket_fd, data + offset, size - offset, 0);
        if (sent <= 0)
            return false;
        offset += (size_t)sent;
    }
    return true;
}

static bool send_packet(int socket_fd, uint8_t type, uint64_t timestamp,
                        const uint8_t *payload, size_t payload_size)
{
    uint8_t header[AIRPLAY_MIRROR_HEADER_SIZE] = {0};
    write_le32(header, (uint32_t)payload_size);
    header[4] = type;
    write_le64(header + 8u, timestamp);
    return send_all(socket_fd, header, sizeof(header)) &&
           send_all(socket_fd, payload, payload_size);
}

static int connect_session(uint16_t port)
{
    struct sockaddr_in address;
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (socket_fd < 0)
        return -1;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(socket_fd, (struct sockaddr *)&address, sizeof(address)) != 0)
    {
        close(socket_fd);
        return -1;
    }
    return socket_fd;
}

static void sleep_milliseconds(unsigned milliseconds)
{
    struct timespec duration;
    duration.tv_sec = (time_t)(milliseconds / 1000u);
    duration.tv_nsec = (long)(milliseconds % 1000u) * 1000000L;
    nanosleep(&duration, NULL);
}

static bool wait_for_count(Recorder *recorder, unsigned expected)
{
    for (unsigned attempt = 0u; attempt < 100u; ++attempt)
    {
        if (atomic_load_explicit(&recorder->count, memory_order_acquire) >= expected)
            return true;
        sleep_milliseconds(10u);
    }
    return false;
}

static void test_tcp_session(void)
{
    Recorder recorder = {0};
    AirPlayMirrorSessionConfig session_config = {0};
    AirPlayMirrorSession *session = NULL;
    AirPlayMirrorSessionStats stats = {0};
    AirPlayCryptoAesCtr aes = {0};
    uint8_t session_key[16];
    uint8_t key[16];
    uint8_t iv[16];
    uint8_t *config;
    uint8_t *idr;
    uint8_t *encrypted;
    size_t config_size;
    size_t idr_size;
    int socket_fd;

    config = read_hex("scripts/fixtures/airplay/mirror/h264-avcc.hex", &config_size);
    idr = read_hex("scripts/fixtures/airplay/mirror/h264-idr-au.hex", &idr_size);
    encrypted = malloc(idr_size);
    CHECK(config && idr && encrypted);
    for (size_t index = 0u; index < sizeof(session_key); ++index)
        session_key[index] = (uint8_t)index;
    session_config.session_id = 77u;
    session_config.session_key = session_key;
    session_config.stream_connection_id = 123456u;
    session_config.video_callback = record_video;
    session_config.video_user_data = &recorder;
    CHECK(airplay_mirror_session_create(&session_config, &session));
    CHECK(airplay_mirror_session_port(session) != 0u);
    airplay_mirror_session_set_recording(session, true);
    CHECK(airplay_mirror_session_derive_crypto(session_key, 123456u, key, iv));
    CHECK(airplay_crypto_aes_ctr_init(&aes, key, sizeof(key), iv));
    CHECK(airplay_crypto_aes_ctr_crypt(&aes, idr, encrypted, idr_size));
    socket_fd = connect_session(airplay_mirror_session_port(session));
    CHECK(socket_fd >= 0);
    if (socket_fd >= 0)
    {
        CHECK(send_packet(socket_fd, AIRPLAY_MIRROR_PACKET_CODEC, 500u,
                          config, config_size));
        CHECK(send_packet(socket_fd, AIRPLAY_MIRROR_PACKET_VIDEO, 500u,
                          encrypted, idr_size));
        CHECK(wait_for_count(&recorder, 1u));
        CHECK(recorder.keyframe && recorder.timestamp == 500u);
        close(socket_fd);
    }
    airplay_crypto_aes_ctr_deinit(&aes);

    CHECK(airplay_crypto_aes_ctr_init(&aes, key, sizeof(key), iv));
    CHECK(airplay_crypto_aes_ctr_crypt(&aes, idr, encrypted, idr_size));
    socket_fd = connect_session(airplay_mirror_session_port(session));
    CHECK(socket_fd >= 0);
    if (socket_fd >= 0)
    {
        CHECK(send_packet(socket_fd, AIRPLAY_MIRROR_PACKET_CODEC, 600u,
                          config, config_size));
        CHECK(send_packet(socket_fd, AIRPLAY_MIRROR_PACKET_VIDEO, 600u,
                          encrypted, idr_size));
        CHECK(wait_for_count(&recorder, 2u));
        CHECK(recorder.timestamp == 600u);
        close(socket_fd);
    }
    airplay_crypto_aes_ctr_deinit(&aes);
    CHECK(airplay_mirror_session_get_stats(session, &stats));
    CHECK(stats.connections_accepted == 2u);
    CHECK(stats.encrypted_packets == 2u && stats.decrypt_ok == 2u &&
          stats.decrypt_failures == 0u);
    CHECK(stats.video.config_ok == 2u && stats.video.access_units_ok == 2u &&
          stats.video.keyframes == 2u);
    airplay_mirror_session_destroy(session);
    free(config);
    free(idr);
    free(encrypted);
}

int main(void)
{
    test_header_and_crypto();
    test_video_parser();
    test_tcp_session();
    if (g_failures != 0)
    {
        fprintf(stderr, "AirPlay mirror tests failed: %d\n", g_failures);
        return 1;
    }
    puts("AirPlay mirror tests passed");
    return 0;
}
