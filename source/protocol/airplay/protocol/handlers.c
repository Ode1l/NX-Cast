#include "handlers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "protocol/airplay/media/remote_video.h"
#include "protocol/airplay/protocol/plist.h"
#include "protocol/airplay/security/crypto.h"
#include "protocol/airplay/trace.h"

#define AIRPLAY_PLIST_CONTENT_TYPE "application/x-apple-binary-plist"
#define AIRPLAY_OCTET_CONTENT_TYPE "application/octet-stream"
#define AIRPLAY_TEXT_CONTENT_TYPE "text/parameters"

typedef struct
{
    AirPlayFairPlay *fairplay;
    bool initial_setup;
    bool mirror_setup;
    bool audio_setup;
    uint8_t aes_key[16];
    uint8_t aes_iv[16];
    uint64_t stream_connection_id;
} AirPlayHandlerSession;

struct AirPlayHandlers
{
    AirPlayHandlersConfig config;
    char friendly_name[AIRPLAY_HANDLER_NAME_MAX + 1u];
    char device_id[AIRPLAY_HANDLER_DEVICE_ID_STRING_SIZE];
    char pairing_id[AIRPLAY_HANDLER_PAIRING_ID_SIZE];
    uint8_t public_key[32];
    uint8_t airplay_txt[AIRPLAY_DNS_TXT_MAX];
};

static bool content_type_is(const AirPlayRtspRequest *request, const char *expected)
{
    const char *value = airplay_rtsp_request_header(request, "Content-Type");
    size_t size = strlen(expected);

    return value && strncasecmp(value, expected, size) == 0 &&
           (value[size] == '\0' || value[size] == ';');
}

static bool dict_set(AirPlayPlistValue *dict, const char *key, AirPlayPlistValue *value)
{
    if (!value)
        return false;
    if (!airplay_plist_dict_set(dict, key, value))
    {
        airplay_plist_free(value);
        return false;
    }
    return true;
}

static bool array_append(AirPlayPlistValue *array, AirPlayPlistValue *value)
{
    if (!value)
        return false;
    if (!airplay_plist_array_append(array, value))
    {
        airplay_plist_free(value);
        return false;
    }
    return true;
}

static bool set_plist_body(AirPlayRtspResponse *response, AirPlayPlistValue *root)
{
    uint8_t *encoded = NULL;
    size_t encoded_size = 0u;
    AirPlayPlistError error;
    bool ok = airplay_plist_encode(root, &encoded, &encoded_size, &error) &&
              airplay_rtsp_response_set_body(response, encoded, encoded_size,
                                              AIRPLAY_PLIST_CONTENT_TYPE);
    airplay_plist_buffer_free(encoded);
    return ok;
}

static AirPlayHandlerSession *session_context(AirPlayRtspSession *session)
{
    AirPlayHandlerSession *context;

    if (!session)
        return NULL;
    context = session->protocol_context;
    if (context)
        return context;
    context = calloc(1, sizeof(*context));
    if (!context || !airplay_fairplay_create(&context->fairplay))
    {
        airplay_fairplay_destroy(context ? context->fairplay : NULL);
        free(context);
        return NULL;
    }
    session->protocol_context = context;
    return context;
}

