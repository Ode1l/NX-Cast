# NX-Cast

`NX-Cast` is an open-source media receiver for Nintendo Switch homebrew on Atmosphère.

The current goal is not to become a source-native app for one specific platform. The project is focused on building a **solid generic DLNA DMR foundation** first, then improving mixed transports, source compatibility, and the full Switch playback backend on top of that foundation.

## Current State

The project already has these major pieces in place:

- `SSDP` discovery
- `device.xml` and `SCPD`
- `SOAP` control for `SetAVTransportURI / Play / Pause / Stop / Seek`
- `GENA` subscriptions and `LastChange`
- `player` owner thread, command queue, snapshot, and backend bridge
- ingress parsing and media modeling
- `libmpv` backend
- `ao=hos`
- `OpenGL/libmpv render API`

The project is currently best described as:

1. generic `DMR` foundation established
2. standard input modeling being tightened
3. mixed transports and transport stability still under active iteration
4. `hwdec=nvtegra` still limited by the current official toolchain

## Core Design Principles

The current project direction follows these rules:

1. structural refactors and behavior changes are done separately
2. standard inputs must be modeled correctly before playback policy is applied
3. vendor hints are additive only and must not override standard parsing results
4. protocol code must not become a source-compatibility hack layer
5. `player` is the single real playback state source
6. `SOAP`, `LastChange`, and compatibility queries all read from one protocol-observed state

In short:

**first model what the source is, then decide how to open it.**

## Architecture

```text
main
  -> protocol/dlna
       -> discovery (SSDP)
       -> description (device.xml / SCPD)
       -> control (SOAP / GENA / protocol_state)
  -> player
       -> core (owner thread / queue / snapshot)
       -> ingress (evidence -> model -> resource_select -> http_probe -> media -> policy)
       -> backend (libmpv / mock)
       -> render (view / frontend)
```

Two state lines matter:

1. `player` owns the real playback state
2. `protocol_state` owns the protocol-facing observed state

## Ingress Pipeline

`player/ingress` is no longer a rule pile that mutates the final media object while parsing. The current flow is:

```text
CurrentURI + CurrentURIMetaData + request headers
  -> evidence
  -> IngressModel
  -> metadata resource selection
  -> http probe / preflight
  -> PlayerMedia
  -> policy
```

This separates two concerns:

1. parsing: what is this source
2. policy: how should the backend open it

Current explicit transport kinds:

- `http-file`
- `hls-direct`
- `hls-local-proxy`
- `hls-gateway`

## Backend Direction

The current intended backend route is:

1. `ao=hos`
2. `OpenGL/libmpv render API`
3. `libmpv` remains the playback core

Important distinction:

- `OpenGL` and `deko3d` are rendering paths
- `hwdec=nvtegra` is a decode path

Current conclusions:

1. `hos-audio + OpenGL` is already integrated
2. `hwdec=nvtegra` is accounted for in code paths, but is not actually available as a working explicit backend under the current official `dkp` `libmpv` toolchain
3. `deko3d` remains a future capability rather than the current default route

## Current Priorities

The next priority is not adding more source-specific hacks. It is:

1. finishing standard input modeling
2. finishing the generic `DMR` compatibility surface
3. stabilizing `local_proxy` and `HLS gateway` transports
4. improving control-point position sync and interoperability
5. revisiting `nvtegra` and future `deko3d` once the toolchain side is ready

## Repository Layout

```text
source/
  main.c
  log/
  player/
    core/
    ingress/
    backend/
    render/
  protocol/
    dlna/
      discovery/
      description/
      control/
    http/
```

## Recommended Reading Order

1. [docs/Player层设计.md](docs/Player层设计.md)
2. [docs/DMR实现细节.md](docs/DMR实现细节.md)
3. [docs/源兼容性.md](docs/源兼容性.md)
4. [docs/render设计.md](docs/render设计.md)
5. [ROADMAP.md](ROADMAP.md)

## Build

Requirements:

- `devkitPro`
- `devkitA64`
- `libnx`

Build:

```bash
make
```

Output:

- `NX-Cast.nro`

## Documentation Note

The repository docs are now written as current-state docs rather than future-plan placeholders. If code and docs ever diverge, the source tree is authoritative and the docs should be updated accordingly.
