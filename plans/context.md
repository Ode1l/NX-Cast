# Session Context

> Last Updated: 2026-07-23 NZST

## Current Task

Publish the accumulated AirPlay/DLNA resource-management and diagnostic work on
the `airplay` branch, then continue on macOS using the consolidated handoff in
`docs/MACOS_HANDOFF_2026-07-23.md`.

## Implementation State

- Profiles 1-14 isolate AirPlay control, mDNS socket/thread/receive behavior,
  BSD-session counts, discovery suspension, exclusive media ownership, and
  bounded observability.
- Profiles 13/14 use a global first-owner coordinator. The active protocol stack
  remains complete; non-owner receiver stacks stop and IPTV background network
  work quiesces before the player command proceeds.
- DLNA controller sessions now have explicit Stop handling plus an armed
  ten-second controller-silence Home recovery path.
- Runtime heartbeat v2, network summaries, resource snapshots, AirPlay setup and
  worker lifecycle counters, mirror pipeline counters, and DLNA libmpv samples
  are implemented and rate limited.
- The Windows `.vscode/tasks.json` and `.vscode/launch.json` changes remain local
  and uncommitted. They contain devkitPro MSYS/cygpath workarounds and must be
  recreated with native macOS shell commands if needed.

## Latest Verified Results

- Profile 5 (`mdns-receive`) was the first startup-matrix boundary where the UI
  stayed responsive but IPTV and DLNA both failed.
- BSD8 restored IPTV and DLNA connectivity but DLNA stuttered; BSD16 was worse.
- Suspending all discovery broke DLNA phone control and later rediscovery.
- Profile 13 made DLNA mostly usable but did not yet pass repeated mixed-protocol
  acceptance; phone Stop and AirPlay video remained open issues.
- The latest Profile 14 run had healthy resource/thread/socket convergence. Its
  four DLNA attempts failed on remote HTTP 514 before `file-loaded`, and its
  three AirPlay attempts negotiated unsupported audio-only ALAC (`ct=2`,
  `mirror=0`) and returned RTSP 461 at the format boundary.
- `ytdl_hook` errors in that run occurred after direct HTTP failure and are
  secondary evidence, not proof of an FFmpeg decoder regression.

## Current Blockers

- The latest DLNA source is unsuitable as the only playback baseline because its
  CDN returned HTTP 514. A stable local/LAN H.264/AAC item is required.
- The tested AirPlay session was audio only and used unsupported ALAC. Video must
  be tested with a sender/session that negotiates screen mirroring.
- Repeated successful-media Stop/Home/reconnect and the 60-minute mixed-protocol
  soak remain physical-device gates.

## macOS Next Actions

1. Install/verify the devkitPro Switch packages and Homebrew `mbedtls@2`,
   `libsodium`, and `ffmpeg` host dependencies.
2. Run the focused host suite and `make test-airplay`, then clean-build Profile
   14 using the commands in the macOS handoff.
3. Run the stable LAN DLNA five-cycle test before changing libmpv or FFmpeg.
4. A/B the same item against the Bilibili source and compare HTTP/Range/cache
   evidence.
5. Test AirPlay audio and screen mirroring separately, recording negotiation and
   the first non-advancing Profile 14 video-pipeline counter.
6. Complete repeated mixed-protocol restoration and soak gates before promoting
   the exclusive policy to the normal profile.