bool airplay_handlers_create(const AirPlayHandlersConfig *config,
                             AirPlayHandlers **handlers_out)
{
    AirPlayHandlers *handlers;
    size_t name_size;

    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu handlers stage=validate begin\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS());
    if (!config || !handlers_out || *handlers_out || !config->friendly_name ||
        !config->device_id || !config->pairing_id || !config->public_key)
        return false;
    name_size = strlen(config->friendly_name);
    if (name_size == 0u || name_size > AIRPLAY_HANDLER_NAME_MAX ||
        strlen(config->device_id) != AIRPLAY_HANDLER_DEVICE_ID_STRING_SIZE - 1u ||
        strlen(config->pairing_id) != AIRPLAY_HANDLER_PAIRING_ID_SIZE - 1u ||
        config->airplay_txt_size > AIRPLAY_DNS_TXT_MAX ||
        (config->airplay_txt_size != 0u && !config->airplay_txt))
        return false;
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu handlers stage=validate done\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS());
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu handlers stage=allocate begin bytes=%zu\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS(), sizeof(*handlers));
    handlers = calloc(1, sizeof(*handlers));
    if (!handlers)
        return false;
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu handlers stage=allocate done\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS());
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu handlers stage=copy begin txt_bytes=%zu\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS(),
                       config->airplay_txt_size);
    handlers->config = *config;
    memcpy(handlers->friendly_name, config->friendly_name, name_size + 1u);
    memcpy(handlers->device_id, config->device_id, sizeof(handlers->device_id));
    memcpy(handlers->pairing_id, config->pairing_id, sizeof(handlers->pairing_id));
    memcpy(handlers->public_key, config->public_key, sizeof(handlers->public_key));
    if (config->airplay_txt_size)
        memcpy(handlers->airplay_txt, config->airplay_txt, config->airplay_txt_size);
    handlers->config.friendly_name = handlers->friendly_name;
    handlers->config.device_id = handlers->device_id;
    handlers->config.pairing_id = handlers->pairing_id;
    handlers->config.public_key = handlers->public_key;
    handlers->config.airplay_txt = handlers->airplay_txt;
    *handlers_out = handlers;
    AIRPLAY_TRACE_SYNC("[airplay] t_ms=%llu handlers stage=copy done\n",
                       (unsigned long long)AIRPLAY_TRACE_NOW_MS());
    return true;
}

void airplay_handlers_destroy(AirPlayHandlers *handlers)
{
    if (!handlers)
        return;
    airplay_crypto_secure_zero(handlers, sizeof(*handlers));
    free(handlers);
}

static bool request_qualifies_txt(const AirPlayRtspRequest *request)
{
    AirPlayPlistValue *root = NULL;
    AirPlayPlistError error;
    const AirPlayPlistValue *qualifier;
    const char *value;
    bool matches = false;

    if (!content_type_is(request, AIRPLAY_PLIST_CONTENT_TYPE) ||
        !airplay_plist_decode(request->body, request->body_length, &root, &error))
        return false;
    qualifier = airplay_plist_dict_get(root, "qualifier");
    value = airplay_plist_get_string(airplay_plist_array_get(qualifier, 0u));
    matches = value && strcmp(value, "txtAirPlay") == 0;
    airplay_plist_free(root);
    return matches;
}

static bool add_display_info(AirPlayPlistValue *root)
{
    AirPlayPlistValue *displays = airplay_plist_new_array();
    AirPlayPlistValue *display = airplay_plist_new_dict();
    bool ok;

    if (!displays || !display)
    {
        airplay_plist_free(displays);
        airplay_plist_free(display);
        return false;
    }
    ok = dict_set(display, "uuid", airplay_plist_new_string("5f45580b-6a34-45df-9b3f-006e78636173")) &&
         dict_set(display, "width", airplay_plist_new_uint(1280u)) &&
         dict_set(display, "height", airplay_plist_new_uint(720u)) &&
         dict_set(display, "widthPixels", airplay_plist_new_uint(1280u)) &&
         dict_set(display, "heightPixels", airplay_plist_new_uint(720u)) &&
         dict_set(display, "rotation", airplay_plist_new_bool(false)) &&
         dict_set(display, "refreshRate", airplay_plist_new_real(60.0)) &&
         dict_set(display, "maxFPS", airplay_plist_new_uint(60u)) &&
         dict_set(display, "overscanned", airplay_plist_new_bool(false)) &&
         dict_set(display, "features", airplay_plist_new_uint(14u));
    if (!ok)
    {
        airplay_plist_free(display);
        airplay_plist_free(displays);
        return false;
    }
    if (!array_append(displays, display))
    {
        airplay_plist_free(displays);
        return false;
    }
    if (!dict_set(root, "displays", displays))
        return false;
    return ok;
}

