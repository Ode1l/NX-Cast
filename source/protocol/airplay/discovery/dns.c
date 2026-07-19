#include "dns.h"

#include <string.h>
#include <strings.h>

#define DNS_HEADER_SIZE 12u
#define DNS_CLASS_IN 1u
#define DNS_CLASS_CACHE_FLUSH 0x8000u
#define DNS_CLASS_UNICAST_RESPONSE 0x8000u
#define DNS_FLAGS_RESPONSE_AUTHORITATIVE 0x8400u
#define DNS_POINTER_MASK 0xc000u
#define DNS_POINTER_VALUE 0xc000u
#define DNS_MAX_QUESTIONS 32u
#define DNS_MAX_RECORDS 64u
#define DNS_MAX_COMPRESSION_JUMPS 16u

static const char g_service_name[] = "_airplay._tcp.local";

typedef struct
{
    uint8_t *data;
    size_t capacity;
    size_t offset;
} DnsWriter;

static uint16_t read_u16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static bool write_bytes(DnsWriter *writer, const void *data, size_t size)
{
    if (!writer || !data || size > writer->capacity - writer->offset)
        return false;
    memcpy(writer->data + writer->offset, data, size);
    writer->offset += size;
    return true;
}

static bool write_u8(DnsWriter *writer, uint8_t value)
{
    return write_bytes(writer, &value, sizeof(value));
}

static bool write_u16(DnsWriter *writer, uint16_t value)
{
    uint8_t bytes[2] = {(uint8_t)(value >> 8), (uint8_t)value};
    return write_bytes(writer, bytes, sizeof(bytes));
}

static bool write_u32(DnsWriter *writer, uint32_t value)
{
    uint8_t bytes[4] = {
        (uint8_t)(value >> 24), (uint8_t)(value >> 16),
        (uint8_t)(value >> 8), (uint8_t)value};
    return write_bytes(writer, bytes, sizeof(bytes));
}

static bool patch_u16(DnsWriter *writer, size_t offset, uint16_t value)
{
    if (!writer || offset + 2u > writer->capacity || offset + 2u > writer->offset)
        return false;
    writer->data[offset] = (uint8_t)(value >> 8);
    writer->data[offset + 1u] = (uint8_t)value;
    return true;
}

static bool write_label(DnsWriter *writer, const char *label, size_t size)
{
    return size > 0u && size <= 63u && write_u8(writer, (uint8_t)size) &&
           write_bytes(writer, label, size);
}

static bool write_name(DnsWriter *writer, const char *name)
{
    const char *label = name;
    const char *cursor = name;
    size_t total = 1u;

    if (!writer || !name || !name[0])
        return false;
    for (;; ++cursor)
    {
        if (*cursor != '.' && *cursor != '\0')
            continue;
        if (!write_label(writer, label, (size_t)(cursor - label)))
            return false;
        total += (size_t)(cursor - label) + 1u;
        if (total > AIRPLAY_DNS_NAME_MAX)
            return false;
        if (*cursor == '\0')
            break;
        label = cursor + 1;
    }
    return write_u8(writer, 0u);
}

static bool write_pointer(DnsWriter *writer, size_t offset)
{
    return offset < DNS_POINTER_MASK &&
           write_u16(writer, (uint16_t)(DNS_POINTER_VALUE | offset));
}

static bool read_name(const uint8_t *packet, size_t packet_size, size_t *offset,
                      char output[AIRPLAY_DNS_NAME_MAX + 1u])
{
    size_t cursor;
    size_t next_offset = 0u;
    size_t output_size = 0u;
    unsigned jumps = 0u;
    bool jumped = false;

    if (!packet || !offset || *offset >= packet_size || !output)
        return false;
    cursor = *offset;
    while (cursor < packet_size)
    {
        uint8_t length = packet[cursor++];
        if ((length & 0xc0u) == 0xc0u)
        {
            uint16_t pointer;
            if (cursor >= packet_size || ++jumps > DNS_MAX_COMPRESSION_JUMPS)
                return false;
            pointer = (uint16_t)(((uint16_t)(length & 0x3fu) << 8) | packet[cursor++]);
            if (pointer >= packet_size || pointer >= cursor - 2u)
                return false;
            if (!jumped)
            {
                next_offset = cursor;
                jumped = true;
            }
            cursor = pointer;
            continue;
        }
        if ((length & 0xc0u) != 0u || length > 63u || cursor + length > packet_size)
            return false;
        if (length == 0u)
        {
            output[output_size] = '\0';
            *offset = jumped ? next_offset : cursor;
            return true;
        }
        if (output_size != 0u)
        {
            if (output_size >= AIRPLAY_DNS_NAME_MAX)
                return false;
            output[output_size++] = '.';
        }
        if (length > AIRPLAY_DNS_NAME_MAX - output_size)
            return false;
        memcpy(output + output_size, packet + cursor, length);
        output_size += length;
        cursor += length;
    }
    return false;
}

