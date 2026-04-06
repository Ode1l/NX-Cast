# NX-Cast Roadmap

## Phase 0
Bootstrap and runtime foundation

- repository, build system, `.nro/.nacp`
- logging, input, network bootstrap
- status: done

## Phase 1
Discovery and DLNA description/control baseline

- `SSDP` responder
- `device.xml`
- `SCPD`
- `SOAP` routing and core actions
- status: done

## Phase 2
Protocol completeness

- `GENA`
- `LastChange`
- `CurrentTransportActions`
- richer metadata return
- expanded `SinkProtocolInfo`
- Macast-style single protocol-observed state
- status: largely done, still being hardened by compatibility testing

## Phase 3
Player core and standard input modeling

- `player` owner thread and command queue
- snapshot and event surface
- `IngressModel`
- `evidence -> model -> resource_select -> http_probe -> media -> policy`
- explicit transport kinds
- status: in progress but already the active architecture

## Phase 4
Playback backend baseline

- `libmpv` backend
- `ao=hos`
- `OpenGL/libmpv render API`
- status: landed on the current baseline

## Phase 5
Generic transport stability

- `http-file`
- `hls-direct`
- `hls-local-proxy`
- `hls-gateway`
- mixed-source session stability
- control-point progress/seek interoperability
- status: main active behavior track

## Phase 6
Source compatibility

- keep parsing generic and standards-first
- vendor hints only as additive policy input
- continue improving bilibili / mgtv-family / qq-video / youku / cctv / iqiyi behavior
- status: ongoing, but no longer treated as the first architectural layer

## Phase 7
Hardware decode

- evaluate actual `hwdec=nvtegra` activation
- if needed, move to a custom media toolchain
- status: blocked by current official `dkp` toolchain limits

## Phase 8
Future backend upgrade

- custom `FFmpeg/mpv` toolchain
- optional `deko3d` / `render_dk3d`
- `libuam`
- status: future work, not current default path

## Phase 9
AirPlay receiver

- discovery
- session control
- media path
- status: not started

## Phase 10
DMP expansion

- source-native browsing
- VOD program lists and detail pages
- optional source adapters
- status: planned
