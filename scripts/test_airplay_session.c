#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "protocol/airplay/protocol/logical_session.h"

static AirPlayRtspRequest make_request(const char *method,
                                       const char *uri,
                                       const char *protocol,
                                       bool has_cseq,
                                       const char *apple_session_id)
{
    AirPlayRtspRequest request;

    memset(&request, 0, sizeof(request));
    snprintf(request.method, sizeof(request.method), "%s", method);
    snprintf(request.uri, sizeof(request.uri), "%s", uri);
    snprintf(request.protocol, sizeof(request.protocol), "%s", protocol);
    request.has_cseq = has_cseq;
    request.cseq = has_cseq ? 1U : 0U;
    if (apple_session_id)
    {
        snprintf(request.headers[0].name,
                 sizeof(request.headers[0].name),
                 "%s",
                 "X-Apple-Session-ID");
        snprintf(request.headers[0].value,
                 sizeof(request.headers[0].value),
                 "%s",
                 apple_session_id);
        request.header_count = 1U;
    }
    return request;
}

static void test_connection_classification(void)
{
    AirPlaySessionManager *manager = airplay_session_manager_create();
    AirPlaySessionSnapshot snapshot;
    AirPlayRtspRequest request;

    assert(manager);
    request = make_request("OPTIONS", "*", "RTSP/1.0", true, NULL);
    assert(airplay_session_manager_observe(manager, 1U, &request, &snapshot) ==
           AIRPLAY_SESSION_OBSERVE_OK);
    assert(snapshot.connection_kind == AIRPLAY_CONNECTION_RAOP);
    assert(snapshot.logical_session_id == 1U);
    assert(!snapshot.logical_session_bound);

    request = make_request("POST", "/pair-verify", "HTTP/1.1", false, NULL);
    assert(airplay_session_manager_observe(manager, 2U, &request, &snapshot) ==
           AIRPLAY_SESSION_OBSERVE_OK);
    assert(snapshot.connection_kind == AIRPLAY_CONNECTION_AIRPLAY);
    assert(!snapshot.logical_session_bound);

    request = make_request("GET", "/info?txtAirPlay&txtRAOP", "RTSP/1.0", false, NULL);
    assert(airplay_session_manager_observe(manager, 3U, &request, &snapshot) ==
           AIRPLAY_SESSION_OBSERVE_OK);
    assert(snapshot.connection_kind == AIRPLAY_CONNECTION_BLE);
    assert(strcmp(airplay_connection_kind_name(snapshot.connection_kind), "ble") == 0);
    airplay_session_manager_destroy(manager);
}

static void test_remote_video_requires_binding(void)
{
    AirPlaySessionManager *manager = airplay_session_manager_create();
    AirPlaySessionSnapshot snapshot;
    AirPlayRtspRequest request;

    assert(manager);
    request = make_request("POST", "/pair-verify", "HTTP/1.1", false, NULL);
    assert(airplay_session_manager_observe(manager, 10U, &request, &snapshot) ==
           AIRPLAY_SESSION_OBSERVE_OK);
    request = make_request("POST", "/play", "HTTP/1.1", false, NULL);
    assert(airplay_session_request_is_remote_video(&request));
    assert(airplay_session_manager_observe(manager, 10U, &request, &snapshot) ==
           AIRPLAY_SESSION_OBSERVE_UNBOUND_MEDIA);
    request = make_request("POST", "/play", "HTTP/1.1", false, "apple-session-a");
    assert(airplay_session_manager_observe(manager, 10U, &request, &snapshot) ==
           AIRPLAY_SESSION_OBSERVE_OK);
    assert(snapshot.logical_session_bound);
    assert(snapshot.logical_session_id != snapshot.connection_id);
    assert(snapshot.logical_connection_count == 1U);
    airplay_session_manager_destroy(manager);
}

