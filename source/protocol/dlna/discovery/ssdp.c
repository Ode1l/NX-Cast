#include "ssdp.h"

#include <switch.h>

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "log/log.h"

#define SSDP_PORT 1900
#define SSDP_MULTICAST "239.255.255.250"
// Keep a conservative stack budget here too. SSDP itself is simple, but
// repeated logging and response formatting make 0x10000 a safer floor.
#define SSDP_THREAD_STACK_SIZE 0x10000
static const char g_serviceTypeAvTransport[] = "urn:schemas-upnp-org:service:AVTransport:1";
static const char g_serviceTypeRenderingControl[] = "urn:schemas-upnp-org:service:RenderingControl:1";
static const char g_serviceTypeConnectionManager[] = "urn:schemas-upnp-org:service:ConnectionManager:1";

#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 16
#endif

typedef struct
{
    SsdpConfig config;                   // metadata advertised via SSDP
    int socket_fd;                       // multicast socket
    Thread thread;                       // background responder thread
    bool running;
    bool thread_started;
    char *local_ip;                      // cached local IPv4
    char *location;                      // http://<ip>:<port>/device.xml
} SsdpState;

static SsdpState g_ssdp;

static char *ssdp_strdup_printf(const char *fmt, ...)
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

static void ssdp_clear_cached_strings(void)
{
    free(g_ssdp.local_ip);
    free(g_ssdp.location);
    g_ssdp.local_ip = NULL;
    g_ssdp.location = NULL;
}

// Determine the outbound IPv4 address by connecting a dummy UDP socket.
static bool determine_local_ip(char **out)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    char *local_ip = NULL;
    if (sock < 0)
    {
        log_error("[ssdp] create socket failed: %s (%d)\n", strerror(errno), errno);
        return false;
    }

    struct sockaddr_in remote;
    memset(&remote, 0, sizeof(remote));
    remote.sin_family = AF_INET;
    remote.sin_port = htons(9);
    remote.sin_addr.s_addr = inet_addr("8.8.8.8");

    if (connect(sock, (struct sockaddr *)&remote, sizeof(remote)) < 0)
    {
        log_error("[ssdp] connect failed: %s (%d)\n", strerror(errno), errno);
        close(sock);
        return false;
    }

    struct sockaddr_in local;
    socklen_t addr_len = sizeof(local);
    if (getsockname(sock, (struct sockaddr *)&local, &addr_len) < 0)
    {
        log_error("[ssdp] getsockname failed: %s (%d)\n", strerror(errno), errno);
        close(sock);
        return false;
    }

    local_ip = malloc(INET_ADDRSTRLEN);
    if (!local_ip)
    {
        close(sock);
        return false;
    }

    if (!inet_ntop(AF_INET, &local.sin_addr, local_ip, INET_ADDRSTRLEN))
    {
        log_error("[ssdp] inet_ntop failed: %s (%d)\n", strerror(errno), errno);
        close(sock);
        free(local_ip);
        return false;
    }

    close(sock);
    *out = local_ip;
    return true;
}

