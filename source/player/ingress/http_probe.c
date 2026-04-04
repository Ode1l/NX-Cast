#include "player/ingress/http_probe.h"

#include <switch.h>

#include <arpa/inet.h>
#include <ctype.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define PLAYER_HTTP_PROBE_HOST_MAX 256
#define PLAYER_HTTP_PROBE_PATH_MAX 1024
#define PLAYER_HTTP_PROBE_REQUEST_MAX 4096
#define PLAYER_HTTP_PROBE_RESPONSE_MAX 4096
#define PLAYER_HTTP_PROBE_TIMEOUT_SEC 3
#define PLAYER_HTTP_PROBE_MAX_REDIRECTS 2

typedef enum
{
    PLAYER_HTTP_SCHEME_UNKNOWN = 0,
    PLAYER_HTTP_SCHEME_HTTP,
    PLAYER_HTTP_SCHEME_HTTPS
} PlayerHttpScheme;

typedef enum
{
    PLAYER_HTTP_METHOD_HEAD = 0,
    PLAYER_HTTP_METHOD_GET
} PlayerHttpMethod;

typedef struct
{
    PlayerHttpScheme scheme;
    char host[PLAYER_HTTP_PROBE_HOST_MAX];
    char host_header[PLAYER_HTTP_PROBE_HOST_MAX + 16];
    char path[PLAYER_HTTP_PROBE_PATH_MAX];
    int port;
} ParsedHttpUrl;

static bool starts_with_ignore_case(const char *value, const char *prefix)
{
    if (!value || !prefix)
        return false;

    while (*prefix)
    {
        if (*value == '\0')
            return false;
        if (tolower((unsigned char)*value) != tolower((unsigned char)*prefix))
            return false;
        ++value;
        ++prefix;
    }

    return true;
}

static bool contains_ignore_case(const char *haystack, const char *needle)
{
    size_t needle_len;

    if (!haystack || !needle)
        return false;

    needle_len = strlen(needle);
    if (needle_len == 0)
        return true;

    for (const char *cursor = haystack; *cursor; ++cursor)
    {
        size_t i = 0;
        while (i < needle_len &&
               cursor[i] &&
               tolower((unsigned char)cursor[i]) == tolower((unsigned char)needle[i]))
        {
            ++i;
        }
        if (i == needle_len)
            return true;
    }

    return false;
}

static void trim_ascii_in_place(char *value)
{
    size_t begin = 0;
    size_t end;

    if (!value || value[0] == '\0')
        return;

    while (value[begin] && isspace((unsigned char)value[begin]))
        ++begin;
    if (begin > 0)
        memmove(value, value + begin, strlen(value + begin) + 1);

    end = strlen(value);
    while (end > 0 && isspace((unsigned char)value[end - 1]))
        value[--end] = '\0';
}

static void strip_content_type_parameters(char *value)
{
    char *semicolon;

    if (!value || value[0] == '\0')
        return;

    semicolon = strchr(value, ';');
    if (semicolon)
        *semicolon = '\0';
    trim_ascii_in_place(value);
}

static void append_request_header(char *request, size_t request_size, const char *name, const char *value)
{
    size_t used;

    if (!request || request_size == 0 || !name || !value || value[0] == '\0')
        return;

    used = strlen(request);
    if (used >= request_size - 1)
        return;

    snprintf(request + used,
             request_size - used,
             "%s: %s\r\n",
             name,
             value);
}

static void append_extra_headers(char *request, size_t request_size, const char *headers)
{
    const char *cursor;

    if (!request || request_size == 0 || !headers || headers[0] == '\0')
        return;

    cursor = headers;
    while (*cursor)
    {
        const char *next = strchr(cursor, ',');
        size_t span_len = next ? (size_t)(next - cursor) : strlen(cursor);
        char line[256];

        if (span_len >= sizeof(line))
            span_len = sizeof(line) - 1;
        memcpy(line, cursor, span_len);
        line[span_len] = '\0';
        trim_ascii_in_place(line);
        if (line[0] != '\0')
        {
            size_t used = strlen(request);
            if (used < request_size - 1)
                snprintf(request + used, request_size - used, "%s\r\n", line);
        }

        if (!next)
            break;
        cursor = next + 1;
    }
}

