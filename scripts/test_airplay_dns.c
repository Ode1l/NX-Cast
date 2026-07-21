#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "protocol/airplay/discovery/dns.h"

static int g_failures;

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

static uint16_t read_u16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t read_u32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static size_t write_name(uint8_t *output, size_t capacity, const char *name)
{
    size_t offset = 0u;
    const char *label = name;
    const char *cursor = name;

    for (;; ++cursor)
    {
        size_t size;
        if (*cursor != '.' && *cursor != '\0')
            continue;
        size = (size_t)(cursor - label);
        if (size == 0u || size > 63u || offset + size + 1u >= capacity)
            return 0u;
        output[offset++] = (uint8_t)size;
        memcpy(output + offset, label, size);
        offset += size;
        if (*cursor == '\0')
            break;
        label = cursor + 1;
    }
    output[offset++] = 0u;
    return offset;
}

static size_t build_query(uint8_t *output, size_t capacity,
                          uint16_t id, const char *name,
                          uint16_t type, bool unicast)
{
    size_t offset;

    if (capacity < 17u)
        return 0u;
    memset(output, 0, capacity);
    output[0] = (uint8_t)(id >> 8);
    output[1] = (uint8_t)id;
    output[5] = 1u;
    offset = 12u;
    offset += write_name(output + offset, capacity - offset, name);
    output[offset++] = (uint8_t)(type >> 8);
    output[offset++] = (uint8_t)type;
    output[offset++] = unicast ? 0x80u : 0u;
    output[offset++] = 1u;
    return offset;
}

static bool skip_name(const uint8_t *packet, size_t size, size_t *offset)
{
    unsigned jumps = 0u;
    size_t cursor = *offset;

    while (cursor < size)
    {
        uint8_t length = packet[cursor++];
        if ((length & 0xc0u) == 0xc0u)
        {
            if (cursor >= size || ++jumps > 8u)
                return false;
            cursor++;
            *offset = cursor;
            return true;
        }
        if (length == 0u)
        {
            *offset = cursor;
            return true;
        }
        if (length > 63u || cursor + length > size)
            return false;
        cursor += length;
    }
    return false;
}

static void check_record_ttls(const uint8_t *packet, size_t size, uint32_t expected)
{
    size_t offset = 12u;
    uint16_t count = read_u16(packet + 6u);

    CHECK(count == 4u);
    for (uint16_t index = 0u; index < count; ++index)
    {
        uint16_t rdata_size;
        CHECK(skip_name(packet, size, &offset));
        if (offset + 10u > size)
            return;
        CHECK(read_u32(packet + offset + 4u) == expected);
        rdata_size = read_u16(packet + offset + 8u);
        offset += 10u;
        CHECK(offset + rdata_size <= size);
        offset += rdata_size;
    }
    CHECK(offset == size);
}

static AirPlayDnsService make_service(void)
{
    AirPlayDnsService service = {0};
    static const uint8_t txt[] = {8, 'p', 'w', '=', 't', 'r', 'u', 'e', 'x'};

    snprintf(service.instance_name, sizeof(service.instance_name), "NX-Cast");
    snprintf(service.host_name, sizeof(service.host_name), "nx-cast-001122334455");
    service.port = 7000u;
    service.ipv4_address = inet_addr("192.168.1.7");
    memcpy(service.txt, txt, sizeof(txt));
    service.txt_size = sizeof(txt);
    return service;
}