static bool handle_info(AirPlayHandlers *handlers,
                        const AirPlayRtspRequest *request,
                        AirPlayRtspResponse *response)
{
    AirPlayPlistValue *root = airplay_plist_new_dict();
    bool ok = false;

    if (strcmp(request->method, "GET") != 0 || !root)
        goto cleanup;
    if (request_qualifies_txt(request))
    {
        ok = handlers->config.airplay_txt_size != 0u &&
             dict_set(root, "txtAirPlay",
                      airplay_plist_new_data(handlers->config.airplay_txt,
                                             handlers->config.airplay_txt_size));
    }
    else
    {
        ok = dict_set(root, "deviceID", airplay_plist_new_string(handlers->device_id)) &&
             dict_set(root, "macAddress", airplay_plist_new_string(handlers->device_id)) &&
             dict_set(root, "pk", airplay_plist_new_data(handlers->public_key, 32u)) &&
             dict_set(root, "features", airplay_plist_new_uint(handlers->config.features)) &&
             dict_set(root, "name", airplay_plist_new_string(handlers->friendly_name)) &&
             dict_set(root, "pi", airplay_plist_new_string(handlers->pairing_id)) &&
             dict_set(root, "vv", airplay_plist_new_uint(2u)) &&
             dict_set(root, "statusFlags", airplay_plist_new_uint(68u)) &&
             dict_set(root, "keepAliveLowPower", airplay_plist_new_uint(1u)) &&
             dict_set(root, "sourceVersion", airplay_plist_new_string("220.68")) &&
             dict_set(root, "model", airplay_plist_new_string("AppleTV3,2"));
        if (ok && (handlers->config.features & (UINT64_C(1) << 7)) != 0u)
            ok = add_display_info(root);
    }
    if (ok)
        ok = set_plist_body(response, root);

cleanup:
    airplay_plist_free(root);
    if (!ok)
        return airplay_rtsp_response_set_status(response,
                                                strcmp(request->method, "GET") == 0 ? 500 : 501);
    return true;
}

static bool handle_fairplay(AirPlayHandlerSession *context,
                            const AirPlayRtspRequest *request,
                            AirPlayRtspResponse *response)
{
    uint8_t output[AIRPLAY_FAIRPLAY_STAGE1_RESPONSE_SIZE];
    size_t output_size = 0u;
    AirPlayFairPlayResult result;

    if (strcmp(request->method, "POST") != 0 ||
        !content_type_is(request, AIRPLAY_OCTET_CONTENT_TYPE))
        return airplay_rtsp_response_set_status(response, 400);
    result = airplay_fairplay_setup(context->fairplay, request->body,
                                    request->body_length, output, sizeof(output),
                                    &output_size);
    AIRPLAY_TRACE("[airplay] endpoint=fp-setup bytes=%zu result=%s\n",
                  request->body_length, airplay_fairplay_result_name(result));
    if (result == AIRPLAY_FAIRPLAY_UNSUPPORTED)
        return airplay_rtsp_response_set_status(response, 501);
    if (result != AIRPLAY_FAIRPLAY_OK)
        return airplay_rtsp_response_set_status(response, 400);
    return airplay_rtsp_response_set_body(response, output, output_size,
                                          AIRPLAY_OCTET_CONTENT_TYPE);
}

static AirPlayPlistValue *decode_dict(const AirPlayRtspRequest *request)
{
    AirPlayPlistValue *root = NULL;
    AirPlayPlistError error;

    if (!content_type_is(request, AIRPLAY_PLIST_CONTENT_TYPE) ||
        !airplay_plist_decode(request->body, request->body_length, &root, &error) ||
        airplay_plist_type(root) != AIRPLAY_PLIST_TYPE_DICT)
    {
        airplay_plist_free(root);
        return NULL;
    }
    return root;
}

