# Miracast And Google Cast Development Preparation

Status: planning and feasibility research only, 2026-10-09. Neither protocol
is currently implemented in NX-Cast. Miracast is the first research priority;
Google Cast is a candidate, not a promised release feature. FCast and DIAL are
deferred until there is a concrete sender/use case worth supporting.

## Product Goal And Scope

Miracast would let a compatible Windows or Android sender use the Switch as a
wireless display. Google Cast would add a different sender ecosystem for media
casting; URL playback and screen mirroring must be evaluated separately.
Chromecast is a product family; Google Cast is the protocol/ecosystem discussed
here. Macast is a DLNA application, not another name for Miracast.

Initial scope is authorized, unprotected video and audio, existing nvtegra
hardware decoding, deko3d presentation, reliable disconnect/reconnect and
coexistence with DLNA, AirPlay and IPTV. Do not promise all phones, all apps,
4K, DRM playback or certified receiver compatibility. Do not add website-specific
branches to compensate for an incomplete generic protocol implementation.

## Evidence And Unknowns

| Area | Verified reference or current fact | What remains unverified |
|---|---|---|
| Miracast reference | MiracleCast has a display-sink implementation; it relies on Linux services, wpa_supplicant and GStreamer. | Which pieces are portable, and whether Horizon exposes the required Wi-Fi operations. |
| Windows infrastructure path | Microsoft's infrastructure extension carries media over an existing network but still describes Wi-Fi Direct discovery. | Whether an unmodified target sender can discover and negotiate with a Switch implementation. |
| Switch networking | NX-Cast already uses libnx sockets for existing protocols. | WFD advertisements, P2P group formation, permissions and simultaneous infrastructure/P2P operation. Ordinary TCP sockets do not prove these capabilities. |
| Google Cast | Open Screen contains Cast discovery/control/streaming code and device authentication verification. | Whether stock target senders accept a receiver identity we can legitimately provision. |
| Google receiver SDK | Google's Web Receiver model runs receiver apps on Cast devices. | It is not evidence that an arbitrary Horizon executable can become a certified Cast device. |
| Media integration | NX-Cast has a media actor, ownership leases and separate control/media session state. | Supported negotiated profiles, transport adaptation and resource use for either new protocol on real hardware. |

A narrow inspection of installed libnx `nifm.h`, `wlaninf.h` and `ldn.h` found
no explicitly named P2P/WFD entry points. This is only a search result, not a
proof that Miracast is impossible. Nintendo local-wireless APIs must not be
assumed to implement interoperable Wi-Fi Direct just because they are wireless.

## Miracast Stages

| Stage | Work | Exit evidence |
|---|---|---|
| M0: platform feasibility | Map discovery, WFD information elements, P2P setup, addressing and teardown onto actual Horizon/libnx facilities. Evaluate Windows infrastructure transport separately. | Record usable APIs/permissions and demonstrate discovery plus session connection from an unmodified sender on Switch. A desktop-only prototype does not pass this gate. |
| M1: bounded control session | Implement the required Wi-Fi Display RTSP negotiation, capability selection, keepalive and teardown in C. Advertise only verified media capabilities. | One sender negotiates repeatedly; reject unsupported capabilities cleanly; disconnect releases its sockets without stopping another owner's media. |
| M2: video transport | Adapt the negotiated media transport, such as RTP carrying MPEG-TS, to the existing media pipeline. Validate sequencing, framing and timestamps before blaming decoding. | Hardware-decoded first frame, stable 720p/1080p baseline, portrait/landscape changes and bounded cancellation. Record packet and decoder evidence. |
| M3: audio and lifecycle | Add the negotiated supported audio format and clock synchronization; integrate Home, reconnect and ownership takeover. | Audible synchronized playback and repeated connect/disconnect with healthy UI, logs and existing protocols. |
| M4: experimental promotion | Enable only after the device matrix below passes; document exact tested senders and limitations. | Measured resource use and regression results, not only a discovery screenshot. |

If M0 cannot provide a supported connection path, stop integration and document
the missing facility. A required sysmodule, custom firmware service, Linux boot
or external adapter changes the product requirements and needs a separate
decision; do not quietly make it a dependency. Do not import MiracleCast's
Linux runtime or GStreamer merely to obtain its protocol logic.

## Google Cast Stages

| Stage | Work | Exit evidence |
|---|---|---|
| C0: sender/authentication feasibility | Study Open Screen discovery, channel framing and device authentication. Test the intended stock sender versions before player integration. | Sender discovers, authenticates and establishes a control session with credentials suitable for our distribution. Modified senders or test trust stores prove only a laboratory path. |
| C1: basic media control | Implement a bounded C receiver adapter for the validated application/message subset: launch, media load, status, pause/resume, seek and stop. | A compatible sender plays an unprotected test URL and receives accurate state. Do not label a custom test sender as general Chromecast compatibility. |
| C2: interoperability | Evaluate real target apps and classify failures as authentication, receiver application requirements, control protocol or media. | A documented sender/app matrix and repeated video replacement/reconnection without service regression. |
| C3: optional mirroring | Assess Cast streaming negotiation, transport, codecs and synchronization independently of URL casting. | Hardware-decoded video plus synchronized audio; URL casting success alone is insufficient. |

