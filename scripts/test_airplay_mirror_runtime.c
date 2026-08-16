#include <arpa/inet.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <mbedtls/aes.h>

#include "protocol/airplay/media/mirror_runtime.h"
#include "protocol/airplay/mirror/mirror_session.h"
#include "protocol/airplay/security/crypto.h"

static atomic_int g_failures;

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            atomic_fetch_add(&g_failures, 1);                                   \
        }                                                                       \
    } while (0)

typedef struct
{
    pthread_mutex_t mutex;
    AirPlayStreamBridge *bridge;
    unsigned bind_count;
    unsigned set_uri_count;
    unsigned play_count;
    unsigned stop_count;
    unsigned replace_count;
    unsigned status_count;
    AirPlayMirrorRuntimeStatus status;
    uint32_t generation;
    uint32_t bind_generation;
    uint32_t set_uri_generation;
    uint32_t play_generation;
    uint32_t stop_generation;
    uint32_t replace_previous_generation;
    uint32_t replace_generation;
} FakePlayer;

static bool fake_bind(AirPlayStreamBridge *bridge, uint32_t generation,
                      void *user_data)
{
    FakePlayer *player = user_data;
    AirPlayStreamBridge *previous;

    if (bridge)
        airplay_stream_bridge_retain(bridge);
    pthread_mutex_lock(&player->mutex);
    previous = player->bridge;
    player->bridge = bridge;
    player->bind_generation = generation;
    player->bind_count++;
    pthread_mutex_unlock(&player->mutex);
    if (previous)
    {
        airplay_stream_bridge_cancel(previous);
        airplay_stream_bridge_release(previous);
    }
    return true;
}

static bool fake_set_uri(const char *uri, const char *metadata,
                         uint32_t generation, void *user_data)
{
    FakePlayer *player = user_data;

    CHECK(uri && strcmp(uri, "airplay://mirror") == 0);
    CHECK(metadata && metadata[0] != '\0');
    pthread_mutex_lock(&player->mutex);
    player->set_uri_generation = generation;
    player->set_uri_count++;
    pthread_mutex_unlock(&player->mutex);
    return true;
}

static bool fake_play(uint32_t generation, void *user_data)
{
    FakePlayer *player = user_data;

    pthread_mutex_lock(&player->mutex);
    player->play_generation = generation;
    player->play_count++;
    pthread_mutex_unlock(&player->mutex);
    return true;
}

static bool fake_stop(uint32_t generation, void *user_data)
{
    FakePlayer *player = user_data;

    pthread_mutex_lock(&player->mutex);
    player->stop_generation = generation;
    player->stop_count++;
    pthread_mutex_unlock(&player->mutex);
    return true;
}

static bool fake_replace_generation(uint32_t previous_generation,
                                    uint32_t generation, void *user_data)
{
    FakePlayer *player = user_data;

    pthread_mutex_lock(&player->mutex);
    player->replace_previous_generation = previous_generation;
    player->replace_generation = generation;
    player->replace_count++;
    pthread_mutex_unlock(&player->mutex);
    return true;
}

static void fake_status(AirPlayMirrorRuntimeStatus status, uint32_t generation,
                        void *user_data)
{
    FakePlayer *player = user_data;

    pthread_mutex_lock(&player->mutex);
    player->status = status;
    player->generation = generation;
    player->status_count++;
    pthread_mutex_unlock(&player->mutex);
}

static void sleep_milliseconds(unsigned milliseconds)
{
    struct timespec duration = {
        .tv_sec = (time_t)(milliseconds / 1000u),
        .tv_nsec = (long)(milliseconds % 1000u) * 1000000L};
    nanosleep(&duration, NULL);
}

