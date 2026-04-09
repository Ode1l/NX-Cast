# NX-Cast Roadmap

## Phase 0
Bootstrap and runtime foundation

- repository, build system, `.nro/.nacp`
- logging, input, network bootstrap
- status: done

## Phase 1
Discovery and DLNA description/control baseline

- `SSDP` responder
- device description
- service `SCPD`
- `SOAP` routing and core actions
- status: done

## Phase 2
Protocol completeness

- `GENA`
- `LastChange`
- `CurrentTransportActions`
- richer metadata return
- Macast-style single protocol-observed state
- status: largely done, still being hardened by compatibility testing

## Phase 3
Renderer and session model

- direct `protocol -> renderer` control path
- snapshot and event surface
- dynamic string ownership for long protocol state values
- direct `URL -> libmpv loadfile`
- status: active baseline

## Phase 4
Playback backend baseline

- `libmpv` backend
- `ao=hos`
- `OpenGL/libmpv render API`
- status: landed on the current baseline

## Phase 5
Template-driven description layer

- runtime-served `Description.xml`
- runtime-served `AVTransport.xml`
- runtime-served `RenderingControl.xml`
- runtime-served `ConnectionManager.xml`
- `SinkProtocolInfo.csv`
- status: landed, still being aligned with real implementation details

## Phase 6
Generic transport and interoperability stability

- real-world URL playback stability
- control-point progress/seek interoperability
- event fidelity
- mixed control-point session behavior
- status: main active behavior track

## Phase 7
Source compatibility

- keep compatibility work standards-first
- add the minimum request-context handling needed by real senders
- continue improving interoperability with common mobile and desktop control points
- status: ongoing, but no longer treated as a separate player pipeline

## Phase 8
Hardware decode

- evaluate actual `hwdec=nvtegra` activation
- if needed, move to a custom media toolchain
- status: blocked by current official `dkp` toolchain limits

## Phase 9
Future backend upgrade

- custom `FFmpeg/mpv` toolchain
- optional `deko3d` / `render_dk3d`
- `libuam`
- status: future work, not current default path

## Phase 10
AirPlay receiver

- discovery
- session control
- media path
- status: not started

## Phase 11
DMP expansion

- source-native browsing
- VOD program lists and detail pages
- optional source adapters
- status: planned
