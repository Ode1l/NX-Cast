# NX-Cast Roadmap

## Phase 0
Bootstrap and runtime foundation

- Repository, build system, `.nro/.nacp`
- Logging, input, network bootstrap
- Status: done

## Phase 1
Discovery and DLNA description/control baseline

- SSDP responder
- mDNS probe
- `device.xml` and SCPD
- SOAP router and core actions
- Status: done for current MVP

## Phase 2
Generic DMR completeness

- `GENA` eventing
- richer `SinkProtocolInfo`
- complete metadata return
- `CurrentTransportActions`
- URL preflight and better resource selection
- local-proxy / mixed-source classification
- shared protocol-state layer for `SOAP + LastChange`
- Status: largely done, still iterative

## Phase 3
Player and rendering baseline

- `player` as single state source
- owner thread + command queue
- `libmpv` backend
- `software render + libnx framebuffer`
- Status: done for minimal video path

## Phase 4
Complete backend

- real audio output (`ao=hos`)
- `OpenGL/libmpv render API` path
- Switch-oriented backend hardening
- Status: largely done on the current official toolchain; `ao=hos` and `OpenGL/libmpv render API` are verified on device, remaining work is backend hardening and transport stability

## Phase 5
Hardware decode

- evaluate `hwdec=nvtegra`
- connect decode path to the `OpenGL/libmpv render API` backend
- Status: investigated and partially wired in code, but blocked by the current official `dkp` `libmpv` toolchain lacking a working explicit `nvtegra` hwdec backend

## Phase 6
Source compatibility and mixed transports

- vendor-sensitive sources
- local proxy / hybrid transports
- range/seekability/media preflight
- Status: in progress; `mgtv-family`, Tencent normal/phone-acceleration, and parts of Youku/CCTV have improved, but `iqiyi` and some local-proxy/HLS transport cases remain incomplete

### Current Execution Order

1. stabilize `local_proxy` / mixed-source transport behavior first
2. improve position/progress synchronization and control-point interoperability, continuing to align the shared protocol-state layer with `Macast`-style behavior
3. only after transport/system behavior is stable, revisit custom media-toolchain work for real `nvtegra`

## Phase 7
AirPlay receiver

- discovery
- session control
- media path
- Status: not started

## Phase 8
DMP expansion

- source-native browsing
- VOD program lists and detail pages
- optional source adapters
- Status: planned

## Optional

- custom media toolchain (`libuam + FFmpeg(nvtegra) + mpv(deko3d + hos-audio)`)
- `deko3d` / `render_dk3d`
- `SSDP NOTIFY ssdp:alive/byebye`
- resident/sysmodule mode
- settings and UI polishing
