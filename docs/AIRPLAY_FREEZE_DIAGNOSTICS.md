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

Heartbeat version 2 uses compact grouped tuples. Semantic names such as
`discovery_suspended`, `mdns_phase`, and `resource_applied` used throughout
this playbook refer to the tuple positions documented in **Heartbeat Fields**;
event-specific coordinator logs retain their descriptive field names.

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
| 9 | `mdns-receive-bsd8` | 9 | Same receive-only mDNS path as profile 5, but initializes libnx with eight BSD service sessions | Does the default BSD session pool serialize or starve concurrent network users? |
| 10 | `mdns-receive-bsd16` | 10 | Same as profile 9 with sixteen BSD service sessions | Is residual DLNA instability caused by an eight-session capacity limit? |
| 11 | `full-discovery-suspend-bsd8` | 11 | Full AirPlay with eight BSD sessions; mDNS and SSDP polling/announcements sleep while any media owner is active | Does discovery traffic cause the remaining playback stalls? |
| 12 | `full-mdns-playback-suspend-bsd8` | 12 | Full AirPlay with eight BSD sessions; only AirPlay mDNS sleeps while the player is loading, buffering, seeking, playing, or paused | Can playback stay smooth without breaking DLNA discovery/control or leaving AirPlay discovery suspended after stop/error? |
| 13 | `full-owner-exclusive-bsd12` | 13 | Borealis-like 12 BSD sessions and buffer efficiency 8; the first IPTV/DLNA/AirPlay media owner stops complete non-owner receiver stacks and pauses IPTV background fetching | Does explicit global ownership make every protocol reliable while preserving the active protocol's controls and deterministic Home recovery? |
| 14 | `full-owner-exclusive-observe-bsd12` | 14 | Runtime behavior is identical to ID 13; adds bounded AirPlay/DLNA/video/thread/socket/memory observations | At which exact setup, stream, player, or resource boundary does a failed or degraded run diverge? |

The iPhone must not discover NX-Cast in `control-only`, `mdns-socket`,
`mdns-idle`, `mdns-receive`, `mdns-receive-bsd8`, or `mdns-receive-bsd16`.
That is expected and is not a failed round.

## Playback-Time Discovery Suspension Experiments

### ID 11 comparison: ownership-driven mDNS and SSDP

Use `full-discovery-suspend-bsd8` only after the earlier boundary tests. Unlike
the receive-only profiles, both AirPlay and DLNA must be discoverable while the
Home view is idle. The AirPlay RTSP server, DLNA HTTP/SOAP/event services,
nxlink, discovery sockets, and worker threads remain alive throughout the test;
only mDNS/SSDP receive and announcement work sleeps during owned playback.

ID 11 intentionally follows media ownership, not the visible player state.
NX-Cast retains an owner and URI after some stop/Home transitions for replay and
remote control, so ID 11 can remain suspended after returning Home. It also
suspends SSDP, which can make a DLNA controller drop the renderer. Preserve this
behavior as the comparison baseline; do not use it as the corrected candidate.

Run these checks in one launch, returning Home between protocols:

1. On Home, confirm the iPhone sees NX-Cast in AirPlay and the DLNA sender sees
   NX-Cast. The heartbeat must report `discovery_suspended=0`,
   `mdns_suspended=0`, and `ssdp_suspended=0`.
2. Start the same IPTV channel used in the BSD8 baseline. Confirm a coordinator
   line contains `discovery_suspended=0->1`; within one heartbeat mDNS should be
   `mdns_phase=suspended`, and `mdns_select`, `mdns_recv`, and `mdns_sent` should
   stop changing. Record first-frame time and continuous playback for 30 seconds.
3. Pause/resume and seek once. Discovery must remain suspended. Press `B` and
   record whether `discovery_suspended` remains one because the owner is retained;
   an absent `1->0` edge is an expected limitation of ID 11.
4. Send the exact same DLNA videos used in the BSD8/BSD16 rounds. Repeat the
   30-second, pause/resume, and seek checks. Record whether the prior one-second
   stutter disappears. Return Home and record discovery/control loss while SSDP
   remains suspended.
