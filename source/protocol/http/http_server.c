#include "http_server.h"

#include <switch.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "app/network_diagnostics.h"
#include "log/log.h"

#define HTTP_SERVER_REQUEST_BUFFER_SIZE 16384
#define HTTP_SERVER_RESPONSE_BUFFER_SIZE 131072
// The HTTP thread can traverse SOAP parsing and action handlers. Use a more
// conservative stack than the old 0x8000 baseline to reduce crash risk.
#define HTTP_SERVER_THREAD_STACK_SIZE 0x10000
#define HTTP_SERVER_RECV_IDLE_TIMEOUT_SEC 1

typedef struct
{
    atomic_int listen_sock;
    atomic_int active_client_sock;
    uint16_t port;
    Thread thread;
    atomic_bool running;
    bool thread_started;
    HttpRequestHandler handler;
    void *handler_user_data;
} HttpServerState;

static HttpServerState g_http_server;

static int http_diagnostic_socket(int domain, int type, int protocol)
{
    NetworkOperationToken operation = network_diagnostics_operation_begin(
        NETWORK_DIAGNOSTIC_DLNA_HTTP, NETWORK_OPERATION_SOCKET);
    int socket_fd = socket(domain, type, protocol);
    int saved_errno = socket_fd < 0 ? errno : 0;

    network_diagnostics_operation_end(&operation, saved_errno);
    if (socket_fd >= 0)
        network_diagnostics_socket_opened(NETWORK_DIAGNOSTIC_DLNA_HTTP);
    else
        errno = saved_errno;
    return socket_fd;
}

static void http_diagnostic_close(int socket_fd)
{
    if (socket_fd < 0)
        return;
    (void)close(socket_fd);
    network_diagnostics_socket_closed(NETWORK_DIAGNOSTIC_DLNA_HTTP);
}

static ssize_t http_diagnostic_recv(int socket_fd, void *buffer, size_t length,
                                    int flags)
{
    if (atomic_load(&g_http_server.active_client_sock) != socket_fd)
    {
        errno = ECANCELED;
        return -1;
    }
    NetworkOperationToken operation = network_diagnostics_operation_begin(
        NETWORK_DIAGNOSTIC_DLNA_HTTP, NETWORK_OPERATION_RECV);
    ssize_t result = recv(socket_fd, buffer, length, flags);
    int saved_errno = result < 0 ? errno : 0;

    network_diagnostics_operation_end(&operation, saved_errno);
    if (result < 0)
        errno = saved_errno;
    return result;
}

static ssize_t http_diagnostic_send(int socket_fd, const void *buffer,
                                    size_t length, int flags)
{
    if (atomic_load(&g_http_server.active_client_sock) != socket_fd)
    {
        errno = ECANCELED;
        return -1;
    }
    NetworkOperationToken operation = network_diagnostics_operation_begin(
        NETWORK_DIAGNOSTIC_DLNA_HTTP, NETWORK_OPERATION_SEND);
    ssize_t result = send(socket_fd, buffer, length, flags);
    int saved_errno = result < 0 ? errno : 0;

    network_diagnostics_operation_end(&operation, saved_errno);
    if (result < 0)
        errno = saved_errno;
    return result;
}

static int http_diagnostic_select(int descriptor_count, fd_set *read_set,
                                  struct timeval *timeout)
{
    NetworkOperationToken operation = network_diagnostics_operation_begin(
        NETWORK_DIAGNOSTIC_DLNA_HTTP, NETWORK_OPERATION_SELECT);
    int result = select(descriptor_count, read_set, NULL, NULL, timeout);
    int saved_errno = result < 0 ? errno : 0;

    network_diagnostics_operation_end(&operation, saved_errno);
    if (result < 0)
        errno = saved_errno;
    return result;
}

static int http_diagnostic_accept(int socket_fd, struct sockaddr *address,
                                  socklen_t *address_length)
{
    NetworkOperationToken operation = network_diagnostics_operation_begin(
        NETWORK_DIAGNOSTIC_DLNA_HTTP, NETWORK_OPERATION_ACCEPT);
    int result = accept(socket_fd, address, address_length);
    int saved_errno = result < 0 ? errno : 0;

    network_diagnostics_operation_end(&operation, saved_errno);
    if (result >= 0)
        network_diagnostics_socket_opened(NETWORK_DIAGNOSTIC_DLNA_HTTP);
    else
        errno = saved_errno;
    return result;
}

