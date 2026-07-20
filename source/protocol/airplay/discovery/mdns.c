#if !defined(__SWITCH__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif

#include "mdns.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "protocol/airplay/trace.h"

#ifdef __SWITCH__
#include <switch.h>

#include "log/log.h"

typedef Thread AirPlayMdnsThread;
typedef void (*AirPlayMdnsThreadEntry)(void *argument);
#define AIRPLAY_MDNS_THREAD_RETURN void
#define AIRPLAY_MDNS_THREAD_FINISH() return
#define AIRPLAY_MDNS_LOG_ERROR(...) log_error(__VA_ARGS__)
#define AIRPLAY_MDNS_LOG_INFO(...) log_info(__VA_ARGS__)
#define AIRPLAY_MDNS_LOG_WARN(...) log_warn(__VA_ARGS__)
#else
#include <pthread.h>

typedef pthread_t AirPlayMdnsThread;
typedef void *(*AirPlayMdnsThreadEntry)(void *argument);
#define AIRPLAY_MDNS_THREAD_RETURN void *
#define AIRPLAY_MDNS_THREAD_FINISH() return NULL
#define AIRPLAY_MDNS_LOG_ERROR(...) ((void)fprintf(stderr, __VA_ARGS__))
#define AIRPLAY_MDNS_LOG_INFO(...) ((void)0)
#define AIRPLAY_MDNS_LOG_WARN(...) ((void)fprintf(stderr, __VA_ARGS__))
#endif

#define AIRPLAY_MDNS_MULTICAST "224.0.0.251"
#define AIRPLAY_MDNS_TTL 120u
#define AIRPLAY_MDNS_ANNOUNCE_INTERVAL_MS 60000u
#define AIRPLAY_MDNS_THREAD_STACK_SIZE 0x10000u
#define AIRPLAY_MDNS_POLL_MS 200u
#define AIRPLAY_MDNS_INSTANCE_BASE_MAX 55u
#define AIRPLAY_MDNS_CONFLICT_SUFFIX_MAX 99u

typedef struct
{
    AirPlayMdnsConfig config;
    char friendly_name[AIRPLAY_DNS_NAME_MAX + 1u];
    AirPlayDnsService service;
    struct sockaddr_in announcement_target;
    AirPlayMdnsThread thread;
    bool thread_started;
    atomic_bool running;
    atomic_int socket_fd;
    uint16_t bound_port;
    unsigned conflict_suffix;
} AirPlayMdnsState;

static AirPlayMdnsState g_mdns;

static uint64_t mdns_now_ms(void)
{
#ifdef __SWITCH__
    return armTicksToNs(armGetSystemTick()) / 1000000u;
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0u;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
#endif
}

static bool mdns_thread_start(AirPlayMdnsThread *thread,
                              AirPlayMdnsThreadEntry entry,
                              void *argument)
{
#ifdef __SWITCH__
    Result result = threadCreate(thread, entry, argument, NULL,
                                 AIRPLAY_MDNS_THREAD_STACK_SIZE, 0x2b, -2);
    if (R_FAILED(result))
        return false;
    result = threadStart(thread);
    if (R_FAILED(result))
    {
        threadClose(thread);
        return false;
    }
    return true;
#else
    return pthread_create(thread, NULL, entry, argument) == 0;
#endif
}

static void mdns_thread_join(AirPlayMdnsThread *thread)
{
#ifdef __SWITCH__
    threadWaitForExit(thread);
    threadClose(thread);
#else
    pthread_join(*thread, NULL);
#endif
}

static void mdns_close_socket(void)
{
    int socket_fd = atomic_exchange(&g_mdns.socket_fd, -1);
    if (socket_fd >= 0)
    {
        shutdown(socket_fd, SHUT_RDWR);
        close(socket_fd);
    }
}

static bool determine_local_address(uint32_t *address_out)
{
    struct sockaddr_in remote;
    struct sockaddr_in local;
    socklen_t local_size = sizeof(local);
    int socket_fd;

    if (!address_out)
        return false;
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0)
        return false;
    memset(&remote, 0, sizeof(remote));
    remote.sin_family = AF_INET;
    remote.sin_port = htons(9u);
    remote.sin_addr.s_addr = inet_addr("8.8.8.8");
    if (connect(socket_fd, (struct sockaddr *)&remote, sizeof(remote)) != 0 ||
        getsockname(socket_fd, (struct sockaddr *)&local, &local_size) != 0)
    {
        close(socket_fd);
        return false;
    }
    close(socket_fd);
    if (local.sin_addr.s_addr == 0u)
        return false;
    *address_out = local.sin_addr.s_addr;
    return true;
}