5. Start AirPlay from Home. Discovery stays active long enough for the sender to
   connect, then suspends only after AirPlay claims media ownership. Confirm the
   existing RTSP session and player continue despite mDNS sleeping. Stop casting
   and record whether retained ownership prevents rediscovery.

### ID 12 candidate: player-driven AirPlay mDNS only

Build and select `full-mdns-playback-suspend-bsd8`. SSDP must stay online for
the entire run. AirPlay mDNS is suspended only while the latest player snapshot
is `loading`, `buffering`, `seeking`, `playing`, or `paused`. A `stopped`,
`idle`, or `error` snapshot resumes mDNS immediately even if the coordinator
still reports a DLNA/IPTV/AirPlay owner and the player retains its media URI.
The existing mDNS worker handles the resume edge by sending fresh announcements.

Run these checks in one launch:

1. Verify the startup marker contains `profile_id=12` and
   `profile=full-mdns-playback-suspend-bsd8`. On Home, confirm both AirPlay and
   DLNA discovery. The heartbeat must show `discovery_suspended=0`,
   `mdns_suspended=0`, and `ssdp_suspended=0`.
2. Send the same DLNA videos used for the BSD8/BSD16 and ID 11 runs. During
   loading/playback, expect `discovery_suspended=0->1`,
   `mdns_suspended=1`, `mdns_phase=suspended`, and `ssdp_suspended=0`.
   Test the phone's pause/resume, seek, and stop controls while playback runs;
   record stutter and first-frame time. SSDP and DLNA SOAP control must remain
   available.
3. Press `B` or stop from the phone. As soon as the player reports `stopped`,
   `idle`, or `error`, expect `discovery_suspended=1->0` even if the heartbeat
   still reports `owner=dlna`. Confirm mDNS returns to `waiting`, `mdns_sent`
   increases after a fresh announcement, and AirPlay becomes discoverable again.
4. Play IPTV for 30 seconds, then pause/resume and seek. Expect the same mDNS
   suspension edges, smooth playback, and `ssdp_suspended=0`. Press `B` and
   verify the immediate resume edge while any retained IPTV owner is harmless.
5. Start AirPlay from Home. mDNS remains active through discovery and connection,
   then sleeps once the player enters an active state. Stop casting and verify
   the `1->0` transition, fresh announcement, and successful rediscovery. If
   playback enters `error`, perform the same recovery checks without restarting
   NX-Cast.
6. Throughout the run, check Home/video UI, `X`/`B`, the two-second heartbeat,
   and normal application shutdown.

If the coordinator reports suspension but `mdns_suspended` remains zero, or if
`ssdp_suspended` ever becomes one in ID 12, classify it as wiring failure. If
`discovery_suspended=1`, `mdns_suspended=1`, and `ssdp_suspended=0` but mDNS
counters keep increasing after one heartbeat, classify it as worker
acknowledgement failure. If the counters stop and playback becomes smooth,
discovery contention is the leading cause. If playback still stalls, discovery
traffic is not sufficient to explain the player regression and the next
experiment should target active media socket scheduling rather than increasing
BSD sessions again.

### ID 13 candidate: exclusive protocol ownership

Select `full-owner-exclusive-bsd12` in the VS Code diagnostic profile picker.
Profile 14 is now the picker default; select ID 13 explicitly when collecting
the behavior-only baseline. The startup log must contain all of these values:

```text
profile_id=13 profile=full-owner-exclusive-bsd12
bsd_sessions=override:12 sb_efficiency=override:8
```

ID 13 replaces discovery-only suspension with a global owner state machine.
The first protocol family that claims media wins. A different protocol is
rejected until Stop, idle, error, EOF, disconnect, or Home releases the lease.
Pause retains the lease. Player OPEN/PLAY is not submitted until the following
resource set is fully applied:

| Mode | IPTV core | IPTV refresh/logo network | DLNA SSDP/HTTP/SOAP/event | AirPlay mDNS/control/mirror receiver |
|---|---|---|---|---|
| `home` | running | running | running | running |
| `iptv-exclusive` | running | suspended | stopped | stopped |
| `dlna-exclusive` | running | suspended | running | stopped |
| `airplay-exclusive` | running | suspended | stopped | running |

