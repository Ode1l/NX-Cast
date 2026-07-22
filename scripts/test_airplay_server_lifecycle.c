#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "app/network_diagnostics.h"
#include "app/runtime_diagnostics.h"
#include "protocol/airplay/server.h"

#define TEST_WAIT_LIMIT_MS 2000u
#define TEST_WAIT_STEP_MS 10u
#define TEST_STOP_LIMIT_MS 1000u

static uint64_t test_now_ms(void)
{
    struct timespec now;

    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (uint64_t)now.tv_sec * UINT64_C(1000) +
           (uint64_t)now.tv_nsec / UINT64_C(1000000);
}

static void test_sleep_ms(unsigned milliseconds)
{
    struct timespec delay = {
        .tv_sec = (time_t)(milliseconds / 1000u),
        .tv_nsec = (long)(milliseconds % 1000u) * 1000000L,
    };

    while (nanosleep(&delay, &delay) != 0 && errno == EINTR)
    {
    }
}

static int test_connect(uint16_t port)
{
    struct sockaddr_in address;
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);

    assert(socket_fd >= 0);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(connect(socket_fd, (struct sockaddr *)&address, sizeof(address)) ==
           0);
    return socket_fd;
}

static void test_wait_for_client(void)
{
    for (unsigned waited = 0u; waited < TEST_WAIT_LIMIT_MS;
         waited += TEST_WAIT_STEP_MS)
    {
        if (airplay_server_active_clients() == 1u)
            return;
        test_sleep_ms(TEST_WAIT_STEP_MS);
    }
    assert(!"AirPlay client worker did not start");
}

static void test_assert_balanced(void)
{
    NetworkDiagnosticSnapshot snapshot;

    assert(network_diagnostics_get_snapshot(
        NETWORK_DIAGNOSTIC_AIRPLAY_CONTROL, &snapshot));
    assert(snapshot.open_sockets == 0u);
    assert(snapshot.active_operations == 0u);
    assert(snapshot.socket_close_underflows == 0u);
}

int main(void)
{
    AirPlayServerConfig config = {
        .port = 0u,
        .request_timeout_ms = 5000u,
        .send_timeout_ms = 1000u,
    };
    NetworkDiagnosticSnapshot snapshot;
    RuntimeDiagnosticThreadSnapshot listener_threads;
    RuntimeDiagnosticThreadSnapshot client_threads;
    uint64_t stop_started_ms;
    int client_socket;

    assert(network_diagnostics_reset());
    assert(runtime_diagnostics_reset());
    assert(airplay_server_start(&config));
    assert(airplay_server_port() != 0u);
    client_socket = test_connect(airplay_server_port());
    test_wait_for_client();
    assert(network_diagnostics_get_snapshot(
        NETWORK_DIAGNOSTIC_AIRPLAY_CONTROL, &snapshot));
    assert(snapshot.open_sockets == 2u);

    stop_started_ms = test_now_ms();
    airplay_server_stop();
    assert(test_now_ms() - stop_started_ms < TEST_STOP_LIMIT_MS);
    assert(!airplay_server_is_running());
    test_assert_balanced();
    assert(runtime_diagnostics_get_thread_snapshot(
        RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_LISTENER, &listener_threads));
    assert(runtime_diagnostics_get_thread_snapshot(
        RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_CLIENT, &client_threads));
    assert(listener_threads.created == 1u && listener_threads.joined == 1u &&
           listener_threads.live == 0u);
    assert(client_threads.created == 1u && client_threads.joined == 1u &&
           client_threads.live == 0u);
    close(client_socket);

    airplay_server_stop();
    test_assert_balanced();

    assert(airplay_server_start(&config));
    assert(airplay_server_is_running());
    airplay_server_stop();
    test_assert_balanced();
    assert(runtime_diagnostics_get_thread_snapshot(
        RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_LISTENER, &listener_threads));
    assert(listener_threads.created == 2u && listener_threads.joined == 2u &&
           listener_threads.live == 0u);
    assert(runtime_diagnostics_reset());

    puts("AirPlay server lifecycle tests passed");
    return 0;
}
