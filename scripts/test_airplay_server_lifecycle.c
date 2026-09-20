#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "app/network_diagnostics.h"
#include "app/runtime_diagnostics.h"
#include "protocol/airplay/server.h"

#define TEST_WAIT_LIMIT_MS 2000u
#define TEST_WAIT_STEP_MS 10u
#define TEST_STOP_LIMIT_MS 1000u

typedef struct
{
    atomic_uint_fast64_t reverse_session_id;
} TestRouteContext;

typedef struct
{
    uint64_t session_id;
    unsigned index;
    atomic_bool success;
} ReverseSender;

typedef struct
{
    uint64_t session_id;
    atomic_bool stop;
    atomic_uint attempts;
} ReverseStopSender;

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
    struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);

    assert(socket_fd >= 0);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(UINT32_C(0x7f000001));
    assert(connect(socket_fd, (struct sockaddr *)&address, sizeof(address)) ==
           0);
    assert(setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                      sizeof(timeout)) == 0);
    return socket_fd;
}

static void test_wait_for_clients(size_t expected)
{
    for (unsigned waited = 0u; waited < TEST_WAIT_LIMIT_MS;
         waited += TEST_WAIT_STEP_MS)
    {
        if (airplay_server_active_clients() == expected)
            return;
        test_sleep_ms(TEST_WAIT_STEP_MS);
    }
    assert(!"AirPlay client worker did not start");
}

static bool test_send_all(int socket_fd, const void *bytes, size_t length)
{
    const uint8_t *cursor = bytes;
    size_t sent = 0u;

    while (sent < length)
    {
        ssize_t result = send(socket_fd, cursor + sent, length - sent, 0);

        if (result > 0)
        {
            sent += (size_t)result;
            continue;
        }
        if (result < 0 && errno == EINTR)
            continue;
        return false;
    }
    return true;
}

static size_t test_receive_http_message(int socket_fd, char *buffer,
                                        size_t capacity)
{
    size_t used = 0u;

    assert(buffer && capacity > 1u);
    while (used + 1u < capacity)
    {
        char *header_end;
        size_t message_length;
        size_t body_length = 0u;
        ssize_t received = recv(socket_fd, buffer + used,
                                capacity - used - 1u, 0);

        if (received < 0 && errno == EINTR)
            continue;
        assert(received > 0);
        used += (size_t)received;
        buffer[used] = '\0';
        header_end = strstr(buffer, "\r\n\r\n");
        if (!header_end)
            continue;
        {
            const char *content_length = strstr(buffer, "Content-Length:");

            if (content_length && content_length < header_end)
            {
                char *end = NULL;
                unsigned long parsed = strtoul(
                    content_length + strlen("Content-Length:"), &end, 10);

                assert(end && end <= header_end && parsed <= SIZE_MAX);
                body_length = (size_t)parsed;
            }
        }
        message_length = (size_t)(header_end + 4 - buffer) + body_length;
        if (used >= message_length)
            return message_length;
    }
    assert(!"HTTP message exceeded test buffer");
    return 0u;
}

static bool test_route(AirPlayRtspSession *session,
                       const AirPlayRtspRequest *request,
                       AirPlayRtspResponse *response, void *user_data)
{
    TestRouteContext *context = user_data;

    if (strcmp(request->method, "POST") == 0 &&
        strcmp(request->uri, "/reverse") == 0)
    {
        session->logical_session_id = 77u;
        assert(airplay_rtsp_response_set_status(response, 101));
        assert(airplay_rtsp_response_add_header(response, "Connection",
                                                "Upgrade"));
        assert(airplay_rtsp_response_add_header(response, "Upgrade",
                                                "PTTH/1.0"));
        response->register_reverse_connection = true;
        atomic_store(&context->reverse_session_id,
                     session->logical_session_id);
        return true;
    }
    return airplay_rtsp_response_set_status(response, 404);
}

static void *test_reverse_sender(void *argument)
{
    ReverseSender *sender = argument;
    AirPlayRtspHeader headers[1] = {0};
    AirPlayRtspOutboundRequest outbound = {
        .method = "POST",
        .uri = "/event",
        .protocol = "HTTP/1.1",
        .headers = headers,
        .header_count = 1u};
    char body[64];

    snprintf(headers[0].name, sizeof(headers[0].name), "Content-Type");
    snprintf(headers[0].value, sizeof(headers[0].value), "text/plain");
    snprintf(body, sizeof(body), "reverse-concurrent-%u", sender->index);
    outbound.body = body;
    outbound.body_length = strlen(body);
    atomic_store(&sender->success,
                 airplay_server_send_reverse_request(sender->session_id,
                                                     &outbound));
    return NULL;
}