The active remote protocol remains complete: DLNA playback keeps SSDP, SOAP,
eventing, pause/resume, seek, and Stop; AirPlay playback keeps its control and
media receiver. Only the non-owner receiver is shut down. IPTV background
downloads use an FFmpeg interrupt callback and must report quiesced before the
resource mode can become applied.

Run this sequence in one launch:

1. On Home, confirm both senders discover NX-Cast. Wait for two heartbeats and
   verify `resource_desired=home`, `resource_applied=home`, no transition
   failure, and no socket underflow.
2. Send the baseline DLNA video five times, using phone Stop between attempts.
   On each attempt record first-frame time, 30 seconds of continuous playback,
   one pause/resume, and two seeks. During playback require
   `resource_applied=dlna-exclusive`; AirPlay service, mDNS, and AirPlay control
   socket counts must reach zero. Phone DLNA control must keep working.
3. Stop from the phone and return Home. Require `resource_applied=home`, fresh
   AirPlay announcements, AirPlay rediscovery, and a running DLNA renderer.
   Repeat the DLNA play/Stop cycle once more after restoration; this catches the
   former “first play succeeds, later plays fail” lifecycle leak.
4. Play IPTV for 30 seconds, pause/resume, seek twice, and return Home. Require
   `resource_applied=iptv-exclusive` while active; mDNS, SSDP, DLNA HTTP, and
   AirPlay control socket counts must all reach zero. Home must restore both
   senders before the next step.
5. Start AirPlay video or mirroring, play for 30 seconds, then disconnect and
   reconnect once. Require `resource_applied=airplay-exclusive`; SSDP and DLNA
   HTTP socket counts must reach zero while AirPlay control stays active.
   Disconnect must restore Home and DLNA discovery without restarting NX-Cast.
6. Finish with another DLNA play/seek/Stop cycle, exercise `X`/`B`, then exit
   normally. The final shutdown must not hang or crash.

Use Profiles 9, 12, and 13 as the shortest A/B comparison. Keep the exact same
DLNA files, IPTV channel, sender, Wi-Fi position, and 30-second observation
window. Profile 9 is the BSD8 receive baseline, Profile 12 is the mDNS-only
suspension control, and Profile 13 tests complete owner isolation with the
12-session Borealis baseline. Do not mix retries from different app launches
in one result row.

ID 13 passes only if all repeated plays and seeks work, the active sender keeps
control, every terminal edge restores Home discovery, and the UI/heartbeat and
normal exit remain alive. Any of the following is a failure even if video plays:

- `resource_failed=1`, or desired/applied modes differ for more than the
  six-second handoff bound;
- `[network-stall]` remains after the corresponding service has stopped;
- `underflow` or active-slot `overflow` becomes nonzero;
- a stopped non-owner subsystem retains a nonzero `fd` or active operation;
- IPTV background reports active after an exclusive mode is applied;
- a second or later DLNA/AirPlay attempt fails until the app is restarted.

Remote HTTP 514/554 responses or libmpv `ytdl_hook` failures are media-source
failures, not proof of a local resource transition failure. Classify them
separately unless the same timestamp also shows `resource_failed=1`, a desired/
applied mismatch beyond six seconds, a persistent stopped-subsystem fd, or an
active `[network-stall]` in NX-Cast's owned network paths.

### ID 14 observation build: same behavior, precise failure boundaries

Run the VS Code task
`NX-Cast: AirPlay Diagnostic Profile Rebuild + Nxlink Upload + Server` and
choose `full-owner-exclusive-observe-bsd12` (the picker default). Confirm the
startup marker contains:

```text
profile_id=14 profile=full-owner-exclusive-observe-bsd12
bsd_sessions=override:12 sb_efficiency=override:8
```

ID 14 deliberately inherits every resource and player behavior flag from ID
13. It does not add an AirPlay codec, alter FFmpeg/libmpv options, change the
BSD pool, or change protocol ownership. Therefore use ID 13 and ID 14 with the
same media as an A/B pair; a repeatable playback difference between the two is
diagnostic overhead and must be reported as such.

Capture one complete launch containing this sequence:

