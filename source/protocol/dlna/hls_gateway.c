#include "hls_gateway.h"

#include <switch.h>

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include "log/log.h"
#include "protocol/dlna/server_info.h"

typedef struct
{
    char *data;
    size_t len;
    size_t capacity;
} DynamicBuffer;

typedef struct
{
    char *scheme;
    char *host;
    unsigned short port;
    char *host_header;
    char *origin;
    char *path_query;
    char *base_dir;
} ParsedHttpUrl;

typedef struct
{
    bool running;
    char *url_base;
    char *token;
    char *gateway_path;
    char *source_uri;
    ParsedHttpUrl upstream;
} HlsGatewayState;

static HlsGatewayState g_gateway = {0};

static void dynamic_buffer_init(DynamicBuffer *buffer)
{
    if (!buffer)
        return;
    memset(buffer, 0, sizeof(*buffer));
}

static void dynamic_buffer_clear(DynamicBuffer *buffer)
{
    if (!buffer)
        return;
    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

static bool dynamic_buffer_reserve(DynamicBuffer *buffer, size_t extra)
{
    char *next_data;
    size_t required;
    size_t next_capacity;

    if (!buffer)
        return false;

    if (buffer->len > SIZE_MAX - extra - 1)
        return false;

    required = buffer->len + extra + 1;
    if (required <= buffer->capacity)
        return true;

    next_capacity = buffer->capacity ? buffer->capacity : 256;
    while (next_capacity < required)
    {
        if (next_capacity > SIZE_MAX / 2)
            next_capacity = required;
        else
            next_capacity *= 2;
    }

    next_data = realloc(buffer->data, next_capacity);
    if (!next_data)
        return false;

    buffer->data = next_data;
    buffer->capacity = next_capacity;
    return true;
}

static bool dynamic_buffer_append(DynamicBuffer *buffer, const char *data, size_t len)
{
    if (!buffer || (!data && len > 0))
        return false;

    if (!dynamic_buffer_reserve(buffer, len))
        return false;

    if (len > 0)
        memcpy(buffer->data + buffer->len, data, len);
    buffer->len += len;
    buffer->data[buffer->len] = '\0';
    return true;
}

static bool dynamic_buffer_append_cstr(DynamicBuffer *buffer, const char *value)
{
    return dynamic_buffer_append(buffer, value ? value : "", value ? strlen(value) : 0);
}

static char *dynamic_buffer_detach(DynamicBuffer *buffer)
{
    char *data;

    if (!buffer)
        return NULL;

    data = buffer->data;
    buffer->data = NULL;
    buffer->len = 0;
    buffer->capacity = 0;
    return data;
}

static char *gateway_strdup_printf(const char *fmt, ...)
{
    va_list args;
    va_list args_copy;
    int needed;
    char *buffer;

    if (!fmt)
        return NULL;

    va_start(args, fmt);
    va_copy(args_copy, args);
    needed = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);
    if (needed < 0)
    {
        va_end(args);
        return NULL;
    }

    buffer = malloc((size_t)needed + 1);
    if (!buffer)
    {
        va_end(args);
        return NULL;
    }

    vsnprintf(buffer, (size_t)needed + 1, fmt, args);
    va_end(args);
    return buffer;
}

static char *dup_range(const char *start, const char *end)
{
    size_t len;
    char *value;

    if (!start || !end || end < start)
        return NULL;

    len = (size_t)(end - start);
    value = malloc(len + 1);
    if (!value)
        return NULL;

    memcpy(value, start, len);
    value[len] = '\0';
    return value;
}

static void trim_in_place(char *value)
{
    size_t len;
    size_t start = 0;

    if (!value)
        return;

    len = strlen(value);
    while (len > 0 && (value[len - 1] == ' ' || value[len - 1] == '\t' || value[len - 1] == '\r' || value[len - 1] == '\n'))
        value[--len] = '\0';

    while (value[start] == ' ' || value[start] == '\t')
        ++start;

    if (start > 0)
        memmove(value, value + start, strlen(value + start) + 1);
}