static bool get_header_value(const char *request, const char *header, char *out, size_t out_size)
{
    if (!request || !header || !out || out_size == 0)
        return false;

    size_t header_len = strlen(header);
    const char *cursor = request;
    while (*cursor)
    {
        const char *line_end = strstr(cursor, "\r\n");
        size_t line_len = line_end ? (size_t)(line_end - cursor) : strlen(cursor);

        if (line_len == 0)
            break;

        if (line_len > header_len + 1 &&
            strncasecmp(cursor, header, header_len) == 0 &&
            cursor[header_len] == ':')
        {
            const char *value_start = cursor + header_len + 1;
            while (*value_start == ' ' || *value_start == '\t')
                ++value_start;

            size_t copy_len = line_end ? (size_t)(line_end - value_start) : strlen(value_start);
            if (copy_len >= out_size)
                copy_len = out_size - 1;

            memcpy(out, value_start, copy_len);
            out[copy_len] = '\0';

            while (copy_len > 0 && (out[copy_len - 1] == ' ' || out[copy_len - 1] == '\t'))
                out[--copy_len] = '\0';
            return true;
        }

        if (!line_end)
            break;
        cursor = line_end + 2;
    }

    return false;
}

static bool build_text_response(int status,
                                const char *status_text,
                                const char *body,
                                char *response,
                                size_t response_size,
                                size_t *response_len)
{
    if (!status_text || !body || !response || response_size == 0 || !response_len)
        return false;

    size_t body_len = strlen(body);
    int written = snprintf(response, response_size,
                           "HTTP/1.1 %d %s\r\n"
                           "Content-Type: text/plain; charset=\"utf-8\"\r\n"
                           "Content-Length: %zu\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           status,
                           status_text,
                           body_len,
                           body);

    if (written < 0 || (size_t)written >= response_size)
        return false;

    *response_len = (size_t)written;
    return true;
}

static bool parse_request_line(const char *request,
                               char *method,
                               size_t method_size,
                               char *raw_path,
                               size_t raw_path_size)
{
    if (!request || !method || method_size == 0 || !raw_path || raw_path_size == 0)
        return false;

    if (method_size == 0 || raw_path_size == 0)
        return false;

    char method_fmt[16];
    char path_fmt[16];
    snprintf(method_fmt, sizeof(method_fmt), "%%%zus", method_size - 1);
    snprintf(path_fmt, sizeof(path_fmt), "%%%zus", raw_path_size - 1);

    char format[40];
    snprintf(format, sizeof(format), "%s %s", method_fmt, path_fmt);
    return sscanf(request, format, method, raw_path) == 2;
}

static const char *find_header_end(const char *request, size_t request_len)
{
    if (!request || request_len < 4)
        return NULL;

    for (size_t i = 0; i + 3 < request_len; ++i)
    {
        if (request[i] == '\r' && request[i + 1] == '\n' &&
            request[i + 2] == '\r' && request[i + 3] == '\n')
        {
            return request + i + 4;
        }
    }
    return NULL;
}

static bool parse_content_length(const char *request, size_t *content_length)
{
    if (!request || !content_length)
        return false;

    char content_length_str[32];
    if (!get_header_value(request, "Content-Length", content_length_str, sizeof(content_length_str)))
        return false;

    char *end_ptr = NULL;
    unsigned long parsed = strtoul(content_length_str, &end_ptr, 10);
    if (!end_ptr || *end_ptr != '\0')
        return false;

    *content_length = (size_t)parsed;
    return true;
}

