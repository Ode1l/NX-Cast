#include "timing.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifdef __SWITCH__
#include <switch.h>
typedef Thread AirPlayTimingThread;
typedef void (*AirPlayTimingThreadEntry)(void *argument);
#define AIRPLAY_TIMING_THREAD_RETURN void
#define AIRPLAY_TIMING_THREAD_FINISH() return
#define AIRPLAY_TIMING_THREAD_STACK_SIZE 0x10000u
#else
#include <pthread.h>
typedef pthread_t AirPlayTimingThread;
typedef void *(*AirPlayTimingThreadEntry)(void *argument);
#define AIRPLAY_TIMING_THREAD_RETURN void *
#define AIRPLAY_TIMING_THREAD_FINISH() return NULL
#endif

#include "protocol/airplay/trace.h"
#include "protocol/airplay/diagnostics.h"

#define AIRPLAY_TIMING_PACKET_SIZE 32u
#define AIRPLAY_TIMING_RESPONSE_TIMEOUT_MS 300u
#define AIRPLAY_TIMING_INTERVAL_MS 3000u
#define AIRPLAY_TIMING_SLEEP_SLICE_MS 100u

struct AirPlayMirrorTiming
{
    struct sockaddr_in peer;
    AirPlayTimingThread thread;
    atomic_bool running;
    atomic_int socket_fd;
    atomic_uint_fast64_t requests_sent;
    atomic_uint_fast64_t responses_received;
    uint16_t local_port;
    bool thread_started;
    uint32_t diagnostic_thread_generation;
};

static uint64_t timing_now_ntp(void)
{
    uint64_t seconds;
    uint64_t nanoseconds;

#ifdef __SWITCH__
    uint64_t now_ns = armTicksToNs(armGetSystemTick());

    seconds = now_ns / UINT64_C(1000000000);
    nanoseconds = now_ns % UINT64_C(1000000000);
#else
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0u;
    seconds = (uint64_t)now.tv_sec;
    nanoseconds = (uint64_t)now.tv_nsec;
#endif
    return (seconds << 32) |
           ((nanoseconds << 32) / UINT64_C(1000000000));
}

static void write_be64(uint8_t *output, uint64_t value)
{
    for (unsigned index = 0u; index < 8u; ++index)
        output[index] = (uint8_t)(value >> ((7u - index) * 8u));
}

static void timing_sleep_ms(uint32_t milliseconds)
{
#ifdef __SWITCH__
    svcSleepThread((int64_t)milliseconds * 1000000LL);
#else
    struct timespec delay = {
        .tv_sec = (time_t)(milliseconds / 1000u),
        .tv_nsec = (long)(milliseconds % 1000u) * 1000000L};

    while (nanosleep(&delay, &delay) != 0 && errno == EINTR)
    {
    }
#endif
}

static bool timing_thread_start(AirPlayTimingThread *thread,
                                AirPlayTimingThreadEntry entry,
                                void *argument)
{
#ifdef __SWITCH__
    Result result = threadCreate(thread, entry, argument, NULL,
                                 AIRPLAY_TIMING_THREAD_STACK_SIZE, 0x2b, -2);

    if (R_FAILED(result))
        return false;
    result = threadStart(thread);
    if (R_FAILED(result))
    {
        threadClose(thread);
        return false;
    }
    return true;
#else
    return pthread_create(thread, NULL, entry, argument) == 0;
#endif
}

static void timing_thread_join(AirPlayTimingThread *thread)
{
#ifdef __SWITCH__
    threadWaitForExit(thread);
    threadClose(thread);
#else
    pthread_join(*thread, NULL);
#endif
}

static void timing_close_socket(AirPlayMirrorTiming *timing)
{
    int socket_fd;

    if (!timing)
        return;
    socket_fd = atomic_exchange(&timing->socket_fd, -1);
    if (socket_fd >= 0)
    {
        close(socket_fd);
        airplay_diagnostics_socket_closed(
            NETWORK_DIAGNOSTIC_AIRPLAY_TIMING);
    }
}

static void timing_wait_response(AirPlayMirrorTiming *timing, int socket_fd)
{
    uint8_t response[128];
    fd_set read_fds;
    struct timeval timeout = {
        .tv_sec = 0,
        .tv_usec = AIRPLAY_TIMING_RESPONSE_TIMEOUT_MS * 1000u};
    int selected;

    FD_ZERO(&read_fds);
    FD_SET(socket_fd, &read_fds);
    selected = select(socket_fd + 1, &read_fds, NULL, NULL, &timeout);
    if (selected <= 0 || !FD_ISSET(socket_fd, &read_fds))
        return;
    if (recvfrom(socket_fd, response, sizeof(response), 0, NULL, NULL) >=
        (ssize_t)AIRPLAY_TIMING_PACKET_SIZE)
    {
        uint64_t count = atomic_fetch_add(&timing->responses_received, 1u) + 1u;

        if (count == 1u)
            AIRPLAY_TRACE("[airplay-timing] first NTP response received\n");
    }
}

