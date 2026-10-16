# Miracast Control And Media Development

Updated: 2026-10-16. Experimental branch: `codex/miracast`.

## Current Result

The [offline OPTIONS prototype](../tools/miracast_protocol/README.md) reuses the
common RTSP request parser and covers the first bidirectional handshake. Host
tests, ASan/UBSan and compilation of the core with devkitA64 pass. This does not
complete M1 control negotiation, the M0 wireless gate, or a playable receiver.
Production source, CI, release packaging and the current app are unchanged.

## Connection Flow

The following baseline flow is a roadmap, not a list of implemented features:

| Phase | Exchange / responsibility | Current status |
|---|---|---|
| Wireless | WFD discovery, P2P group, IP configuration | Device probe exists; runtime unknown |
| Control connection | Sink opens TCP control connection to source | Not implemented |
| M1 | Source OPTIONS; sink response | Offline request parsing and output implemented |
| M2 | Sink OPTIONS; source response | Bounded response wire decoder and handshake adapter host-tested |
| M3 | Source GET_PARAMETER queries sink capabilities | Pending |
| M4 | Source SET_PARAMETER selects formats and presentation URL | Pending |
| M5 | Source SET_PARAMETER triggers SETUP | Pending |
| M6 | Sink SETUP with bound media port; source supplies Session/Transport | Pending |
| M7 | Sink PLAY with the negotiated Session | Pending |
| Streaming | RTP payload carries MPEG-TS; demux, decode and render | Pending |
| Lifecycle | Keepalive, pause/resume where supported, teardown, timeout | Pending beyond initial OPTIONS |

The request directions above follow the
[MiracleCast sink implementation](https://github.com/albfan/miraclecast/blob/0b7f1f1f6586dc65ff480f3cda5c2170a70aa020/src/ctl/ctl-sink.c).
Validate parameter formats and optional behaviors against the WFD specification
before implementing them; upstream code alone is not a conformance oracle.

## Next Offline Slices

1. The response decoder and byte-level OPTIONS adapter are implemented. Next add
   bounded live receive/transmit handling, including partial writes, when the
   platform gate permits integration. Offline M2/M3 concatenation is verified;
   M3 capability handling is not. Keep incoming requests independent of pending
   outgoing responses; M3 may already be buffered when M2 completes.
2. Implement M3/M4 parameter parsing and selection validation. Advertise only an
   explicit supported profile; do not borrow AirPlay feature masks. Validate the
   entire proposed configuration before acknowledging/applying it. Do not copy
   the reference's acknowledge-before-validation behavior.
3. Implement SETUP/PLAY/TEARDOWN with explicit Session, CSeq and transport state.
   Do not treat a presentation URL as a DLNA URL load or let mpv open a second
   independent RTSP control session.
4. Implement RTP-to-TS ingress with payload/sequence validation, bounded reordering,
   loss/discontinuity accounting and cancellation. Verify using known TS samples
   before linking a decoder. No new FFmpeg build or muxer is assumed necessary.

No codec capability bitmasks, HDCP support or audio support are advertised by the
running app. Hardware decode availability does not automatically prove support
for every negotiated H.264 profile, level, resolution or transport mode.

## Runtime Ownership Design

This is a proposed integration boundary, not implemented new infrastructure:

- One control-loop owner mutates each WFD session and processes RTSP/timeouts.
  Avoid a thread per request. Media ingress may have a separate bounded worker
  if throughput measurements justify it; neither worker touches UI or deko3d.
- Reuse `MediaActor` for player commands and its session/generation validation.
  Acquire a lease only when the accepted configuration and input are ready to
  load; discovery and OPTIONS must not interrupt another protocol's playback.
- Reuse `ProtocolMediaSessionSnapshot` for application-visible lifecycle. Keep
  WFD transaction state separate from playback loading/active state; do not
  transplant AirPlay detached-connection rules or create an additional global
  arbitration framework.
- The media reader consumes a bounded TS stream. Network/control workers never
  block indefinitely on the decoder; cancel must unblock the reader, release
  ownership once, and close per-session sockets. Define overflow behavior before
  integration instead of allowing an unbounded packet queue.
- Record transition, local transaction ID, peer CSeq, timeout reason and aggregate
  media counts. Do not log whole untrusted payloads per packet or write network
  logs synchronously under protocol locks.

## Device Gates

The [platform feasibility gate](miracast-platform-feasibility.md) still comes
first for runtime integration. No amount of offline RTSP testing establishes
that Horizon exposes usable Wi-Fi Direct operations. After that gate, verify
native Windows/Android discovery, clean connection/teardown, first H.264 frame,
audio sync, reconnect and regression of DLNA/IPTV/AirPlay. Do not merge into main
or label the feature supported before these checks.
