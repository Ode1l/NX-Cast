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
                  airplay_plist_new_uint(200u)) ||
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

    body = build_action(99u, locator, master, sizeof(master) - 1u,
                        &body_length);
    CHECK(body != NULL);
    CHECK(!airplay_remote_hls_handle_action(hls, session_id, body,
                                            body_length, &action));
    airplay_plist_buffer_free(body);

    body = build_action(1u, locator, master, sizeof(master) - 1u,
                        &body_length);
    CHECK(body != NULL);
    CHECK(airplay_remote_hls_handle_action(hls, session_id, body,
                                           body_length, &action));
    CHECK(action.kind == AIRPLAY_REMOTE_HLS_ACTION_REQUEST_NEXT);
    CHECK(action.event.request_id == 2u);
    CHECK(body_contains(action.event.body, action.event.body_length,
                        audio_url));
    airplay_plist_buffer_free(body);
    airplay_remote_hls_event_clear(&action.event);

    body = build_action(2u, audio_url, audio, sizeof(audio) - 1u,
                        &body_length);
    CHECK(body != NULL);
    CHECK(airplay_remote_hls_handle_action(hls, session_id, body,
                                           body_length, &action));
    CHECK(action.kind == AIRPLAY_REMOTE_HLS_ACTION_REQUEST_NEXT);
    CHECK(action.event.request_id == 3u);
    CHECK(body_contains(action.event.body, action.event.body_length,
                        video_url));
    airplay_plist_buffer_free(body);
    airplay_remote_hls_event_clear(&action.event);

    body = build_action(3u, video_url, video, sizeof(video) - 1u,
                        &body_length);
    CHECK(body != NULL);
    CHECK(airplay_remote_hls_handle_action(hls, session_id, body,
                                           body_length, &action));
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
    CHECK(handled && response.status_code == 200);
    CHECK(body_contains(response.body, response.body_length,
                        "media/0.m3u8"));
    CHECK(body_contains(response.body, response.body_length,
                        "media/1.m3u8"));
    airplay_rtsp_response_clear(&response);

    snprintf(request.uri, sizeof(request.uri),
             "/airplay-hls/000000000000002a-00000007/media/0.m3u8");
    CHECK(airplay_rtsp_response_init(&response, "HTTP/1.1", 200));
    CHECK(airplay_remote_hls_serve(hls, &request, &response, &handled));
    CHECK(handled && response.status_code == 200);
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
    CHECK(handled && response.status_code == 200);
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
    uint8_t *body;
    size_t body_length;

    CHECK(airplay_remote_hls_create(&hls));
    CHECK(airplay_remote_hls_begin(hls, 5u, 1u, 7000u, "session",
                                   locator, &event));
    body = build_action(event.request_id, locator, invalid,
                        sizeof(invalid) - 1u, &body_length);
    CHECK(body != NULL);
    CHECK(!airplay_remote_hls_handle_action(hls, 5u, body, body_length,
                                            &action));
    airplay_plist_buffer_free(body);
    airplay_remote_hls_event_clear(&event);
    airplay_remote_hls_destroy(hls);
}

int main(void)
{
    test_master_and_media_transcript();
    test_malformed_playlist();
    if (g_failures)
    {
        fprintf(stderr, "%d AirPlay remote HLS checks failed\n", g_failures);
        return 1;
    }
    puts("AirPlay remote HLS checks passed");
    return 0;
}