static void parsed_http_url_clear(ParsedHttpUrl *url)
{
    if (!url)
        return;

    free(url->scheme);
    free(url->host);
    free(url->host_header);
    free(url->origin);
    free(url->path_query);
    free(url->base_dir);
    memset(url, 0, sizeof(*url));
}

static bool path_has_m3u8_extension(const char *path_query)
{
    const char *path_end;
    const char *cursor;

    if (!path_query)
        return false;

    path_end = strchr(path_query, '?');
    if (!path_end)
        path_end = path_query + strlen(path_query);

    for (cursor = path_query; cursor < path_end; ++cursor)
    {
        if ((size_t)(path_end - cursor) >= 5 && strncasecmp(cursor, ".m3u8", 5) == 0)
            return true;
    }

    return false;
}

static bool parse_http_url(const char *url, ParsedHttpUrl *out)
{
    const char *scheme_end;
    const char *authority_start;
    const char *path_start;
    const char *authority_end;
    const char *port_sep = NULL;
    char *host = NULL;
    char *path_query = NULL;
    char *scheme = NULL;
    char *origin = NULL;
    char *host_header = NULL;
    char *base_dir = NULL;
    unsigned short port;
    bool default_port;

    if (!url || !out)
        return false;

    memset(out, 0, sizeof(*out));
    scheme_end = strstr(url, "://");
    if (!scheme_end)
        return false;

    scheme = dup_range(url, scheme_end);
    if (!scheme)
        goto fail;

    authority_start = scheme_end + 3;
    path_start = strchr(authority_start, '/');
    authority_end = path_start ? path_start : url + strlen(url);
    if (authority_end == authority_start)
        goto fail;

    for (const char *cursor = authority_start; cursor < authority_end; ++cursor)
    {
        if (*cursor == ':')
            port_sep = cursor;
    }

    if (port_sep)
    {
        char *end = NULL;
        unsigned long parsed_port;

        host = dup_range(authority_start, port_sep);
        if (!host)
            goto fail;

        parsed_port = strtoul(port_sep + 1, &end, 10);
        if (!end || end != authority_end || parsed_port == 0 || parsed_port > 65535UL)
            goto fail;
        port = (unsigned short)parsed_port;
    }
    else
    {
        host = dup_range(authority_start, authority_end);
        if (!host)
            goto fail;
        port = (strcasecmp(scheme, "https") == 0) ? 443u : 80u;
    }

    if (host[0] == '\0')
        goto fail;

    path_query = strdup(path_start ? path_start : "/");
    if (!path_query)
        goto fail;

    default_port = (strcasecmp(scheme, "https") == 0 && port == 443u) ||
                   (strcasecmp(scheme, "http") == 0 && port == 80u);
    if (default_port)
    {
        host_header = strdup(host);
        origin = gateway_strdup_printf("%s://%s", scheme, host);
    }
    else
    {
        host_header = gateway_strdup_printf("%s:%u", host, port);
        origin = gateway_strdup_printf("%s://%s:%u", scheme, host, port);
    }
    if (!host_header || !origin)
        goto fail;

    {
        const char *query = strchr(path_query, '?');
        const char *path_end = query ? query : path_query + strlen(path_query);
        const char *last_slash = path_end;

        while (last_slash > path_query && *last_slash != '/')
            --last_slash;
        if (*last_slash != '/')
            last_slash = path_query;

        base_dir = gateway_strdup_printf("%s%.*s",
                                         origin,
                                         (int)(last_slash - path_query + 1),
                                         path_query);
        if (!base_dir)
            goto fail;
    }

    out->scheme = scheme;
    out->host = host;
    out->port = port;
    out->host_header = host_header;
    out->origin = origin;
    out->path_query = path_query;
    out->base_dir = base_dir;
    return true;

fail:
    free(scheme);
    free(host);
    free(host_header);
    free(origin);
    free(path_query);
    free(base_dir);
    return false;
}

static void hls_gateway_clear_mapping(void)
{
    free(g_gateway.token);
    free(g_gateway.gateway_path);
    free(g_gateway.source_uri);
    g_gateway.token = NULL;
    g_gateway.gateway_path = NULL;
    g_gateway.source_uri = NULL;
    parsed_http_url_clear(&g_gateway.upstream);
}

