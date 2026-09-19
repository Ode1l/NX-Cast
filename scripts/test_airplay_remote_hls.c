#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "protocol/airplay/media/remote_hls.h"
#include "protocol/airplay/protocol/plist.h"

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

static bool dict_set(AirPlayPlistValue *dict, const char *key,
                     AirPlayPlistValue *value)
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

static uint8_t *build_action(uint32_t request_id, const char *url,
                             const void *playlist, size_t playlist_length,
                             uint64_t status_code,
                             size_t *body_length_out)
{
    AirPlayPlistValue *root = airplay_plist_new_dict();
    AirPlayPlistValue *params = airplay_plist_new_dict();
    AirPlayPlistError error;
    uint8_t *body = NULL;

    if (!root || !params ||
        !dict_set(root, "type",
                  airplay_plist_new_string("unhandledURLResponse")) ||
        !dict_set(params, "FCUP_Response_StatusCode",
                  airplay_plist_new_uint(status_code)) ||
        !dict_set(params, "FCUP_Response_RequestID",
                  airplay_plist_new_uint(request_id)) ||
        !dict_set(params, "FCUP_Response_URL",
                  airplay_plist_new_string(url)) ||
        !dict_set(params, "FCUP_Response_Data",
                  airplay_plist_new_data(playlist, playlist_length)) ||
        !airplay_plist_dict_set(root, "params", params))
    {
        airplay_plist_free(params);
        airplay_plist_free(root);
        return NULL;
    }
    params = NULL;
    if (!airplay_plist_encode(root, &body, body_length_out, &error))
        body = NULL;
    airplay_plist_free(root);
    return body;
}

static bool body_contains(const uint8_t *body, size_t body_length,
                          const char *needle)
{
    size_t needle_length = strlen(needle);
    size_t index;

    if (!body || needle_length > body_length)
        return false;
    for (index = 0u; index <= body_length - needle_length; ++index)
    {
        if (memcmp(body + index, needle, needle_length) == 0)
            return true;
    }
    return false;
}

