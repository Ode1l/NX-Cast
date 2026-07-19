#!/usr/bin/env python3
"""Exercise the local persistent AirPlay RTSP transport without protocol secrets."""

from __future__ import annotations

import argparse
import selectors
import socket
import subprocess
import sys
import time
from pathlib import Path


class ResponseReader:
    def __init__(self, connection: socket.socket) -> None:
        self.connection = connection
        self.buffer = bytearray()

    def read(self) -> tuple[str, int, dict[str, str], bytes]:
        while b"\r\n\r\n" not in self.buffer:
            chunk = self.connection.recv(4096)
            if not chunk:
                raise AssertionError("connection closed before response headers")
            self.buffer.extend(chunk)
        marker = self.buffer.index(b"\r\n\r\n") + 4
        header_block = bytes(self.buffer[:marker])
        lines = header_block[:-4].decode("ascii").split("\r\n")
        protocol, status_text, _reason = lines[0].split(" ", 2)
        headers: dict[str, str] = {}
        for line in lines[1:]:
            name, value = line.split(":", 1)
            headers[name.lower()] = value.strip()
        body_length = int(headers.get("content-length", "0"))
        while len(self.buffer) < marker + body_length:
            chunk = self.connection.recv(4096)
            if not chunk:
                raise AssertionError("connection closed before response body")
            self.buffer.extend(chunk)
        body = bytes(self.buffer[marker : marker + body_length])
        del self.buffer[: marker + body_length]
        return protocol, int(status_text), headers, body


def expect_response(
    reader: ResponseReader,
    status: int,
    cseq: str | None = None,
) -> dict[str, str]:
    protocol, actual_status, headers, body = reader.read()
    assert protocol == "RTSP/1.0", protocol
    assert actual_status == status, (actual_status, status)
    assert body == b"", body
    if cseq is not None:
        assert headers.get("cseq") == cseq, headers
    return headers


def connect(host: str, port: int) -> socket.socket:
    connection = socket.create_connection((host, port), timeout=2.0)
    connection.settimeout(2.0)
    return connection


def wait_for_ready(process: subprocess.Popen[str], timeout: float = 5.0) -> int:
    assert process.stdout is not None
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    deadline = time.monotonic() + timeout
    try:
        while time.monotonic() < deadline:
            if process.poll() is not None:
                stderr = process.stderr.read() if process.stderr else ""
                raise AssertionError(f"smoke server exited early: {process.returncode}: {stderr}")
            events = selector.select(deadline - time.monotonic())
            if not events:
                continue
            line = process.stdout.readline().strip()
            if line.startswith("READY "):
                return int(line.split()[1])
            if line:
                raise AssertionError(f"unexpected server output: {line}")
    finally:
        selector.close()
    raise AssertionError("timed out waiting for smoke server")


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
                f"AirPlay smoke port {requested_port} is busy; using ephemeral port {port}",
                file=sys.stderr,
            )

        with connect(host, port) as connection:
            reader = ResponseReader(connection)
            for fragment in (
                b"OPTIONS * RTSP/1.0\r\n",
                b"CSeq: 1\r\n",
                b"\r\n",
            ):
                connection.sendall(fragment)
                time.sleep(0.01)
            headers = expect_response(reader, 200, "1")
            assert "SETUP" in headers.get("public", ""), headers

            connection.sendall(
                b"SETUP rtsp://receiver/stream RTSP/1.0\r\nCSeq: 2\r\nContent-Length: 0\r\n\r\n"
                b"RECORD rtsp://receiver/stream RTSP/1.0\r\nCSeq: 3\r\n\r\n"
            )
            expect_response(reader, 200, "2")
            expect_response(reader, 200, "3")

            connection.sendall(
                b"GET_PARAMETER rtsp://receiver/stream RTSP/1.0\r\nCSeq: 4\r\n\r\n"
            )
            expect_response(reader, 200, "4")
            connection.sendall(
                b"TEARDOWN rtsp://receiver/stream RTSP/1.0\r\nCSeq: 5\r\n\r\n"
            )
            headers = expect_response(reader, 200, "5")
            assert headers.get("connection") == "close", headers
            assert connection.recv(1) == b""

        with connect(host, port) as connection:
            reader = ResponseReader(connection)
            connection.sendall(b"OPTIONS * RTSP/1.0\r\nCSeq: invalid\r\n\r\n")
            expect_response(reader, 400)
            assert connection.recv(1) == b""

        with connect(host, port) as connection:
            reader = ResponseReader(connection)
            connection.sendall(
                b"POST /pair-setup RTSP/1.0\r\nCSeq: 6\r\nContent-Length: 1048577\r\n\r\n"
            )
            expect_response(reader, 413)

        with connect(host, port) as connection:
            reader = ResponseReader(connection)
            connection.sendall(b"OPTIONS * RTSP/1.0\r\nCSeq")
            time.sleep(0.9)
            expect_response(reader, 408)

        connection = connect(host, port)
        connection.sendall(b"OPTIONS * RTSP/1.0\r\n")
        connection.close()
        with connect(host, port) as connection:
            reader = ResponseReader(connection)
            connection.sendall(b"OPTIONS * RTSP/1.0\r\nCSeq: 7\r\n\r\n")
            expect_response(reader, 200, "7")

        held_connections = [connect(host, port) for _ in range(4)]
        try:
            time.sleep(0.2)
            with connect(host, port) as connection:
                reader = ResponseReader(connection)
                connection.sendall(b"OPTIONS * RTSP/1.0\r\nCSeq: 8\r\n\r\n")
                expect_response(reader, 503)
        finally:
            for connection in held_connections:
                connection.close()

        time.sleep(0.2)
        shutdown_probe = connect(host, port)
        try:
            shutdown_probe.sendall(b"POST /pair-setup RTSP/1.0\r\nCSeq: 9\r\n")
            process.terminate()
            process.wait(timeout=2.0)
            assert shutdown_probe.recv(1) == b""
        finally:
            shutdown_probe.close()
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
            raise AssertionError(f"smoke server failed: {process.returncode}: {stderr}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=7000)
    parser.add_argument("--server-bin", type=Path)
    parser.add_argument("--mdns", action="store_true")
    parser.add_argument("--receiver", action="store_true")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    if args.receiver:
        from smoke_airplay_receiver import run_smoke as run_receiver_smoke

        receiver_binary = (
            args.server_bin or root / "build" / "tests" / "airplay_receiver_smoke_server"
        )
        if not receiver_binary.is_file():
            parser.error(f"server binary not found: {receiver_binary}; run make test-airplay first")
        run_receiver_smoke(receiver_binary.resolve(), args.port)
        print("AirPlay composed receiver smoke passed")
        return 0
    if args.mdns:
        from smoke_airplay_mdns import run_smoke as run_mdns_smoke

        mdns_binary = args.server_bin or root / "build" / "tests" / "airplay_mdns_smoke_server"
        if not mdns_binary.is_file():
            parser.error(f"server binary not found: {mdns_binary}; run make test-airplay first")
        run_mdns_smoke(mdns_binary.resolve())
        print("AirPlay mDNS lifecycle smoke passed")
        return 0
    server_binary = args.server_bin or root / "build" / "tests" / "airplay_smoke_server"
    if not server_binary.is_file():
        parser.error(f"server binary not found: {server_binary}; run make test-airplay first")
    run_smoke(args.host, args.port, server_binary.resolve())
    print("AirPlay persistent RTSP smoke passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
