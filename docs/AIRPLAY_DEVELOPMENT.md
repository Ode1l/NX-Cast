# AirPlay Video Development

NX-Cast contains an independent, experimental AirPlay video receiver path. It
is designed for same-Wi-Fi iPhone-to-Switch video delivery and deliberately
does not attempt to implement AirPlay 2 multi-room audio, audio-only playback,
AWDL, HEVC mirroring, DRM, MFi certification, or Apple platform services. An
audio-first compatibility bridge is implemented because real video sessions
may negotiate audio before type-110 video; this is not a music-player claim.
Audio-only `RECORD` enables bounded audio ingress but does not claim the mirror
player or switch the UI to video loading.

## Support Status

| Capability | Implementation | Release status |
|---|---|---|
| DNS-SD discovery | Native `_airplay._tcp` responder | Host-tested; iPhone acceptance pending |
| PIN Pair Setup / Pair Verify | SRP, X25519, Ed25519, persistent trust | Host-tested; iPhone acceptance pending |
| URL and HLS video | `/play`, `/rate`, `/scrub`, `/playback-info`, `/stop` routed to the existing player actor | Implemented; real iPhone/Switch validation pending |
| Reverse HLS/FCUP | PTTH `/reverse`, serialized `POST /event`, correlated `/action`, and bounded playlist rewrite | Host-tested; real iPhone/Switch validation pending |
| HLS redirects and relative segments | Absolute URLs stay direct; sender-relative playlists use generation-bound loopback routes | Host-tested; hardware validation pending |
| H.264 mirror transport | Bounded receive, decrypt, Annex B reassembly and keyframe recovery | Internal path implemented |
| AAC/ALAC mirror audio and A/V clock | RTP reorder, Matroska mux and bounded clock correction | Internal path implemented |
| Audio-first video sessions | Audio-only ingress waits for type-110, which replaces the bridge and then claims mirror ownership/player loading | Host-tested; hardware validation pending |
| iPhone screen mirroring | GPL PlayFair key compatibility, H.264/audio transport, Matroska bridge, nvtegra/deko3d | Advertised experimentally; real iPhone/Switch acceptance pending |
| AirPlay 2 multi-room/music playback | Not planned | Unsupported |

The distinction matters: passing host protocol tests does not establish iOS
compatibility. Release notes must keep URL/HLS and screen mirroring marked
experimental until the real-device matrix passes. Mirroring is advertised only
when the built-in PlayFair backend or an external audited unwrap callback and
the required media callbacks are available.

## Architecture

```text
iPhone
  -> mDNS / DNS-SD
  -> persistent RTSP/HTTP server
  -> PIN pairing and verified session
  -> remote URL/HLS handlers
       -> direct absolute URL ----------> player ownership -> renderer/libmpv
       -> reverse FCUP + local playlist -> player ownership -> renderer/libmpv
  -> mirror SETUP/RECORD
       -> H.264/AAC transport
       -> bounded Matroska stream bridge -> libmpv -> nvtegra/deko3d
Audio may arrive before type-110 video and is buffered through an audio-only
bridge without a mirror player load. Type-110 promotion is the mirror ownership
and player-handoff boundary.
```

`source/protocol/airplay/integration.c` is the Switch composition root. It
starts only after network, player, and video rendering are ready. It owns the
receiver, remote-video controller, mirror runtime, libmpv stream bridge, and
the small UI status/PIN snapshot.

The player has one generation-bearing owner at a time: DLNA, IPTV, AirPlay
remote video, or AirPlay mirror. A new claim invalidates stale callbacks from
the previous protocol. On shutdown, AirPlay connections and workers stop
before DLNA, player/UI, logs, and network teardown.

## Storage And Privacy

AirPlay state is stored under:

```text
sdmc:/switch/NX-Cast/airplay/
```

`identity.bin` contains the device identity seed. `pairings.bin` contains
trusted-client records. They are generated at runtime, are private to the SD
card, and are intentionally excluded from release archives. Deleting both
files resets AirPlay identity and trusted pairings.

Logs may contain endpoint, state, sequence, length, and aggregate timing
metadata. They must never contain PINs, identity seeds, private/session keys,
complete pairing payloads, or decrypted media. Do not publish the AirPlay
storage directory with a bug report.

## Build Requirements

The Switch build uses official devkitPro `switch-libsodium` for Ed25519 and
existing `switch-mbedtls` for the other cryptographic primitives. Release
builds must fail rather than silently omit Ed25519:

```bash
make RELEASE_JOBS=4 release-build
```