static bool derive_session_key(AirPlayHandlers *handlers,
                               AirPlayHandlerSession *context,
                               const AirPlayRtspSession *session,
                               const uint8_t wrapped_key[72],
                               uint8_t output[16])
{
    uint8_t unwrapped[16];
    uint8_t shared[32];
    uint8_t input[48];
    uint8_t hash[32];
    bool ok;

    if (handlers->config.unwrap_key_callback)
        ok = handlers->config.unwrap_key_callback(wrapped_key, unwrapped,
                                                  handlers->config.callback_user_data);
    else
        ok = airplay_fairplay_unwrap_key(context->fairplay, wrapped_key, unwrapped) ==
             AIRPLAY_FAIRPLAY_OK;
    if (!ok)
        goto cleanup;
    if (handlers->config.shared_secret_callback)
    {
        if (!handlers->config.shared_secret_callback(session, shared,
                                                     handlers->config.callback_user_data))
        {
            ok = false;
            goto cleanup;
        }
        memcpy(input, unwrapped, sizeof(unwrapped));
        memcpy(input + sizeof(unwrapped), shared, sizeof(shared));
        ok = airplay_crypto_sha256(input, sizeof(input), hash);
        if (ok)
            memcpy(output, hash, 16u);
    }
    else
        memcpy(output, unwrapped, 16u);

cleanup:
    airplay_crypto_secure_zero(unwrapped, sizeof(unwrapped));
    airplay_crypto_secure_zero(shared, sizeof(shared));
    airplay_crypto_secure_zero(input, sizeof(input));
    airplay_crypto_secure_zero(hash, sizeof(hash));
    return ok;
}

static bool setup_initial(AirPlayHandlers *handlers,
                          AirPlayHandlerSession *context,
                          AirPlayRtspSession *session,
                          const AirPlayPlistValue *root,
                          AirPlayPlistValue *response_root)
{
    const uint8_t *wrapped_key;
    const uint8_t *iv;
    size_t wrapped_size;
    size_t iv_size;
    uint16_t timing_port = 0u;

    wrapped_key = airplay_plist_get_data(airplay_plist_dict_get(root, "ekey"),
                                         &wrapped_size);
    iv = airplay_plist_get_data(airplay_plist_dict_get(root, "eiv"), &iv_size);
    if (!wrapped_key || wrapped_size != AIRPLAY_FAIRPLAY_WRAPPED_KEY_SIZE ||
        !iv || iv_size != sizeof(context->aes_iv) || context->initial_setup ||
        !derive_session_key(handlers, context, session, wrapped_key, context->aes_key))
        return false;
    memcpy(context->aes_iv, iv, sizeof(context->aes_iv));
    if (handlers->config.transport_prepare_callback &&
        !handlers->config.transport_prepare_callback(session->id, context->aes_key,
                                                     context->aes_iv, &timing_port,
                                                     handlers->config.callback_user_data))
        return false;
    context->initial_setup = true;
    return dict_set(response_root, "timingPort", airplay_plist_new_uint(timing_port)) &&
           dict_set(response_root, "eventPort", airplay_plist_new_uint(0u));
}

