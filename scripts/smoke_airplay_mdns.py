#!/usr/bin/env python3
"""Exercise AirPlay mDNS announce, query, conflict and goodbye over UDP."""

from __future__ import annotations

import argparse
import selectors
import socket
import struct
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Record:
    name: str
    kind: int
    dns_class: int
    ttl: int
    data: bytes


def decode_name(packet: bytes, offset: int) -> tuple[str, int]:
    labels: list[str] = []
    cursor = offset
    next_offset: int | None = None
    visited: set[int] = set()
    while True:
        if cursor >= len(packet) or cursor in visited:
            raise AssertionError("invalid compressed DNS name")
        visited.add(cursor)
        length = packet[cursor]
        cursor += 1
        if length & 0xC0 == 0xC0:
            if cursor >= len(packet):
                raise AssertionError("truncated DNS pointer")
            pointer = ((length & 0x3F) << 8) | packet[cursor]
            cursor += 1
            if pointer >= len(packet):
                raise AssertionError("DNS pointer outside packet")
            if next_offset is None:
                next_offset = cursor
            cursor = pointer
            continue
        if length & 0xC0:
            raise AssertionError("reserved DNS label form")
        if length == 0:
            return ".".join(labels), next_offset if next_offset is not None else cursor
        if length > 63 or cursor + length > len(packet):
            raise AssertionError("invalid DNS label")
        labels.append(packet[cursor : cursor + length].decode("utf-8"))
        cursor += length


def parse_packet(packet: bytes) -> tuple[int, int, list[Record]]:
    if len(packet) < 12:
        raise AssertionError("short DNS packet")
    transaction_id, flags, questions, answers, authorities, additional = struct.unpack(
        "!6H", packet[:12]
    )
    offset = 12
    for _ in range(questions):
        _, offset = decode_name(packet, offset)
        if offset + 4 > len(packet):
            raise AssertionError("short DNS question")
        offset += 4
    records: list[Record] = []
    for _ in range(answers + authorities + additional):
        name, offset = decode_name(packet, offset)
        if offset + 10 > len(packet):
            raise AssertionError("short DNS record")
        kind, dns_class, ttl, size = struct.unpack("!HHIH", packet[offset : offset + 10])
        offset += 10
        if offset + size > len(packet):
            raise AssertionError("short DNS rdata")
        records.append(Record(name, kind, dns_class, ttl, packet[offset : offset + size]))
        offset += size
    if offset != len(packet):
        raise AssertionError("trailing DNS bytes")
    return transaction_id, flags, records


def encode_name(name: str) -> bytes:
    encoded = bytearray()
    for label in name.split("."):
        data = label.encode("utf-8")
        if not data or len(data) > 63:
            raise AssertionError("invalid test DNS label")
        encoded.append(len(data))
        encoded.extend(data)
    encoded.append(0)
    return bytes(encoded)


def make_query(name: str, transaction_id: int = 0x1234) -> bytes:
    return (
        struct.pack("!6H", transaction_id, 0, 1, 0, 0, 0)
        + encode_name(name)
        + struct.pack("!HH", 12, 0x8001)
    )


def make_txt_conflict(instance: str) -> bytes:
    value = b"\x08pw=false"
    return (
        struct.pack("!6H", 0, 0x8400, 0, 1, 0, 0)
        + encode_name(f"{instance}._airplay._tcp.local")
        + struct.pack("!HHIH", 16, 0x8001, 120, len(value))
        + value
    )


def receive_packet(sock: socket.socket, timeout: float = 3.0) -> bytes:
    sock.settimeout(timeout)
    return sock.recvfrom(2048)[0]


def wait_for_ready(process: subprocess.Popen[str], timeout: float = 5.0) -> tuple[int, str]:
    assert process.stdout is not None
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    deadline = time.monotonic() + timeout
    try:
        while time.monotonic() < deadline:
            if process.poll() is not None:
                stderr = process.stderr.read() if process.stderr else ""
                raise AssertionError(f"mDNS server exited early: {process.returncode}: {stderr}")
            events = selector.select(deadline - time.monotonic())
            if not events:
                continue
            line = process.stdout.readline().strip()
            if line.startswith("READY "):
                _, port, instance = line.split(" ", 2)
                return int(port), instance
            if line:
                raise AssertionError(f"unexpected mDNS server output: {line}")
    finally:
        selector.close()
    raise AssertionError("timed out waiting for mDNS server")


def validate_records(records: list[Record], instance: str, expected_ttl: int) -> None:
    assert len(records) == 4, records
    by_kind = {record.kind: record for record in records}
    assert set(by_kind) == {1, 12, 16, 33}, by_kind
    assert all(record.ttl == expected_ttl for record in records), records
    assert by_kind[12].name.lower() == "_airplay._tcp.local"
    assert by_kind[33].name == f"{instance}._airplay._tcp.local"
    assert by_kind[16].name == f"{instance}._airplay._tcp.local"
    txt = by_kind[16].data
    assert b"features=0x8000000,0x0" in txt, txt
    assert b"pw=true" in txt, txt
    assert b"pk=" in txt, txt
    assert struct.unpack("!H", by_kind[33].data[4:6])[0] == 7000
    assert by_kind[1].data == socket.inet_aton("127.0.0.1")


def run_smoke(server_binary: Path) -> None:
    monitor = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    monitor.bind(("127.0.0.1", 0))
    monitor_port = monitor.getsockname()[1]
    process = subprocess.Popen(
        [str(server_binary), "0", str(monitor_port)],
        cwd=server_binary.parents[2],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        port, instance = wait_for_ready(process)
        transaction_id, flags, records = parse_packet(receive_packet(monitor))
        assert transaction_id == 0 and flags == 0x8400
        validate_records(records, instance, 120)

        query = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        query.bind(("127.0.0.1", 0))
        try:
            query.sendto(make_query("_airplay._tcp.local"), ("127.0.0.1", port))
            transaction_id, flags, records = parse_packet(receive_packet(query))
            assert transaction_id == 0x1234 and flags == 0x8400
            validate_records(records, instance, 120)

            query.sendto(make_txt_conflict(instance), ("127.0.0.1", port))
            _, _, renamed_records = parse_packet(receive_packet(monitor))
            validate_records(renamed_records, f"{instance} (2)", 120)
        finally:
            query.close()

        process.terminate()
        _, _, goodbye = parse_packet(receive_packet(monitor))
        validate_records(goodbye, f"{instance} (2)", 0)
        process.wait(timeout=5.0)
    finally:
        monitor.close()
        if process.poll() is None:
            process.terminate()
        try:
            process.wait(timeout=5.0)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5.0)
        if process.returncode not in (0, -15):
            stderr = process.stderr.read() if process.stderr else ""
            raise AssertionError(f"mDNS smoke server failed: {process.returncode}: {stderr}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server-bin", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    server_binary = args.server_bin or root / "build" / "tests" / "airplay_mdns_smoke_server"
    if not server_binary.is_file():
        parser.error(f"server binary not found: {server_binary}; run make test-airplay first")
    run_smoke(server_binary.resolve())
    print("AirPlay mDNS lifecycle smoke passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
