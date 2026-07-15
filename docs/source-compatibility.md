# Source Compatibility

This document describes how `NX-Cast` should think about source compatibility.

## Principle

`NX-Cast` is a generic DLNA DMR. It should not become a source-native client for iQiyi, MangoTV, CCTV, Bilibili, or IPTV providers.

The expected path is:

```text
controller app
  -> SetAVTransportURI
  -> renderer
  -> libmpv loadfile
  -> FFmpeg probing, networking, demuxing, decoding
```

The project should improve generic protocol compatibility and player robustness before adding source-specific logic.

## What Belongs In NX-Cast

Allowed compatibility work:

1. More tolerant SSDP parsing.
2. Correct absolute and relative URL handling in descriptions.
3. Accurate `SinkProtocolInfo`.
4. Correct SOAP argument parsing and XML entity decoding.
5. Stable `LastChange` state.
6. Better seek target formatting.
7. Better player state synchronization.
8. Better logging around controller commands.

## What Does Not Belong In NX-Cast

Avoid:

1. Hard-coded source user-agent branches.
2. Site-specific ad skipping.
3. Reimplementing HLS in the protocol layer.
4. Rewriting media URLs unless the controller provided a broken relative path inside a context we can safely resolve.
5. DRM bypass, login emulation, or private API scraping.

## HLS

HLS should normally be handled by `libmpv/FFmpeg`.

The project should not maintain a generic HLS gateway/proxy unless there is a precise, reproducible problem that cannot be solved by the media stack.

For relative segment URLs in playlists, the expected owner is FFmpeg's HLS demuxer. If it fails, first capture the original playlist and confirm whether the playlist itself is malformed or whether the media toolchain has a Switch-specific network bug.

## Common Failure Classes

### Discovery Or Description Failure

Symptoms:

1. Controller repeatedly requests `description.xml`.
2. Controller never sends SOAP commands.
3. Controller fetches SCPD but does not show the renderer as playable.

Likely areas:

1. SSDP response headers.
2. `LOCATION`.
3. Device identity fields.
4. `SCPDURL`, `controlURL`, and `eventSubURL`.
5. `SinkProtocolInfo`.

### SOAP Routing Failure

Symptoms:

1. Controller sends commands but playback does not start.
2. Actions are logged with unknown service/action.
3. SOAP body is routed to the wrong service.

Likely areas:

1. SOAPAction parsing.
2. URL path mapping.
3. XML namespace handling.
4. Argument extraction.

### Playback Failure

Symptoms:

1. `SetAVTransportURI` succeeds but media does not play.
2. `libmpv` logs demux or decode errors.
3. Playback stalls after ad insertion, seek, or pause/resume.

Likely areas:

1. Media URL validity.
2. FFmpeg protocol support.
3. Hardware decode path.
4. Cache/buffer behavior.
5. Source-side stream changes.

## Logging Strategy

Logs should identify:

1. Source IP and port.
2. Request path.
3. User-Agent.
4. SOAP service and action.
5. Sequence number for media loads.
6. Current URI.
7. Player state transitions.
8. Seek target and final position.

Sequence numbers are useful when a source inserts ads or sends multiple media URLs in quick succession.

## Test Strategy

When diagnosing a source:

1. Capture SSDP, HTTP description, SCPD, SOAP, and player logs.
2. Save the exact media URL passed through `SetAVTransportURI`.
3. Test whether desktop mpv can open the same URL.
4. Test whether Switch `libmpv` can open it without DLNA.
5. Compare hardware decode and software decode only when it helps isolate media-stack behavior.

The final product should use hardware decode, but software decode can still be useful as a diagnostic control.