static void *test_reverse_stop_sender(void *argument)
{
    ReverseStopSender *sender = argument;
    AirPlayRtspOutboundRequest outbound = {
        .method = "POST",
        .uri = "/event",
        .protocol = "HTTP/1.1",
        .body = "shutdown-race",
        .body_length = sizeof("shutdown-race") - 1u};

    while (!atomic_load(&sender->stop))
    {
        bool sent = airplay_server_send_reverse_request(sender->session_id,
                                                        &outbound);

        atomic_fetch_add(&sender->attempts, 1u);
        if (!sent)
            break;
        test_sleep_ms(1u);
    }
    return NULL;
}

static void test_wait_for_sender_attempt(const ReverseStopSender *sender)
{
    for (unsigned waited = 0u; waited < TEST_WAIT_LIMIT_MS;
         waited += TEST_WAIT_STEP_MS)
    {
        if (atomic_load(&sender->attempts) != 0u)
            return;
        test_sleep_ms(TEST_WAIT_STEP_MS);
    }
    assert(!"Reverse stop sender did not start");
}

static size_t count_occurrences(const char *text, const char *needle)
{
    size_t count = 0u;
    size_t needle_length = strlen(needle);

    while ((text = strstr(text, needle)) != NULL)
    {
        count++;
        text += needle_length;
    }
    return count;
}

static void test_concurrent_reverse_sends(int client_socket,
                                          uint64_t session_id)
{
    enum
    {
        SENDER_COUNT = 8
    };
    ReverseSender senders[SENDER_COUNT] = {0};
    pthread_t threads[SENDER_COUNT];
    char received[16384];
    size_t used = 0u;

    received[0] = '\0';

    for (unsigned index = 0u; index < SENDER_COUNT; ++index)
    {
        senders[index].session_id = session_id;
        senders[index].index = index;
        atomic_init(&senders[index].success, false);
        assert(pthread_create(&threads[index], NULL, test_reverse_sender,
                              &senders[index]) == 0);
    }
    while (count_occurrences(received, "POST /event HTTP/1.1\r\n") <
           SENDER_COUNT)
    {
        ssize_t amount = recv(client_socket, received + used,
                              sizeof(received) - used - 1u, 0);

        assert(amount > 0);
        used += (size_t)amount;
        assert(used + 1u < sizeof(received));
        received[used] = '\0';
    }
    for (unsigned index = 0u; index < SENDER_COUNT; ++index)
    {
        char expected[64];

        assert(pthread_join(threads[index], NULL) == 0);
        assert(atomic_load(&senders[index].success));
        snprintf(expected, sizeof(expected), "reverse-concurrent-%u", index);
        assert(strstr(received, expected) != NULL);
    }
}

static uint64_t test_wait_for_reverse_session(TestRouteContext *context)
{
    for (unsigned waited = 0u; waited < TEST_WAIT_LIMIT_MS;
         waited += TEST_WAIT_STEP_MS)
    {
        uint64_t session_id = atomic_load(&context->reverse_session_id);

        if (session_id != 0u)
            return session_id;
        test_sleep_ms(TEST_WAIT_STEP_MS);
    }
    assert(!"Reverse connection was not registered");
    return 0u;
}

static bool test_wait_for_reverse_send(
    uint64_t session_id, const AirPlayRtspOutboundRequest *request)
{
    for (unsigned waited = 0u; waited < TEST_WAIT_LIMIT_MS;
         waited += TEST_WAIT_STEP_MS)
    {
        if (airplay_server_send_reverse_request(session_id, request))
            return true;
        test_sleep_ms(TEST_WAIT_STEP_MS);
    }
    return false;
}