static void test_master_and_media_transcript(void)
{
    static const uint64_t session_id = 42u;
    static const uint32_t generation = 7u;
    static const char apple_session[] = "phone-session<&>";
    static const char locator[] = "airplay://phone/library/master.m3u8";
    static const char audio_url[] =
        "https://cdn.example/live/audio/program.m3u8";
    static const char video_url[] =
        "https://cdn.example/live/video/program.m3u8";
    static const char master[] =
        "#EXTM3U\n"
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",URI=\""
        "https://cdn.example/live/audio/program.m3u8\"\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=1200000,AUDIO=\"audio\"\n"
        "https://cdn.example/live/video/program.m3u8\n";
    static const char audio[] =
        "#EXTM3U\n"
        "#EXT-X-KEY:METHOD=AES-128,URI=\"keys/audio.key\"\n"
        "#EXTINF:4.0,\n"
        "segments/audio-1.aac\n";
    static const char video[] =
        "#EXTM3U\n"
        "#EXT-X-MAP:URI=\"init.mp4\"\n"
        "#EXTINF:4.0,\n"
        "segments/video-1.m4s\n";
    AirPlayRemoteHls *hls = NULL;
    AirPlayRemoteHlsEvent event = {0};
    AirPlayRemoteHlsAction action = {0};
    AirPlayRemoteHlsActionResult action_result =
        AIRPLAY_REMOTE_HLS_ACTION_RESULT_INVALID_ARGUMENT;
    AirPlayRtspRequest request = {0};
    AirPlayRtspResponse response = {0};
    const char *local_path;
    uint8_t *body;
    size_t body_length;
    bool handled = false;

    CHECK(airplay_remote_hls_locator_supported(locator));
    CHECK(!airplay_remote_hls_locator_supported(
        "https://cdn.example/master.m3u8"));
    CHECK(airplay_remote_hls_create(&hls));
    CHECK(airplay_remote_hls_begin(hls, session_id, generation, 7000u,
                                   apple_session, locator, &event));
    CHECK(event.request_id == 1u && event.body != NULL);
    CHECK(body_contains(event.body, event.body_length,
                        "<integer>1</integer><key>FCUP_Response_URL"));
    CHECK(body_contains(event.body, event.body_length,
                        "phone-session&lt;&amp;&gt;"));
    airplay_remote_hls_event_clear(&event);

    body = build_action(99u, locator, master, sizeof(master) - 1u, 200u,
                        &body_length);
    CHECK(body != NULL);
    CHECK(!airplay_remote_hls_handle_action(
        hls, session_id, body, body_length, &action, &action_result));
    CHECK(action_result ==
          AIRPLAY_REMOTE_HLS_ACTION_RESULT_REQUEST_MISMATCH);
    airplay_plist_buffer_free(body);

    body = build_action(1u, locator, master, sizeof(master) - 1u, 200u,
                        &body_length);
    CHECK(body != NULL);
    CHECK(airplay_remote_hls_handle_action(hls, session_id, body,
                                           body_length, &action,
                                           &action_result));
    CHECK(action_result == AIRPLAY_REMOTE_HLS_ACTION_RESULT_OK);
    CHECK(action.kind == AIRPLAY_REMOTE_HLS_ACTION_REQUEST_NEXT);
    CHECK(action.event.request_id == 2u);
    CHECK(body_contains(action.event.body, action.event.body_length,
                        audio_url));
    airplay_plist_buffer_free(body);
    airplay_remote_hls_event_clear(&action.event);

    body = build_action(2u, audio_url, audio, sizeof(audio) - 1u, 200u,
                        &body_length);
    CHECK(body != NULL);
    CHECK(airplay_remote_hls_handle_action(hls, session_id, body,
                                           body_length, &action,
                                           &action_result));
    CHECK(action_result == AIRPLAY_REMOTE_HLS_ACTION_RESULT_OK);
    CHECK(action.kind == AIRPLAY_REMOTE_HLS_ACTION_REQUEST_NEXT);
    CHECK(action.event.request_id == 3u);
    CHECK(body_contains(action.event.body, action.event.body_length,
                        video_url));
    airplay_plist_buffer_free(body);
    airplay_remote_hls_event_clear(&action.event);

    body = build_action(3u, video_url, video, sizeof(video) - 1u, 200u,
                        &body_length);
    CHECK(body != NULL);
    CHECK(airplay_remote_hls_handle_action(hls, session_id, body,
                                           body_length, &action,
                                           &action_result));
    CHECK(action_result == AIRPLAY_REMOTE_HLS_ACTION_RESULT_OK);
    CHECK(action.kind == AIRPLAY_REMOTE_HLS_ACTION_READY);
    CHECK(strcmp(action.playback_url,
                 "http://127.0.0.1:7000/airplay-hls/"
                 "000000000000002a-00000007/master.m3u8") == 0);
    airplay_plist_buffer_free(body);

    local_path = strstr(action.playback_url, "/airplay-hls/");
    CHECK(local_path != NULL);
    snprintf(request.method, sizeof(request.method), "GET");
    snprintf(request.uri, sizeof(request.uri), "%s", local_path);
    snprintf(request.protocol, sizeof(request.protocol), "HTTP/1.1");
    CHECK(airplay_rtsp_response_init(&response, "HTTP/1.1", 200));
    CHECK(airplay_remote_hls_serve(hls, &request, &response, &handled));
    CHECK(handled && response.status_code == 200 &&
          response.close_connection);
    CHECK(body_contains(
        response.body, response.body_length,
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",URI=\"media/0.m3u8\""));
    CHECK(body_contains(response.body, response.body_length,
                        "#EXT-X-STREAM-INF:BANDWIDTH=1200000,AUDIO=\"audio\"\n"
                        "media/1.m3u8"));
    airplay_rtsp_response_clear(&response);

    snprintf(request.uri, sizeof(request.uri),
             "/airplay-hls/000000000000002a-00000007/media/0.m3u8");
    CHECK(airplay_rtsp_response_init(&response, "HTTP/1.1", 200));
    CHECK(airplay_remote_hls_serve(hls, &request, &response, &handled));
    CHECK(handled && response.status_code == 200 &&
          response.close_connection);
    CHECK(body_contains(response.body, response.body_length,
                        "https://cdn.example/live/audio/keys/audio.key"));
    CHECK(body_contains(
        response.body, response.body_length,
        "https://cdn.example/live/audio/segments/audio-1.aac"));
    airplay_rtsp_response_clear(&response);

    snprintf(request.uri, sizeof(request.uri),
             "/airplay-hls/000000000000002a-00000007/media/1.m3u8");
    CHECK(airplay_rtsp_response_init(&response, "HTTP/1.1", 200));
    CHECK(airplay_remote_hls_serve(hls, &request, &response, &handled));
    CHECK(handled && response.status_code == 200 &&
          response.close_connection);
    CHECK(body_contains(response.body, response.body_length,
                        "https://cdn.example/live/video/init.mp4"));
    CHECK(body_contains(
        response.body, response.body_length,
        "https://cdn.example/live/video/segments/video-1.m4s"));
    airplay_rtsp_response_clear(&response);

    airplay_remote_hls_reset(hls, session_id, generation);
    CHECK(airplay_rtsp_response_init(&response, "HTTP/1.1", 200));
    CHECK(airplay_remote_hls_serve(hls, &request, &response, &handled));
    CHECK(handled && response.status_code == 404);
    airplay_rtsp_response_clear(&response);
    airplay_remote_hls_destroy(hls);
}

