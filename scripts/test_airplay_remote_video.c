#include <stdio.h>
#include <string.h>

#include "protocol/airplay/media/remote_video.h"
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

static bool body_contains(const AirPlayRtspResponse *response,
                          const char *needle)
{
    size_t needle_size = strlen(needle);

    if (!response || !response->body || needle_size > response->body_length)
        return false;
    for (size_t index = 0u; index <= response->body_length - needle_size;
         ++index)
    {
        if (memcmp(response->body + index, needle, needle_size) == 0)
            return true;
    }
    return false;
}

static bool dispatch(AirPlayRemoteVideo *remote, uint64_t session_id,
                     const char *method, const char *uri, const void *body,
                     size_t body_size, const char *content_type,
                     AirPlayRtspResponse *response);

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

typedef struct
{
    AirPlayRemoteVideoSnapshot snapshot;
    char url[AIRPLAY_REMOTE_VIDEO_URL_MAX + 1u];
    char metadata[AIRPLAY_REMOTE_VIDEO_METADATA_MAX + 1u];
    unsigned load_count;
    unsigned claim_count;
    unsigned release_count;
    unsigned play_count;
    unsigned pause_count;
    unsigned stop_count;
    unsigned seek_count;
    unsigned reverse_send_count;
    unsigned control_port_count;
    unsigned control_attached_count;
    unsigned control_detached_count;
    int last_seek_ms;
    AirPlayRemoteVideo *remote;
    bool cancel_during_claim;
    bool fail_load;
} Recorder;

static void fake_control_attached(uint64_t session_id, void *user_data)
{
    Recorder *recorder = user_data;

    CHECK(session_id == 10u || session_id == 11u || session_id == 13u ||
          session_id == 14u);
    recorder->control_attached_count++;
}

static void fake_control_detached(uint64_t session_id, void *user_data)
{
    Recorder *recorder = user_data;

    CHECK(session_id == 10u || session_id == 11u || session_id == 13u ||
          session_id == 14u);
    recorder->control_detached_count++;
}

static bool fake_claim(uint64_t session_id, void *user_data)
{
    Recorder *recorder = user_data;

    CHECK(session_id == 10u || session_id == 11u || session_id == 13u ||
          session_id == 14u);
    recorder->claim_count++;
    if (recorder->cancel_during_claim && recorder->remote)
    {
        AirPlayRtspResponse stop_response = {0};

        (void)dispatch(recorder->remote, session_id, "POST", "/stop", NULL, 0u,
                       NULL, &stop_response);
        airplay_rtsp_response_clear(&stop_response);
    }
    return true;
}

static void fake_release(uint64_t session_id, void *user_data)
{
    Recorder *recorder = user_data;

    CHECK(session_id == 10u || session_id == 11u || session_id == 13u ||
          session_id == 14u);
    recorder->release_count++;
}

static bool fake_load(const char *url, const char *metadata, void *user_data)
{
    Recorder *recorder = user_data;

    snprintf(recorder->url, sizeof(recorder->url), "%s", url);
    snprintf(recorder->metadata, sizeof(recorder->metadata), "%s",
             metadata ? metadata : "");
    recorder->snapshot.has_media = true;
    recorder->snapshot.seekable = true;
    recorder->snapshot.duration_ms = 600000;
    recorder->snapshot.position_ms = 0;
    recorder->snapshot.state = AIRPLAY_REMOTE_VIDEO_LOADING;
    recorder->load_count++;
    return !recorder->fail_load;
}

static bool fake_play(void *user_data)
{
    Recorder *recorder = user_data;

    recorder->snapshot.state = AIRPLAY_REMOTE_VIDEO_PLAYING;
    recorder->play_count++;
    return true;
}

static bool fake_pause(void *user_data)
{
    Recorder *recorder = user_data;

    recorder->snapshot.state = AIRPLAY_REMOTE_VIDEO_PAUSED;
    recorder->pause_count++;
    return true;
}

static bool fake_stop(void *user_data)
{
    Recorder *recorder = user_data;

    recorder->snapshot.state = AIRPLAY_REMOTE_VIDEO_STOPPED;
    recorder->stop_count++;
    return true;
}

static bool fake_seek(int position_ms, void *user_data)
{
    Recorder *recorder = user_data;

    recorder->snapshot.position_ms = position_ms;
    recorder->last_seek_ms = position_ms;
    recorder->seek_count++;
    return true;
}

static bool fake_snapshot(AirPlayRemoteVideoSnapshot *snapshot_out,
                          void *user_data)
{
    Recorder *recorder = user_data;

    *snapshot_out = recorder->snapshot;
    return true;
}

static bool fake_send_reverse(uint64_t session_id,
                              const AirPlayRtspOutboundRequest *request,
                              void *user_data)
{
    Recorder *recorder = user_data;

    CHECK(session_id == 13u || session_id == 14u);
    CHECK(request && strcmp(request->uri, "/event") == 0);
    CHECK(request->body && request->body_length != 0u);
    recorder->reverse_send_count++;
    return true;
}

static uint16_t fake_control_port(void *user_data)
{
    Recorder *recorder = user_data;

    recorder->control_port_count++;
    return 7000u;
}