static void test_reverse_transport(TestRouteContext *context)
{
    static const char reverse_request[] =
        "POST /reverse HTTP/1.1\r\n"
        "Connection: Upgrade\r\n"
        "Upgrade: PTTH/1.0\r\n"
        "Content-Length: 0\r\n\r\n";
    static const char reverse_response[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
    static const char body[] = "<plist>event</plist>";
    AirPlayRtspHeader headers[2] = {0};
    AirPlayRtspOutboundRequest outbound = {
        .method = "POST",
        .uri = "/event",
        .protocol = "HTTP/1.1",
        .headers = headers,
        .header_count = 2u,
        .body = body,
        .body_length = sizeof(body) - 1u,
    };
    char received[2048];
    uint64_t session_id;
    int client_socket = test_connect(airplay_server_port());
    int replacement_socket;

    test_wait_for_clients(1u);
    assert(test_send_all(client_socket, reverse_request,
                         sizeof(reverse_request) - 1u));
    assert(test_receive_http_message(client_socket, received,
                                     sizeof(received)) != 0u);
    assert(strstr(received, "HTTP/1.1 101 Switching Protocols\r\n") != NULL);
    assert(strstr(received, "Upgrade: PTTH/1.0\r\n") != NULL);
    session_id = test_wait_for_reverse_session(context);

    snprintf(headers[0].name, sizeof(headers[0].name),
             "X-Apple-Session-ID");
    snprintf(headers[0].value, sizeof(headers[0].value), "test-session");
    snprintf(headers[1].name, sizeof(headers[1].name), "Content-Type");
    snprintf(headers[1].value, sizeof(headers[1].value),
             "text/x-apple-plist+xml");
    assert(test_wait_for_reverse_send(session_id, &outbound));
    assert(test_receive_http_message(client_socket, received,
                                     sizeof(received)) != 0u);
    assert(strstr(received, "POST /event HTTP/1.1\r\n") != NULL);
    assert(strstr(received, body) != NULL);
    assert(test_send_all(client_socket, reverse_response,
                         sizeof(reverse_response) - 1u));
    test_sleep_ms(50u);
    assert(airplay_server_active_clients() == 1u);
    assert(test_wait_for_reverse_send(session_id, &outbound));
    assert(test_receive_http_message(client_socket, received,
                                     sizeof(received)) != 0u);

    test_concurrent_reverse_sends(client_socket, session_id);

    replacement_socket = test_connect(airplay_server_port());
    test_wait_for_clients(2u);
    assert(test_send_all(replacement_socket, reverse_request,
                         sizeof(reverse_request) - 1u));
    assert(test_receive_http_message(replacement_socket, received,
                                     sizeof(received)) != 0u);
    assert(strstr(received, "HTTP/1.1 101 Switching Protocols\r\n") != NULL);
    test_wait_for_clients(1u);
    assert(airplay_server_send_reverse_request(session_id, &outbound));
    assert(test_receive_http_message(replacement_socket, received,
                                     sizeof(received)) != 0u);

    close(client_socket);
    shutdown(replacement_socket, SHUT_RDWR);
    close(replacement_socket);
    test_wait_for_clients(0u);
    assert(!airplay_server_send_reverse_request(session_id, &outbound));
    atomic_store(&context->reverse_session_id, 0u);
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
    static const char reverse_request[] =
        "POST /reverse HTTP/1.1\r\n"
        "Connection: Upgrade\r\n"
        "Upgrade: PTTH/1.0\r\n"
        "\r\n";
    TestRouteContext route_context;
    AirPlayServerConfig config = {
        .port = 0u,
        .request_timeout_ms = 5000u,
        .send_timeout_ms = 1000u,
        .route_handler = test_route,
        .route_user_data = &route_context,
    };
    NetworkDiagnosticSnapshot snapshot;
    RuntimeDiagnosticThreadSnapshot listener_threads;
    RuntimeDiagnosticThreadSnapshot client_threads;
    uint64_t stop_started_ms;
    ReverseStopSender stop_sender;
    pthread_t stop_sender_thread;
    char response[1024];
    int client_socket;

    atomic_init(&route_context.reverse_session_id, 0u);
    assert(network_diagnostics_reset());
    assert(runtime_diagnostics_reset());
    assert(airplay_server_start(&config));
    assert(airplay_server_port() != 0u);
    test_reverse_transport(&route_context);
    client_socket = test_connect(airplay_server_port());
    assert(test_send_all(client_socket, reverse_request,
                         sizeof(reverse_request) - 1u));
    assert(test_receive_http_message(client_socket, response,
                                     sizeof(response)) > 0u);
    assert(strstr(response, "101 Switching Protocols") != NULL);
    test_wait_for_clients(1u);
    assert(network_diagnostics_get_snapshot(
        NETWORK_DIAGNOSTIC_AIRPLAY_CONTROL, &snapshot));
    assert(snapshot.open_sockets == 2u);

    stop_sender.session_id = test_wait_for_reverse_session(&route_context);
    atomic_init(&stop_sender.stop, false);
    atomic_init(&stop_sender.attempts, 0u);
    assert(pthread_create(&stop_sender_thread, NULL,
                          test_reverse_stop_sender, &stop_sender) == 0);
    test_wait_for_sender_attempt(&stop_sender);

    stop_started_ms = test_now_ms();
    airplay_server_stop();
    atomic_store(&stop_sender.stop, true);
    assert(pthread_join(stop_sender_thread, NULL) == 0);
    assert(test_now_ms() - stop_started_ms < TEST_STOP_LIMIT_MS);
    assert(!airplay_server_is_running());
    test_assert_balanced();
    assert(runtime_diagnostics_get_thread_snapshot(
        RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_LISTENER, &listener_threads));
    assert(runtime_diagnostics_get_thread_snapshot(
        RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_CLIENT, &client_threads));
    assert(listener_threads.created == 1u && listener_threads.joined == 1u &&
           listener_threads.live == 0u);
    assert(client_threads.created == 3u && client_threads.joined == 3u &&
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
