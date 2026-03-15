#include "dlna_control.h"

#include <switch.h>
#include <stdio.h>
#include <arpa/inet.h>

void dlna_update_from_discovery(const DlnaDiscoveryResults *results)
{
    if (!results)
    {
        printf("[dlna] No discovery results provided.\n");
        return;
    }

    if (results->count == 0)
    {
        printf("[dlna] No SSDP devices detected yet.\n");
        return;
    }

    printf("[dlna] Preparing control sessions for %d device(s).\n", results->count);

    for (int i = 0; i < results->count; ++i)
    {
        const DlnaDevice *device = &results->devices[i];
        printf("[dlna] #%d -> %s:%d\n", i + 1,
               inet_ntoa(device->endpoint.sin_addr),
               ntohs(device->endpoint.sin_port));
        if (device->usn[0] != '\0')
            printf("         USN:%s\n", device->usn);
        if (device->st[0] != '\0')
            printf("         ST:%s\n", device->st);
    }

    printf("[dlna] (Placeholder) Control channel not implemented yet.\n");
}