static void test_malformed_playlist(void)
{
    static const char locator[] = "airplay://phone/bad/master.m3u8";
    static const char invalid[] = "not an HLS playlist\n";
    AirPlayRemoteHls *hls = NULL;
    AirPlayRemoteHlsEvent event = {0};
    AirPlayRemoteHlsAction action = {0};
    AirPlayRemoteHlsActionResult action_result =
        AIRPLAY_REMOTE_HLS_ACTION_RESULT_INVALID_ARGUMENT;
    uint8_t *body;
    size_t body_length;

    CHECK(airplay_remote_hls_create(&hls));
    CHECK(airplay_remote_hls_begin(hls, 5u, 1u, 7000u, "session",
                                   locator, &event));
    body = build_action(event.request_id, locator, invalid,
                        sizeof(invalid) - 1u, 200u, &body_length);
    CHECK(body != NULL);
    CHECK(!airplay_remote_hls_handle_action(
        hls, 5u, body, body_length, &action, &action_result));
    CHECK(action_result ==
          AIRPLAY_REMOTE_HLS_ACTION_RESULT_BAD_PLAYLIST);
    airplay_plist_buffer_free(body);
    airplay_remote_hls_event_clear(&event);
    airplay_remote_hls_destroy(hls);
}

static void test_field_validation(void)
{
    static const char locator[] = "airplay://phone/fields/master.m3u8";
    static const char master[] =
        "#EXTM3U\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=1000000\n"
        "segment.m3u8\n";
    AirPlayRemoteHls *hls = NULL;
    AirPlayRemoteHlsEvent event = {0};
    AirPlayRemoteHlsAction action = {0};
    AirPlayRemoteHlsActionResult result =
        AIRPLAY_REMOTE_HLS_ACTION_RESULT_INVALID_ARGUMENT;
    uint8_t *body;
    size_t body_length;

    CHECK(airplay_remote_hls_create(&hls));
    CHECK(airplay_remote_hls_begin(hls, 6u, 2u, 7000u, "session",
                                   locator, &event));
    body = build_action(event.request_id, locator, master,
                        sizeof(master) - 1u, 404u, &body_length);
    CHECK(body != NULL);
    CHECK(airplay_remote_hls_handle_action(
        hls, 6u, body, body_length, &action, &result));
    CHECK(result == AIRPLAY_REMOTE_HLS_ACTION_RESULT_OK);
    CHECK(action.kind == AIRPLAY_REMOTE_HLS_ACTION_REQUEST_NEXT);
    CHECK(action.event.request_id == 2u && action.event.body != NULL);
    airplay_plist_buffer_free(body);
    airplay_remote_hls_event_clear(&action.event);

    body = build_action(0u, locator, master, sizeof(master) - 1u, 200u,
                        &body_length);
    CHECK(body != NULL);
    CHECK(!airplay_remote_hls_handle_action(
        hls, 6u, body, body_length, &action, &result));
    CHECK(result == AIRPLAY_REMOTE_HLS_ACTION_RESULT_BAD_REQUEST_ID);
    airplay_plist_buffer_free(body);
    airplay_remote_hls_event_clear(&event);
    airplay_remote_hls_destroy(hls);
}

