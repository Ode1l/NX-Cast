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
    unsigned status_count;
    AirPlayMirrorRuntimeStatus status;
    uint32_t generation;
} FakePlayer;

static bool fake_bind(AirPlayStreamBridge *bridge, void *user_data)
{
    FakePlayer *player = user_data;
    AirPlayStreamBridge *previous;

    if (bridge)
        airplay_stream_bridge_retain(bridge);
    pthread_mutex_lock(&player->mutex);
    previous = player->bridge;
    player->bridge = bridge;
    player->bind_count++;
    pthread_mutex_unlock(&player->mutex);
    if (previous)
    {
        airplay_stream_bridge_cancel(previous);
        airplay_stream_bridge_release(previous);
    }
    return true;
}

static bool fake_set_uri(const char *uri, const char *metadata, void *user_data)
{
    FakePlayer *player = user_data;

    CHECK(uri && strcmp(uri, "airplay://mirror") == 0);
    CHECK(metadata && metadata[0] != '\0');
    pthread_mutex_lock(&player->mutex);
    player->set_uri_count++;
    pthread_mutex_unlock(&player->mutex);
    return true;
}

static bool fake_play(void *user_data)
{
    FakePlayer *player = user_data;

    pthread_mutex_lock(&player->mutex);
    player->play_count++;
    pthread_mutex_unlock(&player->mutex);
    return true;
}

static bool fake_stop(void *user_data)
{
    FakePlayer *player = user_data;

    pthread_mutex_lock(&player->mutex);
    player->stop_count++;
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
    pthread_mutex_lock(&player->mutex);
    CHECK(player->bind_count == cycle * 2u);
    CHECK(player->set_uri_count == cycle);
    CHECK(player->play_count == cycle);
    CHECK(player->stop_count == cycle);
    pthread_mutex_unlock(&player->mutex);
    airplay_mirror_runtime_record(session_id, runtime);
    CHECK(wait_for_count(player, &player->set_uri_count, expected_load));
    pthread_mutex_lock(&player->mutex);
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
        close(socket_fd);
    }
    CHECK(airplay_mirror_runtime_status(runtime, NULL) ==
          AIRPLAY_MIRROR_RUNTIME_PLAYING);
    airplay_mirror_runtime_stop(session_id, runtime);
    CHECK(wait_for_count(player, &player->stop_count, expected_stop));
    airplay_crypto_aes_ctr_deinit(&aes);
    free(encrypted);
}

static void run_audio_first_cycles(AirPlayMirrorRuntime *runtime,
                                   FakePlayer *player,
                                   unsigned completed_cycles)
{
    uint8_t key[16];
    uint8_t iv[16];
    uint16_t timing_port = 0u;
    uint16_t audio_port = 0u;
    uint16_t control_port = 0u;
    uint16_t mirror_port = 0u;
    uint64_t audio_only_session = 900u;
    uint64_t mirror_session = 901u;

    for (size_t index = 0u; index < sizeof(key); ++index)
    {
        key[index] = (uint8_t)(0x20u + index);
        iv[index] = (uint8_t)(0x40u + index);
    }

    CHECK(airplay_mirror_runtime_transport_prepare(
        audio_only_session, key, iv, 0u, 0u, false, &timing_port, runtime));
    CHECK(timing_port == 0u);
    CHECK(airplay_mirror_runtime_audio_open(
        audio_only_session, key, iv, 8u, 480u, 44100u,
        &audio_port, &control_port, runtime));
    CHECK(audio_port != 0u && control_port != 0u);
    airplay_mirror_runtime_stop(audio_only_session, runtime);
    pthread_mutex_lock(&player->mutex);
    CHECK(player->stop_count == completed_cycles);
    pthread_mutex_unlock(&player->mutex);

    timing_port = 0u;
    audio_port = 0u;
    control_port = 0u;
    CHECK(airplay_mirror_runtime_transport_prepare(
        mirror_session, key, iv, 0u, 0u, false, &timing_port, runtime));
    CHECK(airplay_mirror_runtime_audio_open(
        mirror_session, key, iv, 8u, 480u, 44100u,
        &audio_port, &control_port, runtime));
    CHECK(airplay_mirror_runtime_open(
        mirror_session, key, UINT64_C(0x1020304050607080),
        &mirror_port, runtime));
    CHECK(mirror_port != 0u);
    airplay_mirror_runtime_record(mirror_session, runtime);
    CHECK(wait_for_count(player, &player->set_uri_count,
                         completed_cycles + 1u));
    airplay_mirror_runtime_stop(mirror_session, runtime);
    CHECK(wait_for_count(player, &player->stop_count,
                         completed_cycles + 1u));
}

int main(void)
{
    FakePlayer player = {0};
    AirPlayMirrorRuntimeConfig config = {0};
    AirPlayMirrorRuntime *runtime = NULL;
    uint8_t *avcc = NULL;
    uint8_t *idr = NULL;
    size_t avcc_size = 0u;
    size_t idr_size = 0u;

    atomic_init(&g_failures, 0);
    CHECK(pthread_mutex_init(&player.mutex, NULL) == 0);
    config.player.bind_stream = fake_bind;
    config.player.set_uri = fake_set_uri;
    config.player.play = fake_play;
    config.player.stop = fake_stop;
    config.player.status_changed = fake_status;
    config.player.user_data = &player;
    config.stream_capacity = AIRPLAY_STREAM_BRIDGE_MIN_CAPACITY;
    avcc = read_hex("scripts/fixtures/airplay/mirror/h264-avcc.hex", &avcc_size);
    idr = read_hex("scripts/fixtures/airplay/mirror/h264-idr-au.hex", &idr_size);
    CHECK(avcc && idr);
    CHECK(airplay_mirror_runtime_create(&config, &runtime));
    if (runtime && avcc && idr)
    {
        for (unsigned cycle = 0u; cycle < 10u; ++cycle)
            run_cycle(runtime, &player, avcc, avcc_size, idr, idr_size, cycle);
        run_audio_first_cycles(runtime, &player, 10u);
    }
    airplay_mirror_runtime_destroy(runtime);
    fake_bind(NULL, &player);
    pthread_mutex_destroy(&player.mutex);
    free(avcc);
    free(idr);
    if (atomic_load(&g_failures) != 0)
    {
        fprintf(stderr, "%d AirPlay mirror runtime checks failed\n",
                atomic_load(&g_failures));
        return 1;
    }
    puts("AirPlay mirror runtime checks passed");
    return 0;
}