1. Wait on Home for two heartbeats, then send the baseline DLNA video. Let it
   play for 30 seconds, seek twice, stop from the phone, and repeat with at
   least two other videos. Finish with the baseline video once more to expose
   accumulated state.
2. Play and seek the baseline IPTV stream, then return Home.
3. Connect AirPlay, attempt a video or mirror stream for 30 seconds, and stop
   from the phone. If only audio works, leave the connection active long
   enough to collect three video-pipeline samples. Reconnect once.
4. Finish with one more DLNA play/seek/phone-Stop cycle, return Home, and exit
   NX-Cast normally. Do not restart the app between steps.

The added records are intentionally event driven or rate limited:

| Prefix | Meaning | Healthy expectation |
|---|---|---|
| `[airplay-setup]` / `[airplay-setup-failure]` | Negotiated audio `ct/spf/sr`, video connection ID, and categorized setup failure | Supported audio reports negotiation; failures name one of `format`, `socket-data`, `socket-control`, `thread-create`, `response-plist`, `runtime-state`, or `bridge-config` |
| `[airplay-thread]` | `created`, `joined`, `live`, failure, underflow, and generation for mDNS/listener/client/timing/audio/mirror/runtime workers | After disconnect and Home restoration, terminated session roles have equal created/joined counts and zero live count |
| `[airplay-video-pipeline]` | Mirror accept, encrypted input, decrypt, video configuration/access units, bridge writes, and libmpv-facing bytes | Counters advance in order: accept → decrypt → config/AU → bridge; the first non-advancing stage identifies the video break |
| `[dlna-player-diag]` | Once-per-second sample only while DLNA is loading, buffering, or seeking | Cache bytes/duration recover, `underrun` does not stay one, Range/seek reaches `restart`, and dropped-frame counts do not grow continuously |
| `[resource-snapshot]` | Claim, next-loadfile, Stop, END_FILE, or replaced-END_FILE boundary | App live threads and owned sockets return toward the prior Home values; heap/memory do not grow monotonically across repeated plays |

`unknown` means the installed libmpv or Horizon version did not expose that
property; it is not itself a failure. HTTP and Range fields are best-effort
metadata from libmpv log events and never include the media URL. The video
`decoded` count is currently reported as `unknown` because this libmpv build
does not expose a stable decoded-frame counter; track, format, hardware decode,
estimated presented frames, and both drop counters are still recorded.

For an AirPlay audio-only result, find the earliest stalled video counter:

- no accepted mirror connection: control negotiation succeeded but the sender
  did not open the mirror data channel;
- accepted bytes but decrypt failures rise: session key/IV or encrypted packet
  handling failed;
- decrypt succeeds but config/AU stays zero: the sender format or packet parser
  is the boundary;
- AU rises but bridge bytes do not: MPEG-TS bridge/mux is the boundary;
- bridge bytes rise while libmpv never loads video: the custom stream/demux/
  decoder/render path is the boundary.

NX-Cast can run AirPlay video through its mirror TCP → decrypt → H.264 access
unit → MPEG-TS bridge → libmpv/deko3d path. ID 14 observes that existing path;
it does not claim compatibility with every sender format. Preserve the entire
nxlink log from startup through shutdown so counters from separate generations
can be paired correctly.

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

`[runtime-heartbeat] v=2` remains a two-second liveness signal. Its grouped
tuples avoid repeating a field name for every value:

| Group | Ordered values |
|---|---|
| `t`, `f`, `ui` | monotonic time, main/render frame, active view |
| `p` | player state / has media / return-Home pending |
| `proto` | coordinator state |
| `own` | media owner / generation |
| `res` | desired mode / applied mode / transition active / transition failed / transition age ms |
| `svc` | IPTV background suspended / IPTV worker busy / discovery suspended / IPTV transition worker:age ms / DLNA transition worker:age ms / AirPlay transition worker:age ms |
| `media` | queue depth / high watermark / command id / command kind / producer / token / generation / command age ms / actor heartbeat age ms / rejected-full / rejected-stale / timed-out / coalesced / accepting |
| `log` | queue depth / high watermark / worker heartbeat age ms / queue dropped / mirror dropped / mirror failures |
| `mdns` | phase / running / suspended / socket open / heartbeat age ms / select count / receive count / sent count |
| `ssdp` | suspended |