static AIRPLAY_TIMING_THREAD_RETURN timing_worker(void *argument)
{
    AirPlayMirrorTiming *timing = argument;

    while (atomic_load(&timing->running))
    {
        uint8_t request[AIRPLAY_TIMING_PACKET_SIZE] = {
            0x80u, 0xd2u, 0x00u, 0x07u};
        int socket_fd = atomic_load(&timing->socket_fd);

        if (socket_fd < 0)
            break;
        write_be64(request + 24u, timing_now_ntp());
        if (sendto(socket_fd, request, sizeof(request), 0,
                   (const struct sockaddr *)&timing->peer,
                   sizeof(timing->peer)) == (ssize_t)sizeof(request))
        {
            atomic_fetch_add(&timing->requests_sent, 1u);
            timing_wait_response(timing, socket_fd);
        }
        for (uint32_t waited = 0u;
             waited < AIRPLAY_TIMING_INTERVAL_MS &&
             atomic_load(&timing->running);
             waited += AIRPLAY_TIMING_SLEEP_SLICE_MS)
        {
            timing_sleep_ms(AIRPLAY_TIMING_SLEEP_SLICE_MS);
        }
    }
    AIRPLAY_TIMING_THREAD_FINISH();
}

bool airplay_mirror_timing_create(uint32_t peer_ipv4_address,
                                  uint16_t peer_port,
                                  AirPlayMirrorTiming **timing_out)
{
    AirPlayMirrorTiming *timing;
    struct sockaddr_in local;
    socklen_t local_size = sizeof(local);
    int socket_fd;

    if (!timing_out || *timing_out || peer_ipv4_address == 0u || peer_port == 0u)
        return false;
    timing = calloc(1, sizeof(*timing));
    if (!timing)
        return false;
    atomic_init(&timing->running, false);
    atomic_init(&timing->socket_fd, -1);
    atomic_init(&timing->requests_sent, 0u);
    atomic_init(&timing->responses_received, 0u);
    memset(&timing->peer, 0, sizeof(timing->peer));
    timing->peer.sin_family = AF_INET;
    timing->peer.sin_addr.s_addr = peer_ipv4_address;
    timing->peer.sin_port = htons(peer_port);

    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0)
    {
        AIRPLAY_OBSERVE(
            "[airplay-setup-failure] session=0 stream=timing stage=socket-data\n");
        goto failure;
    }
    airplay_diagnostics_socket_opened(NETWORK_DIAGNOSTIC_AIRPLAY_TIMING);
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(0u);
    if (bind(socket_fd, (const struct sockaddr *)&local, sizeof(local)) != 0 ||
        getsockname(socket_fd, (struct sockaddr *)&local, &local_size) != 0)
    {
        close(socket_fd);
        airplay_diagnostics_socket_closed(
            NETWORK_DIAGNOSTIC_AIRPLAY_TIMING);
        AIRPLAY_OBSERVE(
            "[airplay-setup-failure] session=0 stream=timing stage=socket-data\n");
        goto failure;
    }
    timing->local_port = ntohs(local.sin_port);
    atomic_store(&timing->socket_fd, socket_fd);
    atomic_store(&timing->running, true);
    if (!timing_thread_start(&timing->thread, timing_worker, timing))
    {
        airplay_diagnostics_thread_create_failed(
            RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_TIMING);
        AIRPLAY_OBSERVE(
            "[airplay-setup-failure] session=0 stream=timing stage=thread-create\n");
        atomic_store(&timing->running, false);
        timing_close_socket(timing);
        goto failure;
    }
    timing->thread_started = true;
    timing->diagnostic_thread_generation =
        airplay_diagnostics_thread_created(
            RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_TIMING);
    *timing_out = timing;
    return true;

failure:
    free(timing);
    return false;
}

void airplay_mirror_timing_destroy(AirPlayMirrorTiming *timing)
{
    if (!timing)
        return;
    atomic_store(&timing->running, false);
    timing_close_socket(timing);
    if (timing->thread_started)
    {
        timing_thread_join(&timing->thread);
        airplay_diagnostics_thread_joined(
            RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_TIMING,
            timing->diagnostic_thread_generation);
    }
    memset(timing, 0, sizeof(*timing));
    free(timing);
}

uint16_t airplay_mirror_timing_port(const AirPlayMirrorTiming *timing)
{
    return timing && atomic_load(&timing->running) ? timing->local_port : 0u;
}

bool airplay_mirror_timing_get_stats(const AirPlayMirrorTiming *timing,
                                     AirPlayMirrorTimingStats *stats_out)
{
    if (!timing || !stats_out)
        return false;
    stats_out->requests_sent = atomic_load(&timing->requests_sent);
    stats_out->responses_received = atomic_load(&timing->responses_received);
    return true;
}