static bool wait_for_count(FakePlayer *player, unsigned *field, unsigned expected)
{
    for (unsigned attempt = 0u; attempt < 200u; ++attempt)
    {
        unsigned value;

        pthread_mutex_lock(&player->mutex);
        value = *field;
        pthread_mutex_unlock(&player->mutex);
        if (value >= expected)
            return true;
        sleep_milliseconds(5u);
    }
    return false;
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

static bool encrypt_audio_payload(const uint8_t key[16], const uint8_t iv[16],
                                  const uint8_t *input, uint8_t *output,
                                  size_t size)
{
    mbedtls_aes_context aes;
    uint8_t working_iv[16];
    size_t encrypted_size = size / 16u * 16u;
    int result;

    memcpy(working_iv, iv, sizeof(working_iv));
    mbedtls_aes_init(&aes);
    result = mbedtls_aes_setkey_enc(&aes, key, 128u);
    if (result == 0 && encrypted_size != 0u)
        result = mbedtls_aes_crypt_cbc(
            &aes, MBEDTLS_AES_ENCRYPT, encrypted_size, working_iv, input,
            output);
    mbedtls_aes_free(&aes);
    if (result != 0)
        return false;
    memcpy(output + encrypted_size, input + encrypted_size,
           size - encrypted_size);
    return true;
}

static size_t make_audio_packet(uint8_t *packet, size_t capacity,
                                uint16_t sequence, uint32_t timestamp,
                                const uint8_t *payload, size_t payload_size,
                                const uint8_t key[16], const uint8_t iv[16])
{
    if (capacity < AIRPLAY_MIRROR_AUDIO_RTP_HEADER_SIZE + payload_size)
        return 0u;
    memset(packet, 0, AIRPLAY_MIRROR_AUDIO_RTP_HEADER_SIZE + payload_size);
    packet[0] = 0x80u;
    packet[1] = 0x60u;
    packet[2] = (uint8_t)(sequence >> 8);
    packet[3] = (uint8_t)sequence;
    packet[4] = (uint8_t)(timestamp >> 24);
    packet[5] = (uint8_t)(timestamp >> 16);
    packet[6] = (uint8_t)(timestamp >> 8);
    packet[7] = (uint8_t)timestamp;
    if (!encrypt_audio_payload(
            key, iv, payload,
            packet + AIRPLAY_MIRROR_AUDIO_RTP_HEADER_SIZE, payload_size))
        return 0u;
    return AIRPLAY_MIRROR_AUDIO_RTP_HEADER_SIZE + payload_size;
}

static bool send_udp(uint16_t port, const uint8_t *packet, size_t size)
{
    struct sockaddr_in address;
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    bool ok;

    if (socket_fd < 0)
        return false;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok = sendto(socket_fd, packet, size, 0,
                (struct sockaddr *)&address, sizeof(address)) ==
         (ssize_t)size;
    close(socket_fd);
    return ok;
}

static bool send_audio_sync(uint16_t port, uint32_t rtp_timestamp,
                            uint64_t ntp_timestamp)
{
    uint8_t packet[20] = {0};

    packet[0] = 0x80u;
    packet[1] = 0xd4u;
    packet[4] = (uint8_t)(rtp_timestamp >> 24);
    packet[5] = (uint8_t)(rtp_timestamp >> 16);
    packet[6] = (uint8_t)(rtp_timestamp >> 8);
    packet[7] = (uint8_t)rtp_timestamp;
    for (unsigned index = 0u; index < 8u; ++index)
        packet[8u + index] =
            (uint8_t)(ntp_timestamp >> ((7u - index) * 8u));
    return send_udp(port, packet, sizeof(packet));
}

static int connect_local(uint16_t port)
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

static void run_cycle(AirPlayMirrorRuntime *runtime, FakePlayer *player,
                      const uint8_t *config, size_t config_size,
                      const uint8_t *idr, size_t idr_size, unsigned cycle)
{
    uint8_t session_key[16];
    uint8_t stream_key[16];
    uint8_t stream_iv[16];
    uint8_t *encrypted = malloc(idr_size);
    AirPlayCryptoAesCtr aes = {0};
    uint64_t session_id = 100u + cycle;
    uint64_t connection_id = 123456u + cycle;
    uint16_t port = 0u;
    uint32_t generation = 0u;
    unsigned expected_load = cycle + 1u;
    unsigned expected_play = cycle + 1u;
    unsigned expected_stop = cycle + 1u;
    int socket_fd;

    CHECK(encrypted != NULL);
    for (size_t index = 0u; index < sizeof(session_key); ++index)
        session_key[index] = (uint8_t)(index + cycle);
    CHECK(airplay_mirror_runtime_open(session_id, session_key, connection_id,
                                      &port, runtime));
    CHECK(port != 0u);
    CHECK(airplay_mirror_runtime_status(runtime, &generation) ==
          AIRPLAY_MIRROR_RUNTIME_PREPARING);
    CHECK(generation != 0u);
    pthread_mutex_lock(&player->mutex);
    CHECK(player->bind_count == cycle * 2u);
    CHECK(player->set_uri_count == cycle);
    CHECK(player->play_count == cycle);
    CHECK(player->stop_count == cycle);
    pthread_mutex_unlock(&player->mutex);
    airplay_mirror_runtime_record(session_id, runtime);
    CHECK(wait_for_count(player, &player->set_uri_count, expected_load));
    pthread_mutex_lock(&player->mutex);
    CHECK(player->bind_generation == generation);
    CHECK(player->set_uri_generation == generation);
    CHECK(player->play_count < expected_play);
    pthread_mutex_unlock(&player->mutex);
    CHECK(airplay_mirror_session_derive_crypto(session_key, connection_id,
                                               stream_key, stream_iv));
    CHECK(airplay_crypto_aes_ctr_init(&aes, stream_key, sizeof(stream_key), stream_iv));
    CHECK(airplay_crypto_aes_ctr_crypt(&aes, idr, encrypted, idr_size));
    socket_fd = connect_local(port);
    CHECK(socket_fd >= 0);
    if (socket_fd >= 0)
    {
        CHECK(send_packet(socket_fd, AIRPLAY_MIRROR_PACKET_CODEC,
                          UINT64_C(1) << 32, config, config_size));
        CHECK(send_packet(socket_fd, AIRPLAY_MIRROR_PACKET_VIDEO,
                          UINT64_C(1) << 32, encrypted, idr_size));
        CHECK(wait_for_count(player, &player->play_count, expected_play));
        pthread_mutex_lock(&player->mutex);
        CHECK(player->play_generation == generation);
        pthread_mutex_unlock(&player->mutex);
        close(socket_fd);
    }
    CHECK(airplay_mirror_runtime_status(runtime, NULL) ==
          AIRPLAY_MIRROR_RUNTIME_PLAYING);
    airplay_mirror_runtime_stop(session_id, runtime);
    CHECK(wait_for_count(player, &player->stop_count, expected_stop));
    pthread_mutex_lock(&player->mutex);
    CHECK(player->stop_generation == generation);
    CHECK(player->bind_generation == generation);
    pthread_mutex_unlock(&player->mutex);
    airplay_crypto_aes_ctr_deinit(&aes);
    free(encrypted);
}

static void run_audio_first_cycles(AirPlayMirrorRuntime *runtime,
                                   FakePlayer *player,
                                   const uint8_t *avcc, size_t avcc_size,
                                   const uint8_t *idr, size_t idr_size,
                                   const uint8_t *audio_payload,
                                   size_t audio_payload_size,
                                   unsigned completed_cycles)
{
    uint8_t key[16];
    uint8_t iv[16];
    uint8_t audio_packet[AIRPLAY_MIRROR_AUDIO_MAX_PACKET];
    uint8_t stream_key[16];
    uint8_t stream_iv[16];
    uint8_t *encrypted_video = malloc(idr_size);
    AirPlayCryptoAesCtr aes = {0};
    uint16_t timing_port = 0u;
    uint16_t audio_port = 0u;
    uint16_t control_port = 0u;
    uint16_t mirror_port = 0u;
    uint64_t audio_only_session = 900u;
    uint64_t mirror_session = 901u;
    uint64_t connection_id = UINT64_C(0x1020304050607080);
    uint32_t audio_generation = 0u;
    uint32_t combined_generation = 0u;
    size_t audio_packet_size;
    int socket_fd;

    CHECK(encrypted_video != NULL);

    for (size_t index = 0u; index < sizeof(key); ++index)
    {
        key[index] = (uint8_t)(0x20u + index);
        iv[index] = (uint8_t)(0x40u + index);
    }

    CHECK(airplay_mirror_runtime_transport_prepare(
        audio_only_session, key, iv, 0u, 0u, false, &timing_port, runtime));
    CHECK(timing_port == 0u);
    CHECK(airplay_mirror_runtime_audio_open(
        audio_only_session, key, iv, AIRPLAY_MIRROR_AUDIO_CT_AAC_LC, 1024u,
        44100u,
        &audio_port, &control_port, runtime));
    CHECK(audio_port != 0u && control_port != 0u);
    CHECK(airplay_mirror_runtime_status(runtime, &audio_generation) ==
          AIRPLAY_MIRROR_RUNTIME_PREPARING);
    CHECK(airplay_mirror_runtime_profile(runtime) ==
          AIRPLAY_STREAM_BRIDGE_PROFILE_AUDIO_ONLY);
    airplay_mirror_runtime_record(audio_only_session, runtime);
    CHECK(wait_for_count(player, &player->set_uri_count,
                         completed_cycles + 1u));
    CHECK(send_audio_sync(control_port, 44100u,
                          UINT64_C(100) << 32));
    sleep_milliseconds(20u);
    audio_packet_size = make_audio_packet(
        audio_packet, sizeof(audio_packet), 1u, 44100u, audio_payload,
        audio_payload_size, key, iv);
    CHECK(audio_packet_size != 0u &&
          send_udp(audio_port, audio_packet, audio_packet_size));
    CHECK(wait_for_count(player, &player->play_count,
                         completed_cycles + 1u));
    airplay_mirror_runtime_stop(audio_only_session, runtime);
    CHECK(wait_for_count(player, &player->stop_count,
                         completed_cycles + 1u));

    timing_port = 0u;
    audio_port = 0u;
    control_port = 0u;
    CHECK(airplay_mirror_runtime_transport_prepare(
        mirror_session, key, iv, 0u, 0u, false, &timing_port, runtime));
    CHECK(airplay_mirror_runtime_audio_open(
        mirror_session, key, iv, AIRPLAY_MIRROR_AUDIO_CT_ALAC, 352u, 44100u,
        &audio_port, &control_port, runtime));
    airplay_mirror_runtime_record(mirror_session, runtime);
    CHECK(wait_for_count(player, &player->set_uri_count,
                         completed_cycles + 2u));
    CHECK(airplay_mirror_runtime_open(
        mirror_session, key, connection_id, &mirror_port, runtime));
    CHECK(mirror_port != 0u);
    CHECK(wait_for_count(player, &player->replace_count, 1u));
    CHECK(wait_for_count(player, &player->set_uri_count,
                         completed_cycles + 3u));
    CHECK(airplay_mirror_runtime_profile(runtime) ==
          AIRPLAY_STREAM_BRIDGE_PROFILE_VIDEO_AUDIO);
    CHECK(airplay_mirror_runtime_status(runtime, &combined_generation) ==
          AIRPLAY_MIRROR_RUNTIME_WAITING_KEYFRAME);
    pthread_mutex_lock(&player->mutex);
    CHECK(player->replace_previous_generation == audio_generation + 1u);
    CHECK(player->replace_generation == combined_generation);
    pthread_mutex_unlock(&player->mutex);

    CHECK(airplay_mirror_session_derive_crypto(
        key, connection_id, stream_key, stream_iv));
    CHECK(airplay_crypto_aes_ctr_init(&aes, stream_key, sizeof(stream_key),
                                      stream_iv));
    CHECK(encrypted_video && airplay_crypto_aes_ctr_crypt(
                                 &aes, idr, encrypted_video, idr_size));
    socket_fd = connect_local(mirror_port);
    CHECK(socket_fd >= 0);
    if (socket_fd >= 0)
    {
        CHECK(send_packet(socket_fd, AIRPLAY_MIRROR_PACKET_CODEC,
                          UINT64_C(1) << 32, avcc, avcc_size));
        CHECK(send_packet(socket_fd, AIRPLAY_MIRROR_PACKET_VIDEO,
                          UINT64_C(1) << 32, encrypted_video, idr_size));
        CHECK(wait_for_count(player, &player->play_count,
                             completed_cycles + 2u));
        close(socket_fd);
    }
    airplay_mirror_runtime_stop(mirror_session, runtime);
    CHECK(wait_for_count(player, &player->stop_count,
                         completed_cycles + 3u));
    airplay_crypto_aes_ctr_deinit(&aes);
    free(encrypted_video);
}

int main(void)
{
    FakePlayer player = {0};
    AirPlayMirrorRuntimeConfig config = {0};
    AirPlayMirrorRuntime *runtime = NULL;
    uint8_t *avcc = NULL;
    uint8_t *idr = NULL;
    uint8_t *aac = NULL;
    size_t avcc_size = 0u;
    size_t idr_size = 0u;
    size_t aac_size = 0u;

    atomic_init(&g_failures, 0);
    CHECK(pthread_mutex_init(&player.mutex, NULL) == 0);
    config.player.bind_stream = fake_bind;
    config.player.set_uri = fake_set_uri;
    config.player.play = fake_play;
    config.player.stop = fake_stop;
    config.player.replace_generation = fake_replace_generation;
    config.player.status_changed = fake_status;
    config.player.user_data = &player;
    config.stream_capacity = AIRPLAY_STREAM_BRIDGE_MIN_CAPACITY;
    avcc = read_hex("scripts/fixtures/airplay/mirror/h264-avcc.hex", &avcc_size);
    idr = read_hex("scripts/fixtures/airplay/mirror/h264-idr-au.hex", &idr_size);
    aac = read_hex("scripts/fixtures/airplay/mirror/aac-lc-frame.hex",
                   &aac_size);
    CHECK(avcc && idr && aac);
    CHECK(airplay_mirror_runtime_create(&config, &runtime));
    if (runtime && avcc && idr && aac)
    {
        for (unsigned cycle = 0u; cycle < 10u; ++cycle)
            run_cycle(runtime, &player, avcc, avcc_size, idr, idr_size, cycle);
        run_audio_first_cycles(runtime, &player, avcc, avcc_size, idr,
                               idr_size, aac, aac_size, 10u);
    }
    airplay_mirror_runtime_destroy(runtime);
    fake_bind(NULL, player.generation, &player);
    pthread_mutex_destroy(&player.mutex);
    free(avcc);
    free(idr);
    free(aac);
    if (atomic_load(&g_failures) != 0)
    {
        fprintf(stderr, "%d AirPlay mirror runtime checks failed\n",
                atomic_load(&g_failures));
        return 1;
    }
    puts("AirPlay mirror runtime checks passed");
    return 0;
}