static bool service_valid(const AirPlayDnsService *service)
{
    return service && service->instance_name[0] && service->host_name[0] &&
           service->port != 0u && service->ipv4_address != 0u &&
           service->txt_size > 0u && service->txt_size <= sizeof(service->txt);
}

static bool make_instance_full(const AirPlayDnsService *service,
                               char output[AIRPLAY_DNS_NAME_MAX + 1u])
{
    size_t instance_size;
    size_t service_size = strlen(g_service_name);

    if (!service)
        return false;
    instance_size = strlen(service->instance_name);
    if (instance_size == 0u || instance_size > 63u ||
        instance_size + 1u + service_size > AIRPLAY_DNS_NAME_MAX)
        return false;
    memcpy(output, service->instance_name, instance_size);
    output[instance_size] = '.';
    memcpy(output + instance_size + 1u, g_service_name, service_size + 1u);
    return true;
}

static bool make_host_full(const AirPlayDnsService *service,
                           char output[AIRPLAY_DNS_NAME_MAX + 1u])
{
    size_t host_size;
    static const char suffix[] = ".local";

    if (!service)
        return false;
    host_size = strlen(service->host_name);
    if (host_size == 0u || host_size > 63u ||
        host_size + sizeof(suffix) - 1u > AIRPLAY_DNS_NAME_MAX)
        return false;
    memcpy(output, service->host_name, host_size);
    memcpy(output + host_size, suffix, sizeof(suffix));
    return true;
}

static bool write_record_header(DnsWriter *writer, size_t owner_offset,
                                uint16_t type, bool unique, uint32_t ttl,
                                size_t *length_offset, size_t *rdata_offset)
{
    if (!write_pointer(writer, owner_offset) || !write_u16(writer, type) ||
        !write_u16(writer, (uint16_t)(DNS_CLASS_IN |
                                     (unique ? DNS_CLASS_CACHE_FLUSH : 0u))) ||
        !write_u32(writer, ttl))
        return false;
    *length_offset = writer->offset;
    if (!write_u16(writer, 0u))
        return false;
    *rdata_offset = writer->offset;
    return true;
}