static bool dispatch(AirPlayRemoteVideo *remote, uint64_t session_id,
                     const char *method, const char *uri, const void *body,
                     size_t body_size, const char *content_type,
                     AirPlayRtspResponse *response)
{
    AirPlayRtspRequest request = {0};
    bool handled = false;

    snprintf(request.method, sizeof(request.method), "%s", method);
    snprintf(request.uri, sizeof(request.uri), "%s", uri);
    snprintf(request.protocol, sizeof(request.protocol), "HTTP/1.1");
    request.body = (uint8_t *)body;
    request.body_length = body_size;
    if (content_type)
    {
        snprintf(request.headers[0].name, sizeof(request.headers[0].name),
                 "Content-Type");
        snprintf(request.headers[0].value, sizeof(request.headers[0].value),
                 "%s", content_type);
    }
    snprintf(request.headers[1].name, sizeof(request.headers[1].name),
             "X-Apple-Session-ID");
    snprintf(request.headers[1].value, sizeof(request.headers[1].value),
             "test-session");
    request.header_count = 2u;
    CHECK(airplay_rtsp_response_init(response, "HTTP/1.1", 200));
    return airplay_remote_video_route(remote, session_id, &request, response,
                                      &handled) &&
           handled;
}

static uint8_t *binary_play_body(const char *title, const char *client_name,
                                 size_t *size_out)
{
    AirPlayPlistValue *root = airplay_plist_new_dict();
    uint8_t *body = NULL;
    AirPlayPlistError error;

    if (!root ||
        !airplay_plist_dict_set(
            root, "Content-Location",
            airplay_plist_new_string("https://media.example/live/master.m3u8")) ||
        !airplay_plist_dict_set(root, "Start-Position-Seconds",
                                airplay_plist_new_real(7.25)) ||
        (title && !airplay_plist_dict_set(root, "title",
                                          airplay_plist_new_string(title))) ||
        (client_name &&
         !airplay_plist_dict_set(root, "clientProcName",
                                 airplay_plist_new_string(client_name))) ||
        !airplay_plist_encode(root, &body, size_out, &error))
    {
        airplay_plist_buffer_free(body);
        body = NULL;
    }
    airplay_plist_free(root);
    return body;
}

static uint8_t *reverse_play_body(size_t *size_out)
{
    AirPlayPlistValue *root = airplay_plist_new_dict();
    uint8_t *body = NULL;
    AirPlayPlistError error;

    if (!root ||
        !airplay_plist_dict_set(
            root, "Content-Location",
            airplay_plist_new_string(
                "airplay://phone/library/master.m3u8")) ||
        !airplay_plist_encode(root, &body, size_out, &error))
    {
        airplay_plist_buffer_free(body);
        body = NULL;
    }
    airplay_plist_free(root);
    return body;
}

static uint8_t *hls_action_body(uint32_t request_id, const char *url,
                                const void *playlist,
                                size_t playlist_length,
                                size_t *body_size_out)
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
    if (!airplay_plist_encode(root, &body, body_size_out, &error))
        body = NULL;
    airplay_plist_free(root);
    return body;
}

