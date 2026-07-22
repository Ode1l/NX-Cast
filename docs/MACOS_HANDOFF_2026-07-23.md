# macOS Handoff: AirPlay/DLNA Resource Diagnostics

> Snapshot date: 2026-07-23
>
> Branch: `airplay`
>
> Current diagnostic candidate: Profile 14, `full-owner-exclusive-observe-bsd12`

This document is the durable handoff for the AirPlay/DLNA playback investigation.
It summarizes the code now in the branch, the physical Switch results, what is
still unproven, and how to resume on macOS. Raw nxlink logs, signed media URLs,
compiled Switch files, and machine-specific VS Code configuration are
intentionally not part of the repository.

## Problem Statement

The original failure appeared only after enabling AirPlay: nxlink heartbeats
could stop, IPTV and DLNA could remain loading, and returning Home could crash.
The investigation separated AirPlay startup into control, mDNS socket, mDNS
thread, receive, announcement, scheduling, BSD-session, discovery-suspension,
exclusive-ownership, and bounded-observability profiles.

The evidence does not support “the Switch cannot decode while mDNS runs” as a
complete explanation. Receive-only mDNS exposed contention under the default
network configuration, but larger BSD pools alone did not make playback stable.
Stopping discovery indiscriminately improved some local playback operations but
broke remote control and rediscovery. The current design therefore manages whole
protocol stacks according to an explicit media owner and keeps the active
protocol complete.

## Implemented Architecture

### Exclusive protocol ownership

Profiles 13 and 14 use one first-owner lease across IPTV, DLNA, and AirPlay.
Pause retains the lease. Stop, idle, error, EOF, disconnect, or Home releases it.
The coordinator applies resource transitions asynchronously and does not submit
the next player open/play command until the requested resource mode is applied.

| Mode | IPTV core | IPTV background network | DLNA stack | AirPlay stack |
|---|---|---|---|---|
| Home | running | running | running | running |
| IPTV exclusive | running | suspended | stopped | stopped |
| DLNA exclusive | running | suspended | running | stopped |
| AirPlay exclusive | running | suspended | stopped | running |

This preserves DLNA SSDP/SOAP/event/control while DLNA owns playback and
preserves AirPlay control/media workers while AirPlay owns playback. It avoids
the Profile 11 failure mode where suspending both discovery systems made the
controller lose the renderer.

### Lifecycle and recovery hardening

- IPTV background fetches use an interruptible quiesce path before an exclusive
  resource mode is declared applied.
- AirPlay and DLNA start/stop operations are driven by the coordinator worker,
  with bounded transition waits outside shared locks.
- AirPlay mDNS, listener, client, timing, audio, mirror, and runtime workers have
  generation-aware lifecycle accounting.
- DLNA controller activity is tracked separately from player state. In Profiles
  13/14, four controller queries within three seconds arm a session; ten seconds
  of controller silence requests a return to Home. An explicit remote Stop uses
  the immediate terminal path.
- AirPlay audio-first and mirror-first setup both construct and clean up the
  media bridge without relying on setup order.

### Bounded diagnostics

Runtime heartbeat v2 is emitted every two seconds using compact tuples. Network
summaries are emitted every ten seconds, while slow-operation checks still run
every two seconds. Profile 14 adds event-driven or rate-limited records for:

- AirPlay setup `ct/spf/sr` and categorized failure stages;
- AirPlay worker created/joined/live/generation counts;
- mirror accept, encrypted input, decrypt, H.264 configuration/access units,
  bridge writes, and libmpv-facing bytes;
- DLNA libmpv cache, demux, HTTP/Range, seek, and video state while loading,
  buffering, or seeking;
- thread, heap, and owned-socket snapshots at claim, load, Stop, END_FILE, and
  Home restoration.

The detailed field contract and repeatable device procedure remain in
[`AIRPLAY_FREEZE_DIAGNOSTICS.md`](AIRPLAY_FREEZE_DIAGNOSTICS.md).

## Diagnostic Profile Results

