#include "ssdp.h"

#include <switch.h>

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "log/log.h"

#define SSDP_PORT 1900
#define SSDP_MULTICAST "239.255.255.250"

typedef struct
{
    SsdpConfig config;
    int socket_fd;
    Thread thread;
    bool running;
    bool thread_started;
    char local_ip[INET_ADDRSTRLEN];
    char location[128];
} SsdpState;

static SsdpState g_ssdp;

static bool determine_local_ip(char *out, size_t len)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
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

    if (!inet_ntop(AF_INET, &local.sin_addr, out, len))
    {
        log_error("[ssdp] inet_ntop failed: %s (%d)\n", strerror(errno), errno);
        close(sock);
        return false;
    }

    close(sock);
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

static bool get_header_value(const char *packet, const char *header, char *out, size_t out_len)
{
    size_t header_len = strlen(header);
    const char *cursor = packet;
    while (*cursor)
    {
        if (strncasecmp(cursor, header, header_len) == 0 && cursor[header_len] == ':')
        {
            cursor += header_len + 1;
            while (*cursor == ' ' || *cursor == '\t')
                ++cursor;
            size_t i = 0;
            while (*cursor && *cursor != '\r' && *cursor != '\n' && i + 1 < out_len)
                out[i++] = *cursor++;
            out[i] = '\0';
            while (i > 0 && isspace((unsigned char)out[i - 1]))
                out[--i] = '\0';
            return true;
        }

        const char *newline = strchr(cursor, '\n');
        if (!newline)
            break;
        cursor = newline + 1;
    }
    return false;
}

static void respond_to_msearch(const char *st_value, const struct sockaddr_in *from)
{
    if (g_ssdp.socket_fd < 0)
        return;

    const char *st = g_ssdp.config.device_type;
    if (st_value && strnlen(st_value, 128) > 0)
    {
        if (strcasecmp(st_value, "ssdp:all") == 0)
            st = g_ssdp.config.device_type;
        else if (strcasecmp(st_value, "upnp:rootdevice") == 0)
            st = "upnp:rootdevice";
        else
            st = st_value;
    }

    char usn[256];
    if (strcasecmp(st, "upnp:rootdevice") == 0)
        snprintf(usn, sizeof(usn), "%s::upnp:rootdevice", g_ssdp.config.uuid);
    else if (strcasecmp(st, g_ssdp.config.device_type) == 0)
        snprintf(usn, sizeof(usn), "%s::%s", g_ssdp.config.uuid, g_ssdp.config.device_type);
    else
        snprintf(usn, sizeof(usn), "%s::%s", g_ssdp.config.uuid, st);

    char response[768];
    int len = snprintf(response, sizeof(response),
                       "HTTP/1.1 200 OK\r\n"
                       "CACHE-CONTROL: max-age=1800\r\n"
                       "DATE: Sat, 01 Jan 2000 00:00:00 GMT\r\n"
                       "EXT:\r\n"
                       "LOCATION: %s\r\n"
                       "SERVER: NintendoSwitch/1.0 UPnP/1.1 NX-Cast/0.1\r\n"
                       "ST: %s\r\n"
                       "USN: %s\r\n"
                       "\r\n",
                       g_ssdp.location, st, usn);

    if (len <= 0)
        return;

    sendto(g_ssdp.socket_fd, response, (size_t)len, 0,
           (const struct sockaddr *)from, sizeof(*from));
    log_info("[ssdp] Responded to M-SEARCH from %s:%d (%s)\n",
             inet_ntoa(from->sin_addr), ntohs(from->sin_port), st);
}

static void handle_packet(char *packet, ssize_t length, const struct sockaddr_in *from)
{
    if (length <= 0)
        return;
    packet[length] = '\0';

    if (strncasecmp(packet, "M-SEARCH", 8) != 0)
        return;

    char man[64];
    if (!get_header_value(packet, "MAN", man, sizeof(man)))
        return;
    if (strcasecmp(man, "\"ssdp:discover\"") != 0)
        return;

    char st[128];
    if (!get_header_value(packet, "ST", st, sizeof(st)))
        return;

    respond_to_msearch(st, from);
}

static void ssdp_thread(void *arg)
{
    (void)arg;
    while (g_ssdp.running)
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(g_ssdp.socket_fd, &readfds);

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int ret = select(g_ssdp.socket_fd + 1, &readfds, NULL, NULL, &timeout);
        if (ret < 0)
        {
            if (errno == EINTR)
                continue;
            log_error("[ssdp] select failed: %s (%d)\n", strerror(errno), errno);
            break;
        }

        if (ret == 0)
            continue;

        if (FD_ISSET(g_ssdp.socket_fd, &readfds))
        {
            char buffer[1024];
            struct sockaddr_in from;
            socklen_t from_len = sizeof(from);
            ssize_t len = recvfrom(g_ssdp.socket_fd, buffer, sizeof(buffer) - 1, 0,
                                   (struct sockaddr *)&from, &from_len);
            if (len > 0)
                handle_packet(buffer, len, &from);
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

    if (!determine_local_ip(g_ssdp.local_ip, sizeof(g_ssdp.local_ip)))
        return false;

    snprintf(g_ssdp.location, sizeof(g_ssdp.location),
             "http://%s:%u%s",
             g_ssdp.local_ip, g_ssdp.config.http_port, g_ssdp.config.location_path);

    if (!create_socket(&g_ssdp))
        return false;

    g_ssdp.running = true;
    Result rc = threadCreate(&g_ssdp.thread, ssdp_thread, NULL, NULL, 0x4000, 0x2B, -2);
    if (R_FAILED(rc))
    {
        log_error("[ssdp] threadCreate failed: 0x%08X\n", rc);
        g_ssdp.running = false;
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
        close(g_ssdp.socket_fd);
        g_ssdp.socket_fd = -1;
        return false;
    }

    g_ssdp.thread_started = true;
    log_info("[ssdp] Responder ready on %s (Location %s)\n", g_ssdp.local_ip, g_ssdp.location);
    return true;
}

void ssdp_stop(void)
{
    if (!g_ssdp.running)
        return;

    g_ssdp.running = false;

    if (g_ssdp.thread_started)
    {
        threadWaitForExit(&g_ssdp.thread);
        threadClose(&g_ssdp.thread);
        g_ssdp.thread_started = false;
    }

    if (g_ssdp.socket_fd >= 0)
    {
        close(g_ssdp.socket_fd);
        g_ssdp.socket_fd = -1;
    }

    log_info("[ssdp] Responder stopped.\n");
}