static bool hls_gateway_path_matches(const char *path)
{
    return path && g_gateway.gateway_path && strcmp(path, g_gateway.gateway_path) == 0;
}

static bool parsed_http_url_host_is_private_ipv4(const ParsedHttpUrl *url)
{
    struct in_addr addr;
    uint32_t host_order;
    uint8_t a;
    uint8_t b;

    if (!url || !url->host)
        return false;
    if (inet_pton(AF_INET, url->host, &addr) != 1)
        return false;

    host_order = ntohl(addr.s_addr);
    a = (uint8_t)((host_order >> 24) & 0xffu);
    b = (uint8_t)((host_order >> 16) & 0xffu);

    if (a == 10u || a == 127u)
        return true;
    if (a == 192u && b == 168u)
        return true;
    if (a == 172u && b >= 16u && b <= 31u)
        return true;
    if (a == 169u && b == 254u)
        return true;
    if (a == 100u && b >= 64u && b <= 127u)
        return true;
    return false;
}

static bool uri_is_http_hls(const char *uri, ParsedHttpUrl *parsed)
{
    ParsedHttpUrl local = {0};

    if (!uri || !parse_http_url(uri, &local))
        return false;
    if (strcasecmp(local.scheme, "http") != 0 || !path_has_m3u8_extension(local.path_query))
    {
        parsed_http_url_clear(&local);
        return false;
    }
    if (!parsed_http_url_host_is_private_ipv4(&local))
    {
        parsed_http_url_clear(&local);
        return false;
    }

    if (parsed)
        *parsed = local;
    else
        parsed_http_url_clear(&local);
    return true;
}

static void extract_gateway_request_path(const HttpRequestContext *ctx, char *out, size_t out_size)
{
    const char *source = NULL;
    size_t write_index = 0;
    bool previous_was_slash = false;

    if (!out || out_size == 0)
        return;
    out[0] = '\0';

    if (ctx && ctx->path && ctx->path[0] == '/')
        source = ctx->path;
    else if (ctx && ctx->raw_path)
        source = ctx->raw_path;

    if (!source)
        return;

    if (strncasecmp(source, "http://", 7) == 0 || strncasecmp(source, "https://", 8) == 0)
    {
        const char *scheme_sep = strstr(source, "://");
        const char *first_slash = scheme_sep ? strchr(scheme_sep + 3, '/') : NULL;
        source = first_slash ? first_slash : "/";
    }

    while (*source != '\0' && *source != '?' && write_index + 1 < out_size)
    {
        char ch = *source++;
        if (ch == '/')
        {
            if (previous_was_slash)
                continue;
            previous_was_slash = true;
        }
        else
            previous_was_slash = false;

        out[write_index++] = ch;
    }

    if (write_index == 0)
        out[write_index++] = '/';
    out[write_index] = '\0';
}

static char *join_url_simple_alloc(const char *prefix, const char *suffix)
{
    if (!prefix || !suffix)
        return NULL;
    return gateway_strdup_printf("%s%s", prefix, suffix);
}

static char *resolve_playlist_reference_alloc(const ParsedHttpUrl *base, const char *reference)
{
    if (!base || !reference || reference[0] == '\0')
        return NULL;

    if (strncasecmp(reference, "http://", 7) == 0 || strncasecmp(reference, "https://", 8) == 0)
        return strdup(reference);
    if (strncmp(reference, "//", 2) == 0)
        return gateway_strdup_printf("%s:%s", base->scheme ? base->scheme : "http", reference);
    if (reference[0] == '/')
        return join_url_simple_alloc(base->origin, reference);
    return join_url_simple_alloc(base->base_dir, reference);
}