These are physical observations supplied during the investigation. Timings are
approximate and are meaningful only as comparisons within the same network and
sender setup.

| ID | Profile | Result |
|---:|---|---|
| 1 | `airplay-off` | UI and X/B stayed responsive at 10/20/30/40 s. IPTV first frame was about 1 s; DLNA about 9 s. |
| 2 | `control-only` | UI and X/B stayed responsive. IPTV was about 9 s; DLNA about 1 s. |
| 3 | `mdns-socket` | Normal UI, Home return, and shutdown; no crash. |
| 4 | `mdns-idle` | UI and X/B stayed responsive. IPTV was about 5 s; DLNA about 1 s. |
| 5 | `mdns-receive` | UI remained responsive, but both IPTV and DLNA failed to play. This isolated active multicast receive as the first failing boundary in that matrix. |
| 8 | `full-low-priority` | nxlink stayed connected, but priority reduction alone did not establish reliable playback. |
| 9 | `mdns-receive-bsd8` | UI/X/B were normal and IPTV was fast. DLNA played but stuttered about once per second; multiple files and seek remained unstable. |
| 10 | `mdns-receive-bsd16` | DLNA stutter was worse than BSD8. More BSD sessions were not the solution. |
| 11 | `full-discovery-suspend-bsd8` | One DLNA retry played and local Switch seek was smooth, but phone control was lost. After exit, AirPlay and DLNA discovery did not recover. |
| 12 | `full-mdns-playback-suspend-bsd8` | IPTV was good and DLNA control remained available, but DLNA play and seek were frequently unreliable. |
| 13 | `full-owner-exclusive-bsd12` | DLNA became mostly good with occasional small stutters. AirPlay reached audio without video in one run. Phone-side DLNA termination sometimes failed to leave the Switch player. |
| 14 | `full-owner-exclusive-observe-bsd12` | Latest run showed healthy resource convergence, but the tested DLNA source returned HTTP 514 before `file-loaded`; AirPlay negotiated unsupported audio-only ALAC and returned RTSP 461. |

## Latest Profile 14 Interpretation

The latest complete device run contained four DLNA attempts and three AirPlay
attempts.

### DLNA

All four Bilibili CDN requests returned `HTTP 514 Frequency Capped` before
libmpv emitted `file-loaded`. The later `ytdl_hook` messages were secondary
fallback errors after direct HTTP loading had already failed. A one-byte Range
request from the development PC later received HTTP 206, so the signed URL was
not simply expired. The remaining hypotheses include per-client CDN behavior,
request headers, retry/frequency policy, or sender-specific URL generation.

This run does **not** prove an FFmpeg decoder regression and does **not** prove
that the exclusive coordinator caused DLNA failure. Retest with a known local
HTTP file or stable LAN DLNA source before changing the player.

### AirPlay

Pairing, FairPlay, timing, and RTSP setup reached media negotiation. Every
attempt reported:

```text
streams=1 mirror=0 audio=1
ct=2 spf=352 sr=44100
```

`ct=2` is ALAC. NX-Cast currently accepts audio type 4 (AAC-LC) and type 8
(AAC-ELD), so setup was correctly categorized as `stage=format` and returned
RTSP 461. This same setup shape was already rejected in a Profile 13 log, so it
is not introduced by Profile 14 diagnostics.

Because the sender negotiated `mirror=0`, this was an audio-only connection.
Adding ALAC would enable this audio format, not video. AirPlay video requires
the sender to choose screen mirroring so the mirror TCP/decrypt/H.264 bridge is
used, or a future implementation of the non-mirroring remote-video/HLS path.

### Resource health

The run contained no resource-transition failure, network stall, socket
accounting underflow/overflow, or worker-creation failure. Home restoration
returned to the expected three idle AirPlay workers, stable thread-slot range,
four expected service sockets, and non-monotonic heap usage. This is strong
evidence that no obvious accumulated receiver-thread/socket leak caused those
specific failures, but a longer successful-media soak is still required.

## macOS Setup and Commands

