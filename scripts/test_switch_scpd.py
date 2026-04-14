#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime as dt
import json
import sys
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT_ROOT = REPO_ROOT / "artifacts" / "switch-scpd"
USER_AGENT_DEFAULT = "NX-Cast-SCPD-Probe/1.0"
DEVICE_NS = {"d": "urn:schemas-upnp-org:device-1-0"}


def safe_name(value: str) -> str:
    parsed = urllib.parse.urlsplit(value)
    base = parsed.path or value
    if parsed.query:
        base = f"{base}__query"
    base = base.lstrip("/").replace("/", "__")
    return base or "root"


def ensure_base_url(value: str) -> str:
    if "://" not in value:
        value = f"http://{value}"
    if not value.endswith("/"):
        value = f"{value}/"
    return value


def fetch_url(url: str, output_dir: Path, label: str, user_agent: str, validate_xml: bool) -> dict:
    request = urllib.request.Request(url, headers={"User-Agent": user_agent})
    responses_dir = output_dir / "responses"
    responses_dir.mkdir(parents=True, exist_ok=True)

    body_path = responses_dir / label
    meta_path = responses_dir / f"{label}.json"

    with urllib.request.urlopen(request, timeout=8) as response:
        body = response.read()
        meta = {
            "url": response.geturl(),
            "status": response.status,
            "headers": dict(response.headers.items()),
            "bytes": len(body),
        }
        body_path.write_bytes(body)
        meta_path.write_text(json.dumps(meta, ensure_ascii=False, indent=2), encoding="utf-8")
        if validate_xml:
            xml_dir = output_dir / "xml-validation"
            xml_dir.mkdir(parents=True, exist_ok=True)
            ET.fromstring(body)
            (xml_dir / f"{label}.ok").write_text("ok\n", encoding="utf-8")
        return {"meta": meta, "body": body}


def try_fetch_url(url: str, output_dir: Path, label: str, user_agent: str, validate_xml: bool) -> dict:
    try:
        result = fetch_url(url, output_dir, label, user_agent, validate_xml)
        result["ok"] = True
        return result
    except Exception as exc:  # pragma: no cover - diagnostic path
        errors_dir = output_dir / "errors"
        errors_dir.mkdir(parents=True, exist_ok=True)
        error = {
            "url": url,
            "error": str(exc),
            "type": type(exc).__name__,
        }
        (errors_dir / f"{label}.json").write_text(json.dumps(error, ensure_ascii=False, indent=2), encoding="utf-8")
        return {"ok": False, "error": error}


def summary_safe_result(result: dict) -> dict:
    safe = dict(result)
    safe.pop("body", None)
    return safe


def parse_description(description_xml: bytes, fetched_url: str) -> dict:
    root = ET.fromstring(description_xml)
    url_base = root.findtext("d:URLBase", default="", namespaces=DEVICE_NS).strip()
    device = root.find("d:device", DEVICE_NS)
    if device is None:
        raise ValueError("Description.xml missing device node")

    services = []
    for service in device.findall("d:serviceList/d:service", DEVICE_NS):
        services.append(
            {
                "serviceType": service.findtext("d:serviceType", default="", namespaces=DEVICE_NS),
                "serviceId": service.findtext("d:serviceId", default="", namespaces=DEVICE_NS),
                "controlURL": service.findtext("d:controlURL", default="", namespaces=DEVICE_NS),
                "eventSubURL": service.findtext("d:eventSubURL", default="", namespaces=DEVICE_NS),
                "SCPDURL": service.findtext("d:SCPDURL", default="", namespaces=DEVICE_NS),
            }
        )

    icons = []
    for icon in device.findall("d:iconList/d:icon", DEVICE_NS):
        icons.append(
            {
                "mimetype": icon.findtext("d:mimetype", default="", namespaces=DEVICE_NS),
                "url": icon.findtext("d:url", default="", namespaces=DEVICE_NS),
            }
        )

    presentation_url = device.findtext("d:presentationURL", default="", namespaces=DEVICE_NS)
    resolution_base = url_base or fetched_url
    return {
        "url_base": url_base,
        "resolution_base": resolution_base,
        "services": services,
        "icons": icons,
        "presentationURL": presentation_url,
    }


