#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime as dt
import html
import http.server
import json
import socketserver
import threading
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TEMPLATE_ROOT = REPO_ROOT / "assets" / "dlna"
DEFAULT_OUTPUT_ROOT = REPO_ROOT / "artifacts" / "dlna-smoke"

RAW_PLACEHOLDERS = {"header_extra", "service_extra"}
CONTENT_TYPES = {
    "Description.xml": "text/xml; charset=\"utf-8\"",
    "AVTransport.xml": "text/xml; charset=\"utf-8\"",
    "RenderingControl.xml": "text/xml; charset=\"utf-8\"",
    "ConnectionManager.xml": "text/xml; charset=\"utf-8\"",
    "Presentation.html": "text/html; charset=\"utf-8\"",
    "icon.jpg": "image/jpeg",
}


def build_values(port: int) -> dict[str, str]:
    base = f"http://127.0.0.1:{port}/"
    return {
        "url_base": base,
        "friendly_name": "NX-Cast Smoke Test",
        "manufacturer": "Ode1l",
        "manufacturer_url": "https://example.invalid/ode1l",
        "model_description": "Nintendo Switch DLNA Media Renderer",
        "model_name": "NX-Cast Virtual Renderer",
        "model_number": "0.1.0",
        "model_url": "https://example.invalid/nx-cast",
        "serial_num": "123456789012",
        "uuid": "6b0d3c60-3d96-41f4-986c-0a4bb12b0001",
        "header_extra": "",
        "service_extra": "",
    }


def render_template(template_name: str, values: dict[str, str]) -> bytes:
    text = (TEMPLATE_ROOT / template_name).read_text(encoding="utf-8")
    out: list[str] = []
    index = 0
    while index < len(text):
        start = text.find("{", index)
        if start < 0:
            out.append(text[index:])
            break
        out.append(text[index:start])
        end = text.find("}", start + 1)
        if end < 0:
            out.append(text[start:])
            break
        key = text[start + 1 : end]
        replacement = values.get(key)
        if replacement is None:
            out.append(text[start : end + 1])
        elif key in RAW_PLACEHOLDERS:
            out.append(replacement)
        else:
            out.append(html.escape(replacement, quote=True).replace("'", "&apos;"))
        index = end + 1
    return "".join(out).encode("utf-8")


def load_resource(template_name: str, values: dict[str, str]) -> bytes:
    if template_name in {"Description.xml", "Presentation.html"}:
        return render_template(template_name, values)
    return (TEMPLATE_ROOT / template_name).read_bytes()


def route_table(values: dict[str, str]) -> dict[str, tuple[str, bytes]]:
    table: dict[str, tuple[str, bytes]] = {}
    mappings = {
        "/description.xml": "Description.xml",
        "/Description.xml": "Description.xml",
        "/device.xml": "Description.xml",
        "/presentation.html": "Presentation.html",
        "/dlna/icon.jpg": "icon.jpg",
        "/dlna/AVTransport.xml": "AVTransport.xml",
        "/scpd/AVTransport.xml": "AVTransport.xml",
        "/dlna/RenderingControl.xml": "RenderingControl.xml",
        "/scpd/RenderingControl.xml": "RenderingControl.xml",
        "/dlna/ConnectionManager.xml": "ConnectionManager.xml",
        "/scpd/ConnectionManager.xml": "ConnectionManager.xml",
    }
    for path, template_name in mappings.items():
        table[path] = (CONTENT_TYPES[template_name], load_resource(template_name, values))
    return table


class SingleThreadedTCPServer(socketserver.TCPServer):
    allow_reuse_address = True


def make_handler(routes: dict[str, tuple[str, bytes]]):
    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def do_HEAD(self) -> None:
            self._serve(include_body=False)

        def do_GET(self) -> None:
            self._serve(include_body=True)

        def log_message(self, fmt: str, *args) -> None:
            return

        def _serve(self, include_body: bool) -> None:
            path = self.path.split("?", 1)[0]
            if path not in routes:
                self.send_response(404)
                self.send_header("Content-Type", "text/plain; charset=\"utf-8\"")
                self.send_header("Content-Length", "9")
                self.send_header("Connection", "close")
                self.end_headers()
                if include_body:
                    self.wfile.write(b"Not Found")
                return

            content_type, body = routes[path]
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            if include_body:
                self.wfile.write(body)

    return Handler


def save_response(output_dir: Path, endpoint: str, response: urllib.response.addinfourl, body: bytes) -> None:
    safe_name = endpoint.lstrip("/").replace("/", "__")
    if not safe_name:
        safe_name = "root"
    (output_dir / "responses").mkdir(parents=True, exist_ok=True)
    body_path = output_dir / "responses" / safe_name
    body_path.write_bytes(body)
    meta = {
        "url": response.geturl(),
        "status": response.status,
        "headers": dict(response.headers.items()),
        "bytes": len(body),
    }
    (output_dir / "responses" / f"{safe_name}.json").write_text(
        json.dumps(meta, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )


def validate_xml(output_dir: Path, endpoint: str, body: bytes) -> None:
    safe_name = endpoint.lstrip("/").replace("/", "__")
    xml_dir = output_dir / "xml-validation"
    xml_dir.mkdir(parents=True, exist_ok=True)
    ET.fromstring(body)
    (xml_dir / f"{safe_name}.ok").write_text("ok\n", encoding="utf-8")


def fetch_and_save(base_url: str, output_dir: Path, endpoints: list[str]) -> None:
    for endpoint in endpoints:
        url = base_url + endpoint.lstrip("/")
        with urllib.request.urlopen(url, timeout=5) as response:
            body = response.read()
            save_response(output_dir, endpoint, response, body)
            if endpoint.endswith(".xml"):
                validate_xml(output_dir, endpoint, body)


def main() -> int:
    parser = argparse.ArgumentParser(description="Smoke test DLNA description/SCPD endpoints locally.")
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=None,
        help="Directory to save fetched responses. Defaults to artifacts/dlna-smoke/<timestamp>.",
    )
    args = parser.parse_args()

    timestamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    output_dir = args.output_dir or (DEFAULT_OUTPUT_ROOT / timestamp)
    output_dir.mkdir(parents=True, exist_ok=True)

    with SingleThreadedTCPServer(("127.0.0.1", 0), make_handler({})) as server:
        port = server.server_address[1]
        values = build_values(port)
        routes = route_table(values)
        server.RequestHandlerClass = make_handler(routes)

        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            endpoints = [
                "/description.xml",
                "/Description.xml",
                "/device.xml",
                "/dlna/AVTransport.xml",
                "/dlna/RenderingControl.xml",
                "/dlna/ConnectionManager.xml",
                "/presentation.html",
                "/dlna/icon.jpg",
            ]
            fetch_and_save(f"http://127.0.0.1:{port}/", output_dir, endpoints)
        finally:
            server.shutdown()
            thread.join(timeout=2)

    summary = {
        "output_dir": str(output_dir),
        "endpoints": endpoints,
        "template_root": str(TEMPLATE_ROOT),
    }
    (output_dir / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    print(output_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