static size_t sanitize_label(const char *input, char *output, size_t capacity)
{
    size_t written = 0u;

    if (!input || !output || capacity < 2u)
        return 0u;
    for (size_t index = 0u; input[index] && written + 1u < capacity; ++index)
    {
        unsigned char value = (unsigned char)input[index];
        if (value == '.' || value == '\\' || value < 0x20u || value == 0x7fu)
            output[written++] = '-';
        else
            output[written++] = (char)value;
    }
    while (written > 0u && (output[written - 1u] == ' ' || output[written - 1u] == '-'))
        written--;
    output[written] = '\0';
    return written;
}

static bool append_txt(AirPlayDnsService *service, const char *key, const char *value)
{
    size_t key_size;
    size_t value_size;
    size_t item_size;

    if (!service || !key || !value)
        return false;
    key_size = strlen(key);
    value_size = strlen(value);
    item_size = key_size + 1u + value_size;
    if (item_size > 255u || item_size + 1u > sizeof(service->txt) - service->txt_size)
        return false;
    service->txt[service->txt_size++] = (uint8_t)item_size;
    memcpy(service->txt + service->txt_size, key, key_size);
    service->txt_size += key_size;
    service->txt[service->txt_size++] = '=';
    memcpy(service->txt + service->txt_size, value, value_size);
    service->txt_size += value_size;
    return true;
}

static bool format_hex(const uint8_t *input, size_t input_size, char *output,
                       size_t output_size, char separator)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t needed = input_size * 2u + (separator ? input_size - 1u : 0u) + 1u;
    size_t offset = 0u;

    if (!input || !output || input_size == 0u || needed > output_size)
        return false;
    for (size_t index = 0u; index < input_size; ++index)
    {
        if (index > 0u && separator)
            output[offset++] = separator;
        output[offset++] = hex[input[index] >> 4];
        output[offset++] = hex[input[index] & 0x0fu];
    }
    output[offset] = '\0';
    return true;
}

static bool build_txt_record(const AirPlayMdnsConfig *config,
                             AirPlayDnsService *service)
{
    char device_id[18];
    char public_key[65];
    char features[32];
    int written;

    if (!config || !service || config->features == 0u ||
        !format_hex(config->device_id, sizeof(config->device_id),
                    device_id, sizeof(device_id), ':') ||
        !format_hex(config->public_key, sizeof(config->public_key),
                    public_key, sizeof(public_key), 0))
        return false;
    written = snprintf(features, sizeof(features), "0x%X,0x%X",
                       (unsigned)(config->features & UINT32_MAX),
                       (unsigned)(config->features >> 32));
    if (written <= 0 || (size_t)written >= sizeof(features))
        return false;
    return append_txt(service, "deviceid", device_id) &&
           append_txt(service, "features", features) &&
           append_txt(service, "flags", "0x4") &&
           append_txt(service, "model", "AppleTV3,2") &&
           append_txt(service, "pk", public_key) &&
           append_txt(service, "pw", config->pin_required ? "true" : "false") &&
           append_txt(service, "srcvers", "220.68") &&
           append_txt(service, "vv", "2");
}

static bool build_service(unsigned suffix)
{
    char base[AIRPLAY_MDNS_INSTANCE_BASE_MAX + 1u];
    int written;

    memset(&g_mdns.service, 0, sizeof(g_mdns.service));
    if (!sanitize_label(g_mdns.friendly_name, base, sizeof(base)))
        return false;
    if (suffix <= 1u)
        written = snprintf(g_mdns.service.instance_name,
                           sizeof(g_mdns.service.instance_name), "%s", base);
    else
        written = snprintf(g_mdns.service.instance_name,
                           sizeof(g_mdns.service.instance_name), "%s (%u)", base, suffix);
    if (written <= 0 || (size_t)written >= sizeof(g_mdns.service.instance_name))
        return false;
    written = snprintf(g_mdns.service.host_name, sizeof(g_mdns.service.host_name),
                       "nx-cast-%02x%02x%02x%02x%02x%02x",
                       g_mdns.config.device_id[0], g_mdns.config.device_id[1],
                       g_mdns.config.device_id[2], g_mdns.config.device_id[3],
                       g_mdns.config.device_id[4], g_mdns.config.device_id[5]);
    if (written <= 0 || (size_t)written >= sizeof(g_mdns.service.host_name))
        return false;
    g_mdns.service.port = g_mdns.config.control_port;
    g_mdns.service.ipv4_address = g_mdns.config.ipv4_address;
    return build_txt_record(&g_mdns.config, &g_mdns.service);
}

