#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "protocol/airplay/mirror/timing.h"

static int g_failures;

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            g_failures++;                                                       \
        }                                                                       \
    } while (0)

static void sleep_milliseconds(unsigned milliseconds)
{
    struct timespec delay = {
        .tv_sec = (time_t)(milliseconds / 1000u),
        .tv_nsec = (long)(milliseconds % 1000u) * 1000000L};

    nanosleep(&delay, NULL);
}

int main(void)
{
    AirPlayMirrorTiming *timing = NULL;
    AirPlayMirrorTimingStats stats = {0};
    struct sockaddr_in local = {0};
    struct sockaddr_in sender = {0};
    struct timeval timeout = {.tv_sec = 2, .tv_usec = 0};
    socklen_t local_size = sizeof(local);
    socklen_t sender_size = sizeof(sender);
    uint8_t request[32] = {0};
    uint8_t response[32] = {0};
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    ssize_t received;

    CHECK(socket_fd >= 0);
    if (socket_fd < 0)
        return 1;
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(socket_fd, (const struct sockaddr *)&local, sizeof(local)) == 0);
    CHECK(getsockname(socket_fd, (struct sockaddr *)&local, &local_size) == 0);
    CHECK(setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                     sizeof(timeout)) == 0);
    CHECK(airplay_mirror_timing_create(htonl(INADDR_LOOPBACK),
                                       ntohs(local.sin_port), &timing));
    CHECK(airplay_mirror_timing_port(timing) != 0u);

    received = recvfrom(socket_fd, request, sizeof(request), 0,
                        (struct sockaddr *)&sender, &sender_size);
    CHECK(received == (ssize_t)sizeof(request));
    CHECK(request[0] == 0x80u && request[1] == 0xd2u &&
          request[2] == 0x00u && request[3] == 0x07u);
    response[0] = 0x80u;
    response[1] = 0xd3u;
    response[3] = 0x07u;
    CHECK(sendto(socket_fd, response, sizeof(response), 0,
                 (const struct sockaddr *)&sender, sender_size) ==
          (ssize_t)sizeof(response));

    for (unsigned attempt = 0u; attempt < 100u; ++attempt)
    {
        CHECK(airplay_mirror_timing_get_stats(timing, &stats));
        if (stats.responses_received != 0u)
            break;
        sleep_milliseconds(10u);
    }
    CHECK(stats.requests_sent == 1u);
    CHECK(stats.responses_received == 1u);

    airplay_mirror_timing_destroy(timing);
    close(socket_fd);
    if (g_failures != 0)
        return 1;
    printf("airplay timing tests passed\n");
    return 0;
}
