#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "protocol/airplay/protocol/handlers.h"
#include "protocol/airplay/protocol/plist.h"
#include "protocol/airplay/security/crypto.h"

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

typedef struct
{
    unsigned unwrap_count;
    unsigned prepare_count;
    unsigned open_count;
    unsigned audio_open_count;
    unsigned record_count;
    unsigned stop_count;
    uint8_t prepared_key[16];
    uint8_t prepared_iv[16];
    uint8_t opened_key[16];
    uint64_t connection_id;
} Recorder;

static bool fake_unwrap(const uint8_t wrapped_key[72], uint8_t key_out[16], void *user_data)
{
    Recorder *recorder = user_data;
    recorder->unwrap_count++;
    CHECK(wrapped_key[0] == 0xa0u && wrapped_key[71] == 0xe7u);
    for (size_t index = 0u; index < 16u; ++index)
        key_out[index] = (uint8_t)index;
    return true;
}

static bool fake_shared(const AirPlayRtspSession *session, uint8_t output[32], void *user_data)
{
    (void)user_data;
    CHECK(session->id == 42u);
    for (size_t index = 0u; index < 32u; ++index)
        output[index] = (uint8_t)(0x20u + index);
    return true;
}

static bool fake_prepare(uint64_t session_id, const uint8_t key[16], const uint8_t iv[16],
                         uint16_t *timing_port, void *user_data)
{
    Recorder *recorder = user_data;
    CHECK(session_id == 42u);
    recorder->prepare_count++;
    memcpy(recorder->prepared_key, key, 16u);
    memcpy(recorder->prepared_iv, iv, 16u);
    *timing_port = 7010u;
    return true;
}

static bool fake_open(uint64_t session_id, const uint8_t key[16], uint64_t connection_id,
                      uint16_t *data_port, void *user_data)
{
    Recorder *recorder = user_data;
    CHECK(session_id == 42u);
    recorder->open_count++;
    memcpy(recorder->opened_key, key, 16u);
    recorder->connection_id = connection_id;
    *data_port = 7100u;
    return true;
}

static bool fake_audio_open(uint64_t session_id, const uint8_t key[16],
                            const uint8_t iv[16], uint8_t compression_type,
                            uint16_t samples_per_frame, uint32_t sample_rate,
                            uint16_t *data_port, uint16_t *control_port,
                            void *user_data)
{
    Recorder *recorder = user_data;

    CHECK(session_id == 42u && key && iv);
    CHECK(compression_type == 8u && samples_per_frame == 480u);
    CHECK(sample_rate == 44100u);
    recorder->audio_open_count++;
    *data_port = 7200u;
    *control_port = 7201u;
    return true;
}

static void fake_record(uint64_t session_id, void *user_data)
{
    Recorder *recorder = user_data;
    CHECK(session_id == 42u);
    recorder->record_count++;
}

static void fake_stop(uint64_t session_id, void *user_data)
{
    Recorder *recorder = user_data;
    CHECK(session_id == 42u);
    recorder->stop_count++;
}

static bool dispatch(AirPlayHandlers *handlers, AirPlayRtspSession *session,
                     const char *method, const char *uri, const uint8_t *body,
                     size_t body_size, const char *content_type,
                     AirPlayRtspResponse *response)
{
    AirPlayRtspRequest request = {0};

    snprintf(request.method, sizeof(request.method), "%s", method);
    snprintf(request.uri, sizeof(request.uri), "%s", uri);
    snprintf(request.protocol, sizeof(request.protocol), "RTSP/1.0");
    request.body = (uint8_t *)body;
    request.body_length = body_size;
    request.has_cseq = true;
    request.cseq = session->request_count + 1u;
    if (content_type)
    {
        snprintf(request.headers[0].name, sizeof(request.headers[0].name), "Content-Type");
        snprintf(request.headers[0].value, sizeof(request.headers[0].value), "%s", content_type);
        request.header_count = 1u;
    }
    return airplay_rtsp_dispatch(session, &request, airplay_handlers_route,
                                 handlers, response);
}

static bool encode(AirPlayPlistValue *root, uint8_t **body, size_t *body_size)
{
    AirPlayPlistError error;
    bool ok = airplay_plist_encode(root, body, body_size, &error);
    airplay_plist_free(root);
    return ok;
}

static bool dict_set(AirPlayPlistValue *dict, const char *key, AirPlayPlistValue *value)
{
    if (!value || !airplay_plist_dict_set(dict, key, value))
    {
        airplay_plist_free(value);
        return false;
    }
    return true;
}