static ssize_t recv_full_http_request(int client_sock, char *request_buffer, size_t request_capacity)
{
    if (client_sock < 0 || !request_buffer || request_capacity == 0)
        return -1;

    size_t request_len = 0;
    bool expected_total_known = false;
    size_t expected_total = 0;

    // Avoid parsing partial SOAP bodies by waiting for the declared payload.
    struct timeval recv_timeout;
    recv_timeout.tv_sec = HTTP_SERVER_RECV_IDLE_TIMEOUT_SEC;
    recv_timeout.tv_usec = 0;
    setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));
    setsockopt(client_sock, SOL_SOCKET, SO_SNDTIMEO, &recv_timeout,
               sizeof(recv_timeout));

    while (request_len < request_capacity - 1)
    {
        ssize_t chunk = http_diagnostic_recv(
            client_sock, request_buffer + request_len,
            request_capacity - 1 - request_len, 0);
        if (chunk > 0)
        {
            request_len += (size_t)chunk;
            request_buffer[request_len] = '\0';

            if (!expected_total_known)
            {
                const char *header_end = find_header_end(request_buffer, request_len);
                if (header_end)
                {
                    size_t header_len = (size_t)(header_end - request_buffer);
                    size_t content_length = 0;
                    if (!parse_content_length(request_buffer, &content_length))
                        content_length = 0;

                    expected_total = header_len + content_length;
                    expected_total_known = true;

                    if (expected_total >= request_capacity)
                    {
                        log_warn("[http-server] request too large header=%zu body=%zu capacity=%zu\n",
                                 header_len, content_length, request_capacity - 1);
                        return -1;
                    }
                }
            }

            if (expected_total_known && request_len >= expected_total)
                break;
            continue;
        }

        if (chunk == 0)
            break;

        if (errno == EINTR)
            continue;

        if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;

        return -1;
    }

    if (request_len == 0)
        return -1;

    if (!expected_total_known)
    {
        log_warn("[http-server] incomplete headers bytes=%zu\n", request_len);
        return -1;
    }

    if (expected_total_known && request_len < expected_total)
    {
        log_warn("[http-server] incomplete request bytes=%zu expected=%zu\n", request_len, expected_total);
        return -1;
    }

    request_buffer[request_len] = '\0';
    return (ssize_t)request_len;
}

static void normalize_path(const char *raw_path, char *path, size_t path_size)
{
    const char *source;
    size_t read_index = 0;
    size_t write_index = 0;
    bool previous_was_slash = false;

    if (!path || path_size == 0)
        return;

    if (!raw_path)
    {
        path[0] = '\0';
        return;
    }

    source = raw_path;
    if (strncasecmp(source, "http://", 7) == 0 || strncasecmp(source, "https://", 8) == 0)
    {
        const char *scheme_sep = strstr(source, "://");
        const char *first_slash = scheme_sep ? strchr(scheme_sep + 3, '/') : NULL;
        source = first_slash ? first_slash : "/";
    }
    else if (source[0] == '\0')
    {
        source = "/";
    }

    while (source[read_index] != '\0' && source[read_index] != '?' && write_index + 1 < path_size)
    {
        char ch = source[read_index++];
        if (ch == '/')
        {
            if (previous_was_slash)
                continue;
            previous_was_slash = true;
        }
        else
            previous_was_slash = false;

        path[write_index++] = ch;
    }

    if (write_index == 0)
        path[write_index++] = '/';

    path[write_index] = '\0';
}