static void test_remote_video(void)
{
    Recorder recorder = {0};
    AirPlayRemoteVideoOps ops = {
        .claim_owner = fake_claim,
        .release_owner = fake_release,
        .control_detached = fake_control_detached,
        .load = fake_load,
        .play = fake_play,
        .pause = fake_pause,
        .stop = fake_stop,
        .seek_ms = fake_seek,
        .snapshot = fake_snapshot,
        .send_reverse_request = fake_send_reverse,
        .control_port = fake_control_port,
        .user_data = &recorder};
    AirPlayRemoteVideo *remote = NULL;
    AirPlayRtspResponse response = {0};
    static const char text_play[] =
        "Content-Location: http://127.0.0.1:8080/hls/master.m3u8\r\n"
        "Start-Position: 0.5\r\n";
    uint8_t *binary_body;
    size_t binary_size = 0u;

    CHECK(airplay_remote_video_url_supported("http://example.com/a.mp4"));
    CHECK(airplay_remote_video_url_supported(
        "HTTPS://example.com/live/master.m3u8?token=x"));
    CHECK(!airplay_remote_video_url_supported("file:///video.mp4"));
    CHECK(!airplay_remote_video_url_supported("http://user@example.com/a"));
    CHECK(!airplay_remote_video_url_supported("http://example.com/a\r\nX: y"));
    CHECK(airplay_remote_video_create(&ops, &remote));

    CHECK(dispatch(remote, 10u, "POST", "/play", text_play,
                   sizeof(text_play) - 1u, "text/parameters", &response));
    CHECK(response.status_code == 200 && recorder.load_count == 1u &&
          recorder.play_count == 1u);
    CHECK(strcmp(recorder.url,
                 "http://127.0.0.1:8080/hls/master.m3u8") == 0);
    airplay_rtsp_response_clear(&response);

    CHECK(dispatch(remote, 10u, "GET", "/playback-info", NULL, 0u, NULL,
                   &response));
    CHECK(response.status_code == 200 && recorder.seek_count == 1u &&
          recorder.last_seek_ms == 300000);
    CHECK(body_contains(&response,
                        "<key>duration</key><real>600.000000</real>") &&
          body_contains(&response, "<key>rate</key><real>1.0</real>") &&
          body_contains(&response,
                        "<key>loadedTimeRanges</key><array><dict>"
                        "<key>start</key><real>300.000000</real>"
                        "<key>duration</key><real>300.000000</real>"));
    airplay_rtsp_response_clear(&response);

    recorder.snapshot.position_ms = 700000;
    CHECK(dispatch(remote, 10u, "GET", "/playback-info", NULL, 0u, NULL,
                   &response));
    CHECK(response.status_code == 200 &&
          body_contains(&response,
                        "<key>loadedTimeRanges</key><array><dict>"
                        "<key>start</key><real>600.000000</real>"
                        "<key>duration</key><real>0.000000</real>"));
    airplay_rtsp_response_clear(&response);
    recorder.snapshot.position_ms = 300000;

    CHECK(dispatch(remote, 10u, "POST", "/rate?value=0.000000", NULL, 0u,
                   NULL, &response));
    CHECK(response.status_code == 200 && recorder.pause_count == 1u);
    airplay_rtsp_response_clear(&response);
    CHECK(dispatch(remote, 10u, "GET", "/playback-info", NULL, 0u, NULL,
                   &response));
    CHECK(response.status_code == 200 &&
          body_contains(&response, "<key>rate</key><real>0.0</real>") &&
          body_contains(&response, "<key>readyToPlay</key><true/>") &&
          body_contains(&response,
                        "<key>playbackBufferEmpty</key><false/>"));
    airplay_rtsp_response_clear(&response);
    CHECK(dispatch(remote, 10u, "POST", "/rate?value=1", NULL, 0u, NULL,
                   &response));
    CHECK(response.status_code == 200 && recorder.play_count == 2u);
    airplay_rtsp_response_clear(&response);

    CHECK(dispatch(remote, 10u, "POST", "/scrub?position=12.5", NULL, 0u,
                   NULL, &response));
    CHECK(response.status_code == 200 && recorder.last_seek_ms == 12500);
    airplay_rtsp_response_clear(&response);
    CHECK(dispatch(remote, 10u, "GET", "/scrub", NULL, 0u, NULL, &response));
    CHECK(response.status_code == 200 &&
          body_contains(&response, "position: 12.500000"));
    airplay_rtsp_response_clear(&response);

    CHECK(dispatch(remote, 11u, "POST", "/play", text_play,
                   sizeof(text_play) - 1u, "text/parameters", &response));
    CHECK(response.status_code == 200 && recorder.load_count == 2u &&
          recorder.stop_count == 1u && recorder.release_count == 1u);
    airplay_rtsp_response_clear(&response);
    airplay_remote_video_session_closed(remote, 10u);
    CHECK(recorder.stop_count == 1u && recorder.release_count == 1u);

    binary_body = binary_play_body(NULL, "MobileSafari", &binary_size);
    CHECK(binary_body != NULL);
    CHECK(dispatch(remote, 11u, "POST", "/play", binary_body, binary_size,
                   "application/x-apple-binary-plist", &response));
    CHECK(response.status_code == 200 && recorder.load_count == 3u &&
          strcmp(recorder.url,
                 "https://media.example/live/master.m3u8") == 0 &&
          strcmp(recorder.metadata, "MobileSafari") == 0);
    airplay_rtsp_response_clear(&response);
    CHECK(dispatch(remote, 11u, "POST", "/rate?value=nan", NULL, 0u, NULL,
                   &response));
    CHECK(response.status_code == 400);
    airplay_rtsp_response_clear(&response);
    CHECK(dispatch(remote, 11u, "POST", "/scrub?position=-1", NULL, 0u,
                   NULL, &response));
    CHECK(response.status_code == 400);
    airplay_rtsp_response_clear(&response);
    airplay_plist_buffer_free(binary_body);

    binary_body = binary_play_body("Example channel", "MobileSafari",
                                   &binary_size);
    CHECK(binary_body != NULL);
    CHECK(dispatch(remote, 11u, "POST", "/play", binary_body, binary_size,
                   "application/x-apple-binary-plist", &response));
    CHECK(response.status_code == 200 && recorder.load_count == 4u &&
          strcmp(recorder.metadata, "Example channel") == 0);
    airplay_rtsp_response_clear(&response);
    airplay_plist_buffer_free(binary_body);

    {
        char oversized_title[AIRPLAY_REMOTE_VIDEO_METADATA_MAX + 2u];

        memset(oversized_title, 'T', sizeof(oversized_title) - 1u);
        oversized_title[sizeof(oversized_title) - 1u] = '\0';
        binary_body = binary_play_body(oversized_title, "MobileSafari",
                                       &binary_size);
        CHECK(binary_body != NULL);
        CHECK(dispatch(remote, 11u, "POST", "/play", binary_body,
                       binary_size, "application/x-apple-binary-plist",
                       &response));
        CHECK(response.status_code == 400 && recorder.load_count == 4u);
        airplay_rtsp_response_clear(&response);
        airplay_plist_buffer_free(binary_body);
    }

    CHECK(dispatch(remote, 11u, "GET", "/playback-info", NULL, 0u, NULL,
                   &response));
    CHECK(recorder.last_seek_ms == 7250);
    airplay_rtsp_response_clear(&response);
    CHECK(dispatch(remote, 11u, "POST", "/stop", NULL, 0u, NULL, &response));
    CHECK(response.status_code == 200 && recorder.stop_count == 2u &&
          recorder.claim_count == 2u && recorder.release_count == 2u);
    airplay_rtsp_response_clear(&response);

    static const char invalid_play[] =
        "Content-Location: ftp://example.com/a\r\n";
    CHECK(dispatch(remote, 12u, "POST", "/play", invalid_play,
                   sizeof(invalid_play) - 1u,
                   "text/parameters", &response));
    CHECK(response.status_code == 400 && recorder.load_count == 4u);
    airplay_rtsp_response_clear(&response);
    CHECK(dispatch(remote, 12u, "POST", "/rate?value=nan", NULL, 0u, NULL,
                   &response));
    CHECK(response.status_code == 409);
    airplay_rtsp_response_clear(&response);
    airplay_remote_video_destroy(remote);
}