static char *rewrite_uri_attributes_alloc(const char *line, const ParsedHttpUrl *base)
{
    DynamicBuffer buffer;
    const char *cursor = line;

    if (!line)
        return NULL;

    dynamic_buffer_init(&buffer);
    while (true)
    {
        const char *marker = strstr(cursor, "URI=\"");
        const char *value_start;
        const char *value_end;
        char *reference;
        char *resolved;

        if (!marker)
            break;

        value_start = marker + 5;
        value_end = strchr(value_start, '"');
        if (!value_end)
            break;

        if (!dynamic_buffer_append(&buffer, cursor, (size_t)(value_start - cursor)))
            goto fail;

        reference = dup_range(value_start, value_end);
        if (!reference)
            goto fail;
        resolved = resolve_playlist_reference_alloc(base, reference);
        free(reference);
        if (!resolved)
            goto fail;

        if (!dynamic_buffer_append_cstr(&buffer, resolved))
        {
            free(resolved);
            goto fail;
        }
        free(resolved);
        cursor = value_end;
    }

    if (!dynamic_buffer_append_cstr(&buffer, cursor))
        goto fail;
    return dynamic_buffer_detach(&buffer);

fail:
    dynamic_buffer_clear(&buffer);
    return NULL;
}

static bool rewrite_m3u8_playlist_alloc(const char *playlist,
                                        const ParsedHttpUrl *base,
                                        char **out,
                                        size_t *out_len)
{
    DynamicBuffer buffer;
    const char *cursor;

    if (!playlist || !base || !out || !out_len)
        return false;

    *out = NULL;
    *out_len = 0;
    dynamic_buffer_init(&buffer);
    cursor = playlist;

    while (*cursor)
    {
        const char *line_end = strchr(cursor, '\n');
        const char *line_limit = line_end ? line_end : cursor + strlen(cursor);
        char *line = dup_range(cursor, line_limit);
        char *rewritten = NULL;

        if (!line)
            goto fail;

        trim_in_place(line);
        if (line[0] == '#')
            rewritten = rewrite_uri_attributes_alloc(line, base);
        else if (line[0] == '\0')
            rewritten = strdup("");
        else
            rewritten = resolve_playlist_reference_alloc(base, line);

        free(line);
        if (!rewritten)
            goto fail;
        if (!dynamic_buffer_append_cstr(&buffer, rewritten) ||
            !dynamic_buffer_append(&buffer, "\n", 1))
        {
            free(rewritten);
            goto fail;
        }
        free(rewritten);

        cursor = line_end ? line_end + 1 : line_limit;
    }

    *out = dynamic_buffer_detach(&buffer);
    *out_len = *out ? strlen(*out) : 0;
    return *out != NULL;

fail:
    dynamic_buffer_clear(&buffer);
    return false;
}

static bool socket_send_all(int sock, const char *data, size_t len)
{
    size_t sent = 0;

    if (sock < 0 || (!data && len > 0))
        return false;

    while (sent < len)
    {
        ssize_t rc = send(sock, data + sent, len - sent, 0);
        if (rc < 0)
        {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (rc == 0)
            return false;
        sent += (size_t)rc;
    }

    return true;
}

static bool http_fetch_raw_response_alloc(const ParsedHttpUrl *url, char **out, size_t *out_len)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *it;
    int sock = -1;
    char *request = NULL;
    DynamicBuffer buffer;
    bool ok = false;
    char *recv_chunk = NULL;
    char port_text[16];

    if (!url || !out || !out_len)
        return false;

    *out = NULL;
    *out_len = 0;
    dynamic_buffer_init(&buffer);

    snprintf(port_text, sizeof(port_text), "%u", (unsigned int)url->port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(url->host, port_text, &hints, &result) != 0)
        goto fail;

    for (it = result; it; it = it->ai_next)
    {
        sock = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (sock < 0)
            continue;
        if (connect(sock, it->ai_addr, it->ai_addrlen) == 0)
            break;
        close(sock);
        sock = -1;
    }

    if (sock < 0)
        goto fail;

    request = gateway_strdup_printf("GET %s HTTP/1.1\r\n"
                                    "Host: %s\r\n"
                                    "User-Agent: NX-Cast/0.1 HLS-Gateway\r\n"
                                    "Accept: application/vnd.apple.mpegurl, application/x-mpegURL, */*\r\n"
                                    "Connection: close\r\n"
                                    "\r\n",
                                    url->path_query ? url->path_query : "/",
                                    url->host_header ? url->host_header : url->host);
    if (!request)
        goto fail;

    if (!socket_send_all(sock, request, strlen(request)))
        goto fail;

    recv_chunk = malloc(4096);
    if (!recv_chunk)
        goto fail;

    while (true)
    {
        ssize_t received = recv(sock, recv_chunk, 4096, 0);
        if (received > 0)
        {
            if (!dynamic_buffer_append(&buffer, recv_chunk, (size_t)received))
                goto fail;
            continue;
        }
        if (received == 0)
            break;
        if (errno == EINTR)
            continue;
        goto fail;
    }

    *out = dynamic_buffer_detach(&buffer);
    *out_len = *out ? strlen(*out) : 0;
    ok = *out != NULL;

fail:
    free(request);
    free(recv_chunk);
    if (sock >= 0)
        close(sock);
    if (result)
        freeaddrinfo(result);
    if (!ok)
        dynamic_buffer_clear(&buffer);
    return ok;
}