static void handle_client(int client_sock, const struct sockaddr_in *client_addr)
{
    char *request_buffer = malloc(HTTP_SERVER_REQUEST_BUFFER_SIZE);
    char *response_buffer = malloc(HTTP_SERVER_RESPONSE_BUFFER_SIZE);
    if (!request_buffer || !response_buffer)
    {
        log_error("[http-server] OOM while handling request.\n");
        free(request_buffer);
        free(response_buffer);
        return;
    }

    ssize_t request_size = recv_full_http_request(client_sock,
                                                  request_buffer,
                                                  HTTP_SERVER_REQUEST_BUFFER_SIZE);
    if (request_size <= 0)
    {
        free(request_buffer);
        free(response_buffer);
        return;
    }
    request_buffer[request_size] = '\0';

    char method[16];
    char raw_path[256];
    if (!parse_request_line(request_buffer, method, sizeof(method), raw_path, sizeof(raw_path)))
    {
        log_warn("[http-server] parse request line failed.\n");
        size_t response_len = 0;
        if (build_text_response(400, "Bad Request", "Bad Request", response_buffer,
                                HTTP_SERVER_RESPONSE_BUFFER_SIZE, &response_len) &&
            response_len > 0)
        {
            (void)http_diagnostic_send(client_sock, response_buffer,
                                       response_len, 0);
        }
        free(request_buffer);
        free(response_buffer);
        return;
    }

    char path[256];
    normalize_path(raw_path, path, sizeof(path));

    char host[128];
    host[0] = '\0';
    if (!get_header_value(request_buffer, "Host", host, sizeof(host)))
        snprintf(host, sizeof(host), "localhost:%u", g_http_server.port);

    char user_agent[256];
    user_agent[0] = '\0';
    if (!get_header_value(request_buffer, "User-Agent", user_agent, sizeof(user_agent)))
        snprintf(user_agent, sizeof(user_agent), "(none)");

    char client_ip[32];
    client_ip[0] = '\0';
    uint16_t client_port = 0;
    if (client_addr)
    {
        inet_ntop(AF_INET, &client_addr->sin_addr, client_ip, sizeof(client_ip));
        client_port = ntohs(client_addr->sin_port);
    }
    if (client_ip[0] == '\0')
        snprintf(client_ip, sizeof(client_ip), "unknown");

    log_debug("[http-server] recv %s:%u -> %s http://%s%s bytes=%zd ua=%s\n",
              client_ip, client_port, method, host, raw_path, request_size, user_agent);

    size_t response_len = 0;
    bool handled = false;
    if (g_http_server.handler)
    {
        HttpRequestContext ctx = {
            .method = method,
            .raw_path = raw_path,
            .path = path,
            .host = host,
            .request = request_buffer,
            .request_len = (size_t)request_size,
            .client_ip = client_ip,
            .client_port = client_port
        };

        handled = g_http_server.handler(&ctx,
                                        response_buffer,
                                        HTTP_SERVER_RESPONSE_BUFFER_SIZE,
                                        &response_len,
                                        g_http_server.handler_user_data);
    }

    if (!handled)
    {
        if (!build_text_response(404, "Not Found", "Not Found", response_buffer,
                                 HTTP_SERVER_RESPONSE_BUFFER_SIZE, &response_len))
        {
            response_len = 0;
        }
    }

    if (response_len > 0)
        (void)http_diagnostic_send(client_sock, response_buffer, response_len,
                                   0);

    log_debug("[http-server] send endpoint=%s bytes=%zu handled=%d\n",
              path, response_len, handled ? 1 : 0);

    free(request_buffer);
    free(response_buffer);
}

static void http_server_thread(void *arg)
{
    (void)arg;

    while (atomic_load(&g_http_server.running))
    {
        int listen_sock = atomic_load(&g_http_server.listen_sock);
        fd_set read_fds;

        if (listen_sock < 0)
            break;
        FD_ZERO(&read_fds);
        FD_SET(listen_sock, &read_fds);

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

        int ret = http_diagnostic_select(listen_sock + 1, &read_fds,
                                         &timeout);
        if (ret < 0)
        {
            if (errno == EINTR)
                continue;
            if (!atomic_load(&g_http_server.running))
                break;
            log_error("[http-server] select failed: %s (%d)\n", strerror(errno), errno);
            break;
        }

        if (!atomic_load(&g_http_server.running))
            break;

        if (ret == 0)
            continue;

        if (FD_ISSET(listen_sock, &read_fds))
        {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_sock = http_diagnostic_accept(
                listen_sock, (struct sockaddr *)&client_addr,
                &client_len);
            if (client_sock < 0)
                continue;

            atomic_store(&g_http_server.active_client_sock, client_sock);
            if (atomic_load(&g_http_server.running))
                handle_client(client_sock, &client_addr);
            {
                int expected = client_sock;

                if (atomic_compare_exchange_strong(
                        &g_http_server.active_client_sock, &expected, -1))
                {
                    if (!atomic_load(&g_http_server.running))
                        shutdown(client_sock, SHUT_RDWR);
                    http_diagnostic_close(client_sock);
                }
            }
        }
    }
    atomic_store(&g_http_server.running, false);
}