static bool build_record_set(const AirPlayDnsService *service, uint16_t id,
                             uint32_t ttl, uint8_t output[AIRPLAY_DNS_PACKET_MAX],
                             size_t *output_size)
{
    DnsWriter writer = {output, AIRPLAY_DNS_PACKET_MAX, 0u};
    char instance_full[AIRPLAY_DNS_NAME_MAX + 1u];
    char host_full[AIRPLAY_DNS_NAME_MAX + 1u];
    size_t service_offset;
    size_t instance_offset;
    size_t host_offset;
    size_t length_offset;
    size_t rdata_offset;

    if (!service_valid(service) || !output || !output_size ||
        !make_instance_full(service, instance_full) ||
        !make_host_full(service, host_full))
        return false;
    memset(output, 0, AIRPLAY_DNS_PACKET_MAX);
    if (!write_u16(&writer, id) ||
        !write_u16(&writer, DNS_FLAGS_RESPONSE_AUTHORITATIVE) ||
        !write_u16(&writer, 0u) || !write_u16(&writer, 4u) ||
        !write_u16(&writer, 0u) || !write_u16(&writer, 0u))
        return false;

    service_offset = writer.offset;
    if (!write_name(&writer, g_service_name) ||
        !write_u16(&writer, AIRPLAY_DNS_TYPE_PTR) ||
        !write_u16(&writer, DNS_CLASS_IN) || !write_u32(&writer, ttl))
        return false;
    length_offset = writer.offset;
    if (!write_u16(&writer, 0u))
        return false;
    rdata_offset = writer.offset;
    instance_offset = writer.offset;
    if (!write_label(&writer, service->instance_name, strlen(service->instance_name)) ||
        !write_pointer(&writer, service_offset) ||
        !patch_u16(&writer, length_offset, (uint16_t)(writer.offset - rdata_offset)))
        return false;

    if (!write_record_header(&writer, instance_offset, AIRPLAY_DNS_TYPE_SRV,
                             true, ttl, &length_offset, &rdata_offset) ||
        !write_u16(&writer, 0u) || !write_u16(&writer, 0u) ||
        !write_u16(&writer, service->port))
        return false;
    host_offset = writer.offset;
    if (!write_name(&writer, host_full) ||
        !patch_u16(&writer, length_offset, (uint16_t)(writer.offset - rdata_offset)))
        return false;

    if (!write_record_header(&writer, instance_offset, AIRPLAY_DNS_TYPE_TXT,
                             true, ttl, &length_offset, &rdata_offset) ||
        !write_bytes(&writer, service->txt, service->txt_size) ||
        !patch_u16(&writer, length_offset, (uint16_t)(writer.offset - rdata_offset)))
        return false;

    if (!write_record_header(&writer, host_offset, AIRPLAY_DNS_TYPE_A,
                             true, ttl, &length_offset, &rdata_offset) ||
        !write_bytes(&writer, &service->ipv4_address, sizeof(service->ipv4_address)) ||
        !patch_u16(&writer, length_offset, (uint16_t)(writer.offset - rdata_offset)))
        return false;
    *output_size = writer.offset;
    return true;
}

bool airplay_dns_build_announcement(const AirPlayDnsService *service,
                                    uint32_t ttl,
                                    uint8_t output[AIRPLAY_DNS_PACKET_MAX],
                                    size_t *output_size)
{
    return build_record_set(service, 0u, ttl, output, output_size);
}

static bool name_matches(const char *left, const char *right)
{
    return left && right && strcasecmp(left, right) == 0;
}

bool airplay_dns_build_query_response(const AirPlayDnsService *service,
                                      const uint8_t *query,
                                      size_t query_size,
                                      uint32_t ttl,
                                      uint8_t output[AIRPLAY_DNS_PACKET_MAX],
                                      size_t *output_size,
                                      bool *unicast_response)
{
    char instance_full[AIRPLAY_DNS_NAME_MAX + 1u];
    char host_full[AIRPLAY_DNS_NAME_MAX + 1u];
    size_t offset = DNS_HEADER_SIZE;
    uint16_t id;
    uint16_t flags;
    uint16_t question_count;
    bool relevant = false;
    bool unicast = false;

    if (!service_valid(service) || !query || query_size < DNS_HEADER_SIZE ||
        query_size > AIRPLAY_DNS_PACKET_MAX || !output || !output_size ||
        !unicast_response || !make_instance_full(service, instance_full) ||
        !make_host_full(service, host_full))
        return false;
    id = read_u16(query);
    flags = read_u16(query + 2u);
    question_count = read_u16(query + 4u);
    if ((flags & 0x8000u) != 0u || question_count == 0u ||
        question_count > DNS_MAX_QUESTIONS)
        return false;
    for (uint16_t index = 0u; index < question_count; ++index)
    {
        char name[AIRPLAY_DNS_NAME_MAX + 1u];
        uint16_t type;
        uint16_t dns_class;
        bool question_relevant = false;

        if (!read_name(query, query_size, &offset, name) || offset + 4u > query_size)
            return false;
        type = read_u16(query + offset);
        dns_class = read_u16(query + offset + 2u);
        offset += 4u;
        if ((dns_class & 0x7fffu) != DNS_CLASS_IN)
            continue;
        if (name_matches(name, g_service_name) &&
            (type == AIRPLAY_DNS_TYPE_PTR || type == AIRPLAY_DNS_TYPE_ANY))
            question_relevant = true;
        else if (name_matches(name, instance_full) &&
                 (type == AIRPLAY_DNS_TYPE_SRV || type == AIRPLAY_DNS_TYPE_TXT ||
                  type == AIRPLAY_DNS_TYPE_ANY))
            question_relevant = true;
        else if (name_matches(name, host_full) &&
                 (type == AIRPLAY_DNS_TYPE_A || type == AIRPLAY_DNS_TYPE_ANY))
            question_relevant = true;
        relevant = relevant || question_relevant;
        if (question_relevant && (dns_class & DNS_CLASS_UNICAST_RESPONSE) != 0u)
            unicast = true;
    }
    if (!relevant)
        return false;
    *unicast_response = unicast;
    return build_record_set(service, unicast ? id : 0u, ttl, output, output_size);
}

