# AirPlay Freeze Diagnostic Matrix

This playbook diagnoses the regression where enabling AirPlay can stop nxlink
logs, freeze the Home UI, and prevent IPTV or DLNA playback. It is an evidence
collection procedure, not an AirPlay compatibility test and not a proposed
fix.

Toolchain installation, VS Code task setup, upload commands, and nxlink setup
are intentionally outside this document. Use the same local workflow for every
round and vary only `NXCAST_DIAG_PROFILE`.

## Rules

1. Perform a clean rebuild whenever the profile changes. Reused object files
   invalidate the comparison.
2. Keep the strict Switch feature requirements and all other build settings
   identical between rounds.
3. Use one log file per app launch. Name it with the profile and run number.
4. Use the same Switch, Wi-Fi network, IPTV channel, DLNA sender, and media URL.
5. Do not attempt AirPlay pairing or playback during the startup matrix. That is
   a later test after the runtime remains responsive.
6. Record what is visible on the Switch even when nxlink output stops. Missing
   logs alone do not prove that the main loop stopped.
7. `recv: Connection reset by peer` is expected when the app is closed. It is
   the end of the nxlink session, not the freeze time and not a root cause.

## Build Contract

Select exactly one profile with:

```text
NXCAST_DIAG_PROFILE=<profile-name>
```

Every non-normal profile enables the existing media, input, AirPlay, and
runtime heartbeat traces. A valid diagnostic NRO prints a startup marker like:

```text
[diagnostic] profile_id=5 profile=mdns-receive protocol_start=parallel
```

Do not continue a round if the marker is absent or names the wrong profile.
Record the NRO SHA-256 with the result so an uploaded binary cannot be confused
with an older build.

## Profiles

Run profiles in this order. Each row adds one boundary to the preceding row.

| Order | Profile | ID | AirPlay behavior | Question answered |
|---|---|---:|---|---|
| 1 | `airplay-off` | 1 | AirPlay runtime disabled | Is the non-AirPlay baseline still healthy? |
| 2 | `control-only` | 2 | RTSP/control server starts; discovery is disabled | Does the AirPlay control server alone trigger the regression? |
| 3 | `mdns-socket` | 3 | Adds local-address lookup, UDP socket, bind, and multicast membership; no mDNS worker | Does mDNS socket setup trigger it? |
| 4 | `mdns-idle` | 4 | Adds the mDNS thread, but the thread only sleeps | Does thread creation, stack allocation, affinity, or priority trigger it? |
| 5 | `mdns-receive` | 5 | Adds `select` and `recv`; packets are discarded and nothing is announced or answered | Does multicast receive activity contend with the runtime? |
| 6 | `full-parallel` | 6 | Full mDNS announcement, parsing, and response with parallel service startup | Does normal AirPlay discovery trigger it? |
| 7 | `full-serial` | 7 | Full mDNS, but IPTV, DLNA, then AirPlay are started in order by supervised workers | Is concurrent service initialization the trigger? |
| 8 | `full-low-priority` | 8 | Full parallel startup with the mDNS worker at lower priority | Is mDNS scheduling starving the main/logger/player paths? |

The iPhone must not discover NX-Cast in `control-only`, `mdns-socket`,
`mdns-idle`, or `mdns-receive`. That is expected and is not a failed round.

## Fixed Round

### Gate A: Runtime Liveness

1. Start nxlink capture before launching NX-Cast.
2. Launch the profile build and verify its diagnostic startup marker.
3. Do not touch the Switch for 20 seconds.
4. Confirm that the Home animation still moves and that
   `[runtime-heartbeat]` arrives approximately every two seconds.
5. Press `X` once. Confirm that the IPTV browser opens, then wait five seconds.
6. Press `B` once. Confirm that the browser closes, then wait ten seconds.
7. Record whether the UI and heartbeat were alive at 10, 20, 30, and 40
   seconds.

If Gate A freezes, stop the round. Do not add IPTV, DLNA, or AirPlay traffic to
that run.

### Gate B: Existing Media Paths

Only run this gate when Gate A passes.

1. Open the IPTV browser and play the same known-good channel used in every
   round.
2. Wait up to 30 seconds. Record first-frame time, continuous playback, and
   whether heartbeats continue.
3. Press `B` to return Home. Wait ten seconds and confirm Home remains
   responsive.
4. Send the same known-good DLNA item from the same sender.
5. Wait up to 30 seconds. Record first-frame time, continuous playback, and
   whether the sender can still pause and resume.