bool http_server_start(const HttpServerConfig *config)
{
    if (atomic_load(&g_http_server.running))
        return true;
    if (g_http_server.thread_started)
        return false;

    if (!config || config->port == 0 || !config->handler)
    {
        log_error("[http-server] invalid config.\n");
        return false;
    }

    atomic_store(&g_http_server.listen_sock, -1);
    atomic_store(&g_http_server.active_client_sock, -1);
    atomic_store(&g_http_server.running, false);
    g_http_server.port = 0;
    g_http_server.thread_started = false;
    g_http_server.handler = NULL;
    g_http_server.handler_user_data = NULL;

    int listen_sock = http_diagnostic_socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0)
    {
        log_error("[http-server] socket failed: %s (%d)\n", strerror(errno), errno);
        return false;
    }

    int reuse = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config->port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        log_error("[http-server] bind failed on port %u: %s (%d)\n", config->port, strerror(errno), errno);
        http_diagnostic_close(listen_sock);
        return false;
    }

    if (listen(listen_sock, 8) < 0)
    {
        log_error("[http-server] listen failed: %s (%d)\n", strerror(errno), errno);
        http_diagnostic_close(listen_sock);
        return false;
    }

    atomic_store(&g_http_server.listen_sock, listen_sock);
    g_http_server.port = config->port;
    g_http_server.handler = config->handler;
    g_http_server.handler_user_data = config->user_data;
    atomic_store(&g_http_server.running, true);

    Result rc = threadCreate(&g_http_server.thread,
                             http_server_thread,
                             NULL,
                             NULL,
                             HTTP_SERVER_THREAD_STACK_SIZE,
                             0x2B,
                             -2);
    if (R_FAILED(rc))
    {
        log_error("[http-server] threadCreate failed: 0x%08X\n", rc);
        int owned_socket = atomic_exchange(&g_http_server.listen_sock, -1);

        atomic_store(&g_http_server.running, false);
        http_diagnostic_close(owned_socket);
        return false;
    }

    rc = threadStart(&g_http_server.thread);
    if (R_FAILED(rc))
    {
        log_error("[http-server] threadStart failed: 0x%08X\n", rc);
        threadClose(&g_http_server.thread);
        int owned_socket = atomic_exchange(&g_http_server.listen_sock, -1);

        atomic_store(&g_http_server.running, false);
        http_diagnostic_close(owned_socket);
        return false;
    }

    g_http_server.thread_started = true;
    log_info("[http-server] listening on :%u\n", config->port);
    return true;
}

void http_server_stop(void)
{
    int listen_sock;
    int client_sock;

    if (!atomic_load(&g_http_server.running) &&
        !g_http_server.thread_started)
        return;

    log_info("[http-server] stop begin listen_sock=%d thread_started=%d port=%u\n",
             atomic_load(&g_http_server.listen_sock),
             g_http_server.thread_started ? 1 : 0,
             g_http_server.port);
    atomic_store(&g_http_server.running, false);

    listen_sock = atomic_exchange(&g_http_server.listen_sock, -1);
    client_sock = atomic_exchange(&g_http_server.active_client_sock, -1);
    if (listen_sock >= 0)
    {
        log_info("[http-server] stop shutting down listen socket fd=%d\n",
                 listen_sock);
        shutdown(listen_sock, SHUT_RDWR);
    }
    if (client_sock >= 0)
    {
        log_info("[http-server] stop shutting down active client fd=%d\n",
                 client_sock);
        shutdown(client_sock, SHUT_RDWR);
    }

    if (g_http_server.thread_started)
    {
        log_info("[http-server] stop waiting for thread exit\n");
        threadWaitForExit(&g_http_server.thread);
        threadClose(&g_http_server.thread);
        g_http_server.thread_started = false;
        log_info("[http-server] stop thread closed\n");
    }

    if (client_sock >= 0)
        http_diagnostic_close(client_sock);
    if (listen_sock >= 0)
        http_diagnostic_close(listen_sock);

    g_http_server.port = 0;
    g_http_server.handler = NULL;
    g_http_server.handler_user_data = NULL;
    log_info("[http-server] stopped.\n");
}

bool http_server_is_running(void)
{
    return atomic_load(&g_http_server.running);
}