static void test_shared_session_lifetime(void)
{
    AirPlaySessionManager *manager = airplay_session_manager_create();
    AirPlaySessionSnapshot first;
    AirPlaySessionSnapshot second;
    AirPlaySessionCloseResult close_result;
    AirPlayRtspRequest request;

    assert(manager);
    request = make_request("POST", "/play", "HTTP/1.1", false, "shared-session");
    assert(airplay_session_manager_observe(manager, 20U, &request, &first) ==
           AIRPLAY_SESSION_OBSERVE_OK);
    request = make_request("GET", "/playback-info", "HTTP/1.1", false, "shared-session");
    assert(airplay_session_manager_observe(manager, 21U, &request, &second) ==
           AIRPLAY_SESSION_OBSERVE_OK);
    assert(first.logical_session_id == second.logical_session_id);
    assert(second.logical_connection_count == 2U);

    assert(airplay_session_manager_close(manager, 20U, &close_result));
    assert(close_result.logical_session_bound);
    assert(!close_result.last_logical_connection);
    assert(airplay_session_manager_close(manager, 21U, &close_result));
    assert(close_result.logical_session_bound);
    assert(close_result.last_logical_connection);
    assert(close_result.logical_session_id == first.logical_session_id);
    assert(!airplay_session_manager_close(manager, 21U, &close_result));
    airplay_session_manager_destroy(manager);
}

static void test_reverse_shares_remote_video_identity(void)
{
    AirPlaySessionManager *manager = airplay_session_manager_create();
    AirPlaySessionSnapshot reverse;
    AirPlaySessionSnapshot play;
    AirPlaySessionCloseResult close_result;
    AirPlayRtspRequest request;

    assert(manager);
    request = make_request("POST", "/reverse", "HTTP/1.1", false, NULL);
    assert(airplay_session_request_requires_logical_binding(&request));
    assert(!airplay_session_request_is_remote_video(&request));
    assert(airplay_session_manager_observe(manager, 25U, &request, &reverse) ==
           AIRPLAY_SESSION_OBSERVE_UNBOUND_MEDIA);

    request = make_request("POST", "/reverse", "HTTP/1.1", false,
                           "reverse-session");
    assert(airplay_session_manager_observe(manager, 25U, &request, &reverse) ==
           AIRPLAY_SESSION_OBSERVE_OK);
    request = make_request("POST", "/play", "HTTP/1.1", false,
                           "reverse-session");
    assert(airplay_session_manager_observe(manager, 26U, &request, &play) ==
           AIRPLAY_SESSION_OBSERVE_OK);
    assert(reverse.logical_session_id == play.logical_session_id);
    assert(play.logical_connection_count == 2U);
    assert(airplay_session_manager_close(manager, 25U, &close_result));
    assert(!close_result.last_logical_connection);
    assert(airplay_session_manager_close(manager, 26U, &close_result));
    assert(close_result.last_logical_connection);
    airplay_session_manager_destroy(manager);
}

static void test_conflicts_are_rejected(void)
{
    AirPlaySessionManager *manager = airplay_session_manager_create();
    AirPlaySessionSnapshot snapshot;
    AirPlayRtspRequest request;

    assert(manager);
    request = make_request("POST", "/play", "HTTP/1.1", false, "stable-session");
    assert(airplay_session_manager_observe(manager, 30U, &request, &snapshot) ==
           AIRPLAY_SESSION_OBSERVE_OK);
    request = make_request("GET", "/playback-info", "HTTP/1.1", false, "changed-session");
    assert(airplay_session_manager_observe(manager, 30U, &request, &snapshot) ==
           AIRPLAY_SESSION_OBSERVE_CONFLICT);

    request = make_request("OPTIONS", "*", "RTSP/1.0", true, NULL);
    assert(airplay_session_manager_observe(manager, 31U, &request, &snapshot) ==
           AIRPLAY_SESSION_OBSERVE_OK);
    request = make_request("POST", "/play", "HTTP/1.1", false, "mixed-kind");
    assert(airplay_session_manager_observe(manager, 31U, &request, &snapshot) ==
           AIRPLAY_SESSION_OBSERVE_CONFLICT);

    request = make_request("POST", "/play", "RTSP/1.0", true, "mixed-header");
    assert(airplay_session_manager_observe(manager, 32U, &request, &snapshot) ==
           AIRPLAY_SESSION_OBSERVE_CONFLICT);
    airplay_session_manager_destroy(manager);
}

