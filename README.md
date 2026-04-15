# NX-Cast

`NX-Cast` is an open-source media receiver for Nintendo Switch homebrew on Atmosphère.

The current goal is to build a solid generic `DLNA DMR` receiver on Switch, with a direct renderer-to-`libmpv` playback path and protocol state that stays aligned with runtime playback state.

## Current State

The project already has these major pieces in place:

- `SSDP` discovery
- runtime-served `Description.xml` and `SCPD`
- `SOAP` control for `SetAVTransportURI / Play / Pause / Stop / Seek`
- `GENA` subscriptions and `LastChange`
- renderer snapshot and event bridge
- `libmpv` backend
- `ao=hos`
- `deko3d/libmpv render API`
- controller-driven playback OSD and local seek/volume controls

The project is currently best described as:

1. generic `DMR` foundation established
2. protocol state and renderer state aligned around the same playback session
3. direct `URL -> libmpv` playback path landed
4. `deko3d` is now the preferred render route when the custom media toolchain is installed
5. `hwdec=nvtegra` is requested at runtime, but still depends on the installed `FFmpeg/libmpv` build

## Core Design Principles

The current project direction follows these rules:

1. structural refactors and behavior changes are done separately
2. protocol code stays protocol-focused and standards-first
3. control actions call the renderer directly
4. `libmpv` is used as the playback engine, demuxer, and decoder
5. renderer observes `libmpv` properties and events, then syncs them back to protocol state
6. `SOAP`, `LastChange`, and compatibility queries all read from one protocol-observed state

In short:

**protocol sends commands down, renderer pushes runtime state back up.**

## Architecture

```text
main
  -> protocol/dlna
       -> discovery (SSDP)
       -> description (template XML / CSV)
       -> control (SOAP / GENA / protocol_state)
            -> renderer facade
  -> player
       -> core (session / snapshot / event pump)
       -> backend (libmpv / mock)
       -> render (view / frontend)
```

Two state lines matter:

1. renderer owns the runtime playback session
2. `protocol_state` owns the protocol-facing observed state

## Playback Model

The current playback path is intentionally thin:

```text
SetAVTransportURI
  -> renderer_set_uri(...)
  -> libmpv loadfile
  -> libmpv probes URL / demux / decode
  -> observed properties and events
  -> protocol_state sync
```

Current observed runtime fields include:

- `time-pos`
- `duration`
- `pause`
- `mute`
- `seekable`
- `paused-for-cache`
- `seeking`
- end-of-file / error transitions

`NX-Cast` no longer has a separate pre-open modeling layer in the player path.

## Backend Direction

The current intended backend route is:

1. `ao=hos`
2. `deko3d/libmpv render API`
3. `libmpv` remains the playback core
4. `OpenGL/libmpv render API` remains as a fallback when the custom media toolchain is absent

Important distinction:

- `OpenGL` and `deko3d` are rendering paths
- `hwdec=nvtegra` is a decode path

Current conclusions:

1. `hos-audio + deko3d` is integrated
2. runtime `hwdec=nvtegra` preference is wired in
3. the official `dkp` toolchain still only provides the fallback OpenGL path
4. the custom `wiliwili` media packages are the current recommended baseline for real Switch playback

## Current Priorities

The next priorities are:

1. harden generic `DMR` interoperability
2. improve protocol state fidelity and control-point sync
3. keep the template-driven description layer aligned with actual implementation
4. continue stabilizing playback on real-world URLs and mixed control points
5. continue hardening the new `deko3d` renderer and controller-driven player UI
6. keep `nvtegra` validation tied to the custom media toolchain, not the official fallback packages

## Repository Layout

```text
source/
  main.c
  log/
  player/
    core/
    backend/
    render/
  protocol/
    dlna/
      discovery/
      description/
      control/
    http/
assets/
  dlna/

Runtime storage on Switch:
  sdmc:/switch/NX-Cast/dlna/
```

## Recommended Reading Order

1. [docs/Player层设计.md](docs/Player层设计.md)
2. [docs/DMR实现细节.md](docs/DMR实现细节.md)
3. [docs/SCPD模块说明.md](docs/SCPD模块说明.md)
4. [ROADMAP.md](ROADMAP.md)

## Build

Requirements:

- `devkitPro`
- `devkitA64`
- `libnx`
- recommended custom media packages from `wiliwili`:
  - `libuam`
  - `switch-ffmpeg`
  - `switch-libmpv_deko3d`

Recommended local install:

```bash
base_url="https://github.com/xfangfang/wiliwili/releases/download/v0.1.0"
sudo dkp-pacman -U \
  $base_url/libuam-f8c9eef01ffe06334d530393d636d69e2b52744b-1-any.pkg.tar.zst \
  $base_url/switch-ffmpeg-7.1-1-any.pkg.tar.zst \
  $base_url/switch-libmpv_deko3d-0.36.0-2-any.pkg.tar.zst
```

Build:

```bash
make
```

Output:

- `NX-Cast.nro`

Docker build:

```bash
docker build -t nx-cast-build .
docker run --rm -e DEVKITPRO=/opt/devkitpro -v "$PWD:/workspace" -w /workspace nx-cast-build bash -lc 'make clean && make -j$(nproc)'
```

## Documentation Note

Repository docs are current-state docs. If code and docs diverge, the source tree is authoritative and docs should be updated to match the code.