bool airplay_mdns_build_txt_record(const AirPlayMdnsConfig *config,
                                   uint8_t output[AIRPLAY_DNS_TXT_MAX],
                                   size_t *output_size)
{
    AirPlayDnsService service = {0};

    if (!config || !config->friendly_name || !output || !output_size ||
        !config->friendly_name[0] || config->features == 0u)
        return false;
    if (!build_txt_record(config, &service))
        return false;
    memcpy(output, service.txt, service.txt_size);
    *output_size = service.txt_size;
    return true;
}

static bool address_is_local(uint32_t source, uint32_t local)
{
    uint32_t source_host = ntohl(source);
    uint32_t local_host = ntohl(local);

    if ((source_host >> 24) == 127u && (local_host >> 24) == 127u)
        return true;
    if ((source_host >> 24) == 10u && (local_host >> 24) == 10u)
        return true;
    if ((source_host >> 16) == 0xc0a8u && (local_host >> 16) == 0xc0a8u)
        return true;
    if ((source_host >> 20) == 0xac1u && (local_host >> 20) == 0xac1u)
        return true;
    if ((source_host >> 16) == 0xa9feu && (local_host >> 16) == 0xa9feu)
        return true;
    return source == local;
}

static bool send_packet(const uint8_t *packet, size_t packet_size,
                        const struct sockaddr_in *target)
{
    int socket_fd = atomic_load(&g_mdns.socket_fd);
    ssize_t sent;

    if (socket_fd < 0 || !packet || packet_size == 0u || !target)
        return false;
    sent = sendto(socket_fd, packet, packet_size, 0,
                  (const struct sockaddr *)target, sizeof(*target));
    return sent == (ssize_t)packet_size;
}

static bool send_announcement(uint32_t ttl)
{
    uint8_t packet[AIRPLAY_DNS_PACKET_MAX];
    size_t packet_size;

    return airplay_dns_build_announcement(&g_mdns.service, ttl, packet, &packet_size) &&
           send_packet(packet, packet_size, &g_mdns.announcement_target);
}

static void handle_packet(const uint8_t *packet, size_t packet_size,
                          const struct sockaddr_in *sender)
{
    uint8_t response[AIRPLAY_DNS_PACKET_MAX];
    size_t response_size;
    bool unicast;

    if (!address_is_local(sender->sin_addr.s_addr, g_mdns.config.ipv4_address))
        return;
    if (airplay_dns_packet_conflicts(&g_mdns.service, packet, packet_size))
    {
        if (g_mdns.conflict_suffix < AIRPLAY_MDNS_CONFLICT_SUFFIX_MAX)
        {
            g_mdns.conflict_suffix++;
            if (build_service(g_mdns.conflict_suffix))
            {
                AIRPLAY_MDNS_LOG_WARN("[airplay-mdns] name conflict, renamed to %s\n",
                                     g_mdns.service.instance_name);
                (void)send_announcement(AIRPLAY_MDNS_TTL);
            }
        }
        return;
    }
    if (!airplay_dns_build_query_response(&g_mdns.service, packet, packet_size,
                                          AIRPLAY_MDNS_TTL, response, &response_size,
                                          &unicast))
        return;
    if (unicast || ntohs(sender->sin_port) != AIRPLAY_MDNS_DEFAULT_PORT)
        (void)send_packet(response, response_size, sender);
    else
        (void)send_packet(response, response_size, &g_mdns.announcement_target);
}

