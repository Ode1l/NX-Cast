#include "discovery.h"

#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/time.h>

static void store_device(DiscoveryResults *results, const struct sockaddr_in *endpoint,
                         const char *usn, const char *st)
{
    if (!results || results->count >= DISCOVERY_MAX_DEVICES)
        return;

    DiscoveryDevice *device = &results->devices[results->count++];
    device->endpoint = *endpoint;
    snprintf(device->usn, sizeof(device->usn), "%s", usn ? usn : "");
    snprintf(device->st, sizeof(device->st), "%s", st ? st : "");
}

static void parse_headers(char *buffer, DiscoveryResults *results, const struct sockaddr_in *from)
{
    char *line = strtok(buffer, "\r\n");
    char usn[256] = {0};
    char st[128] = {0};

    while (line)
    {
        if (strncasecmp(line, "USN:", 4) == 0)
            strncpy(usn, line + 4, sizeof(usn) - 1);
        else if (strncasecmp(line, "ST:", 3) == 0)
            strncpy(st, line + 3, sizeof(st) - 1);
        line = strtok(NULL, "\r\n");
    }

    // Trim leading spaces
    char *usn_trim = usn;
    while (*usn_trim == ' ')
        ++usn_trim;

    char *st_trim = st;
    while (*st_trim == ' ')
        ++st_trim;

    store_device(results, from, usn_trim, st_trim);
}

bool discovery_run_ssdp(DiscoveryResults *results)
{
    static const char request[] =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 1\r\n"
        "ST: ssdp:all\r\n\r\n";

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
        printf("[ssdp] socket() failed: %d\n", errno);
        return false;
    }

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(1900);
    dest.sin_addr.s_addr = inet_addr("239.255.255.250");

    ssize_t sent = sendto(sock, request, sizeof(request) - 1, 0, (struct sockaddr*)&dest, sizeof(dest));
    if (sent < 0)
    {
        printf("[ssdp] sendto() failed: %d\n", errno);
        close(sock);
        return false;
    }

    printf("[ssdp] Sent M-SEARCH, waiting for responses...\n");

    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    if (results)
        results->count = 0;

    bool receivedAny = false;

    for (int i = 0; i < 5; ++i)
    {
        char buffer[1024];
        struct sockaddr_in from;
        socklen_t fromLen = sizeof(from);
        ssize_t received = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&from, &fromLen);
        if (received < 0)
        {
            if (errno == EWOULDBLOCK || errno == EAGAIN)
                break;
            continue;
        }

        buffer[received] = '\0';
        printf("[ssdp] Response from %s:%d\n", inet_ntoa(from.sin_addr), ntohs(from.sin_port));
        printf("%s\n", buffer);
        if (results)
            parse_headers(buffer, results, &from);
        receivedAny = true;
    }

    close(sock);
    return receivedAny;
}

bool discovery_run_mdns(void)
{
    printf("[mdns] Placeholder: mDNS probing not yet implemented.\n");
    return false;
}