static bool setup_streams(AirPlayHandlers *handlers,
                          AirPlayHandlerSession *context,
                          AirPlayRtspSession *session,
                          const AirPlayPlistValue *streams,
                          AirPlayPlistValue *response_root)
{
    AirPlayPlistValue *response_streams;
    size_t count;

    if (!context->initial_setup || airplay_plist_type(streams) != AIRPLAY_PLIST_TYPE_ARRAY)
        return false;
    count = airplay_plist_array_size(streams);
    if (count == 0u || count > 4u)
        return false;
    response_streams = airplay_plist_new_array();
    if (!response_streams)
        return false;
    for (unsigned pass = 0u; pass < 2u; ++pass)
    {
        uint64_t expected_type = pass == 0u ? 110u : 96u;

        for (size_t index = 0u; index < count; ++index)
        {
            const AirPlayPlistValue *stream = airplay_plist_array_get(streams, index);
            AirPlayPlistValue *response_stream = NULL;
            uint64_t type;
            uint64_t value;
            uint16_t data_port = 0u;

            if (!airplay_plist_get_uint(airplay_plist_dict_get(stream, "type"), &type) ||
                (type != 110u && type != 96u))
                goto failure;
            if (type != expected_type)
                continue;
            if (type == 110u)
            {
                if (!airplay_plist_get_uint(
                        airplay_plist_dict_get(stream, "streamConnectionID"), &value) ||
                    value == 0u || context->mirror_setup ||
                    !handlers->config.mirror_open_callback ||
                    !handlers->config.mirror_open_callback(
                        session->id, context->aes_key, value, &data_port,
                        handlers->config.callback_user_data) ||
                    data_port == 0u)
                    goto failure;
                context->stream_connection_id = value;
                context->mirror_setup = true;
                response_stream = airplay_plist_new_dict();
            }
            else
            {
                uint16_t control_port = 0u;
                uint8_t compression_type;
                uint16_t samples_per_frame;
                uint32_t sample_rate = 44100u;

                if (!context->mirror_setup || context->audio_setup ||
                    !handlers->config.audio_open_callback ||
                    !airplay_plist_get_uint(airplay_plist_dict_get(stream, "ct"),
                                            &value) || value > UINT8_MAX)
                    goto failure;
                compression_type = (uint8_t)value;
                if (!airplay_plist_get_uint(airplay_plist_dict_get(stream, "spf"),
                                            &value) || value > UINT16_MAX)
                    goto failure;
                samples_per_frame = (uint16_t)value;
                if (airplay_plist_get_uint(airplay_plist_dict_get(stream, "sr"),
                                           &value))
                {
                    if (value > UINT32_MAX)
                        goto failure;
                    sample_rate = (uint32_t)value;
                }
                if (!handlers->config.audio_open_callback(
                        session->id, context->aes_key, context->aes_iv,
                        compression_type, samples_per_frame, sample_rate,
                        &data_port, &control_port,
                        handlers->config.callback_user_data) ||
                    data_port == 0u || control_port == 0u)
                    goto failure;
                context->audio_setup = true;
                response_stream = airplay_plist_new_dict();
                if (!response_stream ||
                    !dict_set(response_stream, "controlPort",
                              airplay_plist_new_uint(control_port)))
                {
                    airplay_plist_free(response_stream);
                    goto failure;
                }
            }
            if (!response_stream ||
                !dict_set(response_stream, "dataPort",
                          airplay_plist_new_uint(data_port)) ||
                !dict_set(response_stream, "type", airplay_plist_new_uint(type)) ||
                !array_append(response_streams, response_stream))
            {
                airplay_plist_free(response_stream);
                goto failure;
            }
        }
    }
    if (!dict_set(response_root, "streams", response_streams))
        return false;
    return true;

failure:
    airplay_plist_free(response_streams);
    return false;
}