`[network-heartbeat] v=2` is emitted approximately every ten seconds. Each
named subsystem uses this ordered tuple:

```text
open-fd / active-operations / oldest-operation:age-ms / heartbeat-age-ms /
maximum-operation-ms / last-error:last-error-operation / slot-overflow /
socket-close-underflow
```

The network snapshot never contains URLs or packet bodies. Network snapshots
are lower frequency, but the slow-operation scan still runs with every
two-second runtime heartbeat. `[network-stall]` therefore remains immediate
once an operation exceeds 1500 ms.

Expected mDNS values by profile:

| Profile | Expected steady state |
|---|---|
| `airplay-off`, `control-only` | `mdns_phase=stopped`, no socket, no worker |
| `mdns-socket` | `mdns_phase=socket-ready`, socket open; mDNS heartbeat age is zero because no worker exists |
| `mdns-idle` | `mdns_phase=idle`; heartbeat age normally remains below 500 ms |
| `mdns-receive` | Usually `mdns_phase=waiting`; `mdns_select` increases about five times per second and `mdns_sent=0` |
| `mdns-receive-bsd8` | Same as `mdns-receive`, plus startup reports `bsd_sessions=override:8` |
| `mdns-receive-bsd16` | Same as `mdns-receive`, plus startup reports `bsd_sessions=override:16` |
| `full-discovery-suspend-bsd8` on Home | Full-profile values with `bsd_sessions=override:8` and all suspension fields zero |
| `full-discovery-suspend-bsd8` during owned media | `discovery_suspended=1`, both worker flags are one, `mdns_phase=suspended`, and mDNS counters remain stable |
| `full-mdns-playback-suspend-bsd8` on Home/stopped/idle/error | Full-profile values with `bsd_sessions=override:8`; all suspension fields are zero and a resume edge sends fresh mDNS announcements |
| `full-mdns-playback-suspend-bsd8` during loading/buffering/seeking/playing/paused | `discovery_suspended=1`, `mdns_suspended=1`, `ssdp_suspended=0`, `mdns_phase=suspended`, and mDNS counters remain stable |
| `full-owner-exclusive-bsd12` on Home | `resource_desired=home`, `resource_applied=home`, all enabled receiver services running, `bsd_sessions=override:12`, `sb_efficiency=override:8` |
| `full-owner-exclusive-bsd12` during media | Desired/applied owner mode match; non-owner receiver `fd=0,active=0`; `iptv-background active=0`; no persistent transition failure |
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
- If `mdns-receive` fails and `mdns-receive-bsd8` passes, the default libnx
  BSD service-session pool is the leading cause; validate the larger pool with
  full AirPlay before promoting it to the normal build.
- If BSD8 restores connectivity but BSD16 alone removes repeated media stalls,
  the session pool still lacks capacity. If both variants stall, stop increasing
  the pool and investigate blocking socket calls and service-thread design.
- If `mdns-receive` passes and `full-parallel` fails, investigate announcement
  send, DNS parsing, conflict handling, or response generation.
- If `full-parallel` fails and `full-serial` passes, concurrent protocol startup
  is the leading cause.
- If `full-parallel` fails and `full-low-priority` passes, scheduler starvation
  by the mDNS worker is the leading cause.
- If both serial and low-priority variants fail at the same mDNS phase, use that
  phase and its counters for the next focused code experiment instead of
  changing player or FFmpeg code.
- Treat `full-discovery-suspend-bsd8` as an isolation result only: it may improve
  playback while breaking DLNA rediscovery/control and can remain suspended when
  ownership is retained. Use ID 12 to decide whether mDNS-only, player-driven
  suspension preserves the playback improvement without those regressions.
- If ID 12 keeps IPTV/DLNA smooth, phone DLNA control works, SSDP remains zero,
  and mDNS recovers on every stopped/idle/error edge, promote this policy beyond
  the diagnostic profile. If playback still stalls with `mdns_suspended=1`, the
  remaining bottleneck is in active media/control sockets or decoder scheduling,
  not Home rendering or mDNS discovery polling.

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