static void test_direct_claim_cancel_release(void)
{
    static const char play[] =
        "Content-Location: https://media.example/live/master.m3u8\r\n";
    Recorder recorder = {.cancel_during_claim = true};
    AirPlayRemoteVideoOps ops = {
        .claim_owner = fake_claim,
        .release_owner = fake_release,
        .control_detached = fake_control_detached,
        .load = fake_load,
        .play = fake_play,
        .pause = fake_pause,
        .stop = fake_stop,
        .seek_ms = fake_seek,
        .snapshot = fake_snapshot,
        .user_data = &recorder};
    AirPlayRemoteVideo *remote = NULL;
    AirPlayRtspResponse response = {0};

    CHECK(airplay_remote_video_create(&ops, &remote));
    recorder.remote = remote;
    CHECK(dispatch(remote, 14u, "POST", "/play", play, sizeof(play) - 1u,
                   "text/parameters", &response));
    CHECK(response.status_code == 409);
    CHECK(recorder.claim_count == 1u && recorder.stop_count == 1u &&
          recorder.release_count == 1u && recorder.load_count == 0u &&
          recorder.play_count == 0u);
    airplay_rtsp_response_clear(&response);
    airplay_remote_video_destroy(remote);
}

static void test_direct_replacement_after_transport_close(void)
{
    static const char first[] =
        "Content-Location: https://media.example/first.m3u8\r\n";
    static const char second[] =
        "Content-Location: https://media.example/second.m3u8\r\n";
    static const char third[] =
        "Content-Location: https://media.example/third.m3u8\r\n";
    Recorder recorder = {0};
    AirPlayRemoteVideoOps ops = {
        .claim_owner = fake_claim,
        .release_owner = fake_release,
        .control_detached = fake_control_detached,
        .load = fake_load,
        .play = fake_play,
        .pause = fake_pause,
        .stop = fake_stop,
        .seek_ms = fake_seek,
        .snapshot = fake_snapshot,
        .user_data = &recorder};
    AirPlayRemoteVideo *remote = NULL;
    AirPlayRtspResponse response = {0};

    CHECK(airplay_remote_video_create(&ops, &remote));
    CHECK(dispatch(remote, 10u, "POST", "/play", first, sizeof(first) - 1u,
                   "text/parameters", &response));
    CHECK(response.status_code == 200 && recorder.claim_count == 1u &&
          recorder.load_count == 1u && recorder.play_count == 1u);
    airplay_rtsp_response_clear(&response);

    CHECK(dispatch(remote, 10u, "POST", "/play", second,
                   sizeof(second) - 1u, "text/parameters", &response));
    CHECK(response.status_code == 200 && recorder.claim_count == 1u &&
          recorder.release_count == 0u && recorder.stop_count == 0u &&
          recorder.load_count == 2u && recorder.play_count == 2u);
    CHECK(strcmp(recorder.url, "https://media.example/second.m3u8") == 0);
    airplay_rtsp_response_clear(&response);

    airplay_remote_video_session_closed(remote, 10u);
    CHECK(recorder.control_detached_count == 1u &&
          recorder.stop_count == 0u && recorder.release_count == 0u);
    airplay_remote_video_session_closed(remote, 10u);
    CHECK(recorder.control_detached_count == 1u);
    CHECK(dispatch(remote, 11u, "POST", "/play", third,
                   sizeof(third) - 1u, "text/parameters", &response));
    CHECK(response.status_code == 200 && recorder.claim_count == 2u &&
          recorder.release_count == 1u && recorder.stop_count == 1u &&
          recorder.load_count == 3u && recorder.play_count == 3u);
    CHECK(strcmp(recorder.url, "https://media.example/third.m3u8") == 0);
    airplay_rtsp_response_clear(&response);

    airplay_remote_video_session_closed(remote, 10u);
    CHECK(recorder.stop_count == 1u && recorder.release_count == 1u);
    CHECK(dispatch(remote, 11u, "POST", "/stop", NULL, 0u, NULL, &response));
    CHECK(response.status_code == 200 && recorder.stop_count == 2u &&
          recorder.release_count == 2u);
    airplay_rtsp_response_clear(&response);
    airplay_remote_video_destroy(remote);
}