static bool header_value_dup(const char *headers,
                             const char *headers_end,
                             const char *name,
                             char **out)
{
    const char *cursor;
    size_t name_len;

    if (!headers || !headers_end || !name || !out)
        return false;

    *out = NULL;
    name_len = strlen(name);
    cursor = headers;

    while (cursor < headers_end)
    {
        const char *line_end = strstr(cursor, "\r\n");
        const char *value_start;
        size_t line_len;

        if (!line_end || line_end > headers_end)
            break;
        if (line_end == cursor)
            break;

        line_len = (size_t)(line_end - cursor);
        if (line_len > name_len + 1 &&
            strncasecmp(cursor, name, name_len) == 0 &&
            cursor[name_len] == ':')
        {
            char *value;

            value_start = cursor + name_len + 1;
            while (value_start < line_end && (*value_start == ' ' || *value_start == '\t'))
                ++value_start;

            value = dup_range(value_start, line_end);
            if (!value)
                return false;
            trim_in_place(value);
            *out = value;
            return true;
        }

        cursor = line_end + 2;
    }

    return false;
}

static bool decode_chunked_body_alloc(const char *body,
                                      size_t body_len,
                                      char **out,
                                      size_t *out_len)
{
    DynamicBuffer buffer;
    size_t pos = 0;

    if (!body || !out || !out_len)
        return false;

    *out = NULL;
    *out_len = 0;
    dynamic_buffer_init(&buffer);

    while (pos < body_len)
    {
        const char *line_start = body + pos;
        const char *line_end = NULL;
        char *line = NULL;
        char *end = NULL;
        unsigned long chunk_size;

        for (size_t i = pos; i + 1 < body_len; ++i)
        {
            if (body[i] == '\r' && body[i + 1] == '\n')
            {
                line_end = body + i;
                break;
            }
        }
        if (!line_end)
            goto fail;

        line = dup_range(line_start, line_end);
        if (!line)
            goto fail;
        if (strchr(line, ';'))
            *strchr(line, ';') = '\0';
        trim_in_place(line);
        chunk_size = strtoul(line, &end, 16);
        if (!end || *end != '\0')
        {
            free(line);
            goto fail;
        }
        free(line);

        pos = (size_t)(line_end - body) + 2;
        if (chunk_size == 0)
            break;
        if (chunk_size > body_len - pos)
            goto fail;
        if (!dynamic_buffer_append(&buffer, body + pos, (size_t)chunk_size))
            goto fail;
        pos += (size_t)chunk_size;
        if (pos + 1 >= body_len || body[pos] != '\r' || body[pos + 1] != '\n')
            goto fail;
        pos += 2;
    }

    *out = dynamic_buffer_detach(&buffer);
    *out_len = *out ? strlen(*out) : 0;
    return *out != NULL;

fail:
    dynamic_buffer_clear(&buffer);
    return false;
}

