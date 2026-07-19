#include <arpa/inet.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "protocol/airplay/discovery/mdns.h"

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

int main(int argc, char **argv)
{
    AirPlayMdnsConfig config = {0};
    char *end = NULL;
    unsigned long bind_port;
    unsigned long announcement_port;
    char instance_name[64];

    if (argc != 3)
        return 2;
    bind_port = strtoul(argv[1], &end, 10);
    if (!end || *end != '\0' || bind_port > UINT16_MAX)
        return 2;
    announcement_port = strtoul(argv[2], &end, 10);
    if (!end || *end != '\0' || announcement_port == 0u || announcement_port > UINT16_MAX)
        return 2;
    config.friendly_name = "NX-Cast";
    config.control_port = 7000u;
    config.ipv4_address = inet_addr("127.0.0.1");
    config.features = AIRPLAY_MDNS_FEATURE_LEGACY_PAIRING;
    config.pin_required = true;
    for (size_t index = 0u; index < sizeof(config.device_id); ++index)
        config.device_id[index] = (uint8_t)(0x10u + index);
    for (size_t index = 0u; index < sizeof(config.public_key); ++index)
        config.public_key[index] = (uint8_t)(0x80u + index);
    config.test_bind_port = (uint16_t)bind_port;
    config.test_announcement_port = (uint16_t)announcement_port;
    config.test_announcement_address = inet_addr("127.0.0.1");
    config.test_skip_multicast_join = true;
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    if (!airplay_mdns_start(&config) ||
        !airplay_mdns_instance_name(instance_name, sizeof(instance_name)))
        return 1;
    printf("READY %u %s\n", airplay_mdns_bound_port(), instance_name);
    fflush(stdout);
    while (!g_stop_requested && airplay_mdns_is_running())
        sleep_milliseconds(20u);
    airplay_mdns_stop();
    return 0;
}