static void test_records_and_queries(void)
{
    AirPlayDnsService service = make_service();
    uint8_t packet[AIRPLAY_DNS_PACKET_MAX];
    uint8_t query[AIRPLAY_DNS_PACKET_MAX];
    uint8_t response[AIRPLAY_DNS_PACKET_MAX];
    size_t packet_size = 0u;
    size_t query_size;
    size_t response_size = 0u;
    bool unicast = false;
    bool compressed = false;

    CHECK(airplay_dns_build_announcement(&service, 120u, packet, &packet_size));
    CHECK(packet_size > 12u && packet_size < AIRPLAY_DNS_PACKET_MAX);
    CHECK(read_u16(packet + 2u) == 0x8400u);
    check_record_ttls(packet, packet_size, 120u);
    for (size_t index = 12u; index + 1u < packet_size; ++index)
        compressed = compressed || (packet[index] & 0xc0u) == 0xc0u;
    CHECK(compressed);
    CHECK(!airplay_dns_packet_conflicts(&service, packet, packet_size));
    packet[packet_size - 1u] ^= 1u;
    CHECK(airplay_dns_packet_conflicts(&service, packet, packet_size));

    query_size = build_query(query, sizeof(query), 0x1234u,
                             "_airplay._tcp.local", AIRPLAY_DNS_TYPE_PTR, true);
    CHECK(query_size > 0u);
    CHECK(airplay_dns_build_query_response(&service, query, query_size, 120u,
                                           response, &response_size, &unicast));
    CHECK(unicast && read_u16(response) == 0x1234u);
    check_record_ttls(response, response_size, 120u);

    query_size = build_query(query, sizeof(query), 0x5678u,
                             "_airplay._tcp.local", AIRPLAY_DNS_TYPE_PTR, false);
    CHECK(airplay_dns_build_query_response(&service, query, query_size, 120u,
                                           response, &response_size, &unicast));
    CHECK(!unicast && read_u16(response) == 0u);
    query_size = build_query(query, sizeof(query), 1u,
                             "_http._tcp.local", AIRPLAY_DNS_TYPE_PTR, true);
    CHECK(!airplay_dns_build_query_response(&service, query, query_size, 120u,
                                            response, &response_size, &unicast));

    CHECK(airplay_dns_build_announcement(&service, 0u, packet, &packet_size));
    check_record_ttls(packet, packet_size, 0u);
}

static void test_malformed_queries(void)
{
    AirPlayDnsService service = make_service();
    uint8_t query[32] = {0};
    uint8_t response[AIRPLAY_DNS_PACKET_MAX];
    size_t response_size = 123u;
    bool unicast = true;

    query[5] = 1u;
    query[12] = 0xc0u;
    query[13] = 12u;
    query[14] = 0u;
    query[15] = AIRPLAY_DNS_TYPE_PTR;
    query[16] = 0u;
    query[17] = 1u;
    CHECK(!airplay_dns_build_query_response(&service, query, 18u, 120u,
                                            response, &response_size, &unicast));
    CHECK(response_size == 0u);
    CHECK(!unicast);
    CHECK(!airplay_dns_build_query_response(&service, query, 11u, 120u,
                                            response, &response_size, &unicast));
}

static void test_invalid_service_boundaries(void)
{
    AirPlayDnsService service = make_service();
    uint8_t packet[AIRPLAY_DNS_PACKET_MAX];
    size_t packet_size = 123u;

    service.txt_size = sizeof(service.txt) + 1u;
    CHECK(!airplay_dns_build_announcement(&service, 120u,
                                          packet, &packet_size));
    CHECK(packet_size == 0u);
}

static void test_raop_service_name(void)
{
    AirPlayDnsService service = make_service();
    uint8_t query[AIRPLAY_DNS_PACKET_MAX];
    uint8_t response[AIRPLAY_DNS_PACKET_MAX];
    size_t query_size;
    size_t response_size = 0u;
    bool unicast = false;

    snprintf(service.service_name, sizeof(service.service_name),
             "_raop._tcp.local");
    snprintf(service.instance_name, sizeof(service.instance_name),
             "001122334455@NX-Cast");
    query_size = build_query(query, sizeof(query), 0x4321u,
                             "_raop._tcp.local", AIRPLAY_DNS_TYPE_PTR, true);
    CHECK(airplay_dns_build_query_response(&service, query, query_size, 120u,
                                           response, &response_size, &unicast));
    CHECK(unicast && read_u16(response) == 0x4321u);
    check_record_ttls(response, response_size, 120u);

    query_size = build_query(query, sizeof(query), 0x1234u,
                             "_airplay._tcp.local", AIRPLAY_DNS_TYPE_PTR, true);
    CHECK(!airplay_dns_build_query_response(&service, query, query_size, 120u,
                                            response, &response_size, &unicast));
}

int main(void)
{
    test_records_and_queries();
    test_malformed_queries();
    test_invalid_service_boundaries();
    test_raop_service_name();
    if (g_failures != 0)
    {
        fprintf(stderr, "AirPlay DNS tests failed: %d\n", g_failures);
        return 1;
    }
    puts("AirPlay DNS tests passed");
    return 0;
}