static void test_invalid_ids_and_capacity(void)
{
    AirPlaySessionManager *manager = airplay_session_manager_create();
    AirPlaySessionSnapshot snapshot;
    AirPlayRtspRequest request;
    char long_id[AIRPLAY_SESSION_APPLE_ID_MAX + 2U];
    uint64_t index;

    assert(manager);
    memset(long_id, 'a', sizeof(long_id) - 1U);
    long_id[sizeof(long_id) - 1U] = '\0';
    request = make_request("POST", "/play", "HTTP/1.1", false, long_id);
    assert(airplay_session_manager_observe(manager, 40U, &request, &snapshot) ==
           AIRPLAY_SESSION_OBSERVE_INVALID_SESSION_ID);
    request = make_request("POST", "/play", "HTTP/1.1", false, "bad session");
    assert(airplay_session_manager_observe(manager, 40U, &request, &snapshot) ==
           AIRPLAY_SESSION_OBSERVE_INVALID_SESSION_ID);

    request = make_request("OPTIONS", "*", "RTSP/1.0", true, NULL);
    for (index = 0U; index < AIRPLAY_SESSION_MAX_CONNECTIONS; ++index)
    {
        assert(airplay_session_manager_observe(manager, 100U + index, &request, &snapshot) ==
               AIRPLAY_SESSION_OBSERVE_OK);
    }
    assert(airplay_session_manager_observe(manager, 999U, &request, &snapshot) ==
           AIRPLAY_SESSION_OBSERVE_CAPACITY);
    assert(strcmp(airplay_session_observe_result_name(AIRPLAY_SESSION_OBSERVE_CAPACITY),
                  "capacity") == 0);
    airplay_session_manager_destroy(manager);
}

static void test_reconnect_releases_capacity(void)
{
    AirPlaySessionManager *manager = airplay_session_manager_create();
    AirPlaySessionSnapshot snapshot;
    AirPlaySessionCloseResult close_result;
    AirPlayRtspRequest request;
    uint64_t previous_logical_id = 0u;

    assert(manager);
    for (uint64_t index = 0u; index < 64u; ++index)
    {
        request = make_request("POST", "/reverse", "HTTP/1.1", false,
                               "reconnect-session");
        assert(airplay_session_manager_observe(
                   manager, 1000u + index * 2u, &request, &snapshot) ==
               AIRPLAY_SESSION_OBSERVE_OK);
        assert(snapshot.logical_session_id != previous_logical_id);
        previous_logical_id = snapshot.logical_session_id;
        request = make_request("POST", "/play", "HTTP/1.1", false,
                               "reconnect-session");
        assert(airplay_session_manager_observe(
                   manager, 1001u + index * 2u, &request, &snapshot) ==
               AIRPLAY_SESSION_OBSERVE_OK);
        assert(airplay_session_manager_close(
            manager, 1000u + index * 2u, &close_result));
        assert(!close_result.last_logical_connection);
        assert(airplay_session_manager_close(
            manager, 1001u + index * 2u, &close_result));
        assert(close_result.last_logical_connection);
    }
    airplay_session_manager_destroy(manager);
}

static void test_active_media_retains_detached_session(void)
{
    AirPlaySessionManager *manager = airplay_session_manager_create();
    AirPlaySessionSnapshot first;
    AirPlaySessionSnapshot reconnected;
    AirPlaySessionSnapshot after_release;
    AirPlaySessionCloseResult close_result;
    AirPlayRtspRequest request;

    assert(manager);
    request = make_request("POST", "/play", "HTTP/1.1", false,
                           "retained-session");
    assert(airplay_session_manager_observe(manager, 150U, &request, &first) ==
           AIRPLAY_SESSION_OBSERVE_OK);
    assert(airplay_session_manager_retain_media(
        manager, first.logical_session_id));
    assert(airplay_session_manager_retain_media(
        manager, first.logical_session_id));
    assert(airplay_session_manager_close(manager, 150U, &close_result));
    assert(close_result.last_logical_connection);
    assert(airplay_session_manager_release_media(
        manager, first.logical_session_id));

    request = make_request("GET", "/playback-info", "HTTP/1.1", false,
                           "retained-session");
    assert(airplay_session_manager_observe(
               manager, 151U, &request, &reconnected) ==
           AIRPLAY_SESSION_OBSERVE_OK);
    assert(reconnected.logical_session_id == first.logical_session_id);
    assert(reconnected.logical_connection_count == 1U);
    assert(airplay_session_manager_close(manager, 151U, &close_result));
    assert(close_result.last_logical_connection);
    assert(airplay_session_manager_release_media(
        manager, first.logical_session_id));
    assert(!airplay_session_manager_release_media(
        manager, first.logical_session_id));

    request = make_request("POST", "/play", "HTTP/1.1", false,
                           "retained-session");
    assert(airplay_session_manager_observe(
               manager, 152U, &request, &after_release) ==
           AIRPLAY_SESSION_OBSERVE_OK);
    assert(after_release.logical_session_id != first.logical_session_id);
    assert(!airplay_session_manager_retain_media(manager, 999999U));
    assert(!airplay_session_manager_release_media(manager, 999999U));
    airplay_session_manager_destroy(manager);
}