Device authentication is a separate question from accepting a TLS connection.
Do not ship extracted device private keys or present test certificates as a
production solution. If C0 cannot be passed, retain the research and defer the
feature instead of adding an unusable discovery advertisement to stable builds.
Do not claim that implementing the default media messages supplies every app's
custom receiver, web runtime, authentication or protected-media support.

## Integration Contract

Use the existing boundaries rather than introducing a second scheduler,
player instance or universal protocol framework:

- Protocol logic stays in C; the existing C++ UI reads snapshots. Proposed directories are `source/protocol/miracast/` and `source/protocol/google_cast/`, created only after the relevant feasibility gate passes.
- Extend the existing service and media-owner identifiers deliberately; they are finite enums, not an already implemented plugin registry. Audit arrays, switches, readiness and shutdown code when adding an identifier.
- Use [protocol_coordinator.h](../source/app/protocol_coordinator.h) for supervised services and ownership transactions, [media_actor.h](../source/player/core/media_actor.h) for player commands, and [protocol_media_session.h](../source/app/protocol_media_session.h) for lease-tagged lifecycle events.
- Every command/callback must match its owner, session and generation. A late disconnect or teardown from the old sender cannot stop a replacement session. Do not merge sessions by IP alone.
- Discovery, control attachment, media lifetime and actual playback state are separate facts. Define terminal protocol events from the protocol before implementing disconnect policy; do not invent a common timeout for all protocols.
- Main owns input/rendering. Network workers never block it or call libmpv/deko3d directly. Backend events, not accepted control requests, establish actual playback progress.
- Keep bounded connection counts, message lengths, queues and media buffers, explicit read/connect deadlines, and cancellation that wakes blocked reads. Give each new listener/stream an owner and deterministic close/join path.
- No network, filesystem or logging I/O under shared state locks. Reuse the existing logger and actor health reporting; protocol failure must not disable nxlink, DLNA or IPTV.
- Reuse media bridge mechanisms only where their contracts match. AirPlay's protocol-specific framing, encryption and lifecycle must not become implicit prerequisites for a Miracast or Cast stream.
- Keep live-mirror UI non-seekable; derive URL-stream seekability from actual media. IPTV channel controls remain IPTV-only.

See [threading-design.md](threading-design.md) and
[player-layer.md](player-layer.md). These are integration constraints, not a
claim that adding a service enum alone is sufficient. The previously deferred
exclusive-mode coordinator race remains a separate known issue; do not enable
that mode as a shortcut for new protocols or claim this document fixes it.

## Diagnostics And Small Test Matrix

Log a monotonic attempt ID, owner/session generation, state transition and
reason, stage duration, socket/worker counts, queue high-water marks, first
media packet, negotiated codec/profile, first decoded frame and first audio.
Record protocol status codes and timeout stages; never log private keys,
certificates' private material, signed URLs or raw media by default.

Use focused black-box state-transition tests and bounded-parser negative cases,
not a large new test framework or mandatory network-heavy CI suite. Small local
checks do not replace these physical acceptance cases:

| Case | Required outcome |
|---|---|
| Discovery only; sender cancels | Home remains responsive; no abandoned media owner or worker. |
| Valid connection with no media | Diagnostic identifies negotiation versus first-packet wait; timeout/cancel releases resources. |
| Video and audio | Verify actual hardware decode, orientation, timestamps and audible synchronization. |
| Home during load; sender disappears | Reads wake and resources settle without hanging input or logs. |
| Reconnect/video replacement ten times | No accumulating sockets/workers; old events cannot control new media. |
| Takeover between new protocol and DLNA/AirPlay/IPTV | Exactly one media owner, existing discovery policy preserved, no unrelated Stop. |
| Thirty-minute session then existing IPTV/DLNA playback | Stable counters, no progressive stalls or loss of logging. |
| Shutdown, nxlink on and off | Service/media/render/network dependencies retire in their existing order. |

For each run record NRO commit, dependency revision, firmware, sender OS/app,
connection route, video profile and whether the sender was unmodified. A failed
feasibility gate must be reported as such rather than hidden behind buffering UI.

## Reference Discipline

Before copying code, pin the upstream revision and inspect each file's license
and dependencies. Retain provenance and notices; a C rewrite does not erase
obligations for copied/derived code. Prefer a small audited adapter to wholesale
imports. No external protocol source is introduced by this document.

Primary references consulted on 2026-10-09:

- [MiracleCast sink implementation and dependencies](https://github.com/albfan/miraclecast).
- [Microsoft wireless projection over an existing network](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/wireless-projection-implementing-over-existing-network).
- [Microsoft Miracast over Infrastructure overview](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-mice/ab6341b7-4fc7-41fd-a74d-3fe023455482).
- [libnx service headers](https://github.com/switchbrew/libnx/tree/master/nx/include/switch/services).
- [Google Cast application model](https://developers.google.com/cast/docs/overview).
- [Open Screen Cast implementation](https://github.com/chromium/openscreen/blob/main/cast/README.md).
- [Open Screen device authentication verifier](https://chromium.googlesource.com/openscreen/+/refs/heads/main/cast/sender/channel/cast_auth_util.cc).

Next action: produce the M0 capability inventory and a minimal real-device
discovery/connection experiment. Google Cast C0 can be researched independently;
neither route should modify stable playback until its gate has usable evidence.