static void test_condensed_initialization_range(void)
{
    static const char locator[] =
        "airplay://phone/condensed/master.m3u8";
    static const char condensed[] =
        "#EXTM3U\n"
        "#YT-EXT-CONDENSED-URL:BASE-URI=\"https://cdn.example/video\","
        "PARAMS=\"slices,sequence\",PREFIX=\"sq/\"\n"
        "#EXT-X-VERSION:3\n"
        "#EXTINF:4.0,\n"
        "sq/0-308589/0\n"
        "#EXTINF:4.0,\n"
        "sq/308590-676419/1\n"
        "#EXT-X-ENDLIST\n";
    AirPlayRemoteHls *hls = NULL;
    AirPlayRemoteHlsEvent event = {0};
    AirPlayRemoteHlsAction action = {0};
    AirPlayRemoteHlsActionResult result =
        AIRPLAY_REMOTE_HLS_ACTION_RESULT_INVALID_ARGUMENT;
    AirPlayRtspRequest request = {0};
    AirPlayRtspResponse response = {0};
    const char *local_path;
    uint8_t *body;
    size_t body_length;
    bool handled = false;

    CHECK(airplay_remote_hls_create(&hls));
    CHECK(airplay_remote_hls_begin(hls, 7u, 3u, 7000u, "session",
                                   locator, &event));
    body = build_action(event.request_id, locator, condensed,
                        sizeof(condensed) - 1u, 200u, &body_length);
    CHECK(body != NULL);
    CHECK(airplay_remote_hls_handle_action(
        hls, 7u, body, body_length, &action, &result));
    airplay_plist_buffer_free(body);
    CHECK(result == AIRPLAY_REMOTE_HLS_ACTION_RESULT_OK);
    CHECK(action.kind == AIRPLAY_REMOTE_HLS_ACTION_READY);
    local_path = strstr(action.playback_url, "/airplay-hls/");
    CHECK(local_path != NULL);
    snprintf(request.method, sizeof(request.method), "GET");
    snprintf(request.uri, sizeof(request.uri), "%s", local_path);
    snprintf(request.protocol, sizeof(request.protocol), "HTTP/1.1");
    CHECK(airplay_rtsp_response_init(&response, "HTTP/1.1", 200));
    CHECK(airplay_remote_hls_serve(hls, &request, &response, &handled));
    CHECK(handled && response.status_code == 200);
    CHECK(body_contains(response.body, response.body_length,
                        "https://cdn.example/video/slices/0-308589/"
                        "sequence/0"));
    CHECK(body_contains(response.body, response.body_length,
                        "https://cdn.example/video/slices/308590-676419/"
                        "sequence/1"));
    CHECK(!body_contains(response.body, response.body_length,
                         "sq/0-308589/0"));
    airplay_rtsp_response_clear(&response);
    airplay_remote_hls_event_clear(&event);
    airplay_remote_hls_destroy(hls);
}

