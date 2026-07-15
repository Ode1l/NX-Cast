# DMR Implementation

This document defines the current `NX-Cast` boundary as a DLNA Digital Media Renderer.

## Role

`NX-Cast` currently implements a `DMR`.

It is responsible for:

1. Being discovered through SSDP.
2. Serving device and service descriptions.
3. Receiving SOAP control commands.
4. Handling GENA subscriptions and `LastChange` notifications.
5. Fetching media URLs provided by the controller.
6. Playing media locally on the Switch.

It is not responsible for:

1. Acting as a DLNA media controller (`DMC`).
2. Acting as a DLNA media server (`DMS`).
3. Implementing AirPlay.
4. Browsing source-native services such as iQiyi, MangoTV, CCTV, Bilibili, or IPTV portals.
5. Bypassing DRM or implementing site login flows.

## Main Flow

```text
SSDP
  -> Description.xml / SCPD
  -> SOAP
  -> renderer
  -> backend/libmpv
  -> runtime properties and events
  -> protocol_state
  -> SOAP query / GENA notify
```

Primary modules:

1. `source/protocol/dlna/discovery/`
2. `source/protocol/dlna/description/`
3. `source/protocol/dlna/control/`
4. `source/player/core/`
5. `source/player/backend/`
6. `source/player/render/`
7. `assets/dlna/`, copied at runtime to `sdmc:/switch/NX-Cast/dlna/`

## Protocol Rules

The DLNA layer follows these rules:

1. SOAP parses protocol input, validates arguments, and maps actions to player commands.
2. Protocol code must not contain source-specific playback workarounds.
3. Protocol-observed state has one source of truth.
4. SOAP queries, `LastChange`, and compatibility helpers read the same protocol state.

The shared state is owned by:

- [protocol_state.h](../source/protocol/dlna/control/protocol_state.h)
- [protocol_state.c](../source/protocol/dlna/control/protocol_state.c)

## Implemented Control Surface

### AVTransport

Implemented actions:

1. `SetAVTransportURI`
2. `Play`
3. `Pause`
4. `Stop`
5. `Seek`
6. `GetTransportInfo`
7. `GetMediaInfo`
8. `GetPositionInfo`
9. `GetCurrentTransportActions`

### RenderingControl

Implemented actions:

1. `GetVolume / SetVolume`
2. `GetMute / SetMute`
3. `GetBrightness` compatibility stub

### ConnectionManager

Implemented actions:

1. `GetProtocolInfo`
2. `GetCurrentConnectionIDs`
3. `GetCurrentConnectionInfo`

## State Ownership

Runtime state is split into two layers:

1. The renderer and player own real playback state, including playback state, position, duration, volume, mute, seekability, and the current media URI.
2. `protocol_state` owns DLNA-observed state, including `TransportState`, `TransportStatus`, URI, metadata, and generated `LastChange`.

The protocol layer does not invent playback state. It consumes player snapshots and player events.

## Renderer Boundary

The protocol layer talks to the renderer only through:

1. Commands.
2. `RendererSnapshot`.
3. `RendererEvent`.

It must not know backend internals, render-path internals, or implementation details outside the public player/renderer API.

## Current Focus

The main DMR work is no longer basic SOAP reception. Current priorities are:

1. Expand generic DMR compatibility.
2. Improve `LastChange` and progress synchronization accuracy.
3. Keep descriptions aligned with real capabilities.
4. Test interoperability against real controllers.

## Related Documents

1. [player-layer.md](player-layer.md)
2. [soap-module.md](soap-module.md)
3. [scpd-module.md](scpd-module.md)