static bool handle_setup(AirPlayHandlers *handlers,
                         AirPlayHandlerSession *context,
                         AirPlayRtspSession *session,
                         const AirPlayRtspRequest *request,
                         AirPlayRtspResponse *response)
{
    AirPlayPlistValue *root = NULL;
    AirPlayPlistValue *response_root = NULL;
    const AirPlayPlistValue *streams;
    bool has_initial;
    bool ok = false;

    if (session->state != AIRPLAY_RTSP_SESSION_CONNECTED &&
        session->state != AIRPLAY_RTSP_SESSION_SETUP)
        return airplay_rtsp_response_set_status(response, 455);
    root = decode_dict(request);
    response_root = airplay_plist_new_dict();
    if (!root || !response_root)
        goto cleanup;
    has_initial = airplay_plist_dict_get(root, "ekey") || airplay_plist_dict_get(root, "eiv");
    streams = airplay_plist_dict_get(root, "streams");
    if (!has_initial && !streams)
        goto cleanup;
    if (has_initial && !setup_initial(handlers, context, session, root, response_root))
        goto cleanup;
    if (streams && !setup_streams(handlers, context, session, streams, response_root))
        goto cleanup;
    if (!set_plist_body(response, response_root))
        goto cleanup;
    session->state = AIRPLAY_RTSP_SESSION_SETUP;
    ok = true;

cleanup:
    airplay_plist_free(root);
    airplay_plist_free(response_root);
    AIRPLAY_TRACE("[airplay] session=%llu method=SETUP initial=%u mirror=%u result=%s\n",
                  (unsigned long long)session->id, context->initial_setup ? 1u : 0u,
                  context->mirror_setup ? 1u : 0u, ok ? "ok" : "failed");
    if (!ok)
    {
        if (context->mirror_setup && handlers->config.mirror_stop_callback)
            handlers->config.mirror_stop_callback(session->id,
                                                  handlers->config.callback_user_data);
        context->initial_setup = false;
        context->mirror_setup = false;
        context->audio_setup = false;
        context->stream_connection_id = 0u;
        airplay_crypto_secure_zero(context->aes_key, sizeof(context->aes_key));
        airplay_crypto_secure_zero(context->aes_iv, sizeof(context->aes_iv));
        return airplay_rtsp_response_set_status(response, 461);
    }
    return true;
}

static bool handle_record(AirPlayHandlers *handlers,
                          AirPlayHandlerSession *context,
                          AirPlayRtspSession *session,
                          AirPlayRtspResponse *response)
{
    if (session->state != AIRPLAY_RTSP_SESSION_SETUP || !context->mirror_setup)
        return airplay_rtsp_response_set_status(response, 455);
    session->state = AIRPLAY_RTSP_SESSION_RECORDING;
    if (handlers->config.mirror_record_callback)
        handlers->config.mirror_record_callback(session->id,
                                                handlers->config.callback_user_data);
    return airplay_rtsp_response_add_header(response, "Audio-Latency", "0") &&
           airplay_rtsp_response_add_header(response, "Audio-Jack-Status",
                                            "connected; type=analog");
}

static bool handle_get_parameter(const AirPlayRtspRequest *request,
                                 AirPlayRtspResponse *response)
{
    static const char volume[] = "volume: 0.000000\r\n";
    static const char needle[] = "volume";
    bool requests_volume = false;

    if (!content_type_is(request, AIRPLAY_TEXT_CONTENT_TYPE))
        return airplay_rtsp_response_set_status(response, 400);
    for (size_t index = 0u; index + sizeof(needle) - 1u <= request->body_length; ++index)
    {
        if (memcmp(request->body + index, needle, sizeof(needle) - 1u) == 0)
        {
            requests_volume = true;
            break;
        }
    }
    if (requests_volume)
        return airplay_rtsp_response_set_body(response, volume, sizeof(volume) - 1u,
                                              AIRPLAY_TEXT_CONTENT_TYPE);
    return true;
}

static bool handle_teardown(AirPlayHandlers *handlers,
                            AirPlayHandlerSession *context,
                            AirPlayRtspSession *session,
                            AirPlayRtspResponse *response)
{
    if (session->state == AIRPLAY_RTSP_SESSION_CLOSED)
        return airplay_rtsp_response_set_status(response, 455);
    if (context->mirror_setup && handlers->config.mirror_stop_callback)
        handlers->config.mirror_stop_callback(session->id,
                                              handlers->config.callback_user_data);
    context->mirror_setup = false;
    context->audio_setup = false;
    context->initial_setup = false;
    airplay_crypto_secure_zero(context->aes_key, sizeof(context->aes_key));
    airplay_crypto_secure_zero(context->aes_iv, sizeof(context->aes_iv));
    session->state = AIRPLAY_RTSP_SESSION_CLOSED;
    response->close_connection = true;
    return true;
}

