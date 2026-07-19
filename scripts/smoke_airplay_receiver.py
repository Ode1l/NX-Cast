#!/usr/bin/env python3
"""Exercise the composed AirPlay receiver without exposing pairing secrets."""

from __future__ import annotations

import socket
import subprocess
from pathlib import Path

from smoke_airplay import ResponseReader, connect, launch_server, wait_for_ready


def request(connection: socket.socket, cseq: int, method: str, uri: str,
            body: bytes = b"", content_type: str | None = None) -> None:
    headers = [
        f"{method} {uri} RTSP/1.0",
        f"CSeq: {cseq}",
        f"Content-Length: {len(body)}",
    ]
    if content_type:
        headers.append(f"Content-Type: {content_type}")
    connection.sendall(("\r\n".join(headers) + "\r\n\r\n").encode("ascii") + body)


def run_smoke(server_binary: Path, requested_port: int = 0) -> None:
    process = launch_server(server_binary, requested_port)
    try:
        port = wait_for_ready(process)

        with connect("127.0.0.1", port) as connection:
            reader = ResponseReader(connection)
            request(connection, 1, "GET", "/info")
            protocol, status, headers, body = reader.read()
            assert protocol == "RTSP/1.0" and status == 200
            assert headers.get("cseq") == "1"
            assert headers.get("content-type") == "application/x-apple-binary-plist"
            assert body.startswith(b"bplist00")

            stage1 = b"FPLY" + bytes((3,)) + bytes(11)
            request(connection, 2, "POST", "/fp-setup", stage1,
                    "application/octet-stream")
            _protocol, status, headers, body = reader.read()
            assert status == 501 and headers.get("cseq") == "2" and body == b""

            request(connection, 3, "SETUP", "/stream")
            _protocol, status, headers, body = reader.read()
            assert status == 470 and headers.get("connection") == "close" and body == b""
            assert connection.recv(1) == b""

        with connect("127.0.0.1", port) as connection:
            reader = ResponseReader(connection)
            request(connection, 4, "OPTIONS", "*")
            _protocol, status, headers, body = reader.read()
            assert status == 200 and "SETUP" in headers.get("public", "") and body == b""

        process.terminate()
        process.wait(timeout=3.0)
        assert process.returncode == 0
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3.0)
        if process.returncode != 0:
            stderr = process.stderr.read() if process.stderr else ""
            raise AssertionError(f"receiver smoke failed: {process.returncode}: {stderr}")