static bool skip_questions(const uint8_t *packet, size_t packet_size,
                           uint16_t count, size_t *offset)
{
    char name[AIRPLAY_DNS_NAME_MAX + 1u];

    if (count > DNS_MAX_QUESTIONS)
        return false;
    for (uint16_t index = 0u; index < count; ++index)
    {
        if (!read_name(packet, packet_size, offset, name) ||
            *offset + 4u > packet_size)
            return false;
        *offset += 4u;
    }
    return true;
}

static bool srv_rdata_matches(const AirPlayDnsService *service,
                              const uint8_t *packet, size_t packet_size,
                              size_t rdata_offset, uint16_t rdata_size)
{
    char host_full[AIRPLAY_DNS_NAME_MAX + 1u];
    char target[AIRPLAY_DNS_NAME_MAX + 1u];
    size_t target_offset = rdata_offset + 6u;

    return rdata_size >= 7u && rdata_offset + rdata_size <= packet_size &&
           read_u16(packet + rdata_offset) == 0u &&
           read_u16(packet + rdata_offset + 2u) == 0u &&
           read_u16(packet + rdata_offset + 4u) == service->port &&
           make_host_full(service, host_full) &&
           read_name(packet, packet_size, &target_offset, target) &&
           target_offset == rdata_offset + rdata_size &&
           name_matches(target, host_full);
}

bool airplay_dns_packet_conflicts(const AirPlayDnsService *service,
                                  const uint8_t *packet,
                                  size_t packet_size)
{
    char instance_full[AIRPLAY_DNS_NAME_MAX + 1u];
    char host_full[AIRPLAY_DNS_NAME_MAX + 1u];
    size_t offset = DNS_HEADER_SIZE;
    uint16_t questions;
    uint32_t record_count;

    if (!service_valid(service) || !packet || packet_size < DNS_HEADER_SIZE ||
        packet_size > AIRPLAY_DNS_PACKET_MAX ||
        !make_instance_full(service, instance_full) || !make_host_full(service, host_full))
        return false;
    questions = read_u16(packet + 4u);
    record_count = (uint32_t)read_u16(packet + 6u) + read_u16(packet + 8u) +
                   read_u16(packet + 10u);
    if (record_count > DNS_MAX_RECORDS ||
        !skip_questions(packet, packet_size, questions, &offset))
        return false;
    for (uint32_t index = 0u; index < record_count; ++index)
    {
        char owner[AIRPLAY_DNS_NAME_MAX + 1u];
        uint16_t type;
        uint16_t rdata_size;
        size_t rdata_offset;

        if (!read_name(packet, packet_size, &offset, owner) || offset + 10u > packet_size)
            return false;
        type = read_u16(packet + offset);
        rdata_size = read_u16(packet + offset + 8u);
        rdata_offset = offset + 10u;
        if (rdata_offset + rdata_size > packet_size)
            return false;
        if (name_matches(owner, host_full) && type == AIRPLAY_DNS_TYPE_A &&
            (rdata_size != 4u || memcmp(packet + rdata_offset,
                                        &service->ipv4_address, 4u) != 0))
            return true;
        if (name_matches(owner, instance_full) && type == AIRPLAY_DNS_TYPE_TXT &&
            (rdata_size != service->txt_size ||
             memcmp(packet + rdata_offset, service->txt, service->txt_size) != 0))
            return true;
        if (name_matches(owner, instance_full) && type == AIRPLAY_DNS_TYPE_SRV &&
            !srv_rdata_matches(service, packet, packet_size, rdata_offset, rdata_size))
            return true;
        offset = rdata_offset + rdata_size;
    }
    return false;
}
