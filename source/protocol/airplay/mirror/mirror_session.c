#include "mirror_session.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __SWITCH__
#include <switch.h>
typedef Thread AirPlayMirrorThread;
typedef void (*AirPlayMirrorThreadEntry)(void *argument);
#define AIRPLAY_MIRROR_THREAD_RETURN void
#define AIRPLAY_MIRROR_THREAD_FINISH() return
#define AIRPLAY_MIRROR_THREAD_STACK_SIZE 0x18000u
#else
#include <pthread.h>
typedef pthread_t AirPlayMirrorThread;
typedef void *(*AirPlayMirrorThreadEntry)(void *argument);
#define AIRPLAY_MIRROR_THREAD_RETURN void *
#define AIRPLAY_MIRROR_THREAD_FINISH() return NULL
#endif

#include "protocol/airplay/security/crypto.h"
#include "protocol/airplay/diagnostics.h"
#include "protocol/airplay/trace.h"

#define AIRPLAY_MIRROR_POLL_MS 200u

struct AirPlayMirrorSession
{
    uint64_t session_id;
    uint8_t key[16];
    uint8_t iv[16];
    AirPlayMirrorVideo *video;
    AirPlayMirrorThread thread;
    atomic_bool running;
    atomic_bool recording;
    atomic_int listener_fd;
    atomic_int client_fd;
    bool thread_started;
    uint16_t port;
    uint32_t diagnostic_thread_generation;
    uint64_t diagnostic_last_log_ms;
    atomic_uint_fast64_t connections_accepted;
    atomic_uint_fast64_t encrypted_packets;
    atomic_uint_fast64_t encrypted_bytes;
    atomic_uint_fast64_t decrypt_ok;
    atomic_uint_fast64_t decrypt_failures;
    atomic_uint_fast64_t invalid_headers;
};

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint64_t read_le64(const uint8_t *data)
{
    uint64_t value = 0u;

    for (unsigned index = 0u; index < 8u; ++index)
        value |= (uint64_t)data[index] << (index * 8u);
    return value;
}

bool airplay_mirror_packet_header_parse(
    const uint8_t data[AIRPLAY_MIRROR_HEADER_SIZE],
    AirPlayMirrorPacketHeader *header_out)
{
    if (!data || !header_out)
        return false;
    header_out->payload_size = read_le32(data);
    header_out->type = data[4];
    header_out->flags = data[5];
    header_out->options = (uint16_t)((uint16_t)data[6] | ((uint16_t)data[7] << 8));
    header_out->timestamp = read_le64(data + 8u);
    return header_out->payload_size <= AIRPLAY_MIRROR_MAX_PAYLOAD;
}

bool airplay_mirror_session_derive_crypto(
    const uint8_t session_key[16], uint64_t stream_connection_id,
    uint8_t key_out[16], uint8_t iv_out[16])
{
    char key_label[64];
    char iv_label[64];
    uint8_t input[80];
    uint8_t digest[64];
    int key_label_size;
    int iv_label_size;
    bool ok = false;

    if (!session_key || !key_out || !iv_out || stream_connection_id == 0u)
        return false;
    key_label_size = snprintf(key_label, sizeof(key_label), "AirPlayStreamKey%llu",
                              (unsigned long long)stream_connection_id);
    iv_label_size = snprintf(iv_label, sizeof(iv_label), "AirPlayStreamIV%llu",
                             (unsigned long long)stream_connection_id);
    if (key_label_size <= 0 || (size_t)key_label_size >= sizeof(key_label) ||
        iv_label_size <= 0 || (size_t)iv_label_size >= sizeof(iv_label))
        goto cleanup;
    memcpy(input, key_label, (size_t)key_label_size);
    memcpy(input + key_label_size, session_key, 16u);
    if (!airplay_crypto_sha512(input, (size_t)key_label_size + 16u, digest))
        goto cleanup;
    memcpy(key_out, digest, 16u);
    memcpy(input, iv_label, (size_t)iv_label_size);
    memcpy(input + iv_label_size, session_key, 16u);
    if (!airplay_crypto_sha512(input, (size_t)iv_label_size + 16u, digest))
        goto cleanup;
    memcpy(iv_out, digest, 16u);
    ok = true;

cleanup:
    if (!ok)
    {
        memset(key_out, 0, 16u);
        memset(iv_out, 0, 16u);
    }
    airplay_crypto_secure_zero(input, sizeof(input));
    airplay_crypto_secure_zero(digest, sizeof(digest));
    airplay_crypto_secure_zero(key_label, sizeof(key_label));
    airplay_crypto_secure_zero(iv_label, sizeof(iv_label));
    return ok;
}

