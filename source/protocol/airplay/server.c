#include "server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifdef __SWITCH__
#include <switch.h>

#include "log/log.h"

typedef Thread AirPlayNativeThread;
typedef void (*AirPlayNativeThreadEntry)(void *argument);
#define AIRPLAY_THREAD_RETURN void
#define AIRPLAY_THREAD_FINISH() return
#define AIRPLAY_SERVER_LOG_ERROR(...) log_error(__VA_ARGS__)
#define AIRPLAY_SERVER_LOG_INFO(...) log_info(__VA_ARGS__)
#else
#include <pthread.h>

typedef pthread_t AirPlayNativeThread;
typedef void *(*AirPlayNativeThreadEntry)(void *argument);
#define AIRPLAY_THREAD_RETURN void *
#define AIRPLAY_THREAD_FINISH() return NULL
#define AIRPLAY_SERVER_LOG_ERROR(...) ((void)fprintf(stderr, __VA_ARGS__))
#define AIRPLAY_SERVER_LOG_INFO(...) ((void)0)
#endif

#define AIRPLAY_SERVER_BACKLOG 8
#define AIRPLAY_SERVER_THREAD_STACK_SIZE 0x10000U
#define AIRPLAY_SERVER_SOCKET_POLL_MS 200U
#define AIRPLAY_SERVER_INITIAL_BUFFER_BYTES 8192U

typedef struct AirPlayServerState AirPlayServerState;

typedef struct
{
    AirPlayServerState *server;
    size_t index;
    AirPlayNativeThread thread;
    bool thread_started;
    atomic_bool active;
    atomic_bool finished;
    atomic_int socket_fd;
} AirPlayServerClient;

struct AirPlayServerState
{
    AirPlayServerConfig config;
    uint16_t bound_port;
    AirPlayNativeThread listener_thread;
    bool listener_started;
    atomic_bool running;
    atomic_int listen_socket;
    atomic_uint_fast64_t next_session_id;
    AirPlayServerClient clients[AIRPLAY_SERVER_MAX_CLIENTS];
};

static AirPlayServerState g_airplay_server;

static uint64_t airplay_server_now_ms(void)
{
#ifdef __SWITCH__
    return armTicksToNs(armGetSystemTick()) / 1000000U;
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
#endif
}