static bool http_extract_body_alloc(const char *raw,
                                    size_t raw_len,
                                    char **out,
                                    size_t *out_len)
{
    const char *headers_end;
    const char *body;
    size_t body_len;
    char *transfer_encoding = NULL;
    bool chunked = false;
    int status = 0;

    if (!raw || !out || !out_len)
        return false;

    *out = NULL;
    *out_len = 0;
    headers_end = strstr(raw, "\r\n\r\n");
    if (!headers_end)
        return false;
    headers_end += 4;
    if (sscanf(raw, "HTTP/%*u.%*u %d", &status) != 1 || status < 200 || status >= 300)
        return false;

    body = headers_end;
    body_len = raw_len - (size_t)(body - raw);
    if (header_value_dup(raw, headers_end, "Transfer-Encoding", &transfer_encoding))
    {
        chunked = transfer_encoding && strstr(transfer_encoding, "chunked");
        if (!chunked && transfer_encoding)
        {
            for (char *cursor = transfer_encoding; *cursor; ++cursor)
                *cursor = (char)tolower((unsigned char)*cursor);
            chunked = strstr(transfer_encoding, "chunked") != NULL;
        }
        free(transfer_encoding);
    }

    if (chunked)
        return decode_chunked_body_alloc(body, body_len, out, out_len);

    *out = malloc(body_len + 1);
    if (!*out)
        return false;
    memcpy(*out, body, body_len);
    (*out)[body_len] = '\0';
    *out_len = body_len;
    return true;
}

static bool hls_gateway_fetch_playlist_alloc(const ParsedHttpUrl *upstream,
                                             char **playlist,
                                             size_t *playlist_len)
{
    char *raw = NULL;
    size_t raw_len = 0;
    bool ok = false;

    if (!upstream || !playlist || !playlist_len)
        return false;

    *playlist = NULL;
    *playlist_len = 0;
    if (!http_fetch_raw_response_alloc(upstream, &raw, &raw_len))
        return false;

    ok = http_extract_body_alloc(raw, raw_len, playlist, playlist_len);
    free(raw);
    return ok;
}

static bool build_http_response(int status,
                                const char *status_text,
                                const char *content_type,
                                const char *body,
                                size_t body_len,
                                bool include_body,
                                char *response,
                                size_t response_size,
                                size_t *response_len)
{
    int written;

    if (!status_text || !content_type || !response || !response_len || response_size == 0)
        return false;

    written = snprintf(response, response_size,
                       "HTTP/1.1 %d %s\r\n"
                       "Content-Type: %s\r\n"
                       "Content-Length: %zu\r\n"
                       "Server: %s\r\n"
                       "Connection: close\r\n"
                       "\r\n",
                       status,
                       status_text,
                       content_type,
                       body_len,
                       dlna_server_info_get());
    if (written < 0 || (size_t)written >= response_size)
        return false;

    *response_len = (size_t)written;
    if (!include_body || !body || body_len == 0)
        return true;
    if (*response_len + body_len >= response_size)
        return false;

    memcpy(response + *response_len, body, body_len);
    *response_len += body_len;
    response[*response_len] = '\0';
    return true;
}

static bool build_text_response(int status,
                                const char *status_text,
                                const char *body,
                                char *response,
                                size_t response_size,
                                size_t *response_len)
{
    size_t body_len = body ? strlen(body) : 0;
    return build_http_response(status,
                               status_text,
                               "text/plain; charset=\"utf-8\"",
                               body ? body : "",
                               body_len,
                               true,
                               response,
                               response_size,
                               response_len);
}

bool hls_gateway_start(const char *url_base)
{
    char *copy = NULL;

    if (g_gateway.running)
        return true;

    if (url_base)
    {
        copy = strdup(url_base);
        if (!copy)
            return false;
    }

    g_gateway.url_base = copy;
    g_gateway.running = true;
    log_info("[hls-gateway] start url_base=%s\n", g_gateway.url_base ? g_gateway.url_base : "");
    return true;
}

void hls_gateway_stop(void)
{
    if (!g_gateway.running)
        return;

    log_info("[hls-gateway] stop begin\n");
    hls_gateway_clear_mapping();
    free(g_gateway.url_base);
    g_gateway.url_base = NULL;
    g_gateway.running = false;
    log_info("[hls-gateway] stop done\n");
}

