#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "protocol/airplay/security/pairing.h"
#include "protocol/airplay/server.h"

static volatile sig_atomic_t g_stop_requested;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    g_stop_requested = 1;
}

static void sleep_milliseconds(unsigned milliseconds)
{
    struct timespec duration;
    duration.tv_sec = (time_t)(milliseconds / 1000U);
    duration.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
    nanosleep(&duration, NULL);
}

static void remove_test_storage(const char *directory)
{
    static const char *const names[] = {
        "identity.bin",
        "identity.bin.corrupt",
        "identity.bin.tmp",
        "pairings.bin",
        "pairings.bin.corrupt",
        "pairings.bin.tmp",
    };
    size_t index;
    char path[512];

    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index)
    {
        if (snprintf(path, sizeof(path), "%s/%s", directory, names[index]) > 0)
            unlink(path);
    }
    rmdir(directory);
}

int main(int argc, char **argv)
{
    AirPlayPairingConfig pairing_config = {0};
    AirPlayPairingService *pairing_service = NULL;
    AirPlayServerConfig server_config = {0};
    char storage_template[] = "/tmp/nxcast-airplay-pairing-smoke.XXXXXX";
    char *storage_directory = NULL;
    unsigned long port = 7000;
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

    pairing_config.storage_directory = storage_directory;
    if (!airplay_pairing_service_create(&pairing_config, &pairing_service))
        goto cleanup;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    server_config.port = (uint16_t)port;
    server_config.request_timeout_ms = 500U;
    server_config.send_timeout_ms = 1000U;
    server_config.route_handler = airplay_pairing_route;
    server_config.route_user_data = pairing_service;
    server_config.session_closed_handler = airplay_pairing_session_closed;
    if (!airplay_server_start(&server_config))
    {
        fputs("FAILED\n", stderr);
        goto cleanup;
    }

    printf("READY %u\n", airplay_server_port());
    fflush(stdout);
    while (!g_stop_requested && airplay_server_is_running())
        sleep_milliseconds(20U);
    airplay_server_stop();
    result = 0;

cleanup:
    airplay_server_stop();
    airplay_pairing_service_destroy(pairing_service);
    remove_test_storage(storage_directory);
    return result;
}
