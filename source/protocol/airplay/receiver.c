#include "receiver.h"

#include <stdio.h>
#include <string.h>

#include "protocol/airplay/discovery/mdns.h"
#include "protocol/airplay/security/crypto.h"
#include "protocol/airplay/security/fairplay.h"
#include "protocol/airplay/security/identity.h"
#include "protocol/airplay/security/pairing.h"
#include "protocol/airplay/server.h"
#include "protocol/airplay/trace.h"

#ifdef __SWITCH__
#include "log/log.h"
#define AIRPLAY_RECEIVER_LOG_ERROR(...) log_error(__VA_ARGS__)
#else
#define AIRPLAY_RECEIVER_LOG_ERROR(...) ((void)fprintf(stderr, __VA_ARGS__))
#endif

typedef struct
{
    AirPlayPairingService *pairing;
    AirPlayHandlers *handlers;
    AirPlayReceiverConfig config;
    bool lifecycle_started;
    bool discovery_started;
    bool running;
} AirPlayReceiverState;

static AirPlayReceiverState g_receiver;

static bool is_pairing_uri(const char *uri)
{
    return uri && (strcmp(uri, "/pair-pin-start") == 0 ||
                   strcmp(uri, "/pair-setup-pin") == 0 ||
                   strcmp(uri, "/pair-verify") == 0);
}

static bool requires_authorization(const char *method)
{
    return method && (strcmp(method, "SETUP") == 0 || strcmp(method, "RECORD") == 0 ||
                      strcmp(method, "FLUSH") == 0 ||
                      strcmp(method, "GET_PARAMETER") == 0 ||
                      strcmp(method, "SET_PARAMETER") == 0 ||
                      strcmp(method, "TEARDOWN") == 0);
}

static bool remote_video_uri(const char *uri)
{
    return uri && (strcmp(uri, "/play") == 0 ||
                   strcmp(uri, "/rate") == 0 ||
                   strncmp(uri, "/rate?", 6u) == 0 ||
                   strcmp(uri, "/scrub") == 0 ||
                   strncmp(uri, "/scrub?", 7u) == 0 ||
                   strcmp(uri, "/playback-info") == 0 ||
                   strcmp(uri, "/stop") == 0);
}

static bool receiver_route(AirPlayRtspSession *session,
                           const AirPlayRtspRequest *request,
                           AirPlayRtspResponse *response,
                           void *user_data)
{
    AirPlayReceiverState *receiver = user_data;

    if (is_pairing_uri(request->uri))
        return airplay_pairing_route(session, request, response, receiver->pairing);
    if ((requires_authorization(request->method) ||
         remote_video_uri(request->uri)) &&
        !airplay_pairing_session_verified(session))
        return airplay_pairing_route(session, request, response, receiver->pairing);
    return airplay_handlers_route(session, request, response, receiver->handlers);
}

static void receiver_session_closed(AirPlayRtspSession *session, void *user_data)
{
    AirPlayReceiverState *receiver = user_data;

    airplay_handlers_session_closed(session, receiver->handlers);
    airplay_pairing_session_closed(session, receiver->pairing);
}

static bool receiver_shared_secret(const AirPlayRtspSession *session,
                                   uint8_t output[32], void *user_data)
{
    (void)user_data;
    return airplay_pairing_session_shared_secret(session, output);
}

static bool format_device_id(const uint8_t device_id[6], char output[18])
{
    int written = snprintf(output, 18u, "%02X:%02X:%02X:%02X:%02X:%02X",
                           device_id[0], device_id[1], device_id[2],
                           device_id[3], device_id[4], device_id[5]);
    return written == 17;
}

static bool make_pairing_id(const uint8_t public_key[32], char output[37])
{
    uint8_t hash[32];
    int written;

    if (!airplay_crypto_sha256(public_key, 32u, hash))
        return false;
    hash[6] = (uint8_t)((hash[6] & 0x0fu) | 0x40u);
    hash[8] = (uint8_t)((hash[8] & 0x3fu) | 0x80u);
    written = snprintf(output, 37u,
                       "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
                       "%02x%02x%02x%02x%02x%02x",
                       hash[0], hash[1], hash[2], hash[3], hash[4], hash[5],
                       hash[6], hash[7], hash[8], hash[9], hash[10], hash[11],
                       hash[12], hash[13], hash[14], hash[15]);
    airplay_crypto_secure_zero(hash, sizeof(hash));
    return written == 36;
}