static AIRPLAY_MDNS_THREAD_RETURN mdns_thread(void *argument)
{
    uint64_t next_announcement = mdns_now_ms() + AIRPLAY_MDNS_ANNOUNCE_INTERVAL_MS;
    (void)argument;

    while (atomic_load(&g_mdns.running))
    {
        fd_set read_set;
        struct timeval timeout;
        int socket_fd = atomic_load(&g_mdns.socket_fd);
        int result;

        if (socket_fd < 0)
            break;
        FD_ZERO(&read_set);
        FD_SET(socket_fd, &read_set);
        timeout.tv_sec = 0;
        timeout.tv_usec = AIRPLAY_MDNS_POLL_MS * 1000u;
        result = select(socket_fd + 1, &read_set, NULL, NULL, &timeout);
        if (result < 0)
        {
            if (errno == EINTR)
                continue;
            if (atomic_load(&g_mdns.running))
                AIRPLAY_MDNS_LOG_ERROR("[airplay-mdns] select failed: %s\n", strerror(errno));
            break;
        }
        if (result > 0 && FD_ISSET(socket_fd, &read_set))
        {
            uint8_t packet[AIRPLAY_DNS_PACKET_MAX];
            struct sockaddr_in sender;
            socklen_t sender_size = sizeof(sender);
            ssize_t received = recvfrom(socket_fd, packet, sizeof(packet), 0,
                                        (struct sockaddr *)&sender, &sender_size);
            if (received > 0)
                handle_packet(packet, (size_t)received, &sender);
        }
        if (mdns_now_ms() >= next_announcement)
        {
            (void)send_announcement(AIRPLAY_MDNS_TTL);
            next_announcement = mdns_now_ms() + AIRPLAY_MDNS_ANNOUNCE_INTERVAL_MS;
        }
    }
    AIRPLAY_MDNS_THREAD_FINISH();
}

static bool create_socket(void)
{
    struct sockaddr_in bind_address;
    struct ip_mreq membership;
    struct in_addr multicast_interface;
    uint16_t bind_port = AIRPLAY_MDNS_DEFAULT_PORT;
    int socket_fd;
    int enabled = 1;
    unsigned char multicast_ttl = 255u;
    unsigned char multicast_loop = 0u;
    const char *failure_stage = "socket";
    int saved_errno;

#if defined(AIRPLAY_TESTING)
    bind_port = g_mdns.config.test_bind_port;
#endif
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0)
    {
        AIRPLAY_MDNS_LOG_ERROR("[airplay-mdns] socket setup failed stage=%s error=%s (%d)\n",
                               failure_stage, strerror(errno), errno);
        return false;
    }
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
#ifdef SO_REUSEPORT
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEPORT, &enabled, sizeof(enabled));
#endif
    memset(&bind_address, 0, sizeof(bind_address));
    bind_address.sin_family = AF_INET;
    bind_address.sin_port = htons(bind_port);
    bind_address.sin_addr.s_addr = htonl(INADDR_ANY);
    failure_stage = "bind";
    if (bind(socket_fd, (struct sockaddr *)&bind_address, sizeof(bind_address)) != 0)
        goto failure;

#if defined(AIRPLAY_TESTING)
    if (!g_mdns.config.test_skip_multicast_join)
#endif
    {
        membership.imr_multiaddr.s_addr = inet_addr(AIRPLAY_MDNS_MULTICAST);
        membership.imr_interface.s_addr = g_mdns.config.ipv4_address;
        failure_stage = "multicast-join";
        if (setsockopt(socket_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                       &membership, sizeof(membership)) != 0)
            goto failure;
    }
    multicast_interface.s_addr = g_mdns.config.ipv4_address;
    setsockopt(socket_fd, IPPROTO_IP, IP_MULTICAST_IF,
               &multicast_interface, sizeof(multicast_interface));
    setsockopt(socket_fd, IPPROTO_IP, IP_MULTICAST_TTL,
               &multicast_ttl, sizeof(multicast_ttl));
    setsockopt(socket_fd, IPPROTO_IP, IP_MULTICAST_LOOP,
               &multicast_loop, sizeof(multicast_loop));
    {
        struct sockaddr_in actual;
        socklen_t actual_size = sizeof(actual);
        failure_stage = "socket-name";
        if (getsockname(socket_fd, (struct sockaddr *)&actual, &actual_size) != 0)
            goto failure;
        g_mdns.bound_port = ntohs(actual.sin_port);
    }
    atomic_store(&g_mdns.socket_fd, socket_fd);
    return true;

failure:
    saved_errno = errno;
    close(socket_fd);
    AIRPLAY_MDNS_LOG_ERROR("[airplay-mdns] socket setup failed stage=%s error=%s (%d)\n",
                           failure_stage, strerror(saved_errno), saved_errno);
    return false;
}

