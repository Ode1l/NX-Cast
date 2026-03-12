#pragma once

#include <stdbool.h>
#include <netinet/in.h>

#define DISCOVERY_MAX_DEVICES 16

typedef struct
{
    struct sockaddr_in endpoint;
    char usn[256];
    char st[128];
} DiscoveryDevice;

typedef struct
{
    DiscoveryDevice devices[DISCOVERY_MAX_DEVICES];
    int count;
} DiscoveryResults;

// Run a single SSDP discovery probe and log responses.
bool discovery_run_ssdp(DiscoveryResults *results);

// Placeholder for future mDNS probing.
bool discovery_run_mdns(void);