bool airplay_receiver_start(const AirPlayReceiverConfig *config)
{
    AirPlayPairingConfig pairing_config = {0};
    AirPlayServerConfig server_config = {0};
    AirPlayHandlersConfig handlers_config = {0};
    AirPlayMdnsConfig mdns_config = {0};
    AirPlayConfig lifecycle_config = {0};
    uint8_t device_id[6];
    uint8_t public_key[32];
    uint8_t txt[AIRPLAY_DNS_TXT_MAX];
    size_t txt_size = 0u;
    char device_id_string[18];
    char pairing_id[37];
    uint64_t advertised_features;
    const char *failure_stage = "config";

    if (g_receiver.running)
        return true;
    if (!config || !config->friendly_name || !config->storage_directory ||
        config->features == 0u)
    {
        AIRPLAY_RECEIVER_LOG_ERROR("[airplay] receiver start failed stage=%s\n",
                                   failure_stage);
        return false;
    }
    memset(&g_receiver, 0, sizeof(g_receiver));
    g_receiver.config = *config;
    pairing_config.storage_directory = config->storage_directory;
    pairing_config.pin_display_callback = config->pin_display_callback;
    pairing_config.pin_dismiss_callback = config->pin_dismiss_callback;
    pairing_config.pin_user_data = config->pin_user_data;
    failure_stage = "pairing-identity";
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu receiver stage=%s begin\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS(), failure_stage);
    if (!airplay_pairing_service_create(&pairing_config, &g_receiver.pairing) ||
        !airplay_pairing_service_device_id(g_receiver.pairing, device_id) ||
        !airplay_pairing_service_public_key(g_receiver.pairing, public_key) ||
        !format_device_id(device_id, device_id_string) ||
        !make_pairing_id(public_key, pairing_id))
        goto failure;
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu receiver stage=%s done\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS(), failure_stage);

    advertised_features = config->features | AIRPLAY_MDNS_FEATURE_LEGACY_PAIRING;
    if ((!config->unwrap_key_callback && !airplay_fairplay_is_available()) ||
        !config->transport_prepare_callback ||
        !config->mirror_open_callback)
        advertised_features &= ~(AIRPLAY_MDNS_FEATURE_SCREEN_MIRROR |
                                 AIRPLAY_MDNS_FEATURE_SCREEN_ROTATE);
    if (!config->remote_video)
        advertised_features &= ~(AIRPLAY_MDNS_FEATURE_VIDEO |
                                 AIRPLAY_MDNS_FEATURE_HLS);
    if (!config->audio_open_callback)
        advertised_features &= ~(AIRPLAY_MDNS_FEATURE_AUDIO |
                                 AIRPLAY_MDNS_FEATURE_RAOP_NOT_REQUIRED);
    mdns_config.friendly_name = config->friendly_name;
    mdns_config.control_port = config->control_port;
    memcpy(mdns_config.device_id, device_id, sizeof(device_id));
    memcpy(mdns_config.public_key, public_key, sizeof(public_key));
    memcpy(mdns_config.pairing_id, pairing_id, sizeof(pairing_id));
    mdns_config.features = advertised_features;
    mdns_config.pin_required = true;
    failure_stage = "dns-sd-txt";
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu receiver stage=%s begin\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS(), failure_stage);
    if (!airplay_mdns_build_txt_record(&mdns_config, txt, &txt_size))
        goto failure;
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu receiver stage=%s done\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS(), failure_stage);

    handlers_config.friendly_name = config->friendly_name;
    handlers_config.device_id = device_id_string;
    handlers_config.pairing_id = pairing_id;
    handlers_config.public_key = public_key;
    handlers_config.features = advertised_features;
    handlers_config.airplay_txt = txt;
    handlers_config.airplay_txt_size = txt_size;
    handlers_config.unwrap_key_callback = config->unwrap_key_callback;
    handlers_config.shared_secret_callback = receiver_shared_secret;
    handlers_config.transport_prepare_callback = config->transport_prepare_callback;
    handlers_config.mirror_open_callback = config->mirror_open_callback;
    handlers_config.audio_open_callback = config->audio_open_callback;
    handlers_config.mirror_record_callback = config->mirror_record_callback;
    handlers_config.mirror_stop_callback = config->mirror_stop_callback;
    handlers_config.remote_video = config->remote_video;
    handlers_config.callback_user_data = config->media_user_data;
    failure_stage = "handlers";
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu receiver stage=%s begin\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS(), failure_stage);
    if (!airplay_handlers_create(&handlers_config, &g_receiver.handlers))
        goto failure;
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu receiver stage=%s done\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS(), failure_stage);

    server_config.port = config->control_port;
    server_config.route_handler = receiver_route;
    server_config.route_user_data = &g_receiver;
    server_config.session_closed_handler = receiver_session_closed;
    failure_stage = "control-server";
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu receiver stage=%s begin\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS(), failure_stage);
    if (!airplay_server_start(&server_config))
        goto failure;
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu receiver stage=%s done port=%u\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS(), failure_stage,
                       airplay_server_port());
    mdns_config.control_port = airplay_server_port();
    lifecycle_config.friendly_name = config->friendly_name;
    lifecycle_config.control_port = mdns_config.control_port;
    lifecycle_config.pin_display_callback = config->pin_display_callback;
    lifecycle_config.pin_dismiss_callback = config->pin_dismiss_callback;
    lifecycle_config.pin_user_data = config->pin_user_data;
    failure_stage = "lifecycle";
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu receiver stage=%s begin\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS(), failure_stage);
    if (!airplay_start(&lifecycle_config))
        goto failure;
    g_receiver.lifecycle_started = true;
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu receiver stage=%s done\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS(), failure_stage);
    if (config->enable_discovery)
    {
        failure_stage = "mdns";
        AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu receiver stage=%s begin\n",
                           (unsigned long long)AIRPLAY_TRACE_NOW_MS(), failure_stage);
        if (!airplay_mdns_start(&mdns_config))
            goto failure;
        g_receiver.discovery_started = true;
        AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu receiver stage=%s done\n",
                           (unsigned long long)AIRPLAY_TRACE_NOW_MS(), failure_stage);
    }
    g_receiver.running = true;
    airplay_crypto_secure_zero(public_key, sizeof(public_key));
    AIRPLAY_TRACE("[airplay] receiver started port=%u discovery=%u features=%llx\n",
                  airplay_server_port(), config->enable_discovery ? 1u : 0u,
                  (unsigned long long)advertised_features);
    return true;

failure:
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu receiver failed stage=%s\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS(), failure_stage);
    AIRPLAY_RECEIVER_LOG_ERROR("[airplay] receiver start failed stage=%s\n",
                               failure_stage);
    airplay_crypto_secure_zero(public_key, sizeof(public_key));
    airplay_receiver_stop();
    return false;
}

void airplay_receiver_stop(void)
{
    if (g_receiver.discovery_started)
    {
        airplay_mdns_stop();
        g_receiver.discovery_started = false;
    }
    airplay_server_stop();
    if (g_receiver.lifecycle_started)
    {
        airplay_stop();
        g_receiver.lifecycle_started = false;
    }
    airplay_handlers_destroy(g_receiver.handlers);
    g_receiver.handlers = NULL;
    airplay_pairing_service_destroy(g_receiver.pairing);
    g_receiver.pairing = NULL;
    g_receiver.running = false;
    memset(&g_receiver.config, 0, sizeof(g_receiver.config));
}

bool airplay_receiver_is_running(void)
{
    return g_receiver.running && airplay_server_is_running();
}

uint16_t airplay_receiver_port(void)
{
    return airplay_receiver_is_running() ? airplay_server_port() : 0u;
}
