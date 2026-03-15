#pragma once

#include <stdbool.h>
#include <netinet/in.h>

#define DLNA_MAX_DEVICES 16

typedef struct
{
    struct sockaddr_in endpoint;
    char usn[256];
    char st[128];
} DlnaDevice;

typedef struct
{
    DlnaDevice devices[DLNA_MAX_DEVICES];
    int count;
} DlnaDiscoveryResults;

// Broadcast SSDP M-SEARCH and cache matching DLNA devices.
bool ssdp_discover(DlnaDiscoveryResults *results);
