# AirPlay Video Development

NX-Cast contains an independent, experimental AirPlay video receiver path. It
is designed for same-Wi-Fi iPhone-to-Switch video delivery and deliberately
does not attempt to implement AirPlay 2 multi-room audio, audio-only playback,
AWDL, DRM, MFi certification, or Apple platform services.

## Support Status

| Capability | Implementation | Release status |
|---|---|---|
| DNS-SD discovery | Native `_airplay._tcp` responder | Host-tested; iPhone acceptance pending |
| PIN Pair Setup / Pair Verify | SRP, X25519, Ed25519, persistent trust | Host-tested; iPhone acceptance pending |
| URL and HLS video | `/play`, `/rate`, `/scrub`, `/playback-info`, `/stop` routed to the existing player | Implemented; real iPhone/Switch validation pending |
| HLS redirects and relative segments | Passed directly to FFmpeg/libmpv | Host-tested without an NX-Cast proxy |
| H.264 mirror transport | Bounded receive, decrypt, Annex B reassembly and keyframe recovery | Internal path implemented |
| AAC mirror audio and A/V clock | RTP reorder, MPEG-TS mux and bounded clock correction | Internal path implemented |
| iPhone screen mirroring | Requires the remaining proprietary FairPlay unwrap boundary | Not advertised and not supported |
| AirPlay 2 multi-room/audio-only | Not planned | Unsupported |

The distinction matters: passing host protocol tests does not establish iOS
compatibility. Release notes must keep URL/HLS marked experimental until the
real-device matrix passes. Screen mirroring must remain unadvertised while the
FairPlay unwrap callback is absent.

## Architecture

```text
iPhone
  -> mDNS / DNS-SD
  -> persistent RTSP/HTTP server
  -> PIN pairing and verified session
  -> remote URL/HLS handlers ----------> player ownership -> renderer/libmpv
  -> mirror SETUP/RECORD
       -> H.264/AAC transport
       -> bounded MPEG-TS stream bridge -> libmpv -> nvtegra/deko3d
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

The Dockerfile installs `switch-libsodium` through `dkp-pacman`. It also
contains native Linux mbedTLS, libsodium, and FFmpeg development packages so
the same image can run the host suite before cross-compiling.

## Automated Validation

Run the deterministic host suite:

```bash
make test-airplay
```

It covers plist and RTSP bounds, published crypto vectors, pairing, DNS-SD,
receiver lifecycle, remote video controls, mirror H.264/AAC, MPEG-TS bridging,
clock behavior, player ownership, reconnects, and direct HLS redirect/relative
segment resolution. The same target also runs real loopback TCP/UDP smoke tests
for persistent RTSP, pairing authorization, mDNS and the composed receiver. CI
then performs the strict Switch build and package inspection.

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

Screen mirroring has a separate gate: no release may advertise mirroring until
the FairPlay unwrap boundary has a legally usable, independently audited
implementation and the H.264/AAC hardware path passes this matrix.

## Reference Boundary

UxPlay is used as the primary behavioral reference and RPiPlay only as a
legacy cross-check. NX-Cast does not vendor or compile either project. Protocol
parsers, security state, storage, networking, playback integration, and tests
are NX-Cast C implementations adapted to libnx and the existing player APIs.
