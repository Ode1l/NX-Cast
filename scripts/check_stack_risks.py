#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

THREAD_STACK_RE = re.compile(r"#define\s+([A-Z0-9_]*STACK_SIZE)\s+(0x[0-9A-Fa-f]+|\d+)")
LOCAL_ARRAY_RE = re.compile(
    r"\b(?:char|unsigned char|uint8_t|int|long|short|float|double|bool)\s+"
    r"([A-Za-z_][A-Za-z0-9_]*)\s*\[(\d+)\]"
)

THREAD_STACK_MIN = 0x10000
LOCAL_ARRAY_WARN = 256
LOCAL_ARRAY_FAIL = 1024


def iter_source_files() -> list[pathlib.Path]:
    return sorted(ROOT.joinpath("source").rglob("*.c"))


def parse_int(value: str) -> int:
    return int(value, 0)


def main() -> int:
    warnings: list[str] = []
    failures: list[str] = []

    for path in iter_source_files():
        try:
            lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
        except OSError as exc:
            failures.append(f"{path}: read failed: {exc}")
            continue

        for lineno, line in enumerate(lines, 1):
            stack_match = THREAD_STACK_RE.search(line)
            if stack_match:
                name = stack_match.group(1)
                value = parse_int(stack_match.group(2))
                if value < THREAD_STACK_MIN:
                    failures.append(
                        f"{path}:{lineno}: {name}={hex(value)} below recommended floor {hex(THREAD_STACK_MIN)}"
                    )

            # We only want function-local stack arrays here. Global/static
            # storage is not a stack-overflow risk, so ignore non-indented
            # declarations.
            if not line[:1].isspace():
                continue

            array_match = LOCAL_ARRAY_RE.search(line)
            if array_match:
                array_name = array_match.group(1)
                size = int(array_match.group(2))
                if size >= LOCAL_ARRAY_FAIL:
                    failures.append(
                        f"{path}:{lineno}: local array {array_name}[{size}] exceeds fail threshold {LOCAL_ARRAY_FAIL}"
                    )
                elif size >= LOCAL_ARRAY_WARN:
                    warnings.append(
                        f"{path}:{lineno}: local array {array_name}[{size}] exceeds warning threshold {LOCAL_ARRAY_WARN}"
                    )

    for item in warnings:
        print(f"WARN: {item}")
    for item in failures:
        print(f"FAIL: {item}")

    if failures:
        print("stack-risk-check: FAILED")
        return 1

    print("stack-risk-check: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
