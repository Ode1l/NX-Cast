# NX-Cast Roadmap

Current release history is tracked in [CHANGELOG.md](CHANGELOG.md).
The next protocol research priorities are Miracast, then Google Cast as a
candidate; neither is currently implemented. See the
[development preparation](docs/casting-protocol-development.md) for feasibility
gates, integration constraints and device acceptance criteria.

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
- `deko3d/libmpv render API`
- `OpenGL/libmpv render API` fallback
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

- runtime `hwdec=nvtegra` preference wiring
- validate actual activation on the custom media toolchain
- status: partially landed, still constrained by the installed `FFmpeg/libmpv` package set

## Phase 9
Custom media toolchain baseline

- custom `FFmpeg/mpv` toolchain
- `render_dk3d`
- `libuam`
- Docker / CI integration
- status: active baseline, still being hardened

## Phase 10
AirPlay receiver

- discovery
- session control
- media path
- status: experimental implementation available; URL/HLS video and screen mirroring are covered in the release history, with compatibility work continuing

## Phase 11
DMP expansion

- source-native browsing
- VOD program lists and detail pages
- optional source adapters
- status: planned

## Phase 12
Application GUI and IPTV

- shared app navigation and input layer
- ImGui/deko3d application shell
- user-provided local/remote M3U sources
- channel groups, favorites, history and live channel switching
- XMLTV current/next programme guide and logo cache
- status: implemented baseline; see [current IPTV support](docs/iptv.md), with the original design in `docs/iptv-gui-plan.md`

## Phase 13
Additional casting protocols

- Miracast: research Switch wireless discovery/P2P access before integrating RTSP or media transport
- Google Cast: candidate; verify stock-sender device authentication before media control or mirroring
- preserve the existing media actor, ownership leases, bounded workers and responsive logging
- FCast and DIAL: deferred pending a concrete supported-sender use case
- status: development documentation prepared, feasibility not yet demonstrated; see [casting-protocol-development.md](docs/casting-protocol-development.md)