static bool parse_http_url(const char *uri, ParsedHttpUrl *out)
{
    const char *cursor;
    const char *host_start;
    const char *host_end;
    const char *port_start = NULL;
    const char *path_start;
    size_t host_len;
    size_t path_len;

    if (!uri || !out)
        return false;

    memset(out, 0, sizeof(*out));

    if (starts_with_ignore_case(uri, "http://"))
    {
        out->scheme = PLAYER_HTTP_SCHEME_HTTP;
        out->port = 80;
        cursor = uri + strlen("http://");
    }
    else if (starts_with_ignore_case(uri, "https://"))
    {
        out->scheme = PLAYER_HTTP_SCHEME_HTTPS;
        out->port = 443;
        cursor = uri + strlen("https://");
    }
    else
    {
        return false;
    }

    host_start = cursor;
    while (*cursor && *cursor != '/' && *cursor != '?' && *cursor != '#')
    {
        if (*cursor == ':')
        {
            port_start = cursor + 1;
            host_end = cursor;
            while (*cursor && *cursor != '/' && *cursor != '?' && *cursor != '#')
                ++cursor;
            goto parse_tail;
        }
        ++cursor;
    }
    host_end = cursor;

parse_tail:
    if (host_end <= host_start)
        return false;

    host_len = (size_t)(host_end - host_start);
    if (host_len >= sizeof(out->host))
        host_len = sizeof(out->host) - 1;
    memcpy(out->host, host_start, host_len);
    out->host[host_len] = '\0';

    if (port_start && port_start < cursor)
    {
        char port_buf[8];
        size_t port_len = (size_t)(cursor - port_start);

        if (port_len == 0 || port_len >= sizeof(port_buf))
            return false;
        memcpy(port_buf, port_start, port_len);
        port_buf[port_len] = '\0';
        out->port = atoi(port_buf);
        if (out->port <= 0 || out->port > 65535)
            return false;
    }

    if (*cursor == '\0')
        path_start = "/";
    else
        path_start = cursor;

    path_len = strlen(path_start);
    if (path_len >= sizeof(out->path))
        path_len = sizeof(out->path) - 1;
    memcpy(out->path, path_start, path_len);
    out->path[path_len] = '\0';

    if ((out->scheme == PLAYER_HTTP_SCHEME_HTTP && out->port == 80) ||
        (out->scheme == PLAYER_HTTP_SCHEME_HTTPS && out->port == 443))
    {
        snprintf(out->host_header, sizeof(out->host_header), "%s", out->host);
    }
    else
    {
        snprintf(out->host_header, sizeof(out->host_header), "%s:%d", out->host, out->port);
    }

    return true;
}

static bool read_http_response_headers(int sock, char *response, size_t response_size)
{
    size_t used = 0;

    if (!response || response_size == 0)
        return false;

    response[0] = '\0';

    while (used + 1 < response_size)
    {
        ssize_t chunk = recv(sock, response + used, response_size - used - 1, 0);
        if (chunk <= 0)
            break;
        used += (size_t)chunk;
        response[used] = '\0';
        if (strstr(response, "\r\n\r\n"))
            return true;
    }

    return strstr(response, "\r\n\r\n") != NULL;
}

static bool extract_header_value(const char *response, const char *header, char *out, size_t out_size)
{
    const char *cursor;
    size_t header_len;

    if (!response || !header || !out || out_size == 0)
        return false;

    out[0] = '\0';
    header_len = strlen(header);
    cursor = strstr(response, "\r\n");
    if (!cursor)
        return false;
    cursor += 2;

    while (*cursor)
    {
        const char *line_end = strstr(cursor, "\r\n");
        size_t line_len;
        const char *value_start;
        size_t copy_len;

        if (!line_end)
            break;
        line_len = (size_t)(line_end - cursor);
        if (line_len == 0)
            break;

        if (line_len > header_len + 1 &&
            strncasecmp(cursor, header, header_len) == 0 &&
            cursor[header_len] == ':')
        {
            value_start = cursor + header_len + 1;
            while (*value_start == ' ' || *value_start == '\t')
                ++value_start;
            copy_len = (size_t)(line_end - value_start);
            if (copy_len >= out_size)
                copy_len = out_size - 1;
            memcpy(out, value_start, copy_len);
            out[copy_len] = '\0';
            trim_ascii_in_place(out);
            return out[0] != '\0';
        }

        cursor = line_end + 2;
    }

    return false;
}

static bool parse_status_code(const char *response, int *status_code)
{
    const char *space;

    if (!response || !status_code)
        return false;

    space = strchr(response, ' ');
    if (!space)
        return false;

    *status_code = atoi(space + 1);
    return *status_code > 0;
}

static bool build_absolute_redirect(const ParsedHttpUrl *base,
                                    const char *location,
                                    char *out,
                                    size_t out_size)
{
    int written;

    if (!base || !location || !out || out_size == 0)
        return false;

    out[0] = '\0';

    if (starts_with_ignore_case(location, "http://") || starts_with_ignore_case(location, "https://"))
    {
        snprintf(out, out_size, "%s", location);
        return true;
    }

    if (location[0] != '/')
        return false;

    written = snprintf(out,
                       out_size,
                       "%s://%s%s",
                       base->scheme == PLAYER_HTTP_SCHEME_HTTPS ? "https" : "http",
                       base->host_header,
                       location);
    return written > 0 && (size_t)written < out_size;
}

