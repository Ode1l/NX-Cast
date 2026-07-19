#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "protocol/airplay/discovery/mdns.h"
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

int main(int argc, char **argv)
{
    AirPlayReceiverConfig config = {0};
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
    config.friendly_name = "NX-Cast Test";
    config.storage_directory = storage_directory;
    config.control_port = (uint16_t)port;
    config.features = AIRPLAY_MDNS_FEATURE_LEGACY_PAIRING;
    config.enable_discovery = false;
    if (!airplay_receiver_start(&config))
        goto cleanup;
    printf("READY %u\n", airplay_receiver_port());
    fflush(stdout);
    while (!g_stop_requested && airplay_receiver_is_running())
        sleep_milliseconds(20u);
    result = 0;

cleanup:
    airplay_receiver_stop();
    remove_test_storage(storage_directory);
    return result;
}
