# Diagnostic Samples

This directory contains reviewable, sanitized evidence captured during NX-Cast
development. It is intentionally separate from the ignored local `logs/`
directory and from compiled artifacts.

## Available Sets

- [nxlink logs](nxlink-logs/README.md) — 19 Switch hardware traces from the
  AirPlay/DLNA resource-contention investigation, including diagnostic Profiles
  1–14 and failed collection attempts.

The samples preserve event order, timestamps, profile markers, error/status
codes, state transitions, counters, and shutdown results. Network locations,
device identity material, authentication values, and host paths are replaced by
semantic placeholders. Never use these files as protocol fixtures that require
the original credentials or addresses.
