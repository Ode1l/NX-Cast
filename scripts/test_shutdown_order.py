#!/usr/bin/env python3
"""Keep shutdown dependency order reviewable and regression-tested."""

from pathlib import Path


SOURCE = Path(__file__).resolve().parents[1] / "source" / "main.c"


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
    print("shutdown order test passed")


if __name__ == "__main__":
    main()