static void test_active_transport_close_requires_explicit_stop(void)
{
    static const char play[] =
        "Content-Location: https://media.example/live.m3u8\r\n";
    Recorder recorder = {0};
    AirPlayRemoteVideoOps ops = {
        .claim_owner = fake_claim,
        .release_owner = fake_release,
        .control_attached = fake_control_attached,
        .control_detached = fake_control_detached,
        .load = fake_load,
        .play = fake_play,
        .pause = fake_pause,
        .stop = fake_stop,
        .seek_ms = fake_seek,
        .snapshot = fake_snapshot,
        .user_data = &recorder};
    AirPlayRemoteVideo *remote = NULL;
    AirPlayRtspResponse response = {0};

    CHECK(airplay_remote_video_create(&ops, &remote));
    CHECK(dispatch(remote, 10u, "POST", "/play", play, sizeof(play) - 1u,
                   "text/parameters", &response));
    airplay_rtsp_response_clear(&response);
    airplay_remote_video_session_closed(remote, 10u);
    CHECK(recorder.control_detached_count == 1u &&
          recorder.stop_count == 0u && recorder.release_count == 0u);

    CHECK(dispatch(remote, 10u, "GET", "/playback-info", NULL, 0u, NULL,
                   &response));
    CHECK(response.status_code == 200 &&
          recorder.control_attached_count == 1u);
    airplay_rtsp_response_clear(&response);
    CHECK(dispatch(remote, 10u, "POST", "/stop", NULL, 0u, NULL, &response));
    CHECK(recorder.stop_count == 1u && recorder.release_count == 1u);
    airplay_rtsp_response_clear(&response);
    airplay_remote_video_destroy(remote);
}

static void test_terminal_transport_release_stops_matching_session(void)
{
    static const char play[] =
        "Content-Location: https://media.example/terminal.m3u8\r\n";
    Recorder recorder = {0};
    AirPlayRemoteVideoOps ops = {
        .claim_owner = fake_claim,
        .release_owner = fake_release,
        .control_detached = fake_control_detached,
        .load = fake_load,
        .play = fake_play,
        .pause = fake_pause,
        .stop = fake_stop,
        .seek_ms = fake_seek,
        .snapshot = fake_snapshot,
        .user_data = &recorder};
    AirPlayRemoteVideo *remote = NULL;
    AirPlayRtspResponse response = {0};

    CHECK(airplay_remote_video_create(&ops, &remote));
    CHECK(dispatch(remote, 10u, "POST", "/play", play, sizeof(play) - 1u,
                   "text/parameters", &response));
    airplay_rtsp_response_clear(&response);
    airplay_remote_video_session_closed(remote, 10u);
    CHECK(recorder.control_detached_count == 1u && recorder.stop_count == 0u);
    CHECK(airplay_remote_video_terminate_session(remote, 10u));
    CHECK(recorder.stop_count == 1u && recorder.release_count == 1u);
    CHECK(!airplay_remote_video_terminate_session(remote, 10u));
    CHECK(recorder.stop_count == 1u && recorder.release_count == 1u);
    airplay_remote_video_destroy(remote);
}

static void test_external_takeover_allows_same_session_reclaim(void)
{
    static const char first[] =
        "Content-Location: https://media.example/first.m3u8\r\n";
    static const char second[] =
        "Content-Location: https://media.example/second.m3u8\r\n";
    Recorder recorder = {0};
    AirPlayRemoteVideoOps ops = {
        .claim_owner = fake_claim,
        .release_owner = fake_release,
        .load = fake_load,
        .play = fake_play,
        .pause = fake_pause,
        .stop = fake_stop,
        .seek_ms = fake_seek,
        .snapshot = fake_snapshot,
        .user_data = &recorder};
    AirPlayRemoteVideo *remote = NULL;
    AirPlayRtspResponse response = {0};

    CHECK(airplay_remote_video_create(&ops, &remote));
    CHECK(dispatch(remote, 10u, "POST", "/play", first,
                   sizeof(first) - 1u, "text/parameters", &response));
    CHECK(response.status_code == 200 && recorder.claim_count == 1u);
    airplay_rtsp_response_clear(&response);

    CHECK(!airplay_remote_video_relinquish_session(remote, 11u));
    CHECK(airplay_remote_video_relinquish_session(remote, 10u));
    CHECK(recorder.stop_count == 0u && recorder.release_count == 0u);
    CHECK(!airplay_remote_video_relinquish_session(remote, 10u));

    CHECK(dispatch(remote, 10u, "POST", "/play", second,
                   sizeof(second) - 1u, "text/parameters", &response));
    CHECK(response.status_code == 200 && recorder.claim_count == 2u &&
          recorder.load_count == 2u && recorder.play_count == 2u);
    CHECK(strcmp(recorder.url, "https://media.example/second.m3u8") == 0);
    airplay_rtsp_response_clear(&response);
    CHECK(dispatch(remote, 10u, "POST", "/stop", NULL, 0u, NULL,
                   &response));
    CHECK(response.status_code == 200 && recorder.stop_count == 1u &&
          recorder.release_count == 1u);
    airplay_rtsp_response_clear(&response);

    airplay_remote_video_destroy(remote);
}

