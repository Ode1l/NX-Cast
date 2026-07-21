#ifndef NXCAST_AIRPLAY_DISCOVERY_MDNS_H
#define NXCAST_AIRPLAY_DISCOVERY_MDNS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dns.h"

#define AIRPLAY_MDNS_DEFAULT_PORT 5353u
#define AIRPLAY_MDNS_DEVICE_ID_SIZE 6u
#define AIRPLAY_MDNS_PUBLIC_KEY_SIZE 32u
#define AIRPLAY_MDNS_PAIRING_ID_SIZE 37u
#define AIRPLAY_MDNS_FEATURE_LEGACY_PAIRING (UINT64_C(1) << 27)
#define AIRPLAY_MDNS_FEATURE_FAIRPLAY_VIDEO (UINT64_C(1) << 2)
#define AIRPLAY_MDNS_FEATURE_FAIRPLAY_SECURE_AUTH (UINT64_C(1) << 12)
#define AIRPLAY_MDNS_FEATURE_FAIRPLAY_AUTHENTICATION (UINT64_C(1) << 14)
#define AIRPLAY_MDNS_FEATURE_FAIRPLAY_AUTH_TYPE (UINT64_C(1) << 22)
#define AIRPLAY_MDNS_FEATURE_SCREEN_MIRROR (UINT64_C(1) << 7)
#define AIRPLAY_MDNS_FEATURE_SCREEN_ROTATE (UINT64_C(1) << 8)
#define AIRPLAY_MDNS_FEATURE_AUDIO (UINT64_C(1) << 9)
#define AIRPLAY_MDNS_FEATURE_VIDEO (UINT64_C(1) << 0)
#define AIRPLAY_MDNS_FEATURE_HLS (UINT64_C(1) << 4)
#define AIRPLAY_MDNS_FEATURE_RAOP_NOT_REQUIRED (UINT64_C(1) << 30)
#define AIRPLAY_MDNS_FEATURES_FAIRPLAY_AUTH \
    (AIRPLAY_MDNS_FEATURE_FAIRPLAY_VIDEO | \
     AIRPLAY_MDNS_FEATURE_FAIRPLAY_SECURE_AUTH | \
     AIRPLAY_MDNS_FEATURE_FAIRPLAY_AUTHENTICATION | \
     AIRPLAY_MDNS_FEATURE_FAIRPLAY_AUTH_TYPE)

/* UxPlay's legacy-PIN profile is required for current iOS mirroring clients. */
#define AIRPLAY_MDNS_FEATURES_MIRROR_COMPAT UINT64_C(0x5A7FFEE6)

typedef struct
{
    const char *friendly_name;
    uint16_t control_port;
    uint32_t ipv4_address;
    uint8_t device_id[AIRPLAY_MDNS_DEVICE_ID_SIZE];
    uint8_t public_key[AIRPLAY_MDNS_PUBLIC_KEY_SIZE];
    char pairing_id[AIRPLAY_MDNS_PAIRING_ID_SIZE];
    uint64_t features;
    bool pin_required;
#if defined(AIRPLAY_TESTING)
    uint16_t test_bind_port;
    uint16_t test_announcement_port;
    uint32_t test_announcement_address;
    bool test_skip_multicast_join;
#endif
} AirPlayMdnsConfig;

typedef enum
{
    AIRPLAY_MDNS_PHASE_STOPPED = 0,
    AIRPLAY_MDNS_PHASE_STARTING,
    AIRPLAY_MDNS_PHASE_SOCKET_READY,
    AIRPLAY_MDNS_PHASE_THREAD_STARTING,
    AIRPLAY_MDNS_PHASE_IDLE,
    AIRPLAY_MDNS_PHASE_ANNOUNCING,
    AIRPLAY_MDNS_PHASE_WAITING,
    AIRPLAY_MDNS_PHASE_PROCESSING,
    AIRPLAY_MDNS_PHASE_STOPPING,
    AIRPLAY_MDNS_PHASE_FAILED
} AirPlayMdnsPhase;

typedef struct
{
    AirPlayMdnsPhase phase;
    int mode;
    bool running;
    bool socket_open;
    uint64_t worker_heartbeat_age_ms;
    uint64_t select_iterations;
    uint64_t packets_received;
    uint64_t packets_sent;
} AirPlayMdnsDiagnostics;

bool airplay_mdns_start(const AirPlayMdnsConfig *config);
void airplay_mdns_stop(void);
bool airplay_mdns_is_running(void);
uint16_t airplay_mdns_bound_port(void);
bool airplay_mdns_instance_name(char *output, size_t output_size);
bool airplay_mdns_get_diagnostics(AirPlayMdnsDiagnostics *diagnostics_out);
const char *airplay_mdns_phase_name(AirPlayMdnsPhase phase);
bool airplay_mdns_build_txt_record(const AirPlayMdnsConfig *config,
                                   uint8_t output[AIRPLAY_DNS_TXT_MAX],
                                   size_t *output_size);

#endif
