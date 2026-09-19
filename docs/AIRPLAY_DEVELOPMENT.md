# AirPlay Video Development

NX-Cast contains an independent, experimental AirPlay media receiver path. It
is designed for same-Wi-Fi iPhone-to-Switch delivery and deliberately does not
attempt to implement AirPlay 2 multi-room audio, a music-library UI, AWDL, HEVC
mirroring, DRM, MFi certification, or Apple platform services. RAOP audio-only
sessions are valid AirPlay media sessions, but NX-Cast currently accepts them
through a diagnostic sink instead of starting the player. If type-110 video
later arrives for the same protocol session, the normal mirror path creates a
combined bridge and starts playback. If audio recording cannot begin, the
handler rejects the setup/record transition with `461` and reports
`AirPlay audio setup error`.

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
| RAOP audio-only sessions | SETUP/RECORD and packet ingress are accepted and logged; standalone playback is not implemented | Diagnostic stub |
| Audio-first video sessions | A later type-110 stream promotes the session to the normal combined mirror path | Host-tested; hardware validation pending |
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
       -> reverse FCUP + local playlist -> ACTION_READY claim/activation
       -> player ownership -> renderer/libmpv
  -> mirror SETUP/RECORD
       -> H.264/AAC transport
       -> bounded Matroska stream bridge -> libmpv -> nvtegra/deko3d
Audio may form a complete RAOP session or arrive before type-110 video. Audio
RECORD enables ingress but does not claim player ownership; the diagnostic sink
logs the first packet and discards payloads. A later type-110 setup promotes the
session to the normal combined mirror path, which then claims ownership and
loads through the runtime worker queue. Reverse HLS `/play` establishes negotiation
without player ownership; the final `/action` `ACTION_READY` claims
`airplay-video`, marks the remote session active, and only then issues the
player load and play operations. Direct URL playback performs its optional
claim outside the state lock, then revalidates the session generation and
holds the state lock through load and play; a stop during the claim releases
the new owner and rejects the stale play request. After an active direct or
`ACTION_READY` session is established, ordinary HTTP or reverse connection
close does not imply media stop. Playback ends on explicit `/stop`, replacement
`/play`, player EOF/error, cross-protocol takeover, or AirPlay/application
shutdown. RAOP transport remains scoped to its RTSP connection and negotiated
stream types, while Apple Session ID remains authoritative for HTTP and
reverse identity. A same-resource AirPlay claim replaces the owner in place;
a DLNA or IPTV claim uses the
coordinator takeover callback to stop retained AirPlay playback and
synchronously release its owner before claiming the new protocol.
```

`source/protocol/airplay/integration.c` is the Switch composition root. It
starts only after network, player, and video rendering are ready. It owns the
receiver, remote-video controller, mirror runtime, libmpv stream bridge, and
the small UI status/PIN snapshot.

The video frontend uses libmpv's current-file readiness as a render epoch.
Until `FILE_LOADED` and the startup seek/restart gate complete, it presents the
loading layer instead of the previous deko3d surface and does not emit a new
first-frame marker.

The player has one generation-bearing owner at a time: DLNA, IPTV, AirPlay
remote video, or AirPlay mirror. A new claim invalidates stale callbacks from
the previous protocol. Cross-protocol takeover is explicit: the AirPlay
integration stops its current player path, releases the exact coordinator
lease, and clears its retained lease before the new protocol proceeds. On
shutdown, AirPlay connections and workers stop before DLNA, player/UI, logs,
and network teardown.

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
remote FCUP/HLS, action-ready claim and load-failure release, pending scrub
serialization, advisory FCUP status handling, reverse claim failure, active
close preservation and pending-close cancellation, condensed empty-parameter
expansion, audio record failure propagation, mirror ownership races, direct
claim cancellation, H.264/audio, audio-first generation replacement, Matroska
bridging, clock behavior, and player ownership. Coordinator takeover tests
cover same-resource replacement, AirPlay-to-DLNA/IPTV handoff, callback
failure, mirror takeover, and stale release races. The same target also runs
real loopback TCP/UDP smoke tests for persistent RTSP, pairing authorization,
mDNS, the composed receiver, and direct HLS redirect/relative segment
resolution. CI then performs the strict Switch build and package inspection.

For redacted protocol/media traces:

```bash
make TRACE_AIRPLAY=1 TRACE_MEDIA=1 \
  NXCAST_USE_IMGUI_UI=1 \
  NXCAST_REQUIRE_LIBMPV=1 \
  NXCAST_REQUIRE_DEKO3D=1 \
  NXCAST_REQUIRE_AIRPLAY_ED25519=1 \
  -j4
```

### App Video Negotiation Trace

Use `NX-Cast: Full Trace (DLNA+IPTV+AirPlay) Rebuild & Upload + nxlink server`
for one clean App-internal AirPlay attempt. Do not start the attempt from iOS
Control Center when diagnosing App video: Control Center may legitimately
negotiate an audio-only path.

After the sender disconnects, find the matching logical session in
`[airplay-flow-summary]`. Its `first_missing` value identifies the next boundary:

- `video-negotiation-request`: no type-110 `SETUP` and no HTTP `/play` arrived.
- `video-setup-request`: initial transport setup succeeded, but no video stream
  setup arrived.
- `video-setup-accept` or `record-accept`: NX-Cast rejected that control stage;
  inspect the preceding `[airplay-setup-failure]` and response status.
- `mirror-media-first-packet`: type-110 setup and `RECORD` succeeded; inspect
  `[airplay-video-pipeline]` for `first-config` and `first-keyframe`.
- `remote-media-handoff`: HTTP `/play` was accepted; inspect player load and
  FFmpeg/libmpv messages after the matching `[airplay-flow]` entry.
- `video-setup-request-audio-only`: the sender negotiated audio but did not
  request a video stream on this connection.

Capture one launch and one casting attempt per log. The summary contains only
IDs, booleans, counts, and status codes; URLs, request bodies, PINs, and key
material are not logged.

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
| RAOP audio and audio-first negotiation | Audio-only ingress is logged without player takeover; type-110 for the same session starts normal combined playback | Pending |
| Reconnect/teardown | Ten cycles, Wi-Fi interruption, stop while loading, exit during connection | Pending |
| AirPlay transport reconnect | Active playback survives ordinary control/reverse close; a new `/play` replaces it; DLNA/IPTV takeover stops and releases the old lease | Pending |
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