static void test_direct_load_failure_allows_new_url(void)
{
    static const char failed[] =
        "Content-Location: https://media.example/failed.m3u8\r\n";
    static const char replacement[] =
        "Content-Location: https://media.example/replacement.m3u8\r\n";
    Recorder recorder = {.fail_load = true};
    AirPlayRemoteVideoOps ops = {
        .claim_owner = fake_claim,
        .release_owner = fake_release,
        .load = fake_load,
        .play = fake_play,
        .pause = fake_pause,
        .stop = fake_stop,
        .seek_ms = fake_seek,
        .snapshot = fake_snapshot,
        .user_data = &recorder};
    AirPlayRemoteVideo *remote = NULL;
    AirPlayRtspResponse response = {0};

    CHECK(airplay_remote_video_create(&ops, &remote));
    CHECK(dispatch(remote, 10u, "POST", "/play", failed,
                   sizeof(failed) - 1u, "text/parameters", &response));
    CHECK(response.status_code == 503 && recorder.claim_count == 1u &&
          recorder.load_count == 1u && recorder.play_count == 0u &&
          recorder.stop_count == 1u && recorder.release_count == 1u);
    airplay_rtsp_response_clear(&response);

    recorder.fail_load = false;
    CHECK(dispatch(remote, 10u, "POST", "/play", replacement,
                   sizeof(replacement) - 1u, "text/parameters", &response));
    CHECK(response.status_code == 200 && recorder.claim_count == 2u &&
          recorder.load_count == 2u && recorder.play_count == 1u);
    CHECK(strcmp(recorder.url, "https://media.example/replacement.m3u8") == 0);
    airplay_rtsp_response_clear(&response);
    CHECK(dispatch(remote, 10u, "POST", "/stop", NULL, 0u, NULL, &response));
    CHECK(response.status_code == 200 && recorder.stop_count == 2u &&
          recorder.release_count == 2u);
    airplay_rtsp_response_clear(&response);
    airplay_remote_video_destroy(remote);
}

static void test_reverse_hls_deferral(void)
{
    static const char locator[] =
        "airplay://phone/library/master.m3u8";
    static const char media_url[] =
        "https://cdn.example/live/video/program.m3u8";
    static const char master[] =
        "#EXTM3U\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=1200000\n"
        "https://cdn.example/live/video/program.m3u8\n";
    static const char media[] =
        "#EXTM3U\n"
        "#EXTINF:4.0,\n"
        "segments/video-1.m4s\n";
    Recorder recorder = {0};
    AirPlayRemoteVideoOps ops = {
        .claim_owner = fake_claim,
        .release_owner = fake_release,
        .load = fake_load,
        .play = fake_play,
        .pause = fake_pause,
        .stop = fake_stop,
        .seek_ms = fake_seek,
        .snapshot = fake_snapshot,
        .send_reverse_request = fake_send_reverse,
        .control_port = fake_control_port,
        .user_data = &recorder};
    AirPlayRemoteVideo *remote = NULL;
    AirPlayRtspResponse response = {0};
    uint8_t *play_body;
    uint8_t *action_body;
    size_t play_size;
    size_t action_size;

    CHECK(airplay_remote_video_create(&ops, &remote));
    play_body = reverse_play_body(&play_size);
    CHECK(play_body != NULL);
    CHECK(dispatch(remote, 13u, "POST", "/play", play_body, play_size,
                   "application/x-apple-binary-plist", &response));
    airplay_plist_buffer_free(play_body);
    CHECK(response.status_code == 200);
    CHECK(recorder.claim_count == 0u && recorder.load_count == 0u &&
          recorder.play_count == 0u &&
          recorder.reverse_send_count == 1u);
    airplay_rtsp_response_clear(&response);

    CHECK(dispatch(remote, 13u, "POST", "/rate?value=0", NULL, 0u, NULL,
                   &response));
    CHECK(response.status_code == 200 && recorder.pause_count == 0u);
    airplay_rtsp_response_clear(&response);
    CHECK(dispatch(remote, 13u, "POST", "/scrub?position=12.5", NULL, 0u,
                   NULL, &response));
    CHECK(response.status_code == 200 && recorder.seek_count == 0u);
    airplay_rtsp_response_clear(&response);
    CHECK(dispatch(remote, 13u, "GET", "/playback-info", NULL, 0u, NULL,
                   &response));
    CHECK(response.status_code == 200 &&
          body_contains(&response, "<key>readyToPlay</key><false/>"));
    airplay_rtsp_response_clear(&response);

    action_body = hls_action_body(1u, locator, master,
                                  sizeof(master) - 1u, &action_size);
    CHECK(action_body != NULL);
    CHECK(dispatch(remote, 13u, "POST", "/action", action_body, action_size,
                   "application/x-apple-binary-plist", &response));
    airplay_plist_buffer_free(action_body);
    CHECK(response.status_code == 200 && recorder.claim_count == 0u &&
          recorder.reverse_send_count == 2u);
    airplay_rtsp_response_clear(&response);

    action_body = hls_action_body(2u, media_url, media,
                                  sizeof(media) - 1u, &action_size);
    CHECK(action_body != NULL);
    CHECK(dispatch(remote, 13u, "POST", "/action", action_body, action_size,
                   "application/x-apple-binary-plist", &response));
    airplay_plist_buffer_free(action_body);
    CHECK(response.status_code == 200 && recorder.claim_count == 1u &&
          recorder.load_count == 1u && recorder.play_count == 1u);
    CHECK(strstr(recorder.url, "/airplay-hls/") != NULL);
    airplay_rtsp_response_clear(&response);
    CHECK(dispatch(remote, 13u, "GET", "/playback-info", NULL, 0u, NULL,
                   &response));
    CHECK(response.status_code == 200 && recorder.seek_count == 1u &&
          recorder.last_seek_ms == 12500);
    airplay_rtsp_response_clear(&response);

    CHECK(dispatch(remote, 13u, "POST", "/stop", NULL, 0u, NULL,
                   &response));
    CHECK(response.status_code == 200 && recorder.stop_count == 1u &&
          recorder.release_count == 1u);
    airplay_rtsp_response_clear(&response);

    play_body = reverse_play_body(&play_size);
    CHECK(play_body != NULL);
    CHECK(dispatch(remote, 14u, "POST", "/play", play_body, play_size,
                   "application/x-apple-binary-plist", &response));
    airplay_plist_buffer_free(play_body);
    CHECK(response.status_code == 200 && recorder.reverse_send_count == 3u);
    airplay_rtsp_response_clear(&response);

    action_body = hls_action_body(1u, locator, master,
                                  sizeof(master) - 1u, &action_size);
    CHECK(action_body != NULL);
    CHECK(dispatch(remote, 14u, "POST", "/action", action_body, action_size,
                   "application/x-apple-binary-plist", &response));
    airplay_plist_buffer_free(action_body);
    CHECK(response.status_code == 200 && recorder.reverse_send_count == 4u);
    airplay_rtsp_response_clear(&response);

    action_body = hls_action_body(2u, media_url, media,
                                  sizeof(media) - 1u, &action_size);
    CHECK(action_body != NULL);
    CHECK(dispatch(remote, 14u, "POST", "/action", action_body, action_size,
                   "application/x-apple-binary-plist", &response));
    airplay_plist_buffer_free(action_body);
    CHECK(response.status_code == 200 && recorder.claim_count == 2u &&
          recorder.load_count == 2u && recorder.play_count == 2u);
    airplay_rtsp_response_clear(&response);
    CHECK(dispatch(remote, 14u, "POST", "/stop", NULL, 0u, NULL,
                   &response));
    CHECK(response.status_code == 200 && recorder.stop_count == 2u &&
          recorder.release_count == 2u);
    airplay_rtsp_response_clear(&response);
    airplay_remote_video_destroy(remote);
}