def resolve_url(base: str, maybe_relative: str) -> str:
    return urllib.parse.urljoin(base, maybe_relative)


def main() -> int:
    parser = argparse.ArgumentParser(description="Fetch and validate SCPD/Description resources from a running Switch device.")
    parser.add_argument("--base-url", required=True, help="Example: http://192.168.1.7:49152/")
    parser.add_argument("--user-agent", default=USER_AGENT_DEFAULT, help="HTTP User-Agent to use while probing.")
    parser.add_argument("--output-dir", type=Path, default=None, help="Defaults to artifacts/switch-scpd/<timestamp>.")
    args = parser.parse_args()

    base_url = ensure_base_url(args.base_url)
    timestamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    output_dir = args.output_dir or (DEFAULT_OUTPUT_ROOT / timestamp)
    output_dir.mkdir(parents=True, exist_ok=True)

    candidates = [
        urllib.parse.urljoin(base_url, "description.xml"),
        urllib.parse.urljoin(base_url, "Description.xml"),
        urllib.parse.urljoin(base_url, "device.xml"),
    ]

    fetched_descriptions = []
    primary_description = None
    for url in candidates:
        label = safe_name(url)
        result = try_fetch_url(url, output_dir, label, args.user_agent, validate_xml=True)
        fetched_descriptions.append({"url": url, **summary_safe_result(result)})
        if primary_description is None and result.get("ok"):
            primary_description = result
            primary_description["source_url"] = url

    if primary_description is None:
        print(f"no description endpoint succeeded, see {output_dir}", file=sys.stderr)
        return 1

    description_info = parse_description(primary_description["body"], primary_description["meta"]["url"])

    discovered = {
        "scpd": [],
        "control": [],
        "event": [],
        "icons": [],
        "presentation": [],
    }

    for service in description_info["services"]:
        if service["SCPDURL"]:
            discovered["scpd"].append(resolve_url(description_info["resolution_base"], service["SCPDURL"]))
        if service["controlURL"]:
            discovered["control"].append(resolve_url(description_info["resolution_base"], service["controlURL"]))
        if service["eventSubURL"]:
            discovered["event"].append(resolve_url(description_info["resolution_base"], service["eventSubURL"]))

    for icon in description_info["icons"]:
        if icon["url"]:
            discovered["icons"].append(resolve_url(description_info["resolution_base"], icon["url"]))
    if description_info["presentationURL"]:
        discovered["presentation"].append(resolve_url(description_info["resolution_base"], description_info["presentationURL"]))

    fetched_resources = []
    for category in ("scpd", "icons", "presentation"):
        for url in discovered[category]:
            label = safe_name(url)
            fetched_resources.append(
                {
                    "category": category,
                    "url": url,
                    **summary_safe_result(
                        try_fetch_url(url, output_dir, label, args.user_agent, validate_xml=url.lower().endswith(".xml"))
                    ),
                }
            )

    summary = {
        "base_url": base_url,
        "user_agent": args.user_agent,
        "output_dir": str(output_dir),
        "description_candidates": fetched_descriptions,
        "description": {
            "source_url": primary_description["meta"]["url"],
            "url_base": description_info["url_base"],
            "resolution_base": description_info["resolution_base"],
            "services": description_info["services"],
            "icons": description_info["icons"],
            "presentationURL": description_info["presentationURL"],
        },
        "discovered_urls": discovered,
        "fetched_resources": fetched_resources,
    }
    (output_dir / "summary.json").write_text(json.dumps(summary), encoding="utf-8")
    print(output_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