This target records a build attestation only after all four strict feature
gates pass. `scripts/package_release.sh` requires that attestation. Developers
may set `NXCAST_ALLOW_UNVERIFIED_PACKAGE=1` only to inspect package layout from
a non-release build; such an archive must not be published.

The generic libsodium sysrandom backend attempts to open `/dev/urandom`, which
is not a libnx device. NX-Cast therefore installs a Switch-only libsodium
`randombytes_implementation` backed by libnx `randomGet()` before the first
`sodium_init()`. Do not move `sodium_init()` ahead of that registration or call
it directly elsewhere: libsodium treats failure to initialize its default
random source as fatal. `release-build` checks the final NRO for the
`libnx-kernel-chacha` implementation marker and records
`airplay-randombytes=libnx` in its attestation.

The pinned official `devkitpro/devkita64` image already contains
`switch-libsodium` as part of `switch-portlibs`. The Dockerfile verifies the
installed package locally with `dkp-pacman -Q`; it does not download packages
from devkitPro servers during CI. Native Linux mbedTLS, libsodium, and FFmpeg
development packages let the same image run the host suite before
cross-compiling.

## Automated Validation

Run the deterministic host suite:

```bash
make test-airplay
```

It covers plist and RTSP bounds, published crypto vectors, all four PlayFair
stage-one replies, bounded stage-two/key unwrap behavior, pairing, DNS-SD,
receiver lifecycle, logical-session reconnects, concurrent reverse sends,
remote FCUP/HLS, mirror H.264/audio, audio-first generation replacement,
Matroska bridging, clock behavior, and player ownership. The same target also
runs real loopback TCP/UDP smoke tests for persistent RTSP, pairing
authorization, mDNS, the composed receiver, and direct HLS redirect/relative
segment resolution. CI then performs the strict Switch build and package
inspection.

For redacted protocol/media traces:

```bash
make TRACE_AIRPLAY=1 TRACE_MEDIA=1 \
  NXCAST_USE_IMGUI_UI=1 \
  NXCAST_REQUIRE_LIBMPV=1 \
  NXCAST_REQUIRE_DEKO3D=1 \
  NXCAST_REQUIRE_AIRPLAY_ED25519=1 \
  -j4
```

## Real-Device Acceptance

The following remains mandatory before calling AirPlay URL/HLS supported:

1. Test at least two documented iPhone/iOS combinations on the same Wi-Fi.
2. Complete PIN pairing, reconnect, unpair, URL play, HLS play, pause, seek,
   resume, stop, return Home, and app exit.
3. Switch between DLNA, AirPlay, and IPTV in both directions and verify stale
   senders cannot control the new owner.
4. Repeat connect/disconnect at least ten times and run one 60-minute session.
5. Test malformed requests, Wi-Fi loss, app exit during pairing/loading, and a
   read-only or corrupted identity directory.
6. Confirm the final archive contains no identity, pairing, key, trace, dump,
   or packet-capture files.

Screen mirroring has a separate release claim gate: the current build may
advertise its experimental GPL compatibility path, but documentation must not
call it compatible or supported until the H.264/AAC hardware path passes this
matrix. Commercial FairPlay/DRM streams remain outside the implementation.

| Hardware case | Required observation | Status |
|---|---|---|
| Control Center screen mirror | PIN/reconnect, H.264 first frame, audio, 60-second run, disconnect to Home | Pending |
| App absolute URL cast | Load, pause/resume, seek, stop, reconnect | Pending |
| App relative HLS cast | Reverse upgrade, FCUP master/media responses, first frame, relative key/map/segment fetch | Pending |
| Audio-first negotiation | Audio starts or waits safely; type-110 replaces the bridge once, then mirror ownership/player loading proceeds | Pending |
| Reconnect/teardown | Ten cycles, Wi-Fi interruption, stop while loading, exit during connection | Pending |
| Protocol regression | DLNA, IPTV, AirPlay, then DLNA again in one process | Pending |

## Reference Boundary

UxPlay is used as the primary behavioral reference and RPiPlay only as a
legacy cross-check. NX-Cast does not vendor either project's server, pairing,
media, renderer, or platform integration. It vendors only UxPlay's GPL
PlayFair subset at commit `3ca7526387e894d6848b84c209de361c3bedd1ec`;
provenance and local changes are recorded in
`third_party/playfair/PROVENANCE.md`. Protocol parsers, security state,
storage, networking, playback integration, and tests remain NX-Cast C code.

UxPlay documents the legal status of PlayFair compatibility as unclear. This
research/homebrew integration is not Apple-authorized, is not MFi-certified,
does not support commercial FairPlay/DRM content, and may require a separate
legal review before redistribution in a particular jurisdiction.