bool hls_gateway_prepare_media_uri(const char *source_uri, char **playback_uri_out)
{
    ParsedHttpUrl parsed = {0};
    char *token = NULL;
    char *gateway_path = NULL;
    char *playback_uri = NULL;

    if (!source_uri || !playback_uri_out)
        return false;

    *playback_uri_out = NULL;
    if (!g_gateway.running || !g_gateway.url_base || !uri_is_http_hls(source_uri, &parsed))
    {
        *playback_uri_out = strdup(source_uri);
        return *playback_uri_out != NULL;
    }

    token = gateway_strdup_printf("%016llx", (unsigned long long)randomGet64());
    if (!token)
        goto fail;

    gateway_path = gateway_strdup_printf("/hls-gateway/%s/index.m3u8", token);
    playback_uri = gateway_strdup_printf("%shls-gateway/%s/index.m3u8", g_gateway.url_base, token);
    if (!gateway_path || !playback_uri)
        goto fail;

    hls_gateway_clear_mapping();
    g_gateway.token = token;
    g_gateway.gateway_path = gateway_path;
    g_gateway.source_uri = strdup(source_uri);
    if (!g_gateway.source_uri)
    {
        hls_gateway_clear_mapping();
        goto fail;
    }
    g_gateway.upstream = parsed;
    memset(&parsed, 0, sizeof(parsed));

    log_info("[hls-gateway] map source=%s local=%s\n", source_uri, playback_uri);
    *playback_uri_out = playback_uri;
    return true;

fail:
    free(token);
    free(gateway_path);
    free(playback_uri);
    parsed_http_url_clear(&parsed);
    return false;
}

bool hls_gateway_try_handle_http(const HttpRequestContext *ctx,
                                 char *response,
                                 size_t response_size,
                                 size_t *response_len)
{
    char path[256];
    char *playlist = NULL;
    size_t playlist_len = 0;
    char *rewritten = NULL;
    size_t rewritten_len = 0;
    bool include_body;
    const char *method;

    if (!ctx || !response || !response_len)
        return false;

    *response_len = 0;
    extract_gateway_request_path(ctx, path, sizeof(path));
    if (strncmp(path, "/hls-gateway/", 13) != 0)
        return false;
    method = ctx->method ? ctx->method : "";

    if (!g_gateway.running)
    {
        return build_text_response(503,
                                   "Service Unavailable",
                                   "HLS gateway is not running",
                                   response,
                                   response_size,
                                   response_len);
    }

    if (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0)
    {
        return build_text_response(405,
                                   "Method Not Allowed",
                                   "HLS gateway requires GET or HEAD",
                                   response,
                                   response_size,
                                   response_len);
    }

    if (!hls_gateway_path_matches(path))
    {
        log_warn("[hls-gateway] path mismatch request=%s expected=%s raw=%s\n",
                 path,
                 g_gateway.gateway_path ? g_gateway.gateway_path : "(null)",
                 ctx->raw_path ? ctx->raw_path : "(null)");
        return build_text_response(404,
                                   "Not Found",
                                   "Unknown HLS gateway path",
                                   response,
                                   response_size,
                                   response_len);
    }

    if (!hls_gateway_fetch_playlist_alloc(&g_gateway.upstream, &playlist, &playlist_len))
    {
        log_warn("[hls-gateway] upstream fetch failed uri=%s\n",
                 g_gateway.source_uri ? g_gateway.source_uri : "");
        return build_text_response(502,
                                   "Bad Gateway",
                                   "Failed to fetch upstream HLS playlist",
                                   response,
                                   response_size,
                                   response_len);
    }

    if (!rewrite_m3u8_playlist_alloc(playlist, &g_gateway.upstream, &rewritten, &rewritten_len))
    {
        free(playlist);
        return build_text_response(500,
                                   "Internal Server Error",
                                   "Failed to rewrite HLS playlist",
                                   response,
                                   response_size,
                                   response_len);
    }
    free(playlist);

    include_body = strcmp(method, "HEAD") != 0;
    if (!build_http_response(200,
                             "OK",
                             "text/plain; charset=\"utf-8\"",
                             rewritten,
                             rewritten_len,
                             include_body,
                             response,
                             response_size,
                             response_len))
    {
        free(rewritten);
        return false;
    }

    log_info("[hls-gateway] served path=%s bytes=%zu upstream=%s\n",
             path,
             rewritten_len,
             g_gateway.source_uri ? g_gateway.source_uri : "");
    free(rewritten);
    return true;
}
