#!/usr/bin/env python3
"""Serve a local file or directory over HTTP for NX-Cast smoke tests."""

from __future__ import annotations

import argparse
import http.server
import ipaddress
import os
import pathlib
import re
import shutil
import socket
import subprocess
import sys
from urllib.parse import quote


def is_rfc1918_ipv4(value: str) -> bool:
    try:
        ip = ipaddress.ip_address(value)
    except ValueError:
        return False
    return (
        ip.version == 4
        and (
            ip in ipaddress.ip_network("192.168.0.0/16")
            or ip in ipaddress.ip_network("10.0.0.0/8")
            or ip in ipaddress.ip_network("172.16.0.0/12")
        )
    )


def detect_lan_ip() -> str:
    try:
        output = subprocess.check_output(["ifconfig"], text=True, stderr=subprocess.DEVNULL)
        candidates = re.findall(r"\binet (\d+\.\d+\.\d+\.\d+)\b", output)
        for prefix in ("192.168.", "10.", "172."):
            for candidate in candidates:
                if candidate.startswith(prefix) and is_rfc1918_ipv4(candidate):
                    return candidate
    except (OSError, subprocess.CalledProcessError):
        pass

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect(("8.8.8.8", 80))
        candidate = sock.getsockname()[0]
        if is_rfc1918_ipv4(candidate):
            return candidate
    except OSError:
        pass
    finally:
        sock.close()

    return "127.0.0.1"


def analyze_mp4_layout(path: pathlib.Path) -> str | None:
    suffix = path.suffix.lower()
    if suffix not in {".mp4", ".m4v", ".mov"}:
        return None

    try:
        data = path.read_bytes()
    except OSError:
        return None

    moov = data.find(b"moov")
    mdat = data.find(b"mdat")
    if moov < 0 or mdat < 0:
        return None
    if moov > mdat:
        return (
            "[local-media-server] warning: 'moov' atom is after 'mdat'. "
            "This file is not faststart and may fail over HTTP unless the client "
            "uses ranged reads."
        )
    return None


class MediaRequestHandler(http.server.SimpleHTTPRequestHandler):
    server_version = "NXCastLocalMedia/1.0"

    def __init__(self, *args, directory: str, **kwargs):
        self._range = None
        super().__init__(*args, directory=directory, **kwargs)

    def log_message(self, fmt: str, *args) -> None:
        sys.stdout.write(
            f"[local-media-server] {self.address_string()} - {fmt % args}\n"
        )

    def send_head(self):
        path = self.translate_path(self.path)
        if os.path.isdir(path):
            self._range = None
            return super().send_head()

        try:
            fh = open(path, "rb")
        except OSError:
            self.send_error(404, "File not found")
            return None

        fs = os.fstat(fh.fileno())
        file_size = fs.st_size
        start = 0
        end = file_size - 1
        status = 200

        range_header = self.headers.get("Range")
        if range_header:
            match = re.fullmatch(r"bytes=(\d*)-(\d*)", range_header.strip())
            if not match:
                fh.close()
                self.send_error(416, "Invalid Range")
                return None

            start_text, end_text = match.groups()
            if start_text:
                start = int(start_text)
            if end_text:
                end = int(end_text)
            if not start_text and end_text:
                suffix_len = int(end_text)
                start = max(file_size - suffix_len, 0)
                end = file_size - 1
            if start > end or start >= file_size:
                fh.close()
                self.send_error(416, "Requested Range Not Satisfiable")
                return None
            end = min(end, file_size - 1)
            status = 206

        self.send_response(status)
        self.send_header("Content-Type", self.guess_type(path))
        self.send_header("Content-Length", str(end - start + 1))
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Last-Modified", self.date_time_string(fs.st_mtime))
        if status == 206:
            self.send_header("Content-Range", f"bytes {start}-{end}/{file_size}")
        self.end_headers()

        fh.seek(start)
        self._range = (start, end)
        return fh

    def copyfile(self, source, outputfile) -> None:
        try:
            if self._range is None:
                shutil.copyfileobj(source, outputfile)
                return

            remaining = self._range[1] - self._range[0] + 1
            while remaining > 0:
                chunk = source.read(min(64 * 1024, remaining))
                if not chunk:
                    break
                outputfile.write(chunk)
                remaining -= len(chunk)
        except (BrokenPipeError, ConnectionResetError):
            pass


class MediaHTTPServer(http.server.ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def handle_error(self, request, client_address) -> None:
        exc = sys.exc_info()[1]
        if isinstance(exc, (BrokenPipeError, ConnectionResetError)):
            return
        super().handle_error(request, client_address)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Serve a local file or directory over HTTP and print usable media URLs."
    )
    parser.add_argument(
        "path",
        nargs="?",
        default=".",
        help="Media file or directory to serve. Defaults to current directory.",
    )
    parser.add_argument("--port", type=int, default=8000, help="TCP port to listen on. Default: 8000")
    parser.add_argument("--bind", default="0.0.0.0", help="Bind address. Default: 0.0.0.0")
    parser.add_argument(
        "--host-ip",
        default=None,
        help="Override the LAN IP shown in printed URLs.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    target = pathlib.Path(args.path).expanduser().resolve()

    if not target.exists():
        print(f"[local-media-server] path not found: {target}", file=sys.stderr)
        return 1

    if target.is_dir():
        serve_dir = target
        media_name = None
    else:
        serve_dir = target.parent
        media_name = target.name

    try:
        httpd = MediaHTTPServer(
            (args.bind, args.port),
            lambda *a, **kw: MediaRequestHandler(*a, directory=str(serve_dir), **kw),
        )
    except OSError as exc:
        if exc.errno == 48:
            print(
                f"[local-media-server] port {args.port} is already in use. "
                f"Stop the existing server or run with --port <new-port>.",
                file=sys.stderr,
            )
            return 1
        raise

    lan_ip = args.host_ip or detect_lan_ip()
    base_url = f"http://{lan_ip}:{args.port}"

    print(f"[local-media-server] serving directory: {serve_dir}")
    print(f"[local-media-server] listen: http://{args.bind}:{args.port}")
    if media_name is not None:
        print(f"[local-media-server] media url: {base_url}/{quote(media_name)}")
        warning = analyze_mp4_layout(target)
        if warning:
            print(warning)
    else:
        print(f"[local-media-server] browse url: {base_url}/")
    print("[local-media-server] press Ctrl+C to stop")

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n[local-media-server] stopping")
    finally:
        httpd.server_close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
