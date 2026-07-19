#!/usr/bin/env python3
"""Verify pairing authorization and reconnect semantics over real TCP."""

from __future__ import annotations

import argparse
import socket
import subprocess
import sys
from pathlib import Path

from smoke_airplay import ResponseReader, connect, expect_response, wait_for_ready


def launch_server(server_binary: Path, port: int) -> subprocess.Popen[str]:
    return subprocess.Popen(
        [str(server_binary), str(port)],
        cwd=server_binary.parents[2],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


def run_smoke(host: str, requested_port: int, server_binary: Path) -> None:
    process = launch_server(server_binary, requested_port)
    try:
        try:
            port = wait_for_ready(process)
        except AssertionError:
            if requested_port == 0 or process.poll() is None:
                raise
            process.wait(timeout=2.0)
            process = launch_server(server_binary, 0)
            port = wait_for_ready(process)
            print(
                f"AirPlay pairing smoke port {requested_port} is busy; using {port}",
                file=sys.stderr,
            )

        with connect(host, port) as connection:
            reader = ResponseReader(connection)
            connection.sendall(
                b"SETUP rtsp://receiver/stream RTSP/1.0\r\n"
                b"CSeq: 1\r\nContent-Length: 0\r\n\r\n"
            )
            headers = expect_response(reader, 470, "1")
            assert headers.get("connection") == "close", headers
            assert connection.recv(1) == b""

        with connect(host, port) as connection:
            reader = ResponseReader(connection)
            connection.sendall(b"OPTIONS * RTSP/1.0\r\nCSeq: 2\r\n\r\n")
            headers = expect_response(reader, 200, "2")
            assert "SETUP" in headers.get("public", ""), headers

        with connect(host, port) as connection:
            reader = ResponseReader(connection)
            connection.sendall(
                b"POST /pair-setup-pin RTSP/1.0\r\n"
                b"CSeq: 3\r\nContent-Type: application/x-apple-binary-plist\r\n"
                b"Content-Length: 3\r\n\r\nBAD"
            )
            headers = expect_response(reader, 470, "3")
            assert headers.get("connection") == "close", headers
            assert connection.recv(1) == b""

        with connect(host, port) as connection:
            reader = ResponseReader(connection)
            connection.sendall(b"OPTIONS * RTSP/1.0\r\nCSeq: 4\r\n\r\n")
            expect_response(reader, 200, "4")
    finally:
        if process.poll() is None:
            process.terminate()
        try:
            process.wait(timeout=5.0)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5.0)
        if process.returncode not in (0, -15):
            stderr = process.stderr.read() if process.stderr else ""
            raise AssertionError(f"pairing smoke server failed: {process.returncode}: {stderr}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=7000)
    parser.add_argument("--server-bin", type=Path)
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    server_binary = args.server_bin or root / "build" / "tests" / "airplay_pairing_smoke_server"
    if not server_binary.is_file():
        parser.error(f"server binary not found: {server_binary}; run make test-airplay first")
    run_smoke(args.host, args.port, server_binary.resolve())
    print("AirPlay pairing authorization smoke passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
