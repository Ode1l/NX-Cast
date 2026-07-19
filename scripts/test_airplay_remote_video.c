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
    int last_seek_ms;
} Recorder;

static bool fake_claim(uint64_t session_id, void *user_data)
{
    Recorder *recorder = user_data;

    CHECK(session_id == 10u || session_id == 11u);
    recorder->claim_count++;
    return true;
}

static void fake_release(uint64_t session_id, void *user_data)
{
    Recorder *recorder = user_data;

    CHECK(session_id == 10u || session_id == 11u);
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
    return true;
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
        request.header_count = 1u;
    }
    CHECK(airplay_rtsp_response_init(response, "HTTP/1.1", 200));
    return airplay_remote_video_route(remote, session_id, &request, response,
                                      &handled) &&
           handled;
}

static uint8_t *binary_play_body(size_t *size_out)
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
        !airplay_plist_dict_set(root, "clientProcName",
                                airplay_plist_new_string("MobileSafari")) ||
        !airplay_plist_encode(root, &body, size_out, &error))
    {
        airplay_plist_buffer_free(body);
        body = NULL;
    }
    airplay_plist_free(root);
    return body;
}

static void test_remote_video(void)
{
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
          body_contains(&response, "<key>rate</key><real>1.0</real>"));
    airplay_rtsp_response_clear(&response);

    CHECK(dispatch(remote, 10u, "POST", "/rate?value=0.000000", NULL, 0u,
                   NULL, &response));
    CHECK(response.status_code == 200 && recorder.pause_count == 1u);
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
    CHECK(response.status_code == 409 && recorder.load_count == 1u);
    airplay_rtsp_response_clear(&response);
    airplay_remote_video_session_closed(remote, 10u);
    CHECK(recorder.stop_count == 1u && recorder.release_count == 1u);

    binary_body = binary_play_body(&binary_size);
    CHECK(binary_body != NULL);
    CHECK(dispatch(remote, 11u, "POST", "/play", binary_body, binary_size,
                   "application/x-apple-binary-plist", &response));
    CHECK(response.status_code == 200 && recorder.load_count == 2u &&
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
    CHECK(response.status_code == 400 && recorder.load_count == 2u);
    airplay_rtsp_response_clear(&response);
    CHECK(dispatch(remote, 12u, "POST", "/rate?value=nan", NULL, 0u, NULL,
                   &response));
    CHECK(response.status_code == 409);
    airplay_rtsp_response_clear(&response);
    airplay_remote_video_destroy(remote);
}

int main(void)
{
    test_remote_video();
    if (g_failures)
    {
        fprintf(stderr, "%d AirPlay remote video checks failed\n", g_failures);
        return 1;
    }
    puts("AirPlay remote video checks passed");
    return 0;
}