static void test_raop_is_independent_from_apple_session(void)
{
    AirPlaySessionManager *manager = airplay_session_manager_create();
    AirPlaySessionSnapshot raop;
    AirPlaySessionSnapshot play;
    AirPlaySessionSnapshot reverse;
    AirPlaySessionSnapshot other;
    AirPlaySessionCloseResult close_result;
    AirPlayRtspRequest request;
    assert(manager);
    request = make_request("OPTIONS", "*", "RTSP/1.0", true, NULL);
    assert(airplay_session_manager_observe(
               manager, 200U, &request, &raop) ==
           AIRPLAY_SESSION_OBSERVE_OK);
    assert(!raop.logical_session_bound);

    request = make_request("POST", "/play", "HTTP/1.1", false,
                           "peer-session");
    assert(airplay_session_manager_observe(
               manager, 201U, &request, &play) ==
           AIRPLAY_SESSION_OBSERVE_OK);
    request = make_request("POST", "/reverse", "HTTP/1.1", false,
                           "peer-session");
    assert(airplay_session_manager_observe(
               manager, 202U, &request, &reverse) ==
           AIRPLAY_SESSION_OBSERVE_OK);
    assert(play.logical_session_id == reverse.logical_session_id);

    request = make_request("POST", "/feedback", "RTSP/1.0", true, NULL);
    assert(airplay_session_manager_observe(
               manager, 200U, &request, &raop) ==
           AIRPLAY_SESSION_OBSERVE_OK);
    assert(!raop.logical_session_bound);
    assert(raop.logical_session_id == 200U);
    assert(play.logical_connection_count == 1U);
    assert(reverse.logical_connection_count == 2U);

    request = make_request("OPTIONS", "*", "RTSP/1.0", true, NULL);
    assert(airplay_session_manager_observe(
               manager, 203U, &request, &other) ==
           AIRPLAY_SESSION_OBSERVE_OK);
    assert(!other.logical_session_bound);

    assert(airplay_session_manager_close(manager, 200U, &close_result));
    assert(!close_result.logical_session_bound);
    assert(!close_result.last_logical_connection);
    assert(airplay_session_manager_close(manager, 201U, &close_result));
    assert(!close_result.last_logical_connection);
    assert(airplay_session_manager_close(manager, 202U, &close_result));
    assert(close_result.last_logical_connection);
    assert(airplay_session_manager_close(manager, 203U, &close_result));
    airplay_session_manager_destroy(manager);
}

static void test_transport_teardown_waits_for_final_control_close(void)
{
    AirPlaySessionManager *manager = airplay_session_manager_create();
    AirPlaySessionSnapshot raop;
    AirPlaySessionSnapshot play;
    AirPlaySessionSnapshot reverse;
    AirPlaySessionCloseResult close_result;
    AirPlayRtspRequest request;
    uint8_t client_id[AIRPLAY_SESSION_CLIENT_ID_SIZE] = {1U};
    uint64_t terminal_media = 0U;

    assert(manager);
    request = make_request("OPTIONS", "*", "RTSP/1.0", true, NULL);
    assert(airplay_session_manager_observe(manager, 300U, &request, &raop) ==
           AIRPLAY_SESSION_OBSERVE_OK);
    assert(airplay_session_manager_bind_client(manager, 300U, client_id));

    request = make_request("POST", "/play", "HTTP/1.1", false,
                           "terminal-after-close");
    assert(airplay_session_manager_observe(manager, 301U, &request, &play) ==
           AIRPLAY_SESSION_OBSERVE_OK);
    assert(airplay_session_manager_bind_client(manager, 301U, client_id));
    request = make_request("POST", "/reverse", "HTTP/1.1", false,
                           "terminal-after-close");
    assert(airplay_session_manager_observe(manager, 302U, &request, &reverse) ==
           AIRPLAY_SESSION_OBSERVE_OK);
    assert(airplay_session_manager_bind_client(manager, 302U, client_id));
    assert(airplay_session_manager_retain_media(manager,
                                                play.logical_session_id));

    assert(airplay_session_manager_mark_transport_teardown(
        manager, 300U, &terminal_media));
    assert(terminal_media == 0U);
    assert(airplay_session_manager_close(manager, 301U, &close_result));
    assert(close_result.terminal_media_session_id == 0U);
    assert(airplay_session_manager_close(manager, 302U, &close_result));
    assert(close_result.terminal_media_session_id == play.logical_session_id);
    assert(airplay_session_manager_release_media(manager,
                                                 play.logical_session_id));
    assert(airplay_session_manager_close(manager, 300U, &close_result));
    airplay_session_manager_destroy(manager);
}

