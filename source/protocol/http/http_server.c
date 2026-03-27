#include "http_server.h"

#include <switch.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "log/log.h"

#define HTTP_SERVER_REQUEST_BUFFER_SIZE 16384
#define HTTP_SERVER_RESPONSE_BUFFER_SIZE 12288
#define HTTP_SERVER_THREAD_STACK_SIZE 0x8000

typedef struct
{
    int listen_sock;
    uint16_t port;
    Thread thread;
    bool running;
    bool thread_started;
    HttpRequestHandler handler;
    void *handler_user_data;
} HttpServerState;

static HttpServerState g_http_server = {
    .listen_sock = -1
};

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

    return sscanf(request, "%7s %255s", method, raw_path) == 2;
}

static void normalize_path(const char *raw_path, char *path, size_t path_size)
{
    if (!path || path_size == 0)
        return;

    if (!raw_path)
    {
        path[0] = '\0';
        return;
    }

    snprintf(path, path_size, "%s", raw_path);
    char *query = strchr(path, '?');
    if (query)
        *query = '\0';
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

    ssize_t request_size = recv(client_sock, request_buffer, HTTP_SERVER_REQUEST_BUFFER_SIZE - 1, 0);
    if (request_size <= 0)
    {
        free(request_buffer);
        free(response_buffer);
        return;
    }
    request_buffer[request_size] = '\0';

    char method[8];
    char raw_path[256];
    if (!parse_request_line(request_buffer, method, sizeof(method), raw_path, sizeof(raw_path)))
    {
        log_warn("[http-server] parse request line failed.\n");
        size_t response_len = 0;
        if (build_text_response(400, "Bad Request", "Bad Request", response_buffer,
                                HTTP_SERVER_RESPONSE_BUFFER_SIZE, &response_len) &&
            response_len > 0)
        {
            send(client_sock, response_buffer, response_len, 0);
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

    log_debug("[http-server] recv %s:%u -> %s http://%s%s bytes=%zd\n",
              client_ip, client_port, method, host, raw_path, request_size);

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
        send(client_sock, response_buffer, response_len, 0);

    log_debug("[http-server] send endpoint=%s bytes=%zu handled=%d\n",
              path, response_len, handled ? 1 : 0);

    free(request_buffer);
    free(response_buffer);
}

static void http_server_thread(void *arg)
{
    (void)arg;

    while (g_http_server.running)
    {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(g_http_server.listen_sock, &read_fds);

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

        int ret = select(g_http_server.listen_sock + 1, &read_fds, NULL, NULL, &timeout);
        if (ret < 0)
        {
            if (errno == EINTR)
                continue;
            if (!g_http_server.running)
                break;
            log_error("[http-server] select failed: %s (%d)\n", strerror(errno), errno);
            break;
        }

        if (ret == 0)
            continue;

        if (FD_ISSET(g_http_server.listen_sock, &read_fds))
        {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_sock = accept(g_http_server.listen_sock, (struct sockaddr *)&client_addr, &client_len);
            if (client_sock < 0)
                continue;

            handle_client(client_sock, &client_addr);
            close(client_sock);
        }
    }
}

bool http_server_start(const HttpServerConfig *config)
{
    if (g_http_server.running)
        return true;

    if (!config || config->port == 0 || !config->handler)
    {
        log_error("[http-server] invalid config.\n");
        return false;
    }

    memset(&g_http_server, 0, sizeof(g_http_server));
    g_http_server.listen_sock = -1;

    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
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
        close(listen_sock);
        return false;
    }

    if (listen(listen_sock, 8) < 0)
    {
        log_error("[http-server] listen failed: %s (%d)\n", strerror(errno), errno);
        close(listen_sock);
        return false;
    }

    g_http_server.listen_sock = listen_sock;
    g_http_server.port = config->port;
    g_http_server.handler = config->handler;
    g_http_server.handler_user_data = config->user_data;
    g_http_server.running = true;

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
        g_http_server.running = false;
        close(g_http_server.listen_sock);
        g_http_server.listen_sock = -1;
        return false;
    }

    rc = threadStart(&g_http_server.thread);
    if (R_FAILED(rc))
    {
        log_error("[http-server] threadStart failed: 0x%08X\n", rc);
        threadClose(&g_http_server.thread);
        g_http_server.running = false;
        close(g_http_server.listen_sock);
        g_http_server.listen_sock = -1;
        return false;
    }

    g_http_server.thread_started = true;
    log_info("[http-server] listening on :%u\n", config->port);
    return true;
}

void http_server_stop(void)
{
    if (!g_http_server.running)
        return;

    g_http_server.running = false;

    if (g_http_server.listen_sock >= 0)
    {
        int sock = g_http_server.listen_sock;
        g_http_server.listen_sock = -1;
        shutdown(sock, SHUT_RDWR);
        close(sock);
    }

    if (g_http_server.thread_started)
    {
        threadWaitForExit(&g_http_server.thread);
        threadClose(&g_http_server.thread);
        g_http_server.thread_started = false;
    }

    g_http_server.port = 0;
    g_http_server.handler = NULL;
    g_http_server.handler_user_data = NULL;
    log_info("[http-server] stopped.\n");
}

bool http_server_is_running(void)
{
    return g_http_server.running;
}
