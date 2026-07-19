#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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

int main(int argc, char **argv)
{
    AirPlayServerConfig config = {0};
    unsigned long port = 7000;
    char *end = NULL;

    if (argc > 2)
        return 2;
    if (argc == 2)
    {
        port = strtoul(argv[1], &end, 10);
        if (!end || *end != '\0' || port > UINT16_MAX)
            return 2;
    }
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    config.port = (uint16_t)port;
    config.request_timeout_ms = 500U;
    config.send_timeout_ms = 1000U;
    if (!airplay_server_start(&config))
    {
        fputs("FAILED\n", stderr);
        return 1;
    }
    printf("READY %u\n", airplay_server_port());
    fflush(stdout);
    while (!g_stop_requested && airplay_server_is_running())
        sleep_milliseconds(20U);
    airplay_server_stop();
    return 0;
}
