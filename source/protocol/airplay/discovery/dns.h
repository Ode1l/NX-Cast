#ifndef NXCAST_AIRPLAY_DISCOVERY_DNS_H
#define NXCAST_AIRPLAY_DISCOVERY_DNS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AIRPLAY_DNS_PACKET_MAX 1500u
#define AIRPLAY_DNS_NAME_MAX 255u
#define AIRPLAY_DNS_SERVICE_NAME_MAX 31u
#define AIRPLAY_DNS_TXT_MAX 512u

#define AIRPLAY_DNS_TYPE_A 1u
#define AIRPLAY_DNS_TYPE_PTR 12u
#define AIRPLAY_DNS_TYPE_TXT 16u
#define AIRPLAY_DNS_TYPE_SRV 33u
#define AIRPLAY_DNS_TYPE_ANY 255u

typedef struct
{
    char service_name[AIRPLAY_DNS_SERVICE_NAME_MAX + 1u];
    char instance_name[64];
    char host_name[64];
    uint16_t port;
    uint32_t ipv4_address;
    uint8_t txt[AIRPLAY_DNS_TXT_MAX];
    size_t txt_size;
} AirPlayDnsService;

bool airplay_dns_build_announcement(const AirPlayDnsService *service,
                                    uint32_t ttl,
                                    uint8_t output[AIRPLAY_DNS_PACKET_MAX],
                                    size_t *output_size);
bool airplay_dns_build_query_response(const AirPlayDnsService *service,
                                      const uint8_t *query,
                                      size_t query_size,
                                      uint32_t ttl,
                                      uint8_t output[AIRPLAY_DNS_PACKET_MAX],
                                      size_t *output_size,
                                      bool *unicast_response);
bool airplay_dns_packet_conflicts(const AirPlayDnsService *service,
                                  const uint8_t *packet,
                                  size_t packet_size);

#endif
