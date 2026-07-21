#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "protocol/airplay/discovery/mdns.h"
#include "protocol/airplay/media/remote_video.h"
#include "protocol/airplay/receiver.h"

static volatile sig_atomic_t g_stop_requested;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    g_stop_requested = 1;
}

static void sleep_milliseconds(unsigned milliseconds)
{
    struct timespec duration;

    duration.tv_sec = (time_t)(milliseconds / 1000u);
    duration.tv_nsec = (long)(milliseconds % 1000u) * 1000000L;
    nanosleep(&duration, NULL);
}

static void remove_test_storage(const char *directory)
{
    static const char *const names[] = {
        "identity.bin", "identity.bin.corrupt", "identity.bin.tmp",
        "pairings.bin", "pairings.bin.corrupt", "pairings.bin.tmp"};
    char path[512];

    for (size_t index = 0u; index < sizeof(names) / sizeof(names[0]); ++index)
    {
        int written = snprintf(path, sizeof(path), "%s/%s", directory, names[index]);
        if (written > 0 && (size_t)written < sizeof(path))
            unlink(path);
    }
    rmdir(directory);
}

static bool remote_load(const char *url, const char *metadata, void *user_data)
{
    (void)url;
    (void)metadata;
    (void)user_data;
    return true;
}

static bool remote_action(void *user_data)
{
    (void)user_data;
    return true;
}

static bool remote_seek(int position_ms, void *user_data)
{
    (void)position_ms;
    (void)user_data;
    return true;
}

static bool remote_snapshot(AirPlayRemoteVideoSnapshot *snapshot_out,
                            void *user_data)
{
    (void)user_data;
    if (!snapshot_out)
        return false;
    memset(snapshot_out, 0, sizeof(*snapshot_out));
    snapshot_out->state = AIRPLAY_REMOTE_VIDEO_STOPPED;
    return true;
}

static bool mirror_prepare(const AirPlayTransportSetup *setup,
                           uint16_t *timing_port_out, void *user_data)
{
    (void)setup;
    (void)user_data;
    if (!timing_port_out)
        return false;
    *timing_port_out = 7010u;
    return true;
}

static bool mirror_open(uint64_t session_id, const uint8_t key[16],
                        uint64_t stream_connection_id, uint16_t *data_port_out,
                        void *user_data)
{
    (void)session_id;
    (void)key;
    (void)stream_connection_id;
    (void)user_data;
    if (!data_port_out)
        return false;
    *data_port_out = 7011u;
    return true;
}

int main(int argc, char **argv)
{
    AirPlayReceiverConfig config = {0};
    AirPlayRemoteVideoOps remote_ops = {
        .load = remote_load,
        .play = remote_action,
        .pause = remote_action,
        .stop = remote_action,
        .seek_ms = remote_seek,
        .snapshot = remote_snapshot};
    AirPlayRemoteVideo *remote_video = NULL;
    char storage_template[] = "/tmp/nxcast-airplay-receiver-smoke.XXXXXX";
    char *storage_directory;
    unsigned long port = 0u;
    char *end = NULL;
    int result = 1;

    if (argc > 2)
        return 2;
    if (argc == 2)
    {
        port = strtoul(argv[1], &end, 10);
        if (!end || *end != '\0' || port > UINT16_MAX)
            return 2;
    }
    storage_directory = mkdtemp(storage_template);
    if (!storage_directory)
        return 1;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    if (!airplay_remote_video_create(&remote_ops, &remote_video))
        goto cleanup;
    config.friendly_name = "NX-Cast Test";
    config.storage_directory = storage_directory;
    config.control_port = (uint16_t)port;
    config.features = AIRPLAY_MDNS_FEATURE_VIDEO |
                      AIRPLAY_MDNS_FEATURE_HLS |
                      AIRPLAY_MDNS_FEATURE_SCREEN_MIRROR |
                      AIRPLAY_MDNS_FEATURE_SCREEN_ROTATE;
    config.enable_discovery = false;
    config.transport_prepare_callback = mirror_prepare;
    config.mirror_open_callback = mirror_open;
    config.remote_video = remote_video;
    if (!airplay_receiver_start(&config))
        goto cleanup;
    printf("READY %u\n", airplay_receiver_port());
    fflush(stdout);
    while (!g_stop_requested && airplay_receiver_is_running())
        sleep_milliseconds(20u);
    result = 0;

cleanup:
    airplay_receiver_stop();
    airplay_remote_video_destroy(remote_video);
    remove_test_storage(storage_directory);
    return result;
}