6. Close NX-Cast normally and retain the complete raw log.

Do not change channel, sender, media URL, Wi-Fi location, or timeout thresholds
between profiles.

## Heartbeat Fields

The existing two-second runtime heartbeat now includes independent main,
logger, player actor, protocol coordinator, and mDNS evidence.

| Field | Meaning |
|---|---|
| `frame` | Main/render loop progress. It must increase. |
| `log_queue`, `enqueued`, `processed` | Logger backlog and work completed. |
| `log_heartbeat_age_ms` | Age of the logger worker heartbeat. |
| `media_heartbeat_age_ms` | Age of the player media actor heartbeat. |
| `service_workers` and `service_age_ms` | IPTV, DLNA, and AirPlay startup worker state and age. |
| `mdns_phase` | Last mDNS lifecycle boundary reached. |
| `mdns_heartbeat_age_ms` | Age of the mDNS worker heartbeat. |
| `mdns_select` | Number of completed `select` calls, including timeouts. |
| `mdns_recv` | Number of datagrams received. |
| `mdns_sent` | Number of datagrams sent successfully. |

Expected mDNS values by profile:

| Profile | Expected steady state |
|---|---|
| `airplay-off`, `control-only` | `mdns_phase=stopped`, no socket, no worker |
| `mdns-socket` | `mdns_phase=socket-ready`, socket open; mDNS heartbeat age is zero because no worker exists |
| `mdns-idle` | `mdns_phase=idle`; heartbeat age normally remains below 500 ms |
| `mdns-receive` | Usually `mdns_phase=waiting`; `mdns_select` increases about five times per second and `mdns_sent=0` |
| Full profiles | Usually `mdns_phase=waiting`; select count increases and announcements make `mdns_sent` nonzero |

`processing` and `announcing` are normally brief. If the last several
heartbeats remain in either phase while its heartbeat age grows, that boundary
is suspect.

## Decision Tree

- If `airplay-off` fails, stop. The result does not support AirPlay as the
  trigger; check build contamination or the shared runtime first.
- If `airplay-off` passes and `control-only` fails, investigate AirPlay
  integration/control-server startup, not mDNS.
- If `control-only` passes and `mdns-socket` fails, investigate local-address
  lookup, UDP bind, multicast membership, or socket resource limits.
- If `mdns-socket` passes and `mdns-idle` fails, investigate mDNS thread stack,
  core affinity, priority, or thread-resource exhaustion.
- If `mdns-idle` passes and `mdns-receive` fails, investigate `select`/`recv`,
  multicast traffic, and shared libnx network scheduling.
- If `mdns-receive` passes and `full-parallel` fails, investigate announcement
  send, DNS parsing, conflict handling, or response generation.
- If `full-parallel` fails and `full-serial` passes, concurrent protocol startup
  is the leading cause.
- If `full-parallel` fails and `full-low-priority` passes, scheduler starvation
  by the mDNS worker is the leading cause.
- If both serial and low-priority variants fail at the same mDNS phase, use that
  phase and its counters for the next focused code experiment instead of
  changing player or FFmpeg code.

## Distinguishing UI And Logger Failure

- UI moves and `X`/`B` work, but nxlink heartbeats stop: classify as a logger or
  nxlink transport failure.
- UI stops and nxlink heartbeats stop at the same observed time: classify as a
  runtime freeze; use the final delivered snapshot only as the last known state.
- UI stops while heartbeats continue: classify as render/input blockage, not a
  network logger failure.
- Heartbeats continue but IPTV/DLNA never produce a frame: classify as a player
  or media-network failure and use the media actor fields.

## Result Record

Copy one row per app launch. Do not combine retries in one row.

| Run | Profile | NRO SHA-256 | Marker correct | HB 10/20/30/40 s | `X`/`B` | IPTV first frame/result | DLNA first frame/result | Last mDNS phase/select/recv/sent | Visible freeze time | Last line before intentional exit | Result |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 01 | `airplay-off` |  |  |  |  |  |  |  |  |  |  |

Return these items for analysis:

- The unedited nxlink log for each run.
- The completed result rows.
- The exact time and input action visible when a freeze occurred.
- The NRO hash and diagnostic marker for every run.
- A short screen recording when the UI freezes but the log gives no matching
  timestamp.

The first failing boundary after a passing profile is more useful than a large
log from one full AirPlay build.
