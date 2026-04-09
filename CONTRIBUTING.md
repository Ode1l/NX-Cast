# Contributing to NX-Cast

Thank you for contributing to `NX-Cast`.

## Development Setup

Install:

- `devkitPro`
- `devkitA64`
- `libnx`

Build:

```bash
make
```

## Current Engineering Rules

This project currently follows these engineering rules:

1. structural refactors and behavior changes must be separated
2. protocol code must stay generic and standards-first
3. control actions should map directly onto renderer commands
4. runtime playback state should come back from renderer and backend events, not duplicated ad hoc in protocol code
5. documentation must follow the current code, not removed designs

In practice this means:

- keep DLNA protocol code out of site-specific compatibility hacks
- keep renderer control thin and explicit
- prefer `libmpv` runtime observation over locally invented playback state
- if a change alters runtime playback behavior, isolate and test it separately from structural cleanup

## Current Architecture

Main areas:

- `source/protocol/dlna/`
- `source/player/core/`
- `source/player/backend/`
- `source/player/render/`
- `romfs/dlna/`

Read these first before larger changes:

1. [README_CN.md](README_CN.md)
2. [docs/Player层设计.md](docs/Player层设计.md)
3. [docs/DMR实现细节.md](docs/DMR实现细节.md)
4. [docs/SCPD模块说明.md](docs/SCPD模块说明.md)

## Pull Requests

Before submitting:

1. make sure the project builds
2. keep structural and behavioral changes separate when possible
3. update docs when architecture or current status changes
4. describe what changed, why, and how it was tested

## Areas Where Help Is Useful

- generic DMR interoperability
- renderer and protocol-state hardening
- `libmpv` integration and media toolchain work
- future `nvtegra` / `deko3d` exploration
- documentation