static bool mirror_thread_start(AirPlayMirrorThread *thread,
                                AirPlayMirrorThreadEntry entry,
                                void *argument)
{
#ifdef __SWITCH__
    Result result = threadCreate(thread, entry, argument, NULL,
                                 AIRPLAY_MIRROR_THREAD_STACK_SIZE, 0x2b, -2);
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

static void mirror_thread_join(AirPlayMirrorThread *thread)
{
#ifdef __SWITCH__
    threadWaitForExit(thread);
    threadClose(thread);
#else
    pthread_join(*thread, NULL);
#endif
}

static void close_owned_socket(atomic_int *owner)
{
    int socket_fd = atomic_exchange(owner, -1);

    if (socket_fd >= 0)
    {
        shutdown(socket_fd, SHUT_RDWR);
        close(socket_fd);
        airplay_diagnostics_socket_closed(
            NETWORK_DIAGNOSTIC_AIRPLAY_MIRROR);
    }
}

static void mirror_log_diagnostics(AirPlayMirrorSession *session,
                                   const char *event, bool force)
{
#if defined(NXCAST_RUNTIME_OBSERVABILITY) && NXCAST_RUNTIME_OBSERVABILITY
    AirPlayMirrorSessionStats stats;
    uint64_t now_ms = AIRPLAY_TRACE_NOW_MS();

    if (!session ||
        (!force && session->diagnostic_last_log_ms != 0u &&
         now_ms - session->diagnostic_last_log_ms < 1000u) ||
        !airplay_mirror_session_get_stats(session, &stats))
        return;
    session->diagnostic_last_log_ms = now_ms;
    AIRPLAY_OBSERVE(
        "[airplay-video-pipeline] stage=mirror event=%s session=%llu "
        "accepted=%llu encrypted=%llu/%llu decrypt=%llu/%llu "
        "headers_invalid=%llu config=%llu/%llu/%u "
        "au=%llu/%llu/%llu/%llu bytes=%llu keyframes=%llu waiting_keyframe=%u\n",
        event ? event : "sample", (unsigned long long)session->session_id,
        (unsigned long long)stats.connections_accepted,
        (unsigned long long)stats.encrypted_packets,
        (unsigned long long)stats.encrypted_bytes,
        (unsigned long long)stats.decrypt_ok,
        (unsigned long long)stats.decrypt_failures,
        (unsigned long long)stats.invalid_headers,
        (unsigned long long)stats.video.config_ok,
        (unsigned long long)stats.video.config_failures,
        stats.video.config_generation,
        (unsigned long long)stats.video.access_units_ok,
        (unsigned long long)stats.video.access_units_dropped,
        (unsigned long long)stats.video.access_units_invalid,
        (unsigned long long)stats.video.access_units_no_memory,
        (unsigned long long)stats.video.access_unit_bytes,
        (unsigned long long)stats.video.keyframes,
        stats.video.waiting_for_keyframe ? 1u : 0u);
#else
    (void)session;
    (void)event;
    (void)force;
#endif
}

static bool socket_wait_readable(int socket_fd, const atomic_bool *running)
{
    while (atomic_load(running))
    {
        fd_set read_set;
        struct timeval timeout;
        int result;

        FD_ZERO(&read_set);
        FD_SET(socket_fd, &read_set);
        timeout.tv_sec = 0;
        timeout.tv_usec = AIRPLAY_MIRROR_POLL_MS * 1000u;
        result = select(socket_fd + 1, &read_set, NULL, NULL, &timeout);
        if (result > 0)
            return true;
        if (result < 0 && errno != EINTR)
            return false;
    }
    return false;
}

static bool receive_exact(int socket_fd, uint8_t *output, size_t size,
                          const atomic_bool *running)
{
    size_t offset = 0u;

    while (offset < size && atomic_load(running))
    {
        ssize_t received;

        if (!socket_wait_readable(socket_fd, running))
            return false;
        received = recv(socket_fd, output + offset, size - offset, 0);
        if (received <= 0)
            return false;
        offset += (size_t)received;
    }
    return offset == size;
}

static bool process_client(AirPlayMirrorSession *session, int client_fd)
{
    AirPlayCryptoAesCtr aes = {0};
    uint8_t header_bytes[AIRPLAY_MIRROR_HEADER_SIZE];
    uint8_t *payload = NULL;
    uint8_t *decrypted = NULL;
    size_t capacity = 0u;
    bool ok = airplay_crypto_aes_ctr_init(&aes, session->key, sizeof(session->key),
                                          session->iv);

    airplay_mirror_video_reset(session->video);
    if (!ok)
        atomic_fetch_add(&session->decrypt_failures, 1u);
    while (ok && atomic_load(&session->running))
    {
        AirPlayMirrorPacketHeader header;
        AirPlayMirrorVideoResult result = AIRPLAY_MIRROR_VIDEO_OK;

        if (!receive_exact(client_fd, header_bytes, sizeof(header_bytes), &session->running))
            break;
        if (!airplay_mirror_packet_header_parse(header_bytes, &header))
        {
            atomic_fetch_add(&session->invalid_headers, 1u);
            ok = false;
            break;
        }
        if (header.payload_size > capacity)
        {
            uint8_t *next_payload = realloc(payload, header.payload_size);
            uint8_t *next_decrypted;

            if (!next_payload)
            {
                ok = false;
                break;
            }
            payload = next_payload;
            next_decrypted = realloc(decrypted, header.payload_size);
            if (!next_decrypted)
            {
                ok = false;
                break;
            }
            decrypted = next_decrypted;
            capacity = header.payload_size;
        }
        if (header.payload_size != 0u &&
            !receive_exact(client_fd, payload, header.payload_size, &session->running))
            break;
        if (header.type == AIRPLAY_MIRROR_PACKET_CODEC)
            result = airplay_mirror_video_process_config(
                session->video, payload, header.payload_size, header.timestamp);
        else if (header.type == AIRPLAY_MIRROR_PACKET_VIDEO)
        {
            atomic_fetch_add(&session->encrypted_packets, 1u);
            atomic_fetch_add(&session->encrypted_bytes, header.payload_size);
            if (!airplay_crypto_aes_ctr_crypt(&aes, payload, decrypted,
                                              header.payload_size))
            {
                atomic_fetch_add(&session->decrypt_failures, 1u);
                result = AIRPLAY_MIRROR_VIDEO_INVALID;
            }
            else if (atomic_load(&session->recording))
            {
                atomic_fetch_add(&session->decrypt_ok, 1u);
                result = airplay_mirror_video_process_access_unit(
                    session->video, decrypted, header.payload_size, header.timestamp);
            }
            else
                atomic_fetch_add(&session->decrypt_ok, 1u);
        }
        else if (header.type != AIRPLAY_MIRROR_PACKET_HEARTBEAT &&
                 header.type != AIRPLAY_MIRROR_PACKET_REPORT)
            result = AIRPLAY_MIRROR_VIDEO_INVALID;
        AIRPLAY_PACKET_TRACE(
            "[airplay-mirror] session=%llu type=%u bytes=%u ts=%llu result=%s\n",
            (unsigned long long)session->session_id, header.type,
            header.payload_size, (unsigned long long)header.timestamp,
            airplay_mirror_video_result_name(result));
        mirror_log_diagnostics(session, "sample", false);
        if (result == AIRPLAY_MIRROR_VIDEO_INVALID ||
            result == AIRPLAY_MIRROR_VIDEO_NO_MEMORY)
        {
            ok = false;
            break;
        }
    }
    airplay_crypto_aes_ctr_deinit(&aes);
    free(payload);
    free(decrypted);
    return ok;
}

static AIRPLAY_MIRROR_THREAD_RETURN mirror_thread(void *argument)
{
    AirPlayMirrorSession *session = argument;

    while (atomic_load(&session->running))
    {
        struct sockaddr_in peer;
        socklen_t peer_size = sizeof(peer);
        int listener_fd = atomic_load(&session->listener_fd);
        int client_fd;

        if (listener_fd < 0 || !socket_wait_readable(listener_fd, &session->running))
            continue;
        if (!atomic_load(&session->running))
            break;
        client_fd = accept(listener_fd, (struct sockaddr *)&peer, &peer_size);
        if (client_fd < 0)
        {
            if (errno == EINTR)
                continue;
            break;
        }
        airplay_diagnostics_socket_opened(
            NETWORK_DIAGNOSTIC_AIRPLAY_MIRROR);
        atomic_fetch_add(&session->connections_accepted, 1u);
        atomic_store(&session->client_fd, client_fd);
        AIRPLAY_TRACE("[airplay-mirror] session=%llu client-connected\n",
                      (unsigned long long)session->session_id);
        mirror_log_diagnostics(session, "client-connected", true);
        (void)process_client(session, client_fd);
        close_owned_socket(&session->client_fd);
        AIRPLAY_TRACE("[airplay-mirror] session=%llu client-disconnected\n",
                      (unsigned long long)session->session_id);
        mirror_log_diagnostics(session, "client-disconnected", true);
    }
    AIRPLAY_MIRROR_THREAD_FINISH();
}

static bool create_listener(AirPlayMirrorSession *session)
{
    struct sockaddr_in address;
    socklen_t address_size = sizeof(address);
    int listener_fd;
    int enabled = 1;

    listener_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listener_fd < 0)
        return false;
    airplay_diagnostics_socket_opened(NETWORK_DIAGNOSTIC_AIRPLAY_MIRROR);
    setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(0u);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listener_fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(listener_fd, 1) != 0 ||
        getsockname(listener_fd, (struct sockaddr *)&address, &address_size) != 0)
    {
        close(listener_fd);
        airplay_diagnostics_socket_closed(
            NETWORK_DIAGNOSTIC_AIRPLAY_MIRROR);
        return false;
    }
    session->port = ntohs(address.sin_port);
    atomic_store(&session->listener_fd, listener_fd);
    return session->port != 0u;
}

