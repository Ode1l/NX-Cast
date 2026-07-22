#!/usr/bin/env python3
"""Keep shutdown dependency order reviewable and regression-tested."""

from pathlib import Path


SOURCE = Path(__file__).resolve().parents[1] / "source" / "main.c"
ROOT = SOURCE.parent


def function_text(relative: str, signature: str, next_signature: str) -> str:
    text = (ROOT / relative).read_text(encoding="utf-8")
    start = text.index(signature)
    end = text.index(next_signature, start)
    return text[start:end]


def assert_in_order(label: str, text: str, markers: list[str]) -> None:
    positions = [text.index(marker) for marker in markers]
    assert positions == sorted(positions), f"{label} shutdown order changed"


def main() -> None:
    text = SOURCE.read_text(encoding="utf-8")
    shutdown = text.index(
        'shutdown_stdio_trace("[INFO] [shutdown] begin reason=%s'
    )
    tail = text[shutdown:]
    markers = [
        "protocol_coordinator_stop();",
        'shutdown_stdio_trace("[INFO] [shutdown] step=player_stop begin',
        "player_quiesce();",
        "player_wait_idle(2000u);",
        'shutdown_stdio_trace("[INFO] [shutdown] step=player_view_deinit begin',
        'shutdown_stdio_trace("[INFO] [shutdown] step=player_deinit begin',
        'shutdown_stdio_trace("[INFO] [shutdown] step=log_runtime_shutdown begin',
        "log_runtime_shutdown();",
        "set_power_policy(false, false);",
        'shutdown_stdio_trace("[INFO] [shutdown] step=main return begin',
    ]
    positions = [tail.index(marker) for marker in markers]

    assert positions == sorted(positions), "shutdown dependency order changed"
    assert "player_submit_command_wait(&stop_request, 2000u);" in tail

    mdns = function_text(
        "protocol/airplay/discovery/mdns.c",
        "void airplay_mdns_stop(void)",
        "bool airplay_mdns_is_running(void)",
    )
    assert_in_order(
        "mDNS",
        mdns,
        [
            "atomic_store(&g_mdns.running, false);",
            "socket_fd = mdns_take_socket();",
            "mdns_thread_join(&g_mdns.thread);",
            "mdns_finish_socket_close(socket_fd);",
            "g_mdns.started = false;",
        ],
    )

    ssdp = function_text(
        "protocol/dlna/discovery/ssdp.c",
        "void ssdp_stop(void)",
        "void ssdp_set_suspended(bool suspended)",
    )
    assert_in_order(
        "SSDP",
        ssdp,
        [
            "atomic_store(&g_ssdp.running, false);",
            "fd = atomic_exchange(&g_ssdp.socket_fd, -1);",
            "shutdown(fd, SHUT_RDWR);",
            "threadWaitForExit(&g_ssdp.thread);",
            "ssdp_socket_close_fd(fd);",
            "ssdp_clear_cached_strings();",
        ],
    )

    http = function_text(
        "protocol/http/http_server.c",
        "void http_server_stop(void)",
        "bool http_server_is_running(void)",
    )
    assert_in_order(
        "DLNA HTTP",
        http,
        [
            "atomic_store(&g_http_server.running, false);",
            "listen_sock = atomic_exchange(&g_http_server.listen_sock, -1);",
            "shutdown(listen_sock, SHUT_RDWR);",
            "threadWaitForExit(&g_http_server.thread);",
            "http_diagnostic_close(client_sock);",
            "g_http_server.handler = NULL;",
        ],
    )

    event = function_text(
        "protocol/dlna/control/event_server.c",
        "void event_server_stop(void)",
        "static bool event_handle_subscribe(",
    )
    assert_in_order(
        "DLNA event",
        event,
        [
            "atomic_store(&g_event_stop_requested, true);",
            "active_socket = atomic_exchange(&g_event_active_socket, -1);",
            "shutdown(active_socket, SHUT_RDWR);",
            "threadWaitForExit(&g_event_thread);",
            "event_diagnostic_close(active_socket);",
            "g_event_thread_started = false;",
        ],
    )

    airplay = function_text(
        "protocol/airplay/server.c",
        "void airplay_server_stop(void)",
        "bool airplay_server_is_running(void)",
    )
    assert_in_order(
        "AirPlay control",
        airplay,
        [
            "atomic_store(&g_airplay_server.running, false);",
            "airplay_server_take_socket(&g_airplay_server.listen_socket);",
            "airplay_native_thread_join(&g_airplay_server.listener_thread);",
            "airplay_server_diagnostic_close(client_sockets[index]);",
            "g_airplay_server.listener_started = false;",
        ],
    )
    print("shutdown order test passed")


if __name__ == "__main__":
    main()