static bool airplay_native_thread_start(AirPlayNativeThread *thread,
                                        AirPlayNativeThreadEntry entry,
                                        void *argument)
{
    if (!thread || !entry)
        return false;
#ifdef __SWITCH__
    Result result = threadCreate(thread,
                                 entry,
                                 argument,
                                 NULL,
                                 AIRPLAY_SERVER_THREAD_STACK_SIZE,
                                 0x2B,
                                 -2);
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

static void airplay_native_thread_join(AirPlayNativeThread *thread)
{
    if (!thread)
        return;
#ifdef __SWITCH__
    threadWaitForExit(thread);
    threadClose(thread);
#else
    pthread_join(*thread, NULL);
#endif
}

static void airplay_server_close_socket(atomic_int *socket_storage)
{
    int socket_fd;

    if (!socket_storage)
        return;
    socket_fd = atomic_exchange(socket_storage, -1);
    if (socket_fd >= 0)
    {
        shutdown(socket_fd, SHUT_RDWR);
        close(socket_fd);
    }
}

static void airplay_server_set_socket_timeout(int socket_fd, int option, uint32_t timeout_ms)
{
    struct timeval timeout;

    timeout.tv_sec = (time_t)(timeout_ms / 1000U);
    timeout.tv_usec = (suseconds_t)((timeout_ms % 1000U) * 1000U);
    setsockopt(socket_fd, SOL_SOCKET, option, &timeout, sizeof(timeout));
}

static bool airplay_server_send_all(int socket_fd, const uint8_t *bytes, size_t length)
{
    size_t sent = 0;
    int flags = 0;

#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif
    while (sent < length)
    {
        ssize_t result = send(socket_fd, bytes + sent, length - sent, flags);
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

static bool airplay_server_send_response(int socket_fd, AirPlayRtspResponse *response)
{
    uint8_t *encoded = NULL;
    size_t encoded_length = 0;
    bool success;

    if (!airplay_rtsp_response_encode(response, &encoded, &encoded_length))
        return false;
    success = airplay_server_send_all(socket_fd, encoded, encoded_length);
    free(encoded);
    return success;
}

static void airplay_server_send_error(int socket_fd, int status_code)
{
    AirPlayRtspResponse response;

    if (!airplay_rtsp_response_init(&response, "RTSP/1.0", status_code))
        return;
    response.close_connection = true;
    (void)airplay_server_send_response(socket_fd, &response);
    airplay_rtsp_response_clear(&response);
}

static bool airplay_server_grow_buffer(uint8_t **buffer, size_t *capacity, size_t used)
{
    size_t next_capacity;
    uint8_t *next;

    if (!buffer || !capacity || used > *capacity || *capacity >= AIRPLAY_RTSP_MAX_MESSAGE_BYTES)
        return false;
    next_capacity = *capacity > AIRPLAY_RTSP_MAX_MESSAGE_BYTES / 2U
                        ? AIRPLAY_RTSP_MAX_MESSAGE_BYTES
                        : *capacity * 2U;
    if (next_capacity < used || next_capacity > AIRPLAY_RTSP_MAX_MESSAGE_BYTES)
        next_capacity = AIRPLAY_RTSP_MAX_MESSAGE_BYTES;
    next = realloc(*buffer, next_capacity);
    if (!next)
        return false;
    *buffer = next;
    *capacity = next_capacity;
    return true;
}

static AIRPLAY_THREAD_RETURN airplay_server_client_thread(void *argument)
{
    AirPlayServerClient *client = argument;
    AirPlayServerState *server;
    AirPlayRtspSession session;
    AirPlayRtspRequest *request = NULL;
    AirPlayRtspResponse *response = NULL;
    uint8_t *buffer = NULL;
    size_t buffer_length = 0;
    size_t buffer_capacity = AIRPLAY_SERVER_INITIAL_BUFFER_BYTES;
    uint64_t request_started_ms = 0;
    int socket_fd;
    bool session_initialized = false;

    if (!client || !client->server)
        AIRPLAY_THREAD_FINISH();
    server = client->server;
    socket_fd = atomic_load(&client->socket_fd);
    buffer = malloc(buffer_capacity);
    request = calloc(1, sizeof(*request));
    response = calloc(1, sizeof(*response));
    if (!buffer || !request || !response)
    {
        airplay_server_send_error(socket_fd, 500);
        goto finished;
    }
    airplay_rtsp_session_init(&session, atomic_fetch_add(&server->next_session_id, 1U) + 1U);
    session_initialized = true;

    while (atomic_load(&server->running) && session.state != AIRPLAY_RTSP_SESSION_CLOSED)
    {
        AirPlayRtspError parse_error = AIRPLAY_RTSP_ERROR_NONE;
        size_t consumed = 0;
        AirPlayRtspParseResult parse_result = airplay_rtsp_parse_request(buffer,
                                                                         buffer_length,
                                                                         request,
                                                                         &consumed,
                                                                         &parse_error);
        if (parse_result == AIRPLAY_RTSP_PARSE_OK)
        {
            bool close_after_response;
            if (!airplay_rtsp_dispatch(&session,
                                       request,
                                       server->config.route_handler,
                                       server->config.route_user_data,
                                       response))
            {
                airplay_rtsp_request_clear(request);
                airplay_server_send_error(socket_fd, 500);
                break;
            }
            close_after_response = response->close_connection;
            if (!airplay_server_send_response(socket_fd, response))
                close_after_response = true;
            airplay_rtsp_response_clear(response);
            airplay_rtsp_request_clear(request);
            if (consumed < buffer_length)
                memmove(buffer, buffer + consumed, buffer_length - consumed);
            buffer_length -= consumed;
            request_started_ms = buffer_length != 0 ? airplay_server_now_ms() : 0;
            if (close_after_response)
                break;
            continue;
        }
        if (parse_result == AIRPLAY_RTSP_PARSE_ERROR)
        {
            airplay_server_send_error(socket_fd,
                                      parse_error == AIRPLAY_RTSP_ERROR_LIMIT_EXCEEDED ? 413 : 400);
            break;
        }

        if (buffer_length == buffer_capacity &&
            !airplay_server_grow_buffer(&buffer, &buffer_capacity, buffer_length))
        {
            airplay_server_send_error(socket_fd, 413);
            break;
        }
        if (buffer_length != 0 && request_started_ms != 0 &&
            airplay_server_now_ms() - request_started_ms >= server->config.request_timeout_ms)
        {
            airplay_server_send_error(socket_fd, 408);
            break;
        }

        ssize_t received = recv(socket_fd, buffer + buffer_length, buffer_capacity - buffer_length, 0);
        if (received > 0)
        {
            if (buffer_length == 0)
                request_started_ms = airplay_server_now_ms();
            buffer_length += (size_t)received;
            continue;
        }
        if (received == 0)
            break;
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            continue;
        break;
    }

finished:
    if (session_initialized && server->config.session_closed_handler)
        server->config.session_closed_handler(&session, server->config.route_user_data);
    airplay_rtsp_request_clear(request);
    airplay_rtsp_response_clear(response);
    free(request);
    free(response);
    free(buffer);
    airplay_server_close_socket(&client->socket_fd);
    atomic_store(&client->active, false);
    atomic_store(&client->finished, true);
    AIRPLAY_THREAD_FINISH();
}

static void airplay_server_reap_clients(AirPlayServerState *server)
{
    size_t index;

    for (index = 0; index < AIRPLAY_SERVER_MAX_CLIENTS; ++index)
    {
        AirPlayServerClient *client = &server->clients[index];
        if (client->thread_started && atomic_load(&client->finished))
        {
            airplay_native_thread_join(&client->thread);
            client->thread_started = false;
            atomic_store(&client->finished, false);
        }
    }
}

static AirPlayServerClient *airplay_server_available_client(AirPlayServerState *server)
{
    size_t index;

    airplay_server_reap_clients(server);
    for (index = 0; index < AIRPLAY_SERVER_MAX_CLIENTS; ++index)
    {
        if (!server->clients[index].thread_started)
            return &server->clients[index];
    }
    return NULL;
}

static void airplay_server_finish_clients(AirPlayServerState *server)
{
    size_t index;

    for (index = 0; index < AIRPLAY_SERVER_MAX_CLIENTS; ++index)
        airplay_server_close_socket(&server->clients[index].socket_fd);
    for (index = 0; index < AIRPLAY_SERVER_MAX_CLIENTS; ++index)
    {
        AirPlayServerClient *client = &server->clients[index];
        if (client->thread_started)
        {
            airplay_native_thread_join(&client->thread);
            client->thread_started = false;
        }
        atomic_store(&client->active, false);
        atomic_store(&client->finished, false);
    }
}

static AIRPLAY_THREAD_RETURN airplay_server_listener_thread(void *argument)
{
    AirPlayServerState *server = argument;

    while (server && atomic_load(&server->running))
    {
        int listen_socket = atomic_load(&server->listen_socket);
        fd_set read_fds;
        struct timeval timeout;
        int selected;

        if (listen_socket < 0)
            break;
        airplay_server_reap_clients(server);
        FD_ZERO(&read_fds);
        FD_SET(listen_socket, &read_fds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;
        selected = select(listen_socket + 1, &read_fds, NULL, NULL, &timeout);
        if (selected < 0)
        {
            if (errno == EINTR)
                continue;
            if (!atomic_load(&server->running))
                break;
            AIRPLAY_SERVER_LOG_ERROR("[airplay-server] select failed: %s (%d)\n", strerror(errno), errno);
            break;
        }
        if (selected == 0 || !FD_ISSET(listen_socket, &read_fds))
            continue;

        struct sockaddr_in peer;
        socklen_t peer_length = sizeof(peer);
        int client_socket = accept(listen_socket, (struct sockaddr *)&peer, &peer_length);
        if (client_socket < 0)
            continue;

#ifdef SO_NOSIGPIPE
        {
            int enabled = 1;
            setsockopt(client_socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
        }
#endif
        airplay_server_set_socket_timeout(client_socket, SO_RCVTIMEO, AIRPLAY_SERVER_SOCKET_POLL_MS);
        airplay_server_set_socket_timeout(client_socket, SO_SNDTIMEO, server->config.send_timeout_ms);

        AirPlayServerClient *client = airplay_server_available_client(server);
        if (!client)
        {
            airplay_server_send_error(client_socket, 503);
            shutdown(client_socket, SHUT_RDWR);
            close(client_socket);
            continue;
        }
        atomic_store(&client->socket_fd, client_socket);
        atomic_store(&client->active, true);
        atomic_store(&client->finished, false);
        if (!airplay_native_thread_start(&client->thread, airplay_server_client_thread, client))
        {
            airplay_server_close_socket(&client->socket_fd);
            atomic_store(&client->active, false);
            AIRPLAY_SERVER_LOG_ERROR("[airplay-server] client thread creation failed\n");
            continue;
        }
        client->thread_started = true;
    }

    if (server)
    {
        atomic_store(&server->running, false);
        airplay_server_finish_clients(server);
    }
    AIRPLAY_THREAD_FINISH();
}

bool airplay_server_start(const AirPlayServerConfig *config)
{
    struct sockaddr_in address;
    socklen_t address_length = sizeof(address);
    int listen_socket;
    int reuse = 1;
    size_t index;

    if (!config || g_airplay_server.listener_started)
        return false;
    memset(&g_airplay_server, 0, sizeof(g_airplay_server));
    g_airplay_server.config = *config;
    if (g_airplay_server.config.request_timeout_ms == 0)
        g_airplay_server.config.request_timeout_ms = AIRPLAY_SERVER_DEFAULT_REQUEST_TIMEOUT_MS;
    if (g_airplay_server.config.send_timeout_ms == 0)
        g_airplay_server.config.send_timeout_ms = AIRPLAY_SERVER_DEFAULT_SEND_TIMEOUT_MS;
    atomic_init(&g_airplay_server.running, false);
    atomic_init(&g_airplay_server.listen_socket, -1);
    atomic_init(&g_airplay_server.next_session_id, 0);
    for (index = 0; index < AIRPLAY_SERVER_MAX_CLIENTS; ++index)
    {
        AirPlayServerClient *client = &g_airplay_server.clients[index];
        client->server = &g_airplay_server;
        client->index = index;
        atomic_init(&client->active, false);
        atomic_init(&client->finished, false);
        atomic_init(&client->socket_fd, -1);
    }

    listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket < 0)
        return false;
    setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(config->port);
    if (bind(listen_socket, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(listen_socket, AIRPLAY_SERVER_BACKLOG) < 0 ||
        getsockname(listen_socket, (struct sockaddr *)&address, &address_length) < 0)
    {
        close(listen_socket);
        return false;
    }
    g_airplay_server.bound_port = ntohs(address.sin_port);
    atomic_store(&g_airplay_server.listen_socket, listen_socket);
    atomic_store(&g_airplay_server.running, true);
    if (!airplay_native_thread_start(&g_airplay_server.listener_thread,
                                     airplay_server_listener_thread,
                                     &g_airplay_server))
    {
        atomic_store(&g_airplay_server.running, false);
        airplay_server_close_socket(&g_airplay_server.listen_socket);
        g_airplay_server.bound_port = 0;
        return false;
    }
    g_airplay_server.listener_started = true;
    AIRPLAY_SERVER_LOG_INFO("[airplay-server] listening on :%u\n", g_airplay_server.bound_port);
    return true;
}

void airplay_server_stop(void)
{
    size_t index;

    if (!g_airplay_server.listener_started)
        return;
    atomic_store(&g_airplay_server.running, false);
    airplay_server_close_socket(&g_airplay_server.listen_socket);
    for (index = 0; index < AIRPLAY_SERVER_MAX_CLIENTS; ++index)
        airplay_server_close_socket(&g_airplay_server.clients[index].socket_fd);
    airplay_native_thread_join(&g_airplay_server.listener_thread);
    g_airplay_server.listener_started = false;
    g_airplay_server.bound_port = 0;
    memset(&g_airplay_server.config, 0, sizeof(g_airplay_server.config));
}

bool airplay_server_is_running(void)
{
    return g_airplay_server.listener_started && atomic_load(&g_airplay_server.running);
}

uint16_t airplay_server_port(void)
{
    return airplay_server_is_running() ? g_airplay_server.bound_port : 0;
}

size_t airplay_server_active_clients(void)
{
    size_t count = 0;
    size_t index;

    for (index = 0; index < AIRPLAY_SERVER_MAX_CLIENTS; ++index)
    {
        if (atomic_load(&g_airplay_server.clients[index].active))
            count++;
    }
    return count;
}