bool airplay_mirror_session_create(const AirPlayMirrorSessionConfig *config,
                                   AirPlayMirrorSession **session_out)
{
    AirPlayMirrorSession *session;

    if (!config || !session_out || *session_out || config->session_id == 0u ||
        !config->session_key || config->stream_connection_id == 0u ||
        !config->video_callback)
        return false;
    session = calloc(1, sizeof(*session));
    if (!session)
        return false;
    session->session_id = config->session_id;
    atomic_init(&session->running, false);
    atomic_init(&session->recording, false);
    atomic_init(&session->listener_fd, -1);
    atomic_init(&session->client_fd, -1);
    atomic_init(&session->connections_accepted, 0u);
    atomic_init(&session->encrypted_packets, 0u);
    atomic_init(&session->encrypted_bytes, 0u);
    atomic_init(&session->decrypt_ok, 0u);
    atomic_init(&session->decrypt_failures, 0u);
    atomic_init(&session->invalid_headers, 0u);
    if (!airplay_mirror_session_derive_crypto(config->session_key,
                                               config->stream_connection_id,
                                               session->key, session->iv) ||
        !airplay_mirror_video_create(config->video_callback,
                                     config->video_user_data,
                                     &session->video))
    {
        AIRPLAY_OBSERVE(
            "[airplay-setup-failure] session=%llu stream=video stage=format\n",
            (unsigned long long)config->session_id);
        airplay_mirror_session_destroy(session);
        return false;
    }
    if (!create_listener(session))
    {
        AIRPLAY_OBSERVE(
            "[airplay-setup-failure] session=%llu stream=video stage=socket-data\n",
            (unsigned long long)config->session_id);
        airplay_mirror_session_destroy(session);
        return false;
    }
    atomic_store(&session->running, true);
    if (!mirror_thread_start(&session->thread, mirror_thread, session))
    {
        airplay_diagnostics_thread_create_failed(
            RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_MIRROR);
        AIRPLAY_OBSERVE(
            "[airplay-setup-failure] session=%llu stream=video stage=thread-create\n",
            (unsigned long long)config->session_id);
        atomic_store(&session->running, false);
        airplay_mirror_session_destroy(session);
        return false;
    }
    session->thread_started = true;
    session->diagnostic_thread_generation =
        airplay_diagnostics_thread_created(
            RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_MIRROR);
    *session_out = session;
    return true;
}