static void test_large_condensed_playlist(void)
{
    static const char locator[] =
        "airplay://phone/large-condensed/master.m3u8";
    static const char header[] =
        "#EXTM3U\n"
        "#YT-EXT-CONDENSED-URL:BASE-URI=\"https://cdn.example/"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "video\","
        "PARAMS=\"slice\",PREFIX=\"sq/\"\n"
        "#EXT-X-VERSION:3\n";
    static const char tail[] = "\n#EXT-X-ENDLIST\n";
    const size_t segment_count = 4096u;
    const size_t playlist_capacity = sizeof(header) + sizeof(tail) +
                                     segment_count * 32u;
    AirPlayRemoteHls *hls = NULL;
    AirPlayRemoteHlsEvent event = {0};
    AirPlayRemoteHlsAction action = {0};
    AirPlayRemoteHlsActionResult result =
        AIRPLAY_REMOTE_HLS_ACTION_RESULT_INVALID_ARGUMENT;
    AirPlayRtspRequest request = {0};
    AirPlayRtspResponse response = {0};
    uint8_t *playlist = NULL;
    uint8_t *body = NULL;
    uint8_t *encoded = NULL;
    size_t playlist_length = 0u;
    size_t body_length;
    size_t encoded_length = 0u;
    const char *local_path;
    bool handled = false;

    playlist = malloc(playlist_capacity);
    CHECK(playlist != NULL);
    if (!playlist)
        return;
    memcpy(playlist, header, sizeof(header) - 1u);
    playlist_length = sizeof(header) - 1u;
    for (size_t index = 0u; index < segment_count; ++index)
    {
        int written = snprintf((char *)playlist + playlist_length,
                               playlist_capacity - playlist_length,
                               "#EXTINF:4.0,\nsq/%zu\n", index);

        CHECK(written > 0 &&
              (size_t)written < playlist_capacity - playlist_length);
        if (written <= 0 ||
            (size_t)written >= playlist_capacity - playlist_length)
        {
            free(playlist);
            return;
        }
        playlist_length += (size_t)written;
    }
    memcpy(playlist + playlist_length, tail, sizeof(tail) - 1u);
    playlist_length += sizeof(tail) - 1u;

    CHECK(airplay_remote_hls_create(&hls));
    CHECK(airplay_remote_hls_begin(hls, 8u, 4u, 7000u, "session",
                                   locator, &event));
    body = build_action(event.request_id, locator, playlist, playlist_length,
                        200u, &body_length);
    CHECK(body != NULL);
    CHECK(airplay_remote_hls_handle_action(
        hls, 8u, body, body_length, &action, &result));
    CHECK(result == AIRPLAY_REMOTE_HLS_ACTION_RESULT_OK);
    CHECK(action.kind == AIRPLAY_REMOTE_HLS_ACTION_READY);

    local_path = strstr(action.playback_url, "/airplay-hls/");
    CHECK(local_path != NULL);
    snprintf(request.method, sizeof(request.method), "GET");
    snprintf(request.uri, sizeof(request.uri), "%s", local_path);
    snprintf(request.protocol, sizeof(request.protocol), "HTTP/1.1");
    CHECK(airplay_rtsp_response_init(&response, "HTTP/1.1", 200));
    CHECK(airplay_remote_hls_serve(hls, &request, &response, &handled));
    CHECK(handled && response.status_code == 200);
    CHECK(response.body_length > AIRPLAY_RTSP_MAX_BODY_BYTES);
    CHECK(body_contains(response.body, response.body_length,
                        "video/slice/4095"));
    CHECK(airplay_rtsp_response_encode(&response, &encoded, &encoded_length));
    CHECK(encoded_length > response.body_length);

    free(encoded);
    airplay_rtsp_response_clear(&response);
    airplay_plist_buffer_free(body);
    free(playlist);
    airplay_remote_hls_event_clear(&event);
    airplay_remote_hls_destroy(hls);
}