bool airplay_mdns_start(const AirPlayMdnsConfig *config)
{
    size_t friendly_size;

    if (airplay_mdns_is_running())
        return true;
    if (!config || !config->friendly_name || !config->friendly_name[0] ||
        config->control_port == 0u || config->features == 0u)
    {
        AIRPLAY_MDNS_LOG_ERROR("[airplay-mdns] start failed stage=config\n");
        return false;
    }
    friendly_size = strlen(config->friendly_name);
    if (friendly_size > AIRPLAY_DNS_NAME_MAX)
        return false;
    memset(&g_mdns, 0, sizeof(g_mdns));
    g_mdns.config = *config;
    memcpy(g_mdns.friendly_name, config->friendly_name, friendly_size + 1u);
    atomic_init(&g_mdns.running, false);
    atomic_init(&g_mdns.socket_fd, -1);
    g_mdns.conflict_suffix = 1u;
    if (g_mdns.config.ipv4_address == 0u &&
        !determine_local_address(&g_mdns.config.ipv4_address))
    {
        AIRPLAY_MDNS_LOG_ERROR("[airplay-mdns] start failed stage=local-address\n");
        return false;
    }
    {
        char local_address[INET_ADDRSTRLEN];
        struct in_addr address = {.s_addr = g_mdns.config.ipv4_address};

        if (inet_ntop(AF_INET, &address, local_address, sizeof(local_address)))
            AIRPLAY_TRACE("[airplay-mdns] local-address=%s\n", local_address);
    }
    if (!build_service(g_mdns.conflict_suffix))
    {
        AIRPLAY_MDNS_LOG_ERROR("[airplay-mdns] start failed stage=service-record\n");
        return false;
    }
    memset(&g_mdns.announcement_target, 0, sizeof(g_mdns.announcement_target));
    g_mdns.announcement_target.sin_family = AF_INET;
    g_mdns.announcement_target.sin_port = htons(AIRPLAY_MDNS_DEFAULT_PORT);
    g_mdns.announcement_target.sin_addr.s_addr = inet_addr(AIRPLAY_MDNS_MULTICAST);
#if defined(AIRPLAY_TESTING)
    if (g_mdns.config.test_announcement_port != 0u)
        g_mdns.announcement_target.sin_port = htons(g_mdns.config.test_announcement_port);
    if (g_mdns.config.test_announcement_address != 0u)
        g_mdns.announcement_target.sin_addr.s_addr =
            g_mdns.config.test_announcement_address;
#endif
    if (!create_socket())
        return false;
    atomic_store(&g_mdns.running, true);
    if (!mdns_thread_start(&g_mdns.thread, mdns_thread, NULL))
    {
        AIRPLAY_MDNS_LOG_ERROR("[airplay-mdns] start failed stage=thread\n");
        atomic_store(&g_mdns.running, false);
        mdns_close_socket();
        return false;
    }
    g_mdns.thread_started = true;
    (void)send_announcement(AIRPLAY_MDNS_TTL);
    AIRPLAY_TRACE("[airplay-mdns] announced %s on port %u\n",
                  g_mdns.service.instance_name, g_mdns.service.port);
    AIRPLAY_MDNS_LOG_INFO("[airplay-mdns] announced %s on port %u\n",
                         g_mdns.service.instance_name, g_mdns.service.port);
    return true;
}

void airplay_mdns_stop(void)
{
    if (!g_mdns.thread_started)
        return;
    (void)send_announcement(0u);
    atomic_store(&g_mdns.running, false);
    mdns_close_socket();
    mdns_thread_join(&g_mdns.thread);
    g_mdns.thread_started = false;
    g_mdns.bound_port = 0u;
    memset(&g_mdns.config, 0, sizeof(g_mdns.config));
    memset(&g_mdns.service, 0, sizeof(g_mdns.service));
    memset(g_mdns.friendly_name, 0, sizeof(g_mdns.friendly_name));
}

bool airplay_mdns_is_running(void)
{
    return g_mdns.thread_started && atomic_load(&g_mdns.running);
}

uint16_t airplay_mdns_bound_port(void)
{
    return airplay_mdns_is_running() ? g_mdns.bound_port : 0u;
}

bool airplay_mdns_instance_name(char *output, size_t output_size)
{
    size_t size;

    if (!output || !airplay_mdns_is_running())
        return false;
    size = strlen(g_mdns.service.instance_name);
    if (size + 1u > output_size)
        return false;
    memcpy(output, g_mdns.service.instance_name, size + 1u);
    return true;
}