void airplay_mirror_session_set_recording(AirPlayMirrorSession *session,
                                          bool recording)
{
    if (session)
        atomic_store(&session->recording, recording);
}

void airplay_mirror_session_destroy(AirPlayMirrorSession *session)
{
    if (!session)
        return;
    atomic_store(&session->running, false);
    close_owned_socket(&session->client_fd);
    close_owned_socket(&session->listener_fd);
    if (session->thread_started)
    {
        mirror_thread_join(&session->thread);
        airplay_diagnostics_thread_joined(
            RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_MIRROR,
            session->diagnostic_thread_generation);
    }
    airplay_mirror_video_destroy(session->video);
    session->video = NULL;
    airplay_crypto_secure_zero(session->key, sizeof(session->key));
    airplay_crypto_secure_zero(session->iv, sizeof(session->iv));
    free(session);
}

uint16_t airplay_mirror_session_port(const AirPlayMirrorSession *session)
{
    return session && atomic_load(&session->running) ? session->port : 0u;
}

bool airplay_mirror_session_is_running(const AirPlayMirrorSession *session)
{
    return session && atomic_load(&session->running);
}

bool airplay_mirror_session_get_stats(const AirPlayMirrorSession *session,
                                      AirPlayMirrorSessionStats *stats_out)
{
    if (!session || !stats_out)
        return false;
    memset(stats_out, 0, sizeof(*stats_out));
    stats_out->connections_accepted =
        atomic_load(&session->connections_accepted);
    stats_out->encrypted_packets = atomic_load(&session->encrypted_packets);
    stats_out->encrypted_bytes = atomic_load(&session->encrypted_bytes);
    stats_out->decrypt_ok = atomic_load(&session->decrypt_ok);
    stats_out->decrypt_failures = atomic_load(&session->decrypt_failures);
    stats_out->invalid_headers = atomic_load(&session->invalid_headers);
    return airplay_mirror_video_get_stats(session->video, &stats_out->video);
}