static void test_master_preserves_all_media(void)
{
    static const char locator[] = "airplay://phone/adaptive/master.m3u8";
    static const char *const media_urls[] = {
        "https://cdn.example/audio-en.m3u8",
        "https://cdn.example/audio-fr.m3u8",
        "https://cdn.example/video-low.m3u8",
        "https://cdn.example/video-high.m3u8"};
    static const char master[] =
        "#EXTM3U\n"
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",NAME=\"English\","
        "DEFAULT=YES,URI=\"https://cdn.example/audio-en.m3u8\"\n"
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",NAME=\"French\","
        "DEFAULT=NO,URI=\"https://cdn.example/audio-fr.m3u8\"\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=1000000,AUDIO=\"audio\"\n"
        "https://cdn.example/video-low.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=4000000,AUDIO=\"audio\"\n"
        "https://cdn.example/video-high.m3u8\n";
    static const char media[] =
        "#EXTM3U\n#EXTINF:4.0,\nsegment.ts\n#EXT-X-ENDLIST\n";
    AirPlayRemoteHls *hls = NULL;
    AirPlayRemoteHlsEvent event = {0};
    AirPlayRemoteHlsAction action = {0};
    AirPlayRemoteHlsActionResult result =
        AIRPLAY_REMOTE_HLS_ACTION_RESULT_INVALID_ARGUMENT;
    AirPlayRtspRequest request = {0};
    AirPlayRtspResponse response = {0};
    uint8_t *body;
    size_t body_length;
    size_t media_index;
    const char *local_path;
    bool handled = false;

    CHECK(airplay_remote_hls_create(&hls));
    CHECK(airplay_remote_hls_begin(hls, 9u, 5u, 7000u, "session",
                                   locator, &event));
    body = build_action(event.request_id, locator, master,
                        sizeof(master) - 1u, 200u, &body_length);
    CHECK(body != NULL);
    CHECK(airplay_remote_hls_handle_action(
        hls, 9u, body, body_length, &action, &result));
    CHECK(action.kind == AIRPLAY_REMOTE_HLS_ACTION_REQUEST_NEXT);
    CHECK(body_contains(action.event.body, action.event.body_length,
                        media_urls[0]));
    airplay_plist_buffer_free(body);
    airplay_remote_hls_event_clear(&action.event);

    for (media_index = 0u;
         media_index < sizeof(media_urls) / sizeof(media_urls[0]);
         ++media_index)
    {
        body = build_action((uint32_t)media_index + 2u,
                            media_urls[media_index], media,
                            sizeof(media) - 1u, 200u, &body_length);
        CHECK(body != NULL);
        CHECK(airplay_remote_hls_handle_action(
            hls, 9u, body, body_length, &action, &result));
        airplay_plist_buffer_free(body);
        if (media_index + 1u < sizeof(media_urls) / sizeof(media_urls[0]))
        {
            CHECK(action.kind == AIRPLAY_REMOTE_HLS_ACTION_REQUEST_NEXT);
            CHECK(body_contains(action.event.body, action.event.body_length,
                                media_urls[media_index + 1u]));
            airplay_remote_hls_event_clear(&action.event);
        }
        else
            CHECK(action.kind == AIRPLAY_REMOTE_HLS_ACTION_READY);
    }

    local_path = strstr(action.playback_url, "/airplay-hls/");
    CHECK(local_path != NULL);
    snprintf(request.method, sizeof(request.method), "GET");
    snprintf(request.uri, sizeof(request.uri), "%s", local_path);
    snprintf(request.protocol, sizeof(request.protocol), "HTTP/1.1");
    CHECK(airplay_rtsp_response_init(&response, "HTTP/1.1", 200));
    CHECK(airplay_remote_hls_serve(hls, &request, &response, &handled));
    CHECK(handled && response.status_code == 200);
    CHECK(body_contains(response.body, response.body_length,
                        "BANDWIDTH=4000000"));
    CHECK(body_contains(response.body, response.body_length,
                        "BANDWIDTH=1000000"));
    CHECK(body_contains(response.body, response.body_length, "English"));
    CHECK(body_contains(response.body, response.body_length, "French"));
    CHECK(body_contains(response.body, response.body_length, "media/0.m3u8"));
    CHECK(body_contains(response.body, response.body_length, "media/1.m3u8"));
    CHECK(body_contains(response.body, response.body_length, "media/2.m3u8"));
    CHECK(body_contains(response.body, response.body_length, "media/3.m3u8"));

    airplay_rtsp_response_clear(&response);
    airplay_remote_hls_event_clear(&event);
    airplay_remote_hls_destroy(hls);
}