static void test_reverse_hls_load_failure_release(void)
{
    static const char locator[] =
        "airplay://phone/library/master.m3u8";
    static const char media_url[] =
        "https://cdn.example/live/video/program.m3u8";
    static const char master[] =
        "#EXTM3U\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=1200000\n"
        "https://cdn.example/live/video/program.m3u8\n";
    static const char media[] =
        "#EXTM3U\n"
        "#EXTINF:4.0,\n"
        "segments/video-1.m4s\n";
    Recorder recorder = {0};
    AirPlayRemoteVideoOps ops = {
        .claim_owner = fake_claim,
        .release_owner = fake_release,
        .load = fake_load,
        .play = fake_play,
        .pause = fake_pause,
        .stop = fake_stop,
        .seek_ms = fake_seek,
        .snapshot = fake_snapshot,
        .send_reverse_request = fake_send_reverse,
        .control_port = fake_control_port,
        .user_data = &recorder};
    AirPlayRemoteVideo *remote = NULL;
    AirPlayRtspResponse response = {0};
    uint8_t *body;
    size_t body_size;

    CHECK(airplay_remote_video_create(&ops, &remote));
    body = reverse_play_body(&body_size);
    CHECK(body != NULL);
    CHECK(dispatch(remote, 13u, "POST", "/play", body, body_size,
                   "application/x-apple-binary-plist", &response));
    airplay_plist_buffer_free(body);
    CHECK(response.status_code == 200);
    airplay_rtsp_response_clear(&response);

    body = hls_action_body(1u, locator, master, sizeof(master) - 1u,
                           &body_size);
    CHECK(body != NULL);
    CHECK(dispatch(remote, 13u, "POST", "/action", body, body_size,
                   "application/x-apple-binary-plist", &response));
    airplay_plist_buffer_free(body);
    CHECK(response.status_code == 200);
    airplay_rtsp_response_clear(&response);

    recorder.fail_load = true;
    body = hls_action_body(2u, media_url, media, sizeof(media) - 1u,
                           &body_size);
    CHECK(body != NULL);
    CHECK(dispatch(remote, 13u, "POST", "/action", body, body_size,
                   "application/x-apple-binary-plist", &response));
    airplay_plist_buffer_free(body);
    CHECK(response.status_code == 503);
    CHECK(recorder.claim_count == 1u && recorder.load_count == 1u &&
          recorder.play_count == 0u && recorder.stop_count == 1u &&
          recorder.release_count == 1u);
    airplay_rtsp_response_clear(&response);
    airplay_remote_video_destroy(remote);
}

