# Sanitized nxlink Log Samples

These 19 logs are the complete local nxlink capture set used for the July 2026
AirPlay/DLNA playback investigation. The original filenames are retained so the
traces can be correlated with the test matrix in
[`docs/MACOS_HANDOFF_2026-07-23.md`](../../docs/MACOS_HANDOFF_2026-07-23.md).

## Sanitization Contract

The committed files are not raw captures. Each output preserves the source line
count and event order, normalizes line endings to LF, removes line-end padding,
and replaces sensitive values in place:

| Placeholder | Replaced data |
|---|---|
| `<URL_REDACTED>` | HTTP(S) URLs, scheme-less `url`/`uri`/`location` values, and SOAP `CurrentURI` content |
| `<IP_REDACTED>` | IPv4 addresses present in this capture set |
| `<MAC_REDACTED>` / `<UUID_REDACTED>` | Link-layer and UUID identifiers |
| `<IDENTIFIER_REDACTED>` | AirPlay/DACP/device/session identity fields |
| `<HEADER_REDACTED>` | Authorization, cookies, and Apple control/authentication header values |
| `<HEX_REDACTED>` / `<BLOB_REDACTED>` | Long opaque hexadecimal or base64-like material |
| `<PATH_REDACTED>` | Windows, MSYS, macOS, Linux, and devkitPro host paths |

Short diagnostic hashes, timestamps, numeric counters, Profile IDs, thread and
socket counts, media format fields, libmpv/HTTP status codes, state-machine
tuples, and `sdmc:/` paths remain visible because they are needed to interpret
the failures. Do not add a new trace without applying the same contract and
running a residual secret/address scan.

## Capture Index

| File | Profile | Primary evidence |
|---|---|---|
| [`run_nxlink-20260722-015737.log`](run_nxlink-20260722-015737.log) | Pre-test collection | nxlink ping received no Switch response. |
| [`run_nxlink-20260722-015831.log`](run_nxlink-20260722-015831.log) | Pre-test collection | Partial NRO upload stopped before launch. |
| [`run_nxlink-20260722-015922.log`](run_nxlink-20260722-015922.log) | Pre-test collection | Second nxlink ping received no Switch response. |
| [`run_nxlink-20260722-020032.log`](run_nxlink-20260722-020032.log) | 1 — `airplay-off` | Baseline run; returned Home and exited normally after DLNA playback. |
| [`run_nxlink-20260722-020323.log`](run_nxlink-20260722-020323.log) | 2 — `control-only` | AirPlay control-only run; DLNA returned Home and shutdown completed. |
| [`run_nxlink-20260722-020526.log`](run_nxlink-20260722-020526.log) | 3 — `mdns-socket` | mDNS socket boundary stayed responsive and exited normally. |
| [`run_nxlink-20260722-020734.log`](run_nxlink-20260722-020734.log) | 5 — `mdns-receive` | Short active-receive capture reaching mDNS worker startup. |
| [`run_nxlink-20260722-021024.log`](run_nxlink-20260722-021024.log) | 3 — `mdns-socket` | IPTV reached first frame and playing state with the socket-only profile. |
| [`run_nxlink-20260722-021400.log`](run_nxlink-20260722-021400.log) | 8 — `full-low-priority` | Idle/home heartbeat run; priority reduction did not break UI or nxlink. |
| [`run_nxlink-20260722-021621.log`](run_nxlink-20260722-021621.log) | 8 — `full-low-priority` | Extended DLNA run with uninterrupted logging and normal exit. |
| [`run_nxlink-20260722-022510.log`](run_nxlink-20260722-022510.log) | 9 — `mdns-receive-bsd8` | BSD8 run where IPTV reached first frame and played. |
| [`run_nxlink-20260722-023200.log`](run_nxlink-20260722-023200.log) | 9 — `mdns-receive-bsd8` | Longer DLNA run exposing repeated playback stutter/seek instability. |
| [`run_nxlink-20260722-024028.log`](run_nxlink-20260722-024028.log) | 10 — `mdns-receive-bsd16` | BSD16 comparison; DLNA contention was worse than BSD8. |
| [`run_nxlink-20260722-124754.log`](run_nxlink-20260722-124754.log) | 11 — `full-discovery-suspend-bsd8` | Local playback improved, but discovery/control remained suspended after Home. |
| [`run_nxlink-20260722-132003.log`](run_nxlink-20260722-132003.log) | 12 — `full-mdns-playback-suspend-bsd8` | DLNA Stop restored discovery, but repeated play/seek remained unreliable. |
| [`run_nxlink-20260722-133422.log`](run_nxlink-20260722-133422.log) | 9 — `mdns-receive-bsd8` | BSD8 repeat run covering DLNA playback and normal Home restoration. |
| [`run_nxlink-20260722-193121.log`](run_nxlink-20260722-193121.log) | 13 — `full-owner-exclusive-bsd12` | Exclusive-owner transitions converged; IPTV asset warnings and mixed-protocol recovery are visible. |
| [`run_nxlink-20260722-212111.log`](run_nxlink-20260722-212111.log) | 13 — `full-owner-exclusive-bsd12` | Long mixed-protocol sequence used to inspect residual resources and Home convergence. |
| [`run_nxlink-20260723-002338.log`](run_nxlink-20260723-002338.log) | 14 — `full-owner-exclusive-observe-bsd12` | Four DLNA HTTP 514 failures and three AirPlay audio-only ALAC setup failures categorized as RTSP 461, with healthy resource restoration. |

The table records the main reason each file is useful; it is not a substitute
for the full interpretation and test procedure in the macOS handoff document.