static void test_master_prefers_explicit_h264_variants(void)
{
    static const char locator[] = "airplay://phone/codecs/master.m3u8";
    static const char master[] =
        "#EXTM3U\n"
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",NAME=\"Main\","
        "DEFAULT=YES,URI=\"https://cdn.example/audio.m3u8\"\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=1200000,CODECS=\"avc1.4d401f,mp4a.40.2\",AUDIO=\"audio\"\n"
        "https://cdn.example/h264.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=900000,CODECS=\"vp09.00.31.08,mp4a.40.2\",AUDIO=\"audio\"\n"
        "https://cdn.example/vp9.m3u8\n";
    AirPlayRemoteHls *hls = NULL;
    AirPlayRemoteHlsEvent event = {0};
    AirPlayRemoteHlsAction action = {0};
    AirPlayRemoteHlsActionResult result =
        AIRPLAY_REMOTE_HLS_ACTION_RESULT_INVALID_ARGUMENT;
    AirPlayRtspRequest request = {0};
    AirPlayRtspResponse response = {0};
    uint8_t *body;
    size_t body_length;
    const char *local_path;
    bool handled = false;

    CHECK(airplay_remote_hls_create(&hls));
    CHECK(airplay_remote_hls_begin(hls, 10u, 6u, 7000u, "session",
                                   locator, &event));
    body = build_action(event.request_id, locator, master,
                        sizeof(master) - 1u, 200u, &body_length);
    CHECK(body != NULL);
    CHECK(airplay_remote_hls_handle_action(
        hls, 10u, body, body_length, &action, &result));
    CHECK(action.kind == AIRPLAY_REMOTE_HLS_ACTION_REQUEST_NEXT);
    CHECK(body_contains(action.event.body, action.event.body_length,
                        "audio.m3u8"));
    airplay_plist_buffer_free(body);
    airplay_remote_hls_event_clear(&action.event);

    local_path = NULL;
    for (uint32_t request_id = 2u; request_id <= 3u; ++request_id)
    {
        static const char media[] =
            "#EXTM3U\n#EXTINF:4.0,\nsegment.ts\n#EXT-X-ENDLIST\n";
        const char *url = request_id == 2u
                              ? "https://cdn.example/audio.m3u8"
                              : "https://cdn.example/h264.m3u8";

        body = build_action(request_id, url, media, sizeof(media) - 1u,
                            200u, &body_length);
        CHECK(body != NULL);
        CHECK(airplay_remote_hls_handle_action(
            hls, 10u, body, body_length, &action, &result));
        airplay_plist_buffer_free(body);
        if (request_id == 2u)
            airplay_remote_hls_event_clear(&action.event);
        else
            local_path = strstr(action.playback_url, "/airplay-hls/");
    }
    CHECK(local_path != NULL);
    snprintf(request.method, sizeof(request.method), "GET");
    snprintf(request.uri, sizeof(request.uri), "%s", local_path);
    snprintf(request.protocol, sizeof(request.protocol), "HTTP/1.1");
    CHECK(airplay_rtsp_response_init(&response, "HTTP/1.1", 200));
    CHECK(airplay_remote_hls_serve(hls, &request, &response, &handled));
    CHECK(handled && response.status_code == 200);
    CHECK(body_contains(response.body, response.body_length, "avc1.4d401f"));
    CHECK(!body_contains(response.body, response.body_length, "vp09"));
    CHECK(!body_contains(response.body, response.body_length, "vp9.m3u8"));
    CHECK(body_contains(response.body, response.body_length, "audio"));

    airplay_rtsp_response_clear(&response);
    airplay_remote_hls_event_clear(&event);
    airplay_remote_hls_destroy(hls);
}

int main(void)
{
    test_master_and_media_transcript();
    test_malformed_playlist();
    test_field_validation();
    test_condensed_initialization_range();
    test_large_condensed_playlist();
    test_master_preserves_all_media();
    test_master_prefers_explicit_h264_variants();
    if (g_failures)
    {
        fprintf(stderr, "%d AirPlay remote HLS checks failed\n", g_failures);
        return 1;
    }
    puts("AirPlay remote HLS checks passed");
    return 0;
}