static bool perform_request(const PlayerMedia *media,
                            const char *uri,
                            PlayerHttpMethod method,
                            char *response,
                            size_t response_size)
{
    ParsedHttpUrl parsed;
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    char port_buf[8];
    char request[PLAYER_HTTP_PROBE_REQUEST_MAX];
    struct timeval timeout = {
        .tv_sec = PLAYER_HTTP_PROBE_TIMEOUT_SEC,
        .tv_usec = 0,
    };
    int sock = -1;
    bool success = false;

    if (!media || !response || !parse_http_url(uri, &parsed))
        return false;
    if (parsed.scheme != PLAYER_HTTP_SCHEME_HTTP)
        return false;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(port_buf, sizeof(port_buf), "%d", parsed.port);
    if (getaddrinfo(parsed.host, port_buf, &hints, &result) != 0 || !result)
        goto cleanup;

    sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock < 0)
        goto cleanup;

    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    if (connect(sock, result->ai_addr, result->ai_addrlen) < 0)
        goto cleanup;

    snprintf(request,
             sizeof(request),
             "%s %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n",
             method == PLAYER_HTTP_METHOD_GET ? "GET" : "HEAD",
             parsed.path,
             parsed.host_header);
    append_request_header(request, sizeof(request), "User-Agent", media->user_agent);
    append_request_header(request, sizeof(request), "Referer", media->referrer);
    append_request_header(request, sizeof(request), "Origin", media->origin);
    append_request_header(request, sizeof(request), "Cookie", media->cookie);
    if (method == PLAYER_HTTP_METHOD_GET)
        append_request_header(request, sizeof(request), "Range", "bytes=0-0");
    append_extra_headers(request, sizeof(request), media->extra_headers);
    {
        size_t used = strlen(request);
        if (used + 2 < sizeof(request))
            snprintf(request + used, sizeof(request) - used, "\r\n");
    }

    if (send(sock, request, strlen(request), 0) < 0)
        goto cleanup;

    if (!read_http_response_headers(sock, response, response_size))
        goto cleanup;

    success = true;

cleanup:
    if (sock >= 0)
        close(sock);
    if (result)
        freeaddrinfo(result);
    return success;
}

bool ingress_http_probe_head(const PlayerMedia *media, PlayerHttpProbe *out)
{
    char current_uri[PLAYER_MEDIA_URI_MAX];
    PlayerHttpMethod method = PLAYER_HTTP_METHOD_HEAD;
    int redirect_count = 0;

    if (!out)
        return false;

    memset(out, 0, sizeof(*out));
    if (!media || !media->uri[0])
        return false;

    snprintf(current_uri, sizeof(current_uri), "%s", media->uri);
    snprintf(out->effective_uri, sizeof(out->effective_uri), "%s", media->uri);

    while (redirect_count <= PLAYER_HTTP_PROBE_MAX_REDIRECTS)
    {
        ParsedHttpUrl parsed;
        char response[PLAYER_HTTP_PROBE_RESPONSE_MAX];
        char location[PLAYER_MEDIA_URI_MAX];
        char accept_ranges[64];
        char content_range[96];

        if (!parse_http_url(current_uri, &parsed))
            return out->attempted;

        if (parsed.scheme != PLAYER_HTTP_SCHEME_HTTP)
        {
            snprintf(out->effective_uri, sizeof(out->effective_uri), "%s", current_uri);
            return out->attempted;
        }

        out->attempted = true;
        response[0] = '\0';
        if (!perform_request(media, current_uri, method, response, sizeof(response)))
            return false;
        if (!parse_status_code(response, &out->status_code))
            return false;

        if (extract_header_value(response, "Content-Type", out->content_type, sizeof(out->content_type)))
            strip_content_type_parameters(out->content_type);

        if (extract_header_value(response, "Accept-Ranges", accept_ranges, sizeof(accept_ranges)))
        {
            out->accept_ranges_known = true;
            out->accept_ranges_seekable = contains_ignore_case(accept_ranges, "bytes");
            if (contains_ignore_case(accept_ranges, "none"))
                out->accept_ranges_seekable = false;
        }

        if (extract_header_value(response, "Content-Range", content_range, sizeof(content_range)))
        {
            out->accept_ranges_known = true;
            out->accept_ranges_seekable = true;
        }

        if ((out->status_code == 405 || out->status_code == 501) && method == PLAYER_HTTP_METHOD_HEAD)
        {
            method = PLAYER_HTTP_METHOD_GET;
            out->used_get_fallback = true;
            continue;
        }

        if ((out->status_code == 301 || out->status_code == 302 || out->status_code == 303 ||
             out->status_code == 307 || out->status_code == 308) &&
            extract_header_value(response, "Location", location, sizeof(location)))
        {
            char redirected_uri[PLAYER_MEDIA_URI_MAX];

            if (!build_absolute_redirect(&parsed, location, redirected_uri, sizeof(redirected_uri)))
                break;

            snprintf(current_uri, sizeof(current_uri), "%s", redirected_uri);
            snprintf(out->effective_uri, sizeof(out->effective_uri), "%s", redirected_uri);
            out->redirected = true;
            out->redirect_count = ++redirect_count;
            method = PLAYER_HTTP_METHOD_HEAD;
            continue;
        }

        snprintf(out->effective_uri, sizeof(out->effective_uri), "%s", current_uri);
        out->ok = out->status_code >= 200 && out->status_code < 400;
        return true;
    }

    return out->attempted;
}