static void test_reverse_hls_close_clears_pending(void)
{
    static const char direct[] =
        "Content-Location: https://media.example/live/master.m3u8\r\n";
    Recorder recorder = {0};
    AirPlayRemoteVideoOps ops = {
        .claim_owner = fake_claim,
        .release_owner = fake_release,
        .load = fake_load,
        .play = fake_play,
        .pause = fake_pause,
        .stop = fake_stop,
        .seek_ms = fake_seek,
        .snapshot = fake_snapshot,
        .send_reverse_request = fake_send_reverse,
        .control_port = fake_control_port,
        .user_data = &recorder};
    AirPlayRemoteVideo *remote = NULL;
    AirPlayRtspResponse response = {0};
    uint8_t *body;
    size_t body_size;

    CHECK(airplay_remote_video_create(&ops, &remote));
    body = reverse_play_body(&body_size);
    CHECK(body != NULL);
    CHECK(dispatch(remote, 13u, "POST", "/play", body, body_size,
                   "application/x-apple-binary-plist", &response));
    airplay_plist_buffer_free(body);
    CHECK(response.status_code == 200);
    airplay_rtsp_response_clear(&response);

    airplay_remote_video_session_closed(remote, 13u);
    CHECK(recorder.claim_count == 0u && recorder.load_count == 0u &&
          recorder.play_count == 0u && recorder.stop_count == 0u &&
          recorder.release_count == 0u);
    CHECK(dispatch(remote, 14u, "POST", "/play", direct,
                   sizeof(direct) - 1u, "text/parameters", &response));
    CHECK(response.status_code == 200 && recorder.claim_count == 1u &&
          recorder.load_count == 1u && recorder.play_count == 1u);
    airplay_rtsp_response_clear(&response);
    CHECK(dispatch(remote, 14u, "POST", "/stop", NULL, 0u, NULL, &response));
    CHECK(response.status_code == 200 && recorder.stop_count == 1u &&
          recorder.release_count == 1u);
    airplay_rtsp_response_clear(&response);
    airplay_remote_video_destroy(remote);
}

static void test_reverse_hls_requires_claim(void)
{
    static const char locator[] =
        "airplay://phone/library/master.m3u8";
    static const char media_url[] =
        "https://cdn.example/live/video/program.m3u8";
    static const char master[] =
        "#EXTM3U\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=1200000\n"
        "https://cdn.example/live/video/program.m3u8\n";
    static const char media[] =
        "#EXTM3U\n"
        "#EXTINF:4.0,\n"
        "segments/video-1.m4s\n";
    Recorder recorder = {0};
    AirPlayRemoteVideoOps ops = {
        .load = fake_load,
        .play = fake_play,
        .pause = fake_pause,
        .stop = fake_stop,
        .seek_ms = fake_seek,
        .snapshot = fake_snapshot,
        .send_reverse_request = fake_send_reverse,
        .control_port = fake_control_port,
        .user_data = &recorder};
    AirPlayRemoteVideo *remote = NULL;
    AirPlayRtspResponse response = {0};
    uint8_t *body;
    size_t body_size;

    CHECK(airplay_remote_video_create(&ops, &remote));
    body = reverse_play_body(&body_size);
    CHECK(body != NULL);
    CHECK(dispatch(remote, 13u, "POST", "/play", body, body_size,
                   "application/x-apple-binary-plist", &response));
    airplay_plist_buffer_free(body);
    CHECK(response.status_code == 200);
    airplay_rtsp_response_clear(&response);

    body = hls_action_body(1u, locator, master, sizeof(master) - 1u,
                           &body_size);
    CHECK(body != NULL);
    CHECK(dispatch(remote, 13u, "POST", "/action", body, body_size,
                   "application/x-apple-binary-plist", &response));
    airplay_plist_buffer_free(body);
    CHECK(response.status_code == 200);
    airplay_rtsp_response_clear(&response);

    body = hls_action_body(2u, media_url, media, sizeof(media) - 1u,
                           &body_size);
    CHECK(body != NULL);
    CHECK(dispatch(remote, 13u, "POST", "/action", body, body_size,
                   "application/x-apple-binary-plist", &response));
    airplay_plist_buffer_free(body);
    CHECK(response.status_code == 503);
    CHECK(recorder.claim_count == 0u && recorder.load_count == 0u &&
          recorder.stop_count == 0u && recorder.release_count == 0u);
    airplay_rtsp_response_clear(&response);
    airplay_remote_video_destroy(remote);
}

int main(void)
{
    test_remote_video();
    test_direct_claim_cancel_release();
    test_direct_replacement_after_transport_close();
    test_active_transport_close_requires_explicit_stop();
    test_terminal_transport_release_stops_matching_session();
    test_external_takeover_allows_same_session_reclaim();
    test_direct_load_failure_allows_new_url();
    test_reverse_hls_deferral();
    test_reverse_hls_load_failure_release();
    test_reverse_hls_close_clears_pending();
    test_reverse_hls_requires_claim();
    if (g_failures)
    {
        fprintf(stderr, "%d AirPlay remote video checks failed\n", g_failures);
        return 1;
    }
    puts("AirPlay remote video checks passed");
    return 0;
}