static AirPlayPlistValue *decode_response(const AirPlayRtspResponse *response)
{
    AirPlayPlistValue *root = NULL;
    AirPlayPlistError error;
    CHECK(airplay_plist_decode(response->body, response->body_length, &root, &error));
    return root;
}

static AirPlayHandlers *create_handlers(Recorder *recorder)
{
    static uint8_t public_key[32];
    static const uint8_t txt[] = {7u, 'p', 'w', '=', 't', 'r', 'u', 'e'};
    AirPlayHandlersConfig config = {
        .friendly_name = "NX-Cast",
        .device_id = "10:11:12:13:14:15",
        .pairing_id = "10111213-1415-4617-9819-1a1b1c1d1e1f",
        .public_key = public_key,
        .features = UINT64_C(1) << 7,
        .airplay_txt = txt,
        .airplay_txt_size = sizeof(txt),
        .unwrap_key_callback = fake_unwrap,
        .shared_secret_callback = fake_shared,
        .transport_prepare_callback = fake_prepare,
        .mirror_open_callback = fake_open,
        .audio_open_callback = fake_audio_open,
        .mirror_record_callback = fake_record,
        .mirror_stop_callback = fake_stop,
        .callback_user_data = recorder};
    AirPlayHandlers *handlers = NULL;

    for (size_t index = 0u; index < sizeof(public_key); ++index)
        public_key[index] = (uint8_t)(0x80u + index);
    CHECK(airplay_handlers_create(&config, &handlers));
    return handlers;
}

static void test_info_and_fairplay(AirPlayHandlers *handlers)
{
    AirPlayRtspSession session;
    AirPlayRtspResponse response = {0};
    AirPlayPlistValue *root;
    uint64_t features = 0u;
    uint8_t stage2[164] = {0};
    uint8_t stage1[16] = {0};

    airplay_rtsp_session_init(&session, 41u);
    CHECK(dispatch(handlers, &session, "GET", "/info", NULL, 0u, NULL, &response));
    CHECK(response.status_code == 200);
    root = decode_response(&response);
    CHECK(strcmp(airplay_plist_get_string(airplay_plist_dict_get(root, "name")), "NX-Cast") == 0);
    CHECK(airplay_plist_get_uint(airplay_plist_dict_get(root, "features"), &features));
    CHECK(features == (UINT64_C(1) << 7));
    CHECK(airplay_plist_array_size(airplay_plist_dict_get(root, "displays")) == 1u);
    airplay_plist_free(root);
    airplay_rtsp_response_clear(&response);

    memcpy(stage2, "FPLY", 4u);
    stage2[4] = 3u;
    for (size_t index = 144u; index < sizeof(stage2); ++index)
        stage2[index] = (uint8_t)index;
    CHECK(dispatch(handlers, &session, "POST", "/fp-setup", stage2, sizeof(stage2),
                   "application/octet-stream", &response));
    CHECK(response.status_code == 200 && response.body_length == 32u);
    CHECK(memcmp(response.body + 12u, stage2 + 144u, 20u) == 0);
    airplay_rtsp_response_clear(&response);

    memcpy(stage1, "FPLY", 4u);
    stage1[4] = 3u;
    CHECK(dispatch(handlers, &session, "POST", "/fp-setup", stage1, sizeof(stage1),
                   "application/octet-stream", &response));
    CHECK(response.status_code == 501);
    airplay_rtsp_response_clear(&response);
    airplay_handlers_session_closed(&session, handlers);
}

