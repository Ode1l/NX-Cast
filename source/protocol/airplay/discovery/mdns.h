#ifndef NXCAST_AIRPLAY_DISCOVERY_MDNS_H
#define NXCAST_AIRPLAY_DISCOVERY_MDNS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dns.h"

#define AIRPLAY_MDNS_DEFAULT_PORT 5353u
#define AIRPLAY_MDNS_DEVICE_ID_SIZE 6u
#define AIRPLAY_MDNS_PUBLIC_KEY_SIZE 32u
#define AIRPLAY_MDNS_FEATURE_LEGACY_PAIRING (UINT64_C(1) << 27)
#define AIRPLAY_MDNS_FEATURE_SCREEN_MIRROR (UINT64_C(1) << 7)
#define AIRPLAY_MDNS_FEATURE_SCREEN_ROTATE (UINT64_C(1) << 8)
#define AIRPLAY_MDNS_FEATURE_VIDEO (UINT64_C(1) << 0)
#define AIRPLAY_MDNS_FEATURE_HLS (UINT64_C(1) << 4)

typedef struct
{
    const char *friendly_name;
    uint16_t control_port;
    uint32_t ipv4_address;
    uint8_t device_id[AIRPLAY_MDNS_DEVICE_ID_SIZE];
    uint8_t public_key[AIRPLAY_MDNS_PUBLIC_KEY_SIZE];
    uint64_t features;
    bool pin_required;
#if defined(AIRPLAY_TESTING)
    uint16_t test_bind_port;
    uint16_t test_announcement_port;
    uint32_t test_announcement_address;
    bool test_skip_multicast_join;
#endif
} AirPlayMdnsConfig;

bool airplay_mdns_start(const AirPlayMdnsConfig *config);
void airplay_mdns_stop(void);
bool airplay_mdns_is_running(void);
uint16_t airplay_mdns_bound_port(void);
bool airplay_mdns_instance_name(char *output, size_t output_size);
bool airplay_mdns_build_txt_record(const AirPlayMdnsConfig *config,
                                   uint8_t output[AIRPLAY_DNS_TXT_MAX],
                                   size_t *output_size);

#endif