static bool create_socket(SsdpState *state)
{
    state->socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (state->socket_fd < 0)
    {
        log_error("[ssdp] socket creation failed: %s (%d)\n", strerror(errno), errno);
        return false;
    }

    int reuse = 1;
    setsockopt(state->socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SSDP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(state->socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        log_error("[ssdp] bind failed: %s (%d)\n", strerror(errno), errno);
        close(state->socket_fd);
        state->socket_fd = -1;
        return false;
    }

    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(SSDP_MULTICAST);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(state->socket_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
    {
        log_error("[ssdp] multicast join failed: %s (%d)\n", strerror(errno), errno);
        close(state->socket_fd);
        state->socket_fd = -1;
        return false;
    }

    return true;
}

// Find a header line (case-insensitive) and copy its value.
static bool get_header_value_alloc(const char *packet, const char *header, char **out)
{
    size_t header_len = strlen(header);
    const char *cursor = packet;
    char *value = NULL;

    if (!out)
        return false;
    *out = NULL;

    while (*cursor)
    {
        if (strncasecmp(cursor, header, header_len) == 0 && cursor[header_len] == ':')
        {
            cursor += header_len + 1;
            while (*cursor == ' ' || *cursor == '\t')
                ++cursor;
            const char *start = cursor;
            while (*cursor && *cursor != '\r' && *cursor != '\n')
                ++cursor;
            size_t len = (size_t)(cursor - start);
            value = malloc(len + 1);
            if (!value)
                return false;
            memcpy(value, start, len);
            value[len] = '\0';
            while (len > 0 && isspace((unsigned char)value[len - 1]))
                value[--len] = '\0';
            *out = value;
            return true;
        }

        const char *newline = strchr(cursor, '\n');
        if (!newline)
            break;
        cursor = newline + 1;
    }
    return false;
}

// Build and send a 200 OK response for an incoming M-SEARCH.
static void send_msearch_response(const char *st, const char *usn, const struct sockaddr_in *from)
{
    char *response;
    size_t response_len;

    if (g_ssdp.socket_fd < 0)
        return;

    if (!st || st[0] == '\0' || !usn || usn[0] == '\0')
        return;

    response = ssdp_strdup_printf("HTTP/1.1 200 OK\r\n"
                                  "CACHE-CONTROL: max-age=1800\r\n"
                                  "DATE: Sat, 01 Jan 2000 00:00:00 GMT\r\n"
                                  "EXT:\r\n"
                                  "LOCATION: %s\r\n"
                                  "SERVER: NintendoSwitch/1.0 UPnP/1.1 NX-Cast/0.1\r\n"
                                  "ST: %s\r\n"
                                  "USN: %s\r\n"
                                  "\r\n",
                                  g_ssdp.location, st, usn);
    if (!response)
        return;
    response_len = strlen(response);

    ssize_t sent = sendto(g_ssdp.socket_fd, response, response_len, 0,
                          (const struct sockaddr *)from, sizeof(*from));
    if (sent < 0)
    {
        log_warn("[ssdp] sendto failed: %s (%d)\n", strerror(errno), errno);
        free(response);
        return;
    }

    log_info("[ssdp] send packet to %s:%d st=%s bytes=%zd\n",
             inet_ntoa(from->sin_addr), ntohs(from->sin_port), st, sent);
    free(response);
}

static char *ssdp_recv_packet_alloc(int socket_fd, struct sockaddr_in *from, ssize_t *out_len)
{
    char discard = '\0';
    socklen_t from_len;
    ssize_t needed;
    ssize_t received;
    char *buffer;

    if (!from || !out_len)
        return NULL;

    *out_len = -1;
    from_len = sizeof(*from);
    needed = recvfrom(socket_fd,
                      &discard,
                      sizeof(discard),
                      MSG_PEEK | MSG_TRUNC,
                      (struct sockaddr *)from,
                      &from_len);
    if (needed <= 0)
        return NULL;

    buffer = malloc((size_t)needed + 1);
    if (!buffer)
    {
        from_len = sizeof(*from);
        (void)recvfrom(socket_fd, &discard, sizeof(discard), 0, (struct sockaddr *)from, &from_len);
        return NULL;
    }

    from_len = sizeof(*from);
    received = recvfrom(socket_fd,
                        buffer,
                        (size_t)needed,
                        0,
                        (struct sockaddr *)from,
                        &from_len);
    if (received <= 0)
    {
        free(buffer);
        return NULL;
    }

    buffer[received] = '\0';
    *out_len = received;
    return buffer;
}

static void respond_to_msearch(const char *st_value, const struct sockaddr_in *from)
{
    if (!st_value || st_value[0] == '\0')
        return;

    char *root_usn = ssdp_strdup_printf("%s::upnp:rootdevice", g_ssdp.config.uuid);
    char *device_usn = ssdp_strdup_printf("%s::%s", g_ssdp.config.uuid, g_ssdp.config.device_type);
    char *avtransport_usn = ssdp_strdup_printf("%s::%s", g_ssdp.config.uuid, g_serviceTypeAvTransport);
    char *renderingcontrol_usn = ssdp_strdup_printf("%s::%s", g_ssdp.config.uuid, g_serviceTypeRenderingControl);
    char *connectionmanager_usn = ssdp_strdup_printf("%s::%s", g_ssdp.config.uuid, g_serviceTypeConnectionManager);

    if (!root_usn || !device_usn || !avtransport_usn || !renderingcontrol_usn || !connectionmanager_usn)
        goto cleanup;

    // Reply only to known/expected ST values.
    if (strcasecmp(st_value, "ssdp:all") == 0)
    {
        send_msearch_response("upnp:rootdevice", root_usn, from);
        send_msearch_response(g_ssdp.config.device_type, device_usn, from);
        send_msearch_response(g_ssdp.config.uuid, g_ssdp.config.uuid, from);
        send_msearch_response(g_serviceTypeAvTransport, avtransport_usn, from);
        send_msearch_response(g_serviceTypeRenderingControl, renderingcontrol_usn, from);
        send_msearch_response(g_serviceTypeConnectionManager, connectionmanager_usn, from);
        goto cleanup;
    }

    if (strcasecmp(st_value, "upnp:rootdevice") == 0)
    {
        send_msearch_response("upnp:rootdevice", root_usn, from);
        goto cleanup;
    }

    if (strcasecmp(st_value, g_ssdp.config.device_type) == 0)
    {
        send_msearch_response(g_ssdp.config.device_type, device_usn, from);
        goto cleanup;
    }

    if (strcasecmp(st_value, g_ssdp.config.uuid) == 0)
    {
        send_msearch_response(g_ssdp.config.uuid, g_ssdp.config.uuid, from);
        goto cleanup;
    }

    if (strcasecmp(st_value, g_serviceTypeAvTransport) == 0)
    {
        send_msearch_response(g_serviceTypeAvTransport, avtransport_usn, from);
        goto cleanup;
    }

    if (strcasecmp(st_value, g_serviceTypeRenderingControl) == 0)
    {
        send_msearch_response(g_serviceTypeRenderingControl, renderingcontrol_usn, from);
        goto cleanup;
    }

    if (strcasecmp(st_value, g_serviceTypeConnectionManager) == 0)
    {
        send_msearch_response(g_serviceTypeConnectionManager, connectionmanager_usn, from);
        goto cleanup;
    }

cleanup:
    free(root_usn);
    free(device_usn);
    free(avtransport_usn);
    free(renderingcontrol_usn);
    free(connectionmanager_usn);
}

// Basic parser that filters for SSDP discovery packets.
static void handle_packet(char *packet, ssize_t length, const struct sockaddr_in *from)
{
    char *man = NULL;
    char *st = NULL;

    if (length <= 0)
        return;
    packet[length] = '\0';
    log_debug("[ssdp] recv packet from %s:%d bytes=%zd\n",
              inet_ntoa(from->sin_addr), ntohs(from->sin_port), length);

    if (strncasecmp(packet, "M-SEARCH", 8) != 0)
        return;

    if (!get_header_value_alloc(packet, "MAN", &man))
        return;
    if (strcasecmp(man, "\"ssdp:discover\"") != 0)
    {
        free(man);
        return;
    }

    if (!get_header_value_alloc(packet, "ST", &st))
    {
        free(man);
        return;
    }
    log_debug("[ssdp] M-SEARCH ST=%s\n", st);

    respond_to_msearch(st, from);
    free(man);
    free(st);
}

// Background thread: wait on the socket and process any packets.
static void ssdp_thread(void *arg)
{
    (void)arg;
    while (g_ssdp.running)
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(g_ssdp.socket_fd, &readfds);

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

        int ret = select(g_ssdp.socket_fd + 1, &readfds, NULL, NULL, &timeout);
        if (ret < 0)
        {
            if (errno == EINTR)
                continue;
            if (!g_ssdp.running)
                break;
            log_error("[ssdp] select failed: %s (%d)\n", strerror(errno), errno);
            break;
        }

        if (ret == 0)
            continue;

        if (FD_ISSET(g_ssdp.socket_fd, &readfds))
        {
            struct sockaddr_in from;
            ssize_t len = -1;
            char *buffer = ssdp_recv_packet_alloc(g_ssdp.socket_fd, &from, &len);

            if (buffer && len > 0)
                handle_packet(buffer, len, &from);
            free(buffer);
        }
    }
}

bool ssdp_start(const SsdpConfig *config)
{
    if (g_ssdp.running)
        return true;

    if (!config || !config->device_type || !config->uuid || !config->location_path)
    {
        log_error("[ssdp] Invalid configuration provided.\n");
        return false;
    }

    memset(&g_ssdp, 0, sizeof(g_ssdp));
    g_ssdp.config = *config;
    g_ssdp.socket_fd = -1;

    if (!determine_local_ip(&g_ssdp.local_ip))
        return false;

    g_ssdp.location = ssdp_strdup_printf("http://%s:%u%s",
                                         g_ssdp.local_ip,
                                         g_ssdp.config.http_port,
                                         g_ssdp.config.location_path);
    if (!g_ssdp.location)
    {
        ssdp_clear_cached_strings();
        return false;
    }

    if (!create_socket(&g_ssdp))
        return false;

    g_ssdp.running = true;
    Result rc = threadCreate(&g_ssdp.thread, ssdp_thread, NULL, NULL, SSDP_THREAD_STACK_SIZE, 0x2B, -2);
    if (R_FAILED(rc))
    {
        log_error("[ssdp] threadCreate failed: 0x%08X\n", rc);
        g_ssdp.running = false;
        ssdp_clear_cached_strings();
        close(g_ssdp.socket_fd);
        g_ssdp.socket_fd = -1;
        return false;
    }

    rc = threadStart(&g_ssdp.thread);
    if (R_FAILED(rc))
    {
        log_error("[ssdp] threadStart failed: 0x%08X\n", rc);
        g_ssdp.running = false;
        threadClose(&g_ssdp.thread);
        ssdp_clear_cached_strings();
        close(g_ssdp.socket_fd);
        g_ssdp.socket_fd = -1;
        return false;
    }

    g_ssdp.thread_started = true;
    log_info("[ssdp] Responder ready on %s\n", g_ssdp.location);
    return true;
}

void ssdp_stop(void)
{
    if (!g_ssdp.running)
        return;

    g_ssdp.running = false;

    if (g_ssdp.socket_fd >= 0)
    {
        int fd = g_ssdp.socket_fd;
        g_ssdp.socket_fd = -1;
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }

    if (g_ssdp.thread_started)
    {
        threadWaitForExit(&g_ssdp.thread);
        threadClose(&g_ssdp.thread);
        g_ssdp.thread_started = false;
    }

    ssdp_clear_cached_strings();
    log_info("[ssdp] Responder stopped.\n");
}