static void test_control_transcript(AirPlayHandlers *handlers, Recorder *recorder)
{
    AirPlayRtspSession session;
    AirPlayRtspResponse response = {0};
    uint8_t wrapped[72];
    uint8_t iv[16];
    uint8_t expected_input[48];
    uint8_t expected_hash[32];
    uint8_t *body = NULL;
    size_t body_size = 0u;
    AirPlayPlistValue *root;
    AirPlayPlistValue *streams;
    AirPlayPlistValue *stream;
    AirPlayPlistValue *audio_stream;
    uint64_t value;
    static const uint8_t volume_query[] = "volume\r\n";

    for (size_t index = 0u; index < sizeof(wrapped); ++index)
        wrapped[index] = (uint8_t)(0xa0u + index);
    for (size_t index = 0u; index < sizeof(iv); ++index)
        iv[index] = (uint8_t)(0x40u + index);
    airplay_rtsp_session_init(&session, 42u);
    CHECK(dispatch(handlers, &session, "RECORD", "/stream", NULL, 0u, NULL, &response));
    CHECK(response.status_code == 455);
    airplay_rtsp_response_clear(&response);

    root = airplay_plist_new_dict();
    CHECK(root && dict_set(root, "ekey", airplay_plist_new_data(wrapped, sizeof(wrapped))) &&
          dict_set(root, "eiv", airplay_plist_new_data(iv, sizeof(iv))) &&
          encode(root, &body, &body_size));
    CHECK(dispatch(handlers, &session, "SETUP", "/stream", body, body_size,
                   "application/x-apple-binary-plist", &response));
    airplay_plist_buffer_free(body);
    CHECK(response.status_code == 200 && recorder->prepare_count == 1u);
    root = decode_response(&response);
    CHECK(airplay_plist_get_uint(airplay_plist_dict_get(root, "timingPort"), &value));
    CHECK(value == 7010u);
    airplay_plist_free(root);
    airplay_rtsp_response_clear(&response);
    for (size_t index = 0u; index < 16u; ++index)
        expected_input[index] = (uint8_t)index;
    for (size_t index = 0u; index < 32u; ++index)
        expected_input[16u + index] = (uint8_t)(0x20u + index);
    CHECK(airplay_crypto_sha256(expected_input, sizeof(expected_input), expected_hash));
    CHECK(memcmp(recorder->prepared_key, expected_hash, 16u) == 0);
    CHECK(memcmp(recorder->prepared_iv, iv, sizeof(iv)) == 0);

    root = airplay_plist_new_dict();
    streams = airplay_plist_new_array();
    stream = airplay_plist_new_dict();
    audio_stream = airplay_plist_new_dict();
    CHECK(root && streams && stream && audio_stream &&
          dict_set(stream, "type", airplay_plist_new_uint(110u)) &&
          dict_set(stream, "streamConnectionID", airplay_plist_new_uint(123456u)) &&
          dict_set(audio_stream, "type", airplay_plist_new_uint(96u)) &&
          dict_set(audio_stream, "ct", airplay_plist_new_uint(8u)) &&
          dict_set(audio_stream, "spf", airplay_plist_new_uint(480u)) &&
          airplay_plist_array_append(streams, audio_stream) &&
          airplay_plist_array_append(streams, stream) &&
          airplay_plist_dict_set(root, "streams", streams) &&
          encode(root, &body, &body_size));
    CHECK(dispatch(handlers, &session, "SETUP", "/stream", body, body_size,
                   "application/x-apple-binary-plist", &response));
    airplay_plist_buffer_free(body);
    CHECK(response.status_code == 200 && recorder->open_count == 1u);
    CHECK(recorder->audio_open_count == 1u);
    CHECK(recorder->connection_id == 123456u);
    CHECK(memcmp(recorder->opened_key, expected_hash, 16u) == 0);
    root = decode_response(&response);
    stream = (AirPlayPlistValue *)airplay_plist_array_get(
        airplay_plist_dict_get(root, "streams"), 0u);
    CHECK(airplay_plist_get_uint(airplay_plist_dict_get(stream, "dataPort"), &value));
    CHECK(value == 7100u);
    airplay_plist_free(root);
    airplay_rtsp_response_clear(&response);

    CHECK(dispatch(handlers, &session, "RECORD", "/stream", NULL, 0u, NULL, &response));
    CHECK(response.status_code == 200 && recorder->record_count == 1u);
    airplay_rtsp_response_clear(&response);
    CHECK(dispatch(handlers, &session, "GET_PARAMETER", "/stream", volume_query,
                   sizeof(volume_query) - 1u, "text/parameters", &response));
    CHECK(response.status_code == 200 && response.body_length != 0u);
    airplay_rtsp_response_clear(&response);
    CHECK(dispatch(handlers, &session, "TEARDOWN", "/stream", NULL, 0u, NULL, &response));
    CHECK(response.status_code == 200 && response.close_connection && recorder->stop_count == 1u);
    airplay_rtsp_response_clear(&response);
    airplay_handlers_session_closed(&session, handlers);
    CHECK(recorder->stop_count == 1u);
}

int main(void)
{
    Recorder recorder = {0};
    AirPlayHandlers *handlers = create_handlers(&recorder);

    test_info_and_fairplay(handlers);
    test_control_transcript(handlers, &recorder);
    airplay_handlers_destroy(handlers);
    if (g_failures != 0)
    {
        fprintf(stderr, "AirPlay handler tests failed: %d\n", g_failures);
        return 1;
    }
    puts("AirPlay handler tests passed");
    return 0;
}