Install the devkitPro Switch toolchain and the project packages first. Confirm
that `DEVKITPRO`, `DEVKITA64`, the cross compiler, libnx, libmpv/deko3d, FFmpeg,
and UAM packages are visible in the same shell used by VS Code.

Typical shell environment:

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITA64="$DEVKITPRO/devkitA64"
export PATH="$DEVKITA64/bin:$DEVKITPRO/tools/bin:$PATH"
```

Clean Profile 14 build from the repository root:

```bash
make clean
make NXCAST_DIAG_PROFILE=full-owner-exclusive-observe-bsd12 \
  NXCAST_USE_IMGUI_UI=1 \
  NXCAST_REQUIRE_LIBMPV=1 \
  NXCAST_REQUIRE_DEKO3D=1 \
  NXCAST_REQUIRE_AIRPLAY_ED25519=1 \
  -j"$(sysctl -n hw.logicalcpu)"
```

For host tests, the Makefile discovers Homebrew packages using `brew --prefix`:

```bash
brew install mbedtls@2 libsodium ffmpeg
make test-airplay
```

If a local package is in a nonstandard prefix, set `HOST_MBEDTLS_PREFIX`,
`HOST_SODIUM_PREFIX`, or `HOST_FFMPEG_PREFIX` explicitly rather than changing
the Makefile.

## Local VS Code Task Contract

`.vscode/tasks.json` and `.vscode/launch.json` are deliberately not included in
this handoff commit. The current working copy contains Windows devkitPro MSYS
paths, `cygpath` short-path handling, and a Windows Bash executable; those
commands are not portable to macOS.

If VS Code tasks are wanted on macOS, recreate them locally with `/bin/bash`
and repository-root `make` commands. The useful local contract is:

- one clean/build/upload task using the strict feature flags shown above;
- one input picker containing all values in `NXCAST_DIAG_PROFILES` in the
  Makefile, defaulting to `full-owner-exclusive-observe-bsd12`;
- one diagnostic task that performs a clean build with
  `NXCAST_DIAG_PROFILE=${input:nxcastDiagProfile}` and then runs nxlink;
- one launch entry whose `preLaunchTask` is that diagnostic task.

Do not copy the Windows drive-prefixed Bash executable, `cygpath`, `TOPDIR`, or
`THIS_MAKEFILE` workarounds into the macOS configuration unless a new path issue
proves they are necessary.

## Next Test Sequence

1. Build Profile 14 and verify the exact startup marker before testing.
2. Serve one known H.264/AAC file over a stable LAN HTTP server and send it by
   DLNA five times in one NX-Cast launch. Include phone pause/resume, two seeks,
   and phone Stop on every cycle.
3. Run the same sequence against the Bilibili source. Compare HTTP status,
   Range/restart state, first-frame time, and cache samples; do not compare two
   different files as if they were an A/B pair.
4. After Home restoration, verify AirPlay and DLNA rediscovery, then run IPTV,
   return Home, and repeat the stable DLNA item to expose residual state.
5. Test both AirPlay audio casting and iPhone screen mirroring. Record the
   `streams/mirror/audio` tuple and `ct/spf/sr`. Treat ALAC audio support and the
   mirror video path as separate work items.
6. Only after stable media succeeds, run repeated mixed-protocol transitions and
   a 60-minute soak. A failed CDN request cannot validate resource cleanup under
   active decoding.

## Open Work

- Add or deliberately decline ALAC (`ct=2`) decoding support.
- Establish why the tested Bilibili URL returns HTTP 514 from Switch/libmpv and
  whether request headers or retry behavior differ from the successful PC Range
  request.
- Verify the mirror video pipeline with a sender that actually negotiates
  `mirror=1`; use Profile 14 counters to find the first non-advancing stage.
- Confirm phone Stop always produces DLNA terminal recovery after repeated
  successful plays, not only after source-load failures.
- Promote exclusive ownership beyond diagnostic Profiles 13/14 only after the
  stable-source mixed-protocol and soak gates pass.