static void test_final_control_close_waits_for_transport_teardown(void)
{
    AirPlaySessionManager *manager = airplay_session_manager_create();
    AirPlaySessionSnapshot snapshot;
    AirPlaySessionCloseResult close_result;
    AirPlayRtspRequest request;
    uint8_t client_id[AIRPLAY_SESSION_CLIENT_ID_SIZE] = {2U};
    uint64_t logical_id;
    uint64_t terminal_media = 0U;

    assert(manager);
    request = make_request("OPTIONS", "*", "RTSP/1.0", true, NULL);
    assert(airplay_session_manager_observe(manager, 310U, &request, &snapshot) ==
           AIRPLAY_SESSION_OBSERVE_OK);
    assert(airplay_session_manager_bind_client(manager, 310U, client_id));
    request = make_request("POST", "/play", "HTTP/1.1", false,
                           "terminal-before-close");
    assert(airplay_session_manager_observe(manager, 311U, &request, &snapshot) ==
           AIRPLAY_SESSION_OBSERVE_OK);
    assert(airplay_session_manager_bind_client(manager, 311U, client_id));
    logical_id = snapshot.logical_session_id;
    assert(airplay_session_manager_retain_media(manager, logical_id));

    assert(airplay_session_manager_close(manager, 311U, &close_result));
    assert(close_result.last_logical_connection);
    assert(close_result.terminal_media_session_id == 0U);
    assert(airplay_session_manager_mark_transport_teardown(
        manager, 310U, &terminal_media));
    assert(terminal_media == logical_id);
    assert(airplay_session_manager_release_media(manager, logical_id));
    assert(airplay_session_manager_close(manager, 310U, &close_result));
    airplay_session_manager_destroy(manager);
}

static void test_transport_teardown_does_not_cross_client_identity(void)
{
    AirPlaySessionManager *manager = airplay_session_manager_create();
    AirPlaySessionSnapshot snapshot;
    AirPlaySessionCloseResult close_result;
    AirPlayRtspRequest request;
    uint8_t transport_client[AIRPLAY_SESSION_CLIENT_ID_SIZE] = {3U};
    uint8_t media_client[AIRPLAY_SESSION_CLIENT_ID_SIZE] = {4U};
    uint64_t logical_id;
    uint64_t terminal_media = 0U;

    assert(manager);
    request = make_request("OPTIONS", "*", "RTSP/1.0", true, NULL);
    assert(airplay_session_manager_observe(manager, 320U, &request, &snapshot) ==
           AIRPLAY_SESSION_OBSERVE_OK);
    assert(airplay_session_manager_bind_client(manager, 320U,
                                               transport_client));
    request = make_request("POST", "/play", "HTTP/1.1", false,
                           "unrelated-media");
    assert(airplay_session_manager_observe(manager, 321U, &request, &snapshot) ==
           AIRPLAY_SESSION_OBSERVE_OK);
    assert(airplay_session_manager_bind_client(manager, 321U, media_client));
    logical_id = snapshot.logical_session_id;
    assert(airplay_session_manager_retain_media(manager, logical_id));
    assert(airplay_session_manager_close(manager, 321U, &close_result));
    assert(close_result.terminal_media_session_id == 0U);
    assert(airplay_session_manager_mark_transport_teardown(
        manager, 320U, &terminal_media));
    assert(terminal_media == 0U);
    assert(airplay_session_manager_release_media(manager, logical_id));
    assert(airplay_session_manager_close(manager, 320U, &close_result));
    airplay_session_manager_destroy(manager);
}

int main(void)
{
    test_connection_classification();
    test_remote_video_requires_binding();
    test_shared_session_lifetime();
    test_reverse_shares_remote_video_identity();
    test_conflicts_are_rejected();
    test_invalid_ids_and_capacity();
    test_reconnect_releases_capacity();
    test_active_media_retains_detached_session();
    test_raop_is_independent_from_apple_session();
    test_transport_teardown_waits_for_final_control_close();
    test_final_control_close_waits_for_transport_teardown();
    test_transport_teardown_does_not_cross_client_identity();
    puts("airplay session tests passed");
    return 0;
}