bool airplay_handlers_route(AirPlayRtspSession *session,
                            const AirPlayRtspRequest *request,
                            AirPlayRtspResponse *response,
                            void *user_data)
{
    AirPlayHandlers *handlers = user_data;
    AirPlayHandlerSession *context;

    if (!handlers || !session || !request || !response)
        return false;
    context = session_context(session);
    if (!context)
        return false;
    AIRPLAY_TRACE("[airplay] session=%llu request=%u method=%s uri=%s bytes=%zu state=%s\n",
                  (unsigned long long)session->id, session->request_count,
                  request->method, request->uri, request->body_length,
                  airplay_rtsp_session_state_name(session->state));
    if (handlers->config.remote_video)
    {
        bool handled = false;

        if (!airplay_remote_video_route(handlers->config.remote_video,
                                        session->id, request, response,
                                        &handled))
            return false;
        if (handled)
            return true;
    }
    if (strcmp(request->method, "OPTIONS") == 0)
        return airplay_rtsp_response_add_header(
            response, "Public",
            "SETUP, RECORD, FLUSH, TEARDOWN, OPTIONS, GET_PARAMETER, SET_PARAMETER");
    if (strcmp(request->uri, "/info") == 0 || strcmp(request->uri, "/server-info") == 0)
        return handle_info(handlers, request, response);
    if (strcmp(request->uri, "/fp-setup") == 0)
        return handle_fairplay(context, request, response);
    if (strcmp(request->uri, "/feedback") == 0 && strcmp(request->method, "POST") == 0)
        return true;
    if (strcmp(request->method, "SETUP") == 0)
        return handle_setup(handlers, context, session, request, response);
    if (strcmp(request->method, "RECORD") == 0)
        return handle_record(handlers, context, session, response);
    if (strcmp(request->method, "GET_PARAMETER") == 0)
    {
        if (session->state != AIRPLAY_RTSP_SESSION_SETUP &&
            session->state != AIRPLAY_RTSP_SESSION_RECORDING)
            return airplay_rtsp_response_set_status(response, 455);
        return handle_get_parameter(request, response);
    }
    if (strcmp(request->method, "SET_PARAMETER") == 0)
    {
        if (session->state != AIRPLAY_RTSP_SESSION_SETUP &&
            session->state != AIRPLAY_RTSP_SESSION_RECORDING)
            return airplay_rtsp_response_set_status(response, 455);
        return content_type_is(request, AIRPLAY_TEXT_CONTENT_TYPE) ||
                       content_type_is(request, "application/x-dmap-tagged") ||
                       content_type_is(request, "image/jpeg")
                   ? true
                   : airplay_rtsp_response_set_status(response, 400);
    }
    if (strcmp(request->method, "FLUSH") == 0)
        return session->state == AIRPLAY_RTSP_SESSION_RECORDING
                   ? true
                   : airplay_rtsp_response_set_status(response, 455);
    if (strcmp(request->method, "TEARDOWN") == 0)
        return handle_teardown(handlers, context, session, response);
    return airplay_rtsp_response_set_status(response, 501);
}

void airplay_handlers_session_closed(AirPlayRtspSession *session, void *user_data)
{
    AirPlayHandlers *handlers = user_data;
    AirPlayHandlerSession *context;

    if (!session)
        return;
    context = session->protocol_context;
    session->protocol_context = NULL;
    if (!context)
        return;
    if (context->mirror_setup && handlers && handlers->config.mirror_stop_callback)
        handlers->config.mirror_stop_callback(session->id,
                                              handlers->config.callback_user_data);
    if (handlers && handlers->config.remote_video)
        airplay_remote_video_session_closed(handlers->config.remote_video,
                                            session->id);
    airplay_fairplay_destroy(context->fairplay);
    airplay_crypto_secure_zero(context, sizeof(*context));
    free(context);
}
