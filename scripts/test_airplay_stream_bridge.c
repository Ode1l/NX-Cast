#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "protocol/airplay/media/stream_bridge.h"
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
    uint8_t data[4096];
    size_t size;
} AccessUnitFixture;

typedef struct
{
    AirPlayStreamBridge *bridge;
    const AccessUnitFixture *fixture;
    unsigned count;
    atomic_bool finished;
    atomic_bool result;
} Producer;

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

static void capture_access_unit(const AirPlayMirrorAccessUnit *access_unit,
                                void *user_data)
{
    AccessUnitFixture *fixture = user_data;

    CHECK(access_unit && access_unit->size <= sizeof(fixture->data));
    if (access_unit && access_unit->size <= sizeof(fixture->data))
    {
        memcpy(fixture->data, access_unit->data, access_unit->size);
        fixture->size = access_unit->size;
    }
}

static bool load_access_unit(AccessUnitFixture *fixture)
{
    AirPlayMirrorVideo *video = NULL;
    uint8_t *config = NULL;
    uint8_t *idr = NULL;
    size_t config_size = 0u;
    size_t idr_size = 0u;
    bool ok = false;

    config = read_hex("scripts/fixtures/airplay/mirror/h264-avcc.hex", &config_size);
    idr = read_hex("scripts/fixtures/airplay/mirror/h264-idr-au.hex", &idr_size);
    if (!config || !idr ||
        !airplay_mirror_video_create(capture_access_unit, fixture, &video))
        goto cleanup;
    ok = airplay_mirror_video_process_config(video, config, config_size, 0u) ==
             AIRPLAY_MIRROR_VIDEO_OK &&
         airplay_mirror_video_process_access_unit(video, idr, idr_size, 0u) ==
             AIRPLAY_MIRROR_VIDEO_OK &&
         fixture->size > 0u;

cleanup:
    airplay_mirror_video_destroy(video);
    free(config);
    free(idr);
    return ok;
}

static void *produce_video(void *opaque)
{
    Producer *producer = opaque;
    AirPlayMirrorAccessUnit access_unit = {0};
    bool ok = true;

    access_unit.data = producer->fixture->data;
    access_unit.size = producer->fixture->size;
    access_unit.config_generation = 1u;
    access_unit.keyframe = true;
    for (unsigned index = 0u; index < producer->count; ++index)
    {
        access_unit.timestamp = (UINT64_C(1) << 32) +
                                ((uint64_t)index << 32) / 30u;
        if (!airplay_stream_bridge_push_video(producer->bridge, &access_unit))
        {
            ok = false;
            break;
        }
    }
    if (ok)
        ok = airplay_stream_bridge_finish(producer->bridge);
    atomic_store(&producer->result, ok);
    atomic_store(&producer->finished, true);
    return NULL;
}

static void sleep_milliseconds(unsigned milliseconds)
{
    struct timespec duration = {
        .tv_sec = (time_t)(milliseconds / 1000u),
        .tv_nsec = (long)(milliseconds % 1000u) * 1000000L};
    nanosleep(&duration, NULL);
}

static void test_stream_and_wrap(const AccessUnitFixture *fixture)
{
    AirPlayStreamBridge *bridge = NULL;
    AirPlayStreamBridgeStats stats = {0};
    Producer producer = {0};
    pthread_t thread;
    FILE *output;
    uint8_t buffer[257];
    uint64_t drained = 0u;
    int64_t amount;

    CHECK(airplay_stream_bridge_ntp_to_90k(UINT64_C(1) << 32) == 90000);
    CHECK(airplay_stream_bridge_create(AIRPLAY_STREAM_BRIDGE_MIN_CAPACITY, &bridge));
    CHECK(airplay_stream_bridge_claim_reader(bridge));
    CHECK(!airplay_stream_bridge_claim_reader(bridge));
    producer.bridge = bridge;
    producer.fixture = fixture;
    producer.count = 300u;
    atomic_init(&producer.finished, false);
    atomic_init(&producer.result, false);
    CHECK(pthread_create(&thread, NULL, produce_video, &producer) == 0);
    output = fopen("build/tests/airplay-mirror.ts", "wb");
    CHECK(output != NULL);
    while ((amount = airplay_stream_bridge_read(bridge, buffer, sizeof(buffer))) > 0)
    {
        drained += (uint64_t)amount;
        if (output)
            CHECK(fwrite(buffer, 1u, (size_t)amount, output) == (size_t)amount);
    }
    CHECK(amount == 0);
    if (output)
        fclose(output);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(atomic_load(&producer.finished));
    CHECK(atomic_load(&producer.result));
    CHECK(airplay_stream_bridge_get_stats(bridge, &stats));
    CHECK(stats.video_packets == producer.count);
    CHECK(stats.bytes_written == drained && stats.bytes_read == drained);
    CHECK(stats.eof && !stats.cancelled && stats.buffered == 0u);
    airplay_stream_bridge_release_reader(bridge);
    airplay_stream_bridge_release(bridge);
}

static void test_cancel_and_restart(const AccessUnitFixture *fixture)
{
    AirPlayStreamBridge *bridge = NULL;
    AirPlayMirrorAccessUnit access_unit = {0};
    Producer producer = {0};
    pthread_t thread;
    uint8_t byte;

    CHECK(airplay_stream_bridge_create(AIRPLAY_STREAM_BRIDGE_MIN_CAPACITY, &bridge));
    producer.bridge = bridge;
    producer.fixture = fixture;
    producer.count = 100000u;
    atomic_init(&producer.finished, false);
    atomic_init(&producer.result, true);
    CHECK(pthread_create(&thread, NULL, produce_video, &producer) == 0);
    sleep_milliseconds(50u);
    CHECK(!atomic_load(&producer.finished));
    airplay_stream_bridge_cancel(bridge);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(atomic_load(&producer.finished));
    CHECK(!atomic_load(&producer.result));
    CHECK(airplay_stream_bridge_read(bridge, &byte, 1u) == -1);
    airplay_stream_bridge_release(bridge);

    bridge = NULL;
    CHECK(airplay_stream_bridge_create(AIRPLAY_STREAM_BRIDGE_MIN_CAPACITY, &bridge));
    airplay_stream_bridge_retain(bridge);
    airplay_stream_bridge_release(bridge);
    access_unit.data = fixture->data;
    access_unit.size = fixture->size;
    access_unit.timestamp = UINT64_C(1) << 32;
    access_unit.config_generation = 2u;
    CHECK(!airplay_stream_bridge_push_video(bridge, &access_unit));
    access_unit.keyframe = true;
    CHECK(airplay_stream_bridge_push_video(bridge, &access_unit));
    airplay_stream_bridge_cancel(bridge);
    airplay_stream_bridge_release(bridge);
}

int main(void)
{
    AccessUnitFixture fixture = {0};

    CHECK(load_access_unit(&fixture));
    if (fixture.size > 0u)
    {
        test_stream_and_wrap(&fixture);
        test_cancel_and_restart(&fixture);
    }
    if (g_failures)
    {
        fprintf(stderr, "%d AirPlay stream bridge checks failed\n", g_failures);
        return 1;
    }
    puts("AirPlay stream bridge checks passed");
    return 0;
}
