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
2. standard inputs must be modeled correctly before any source-specific policy is applied
3. vendor hints are additive only
4. protocol code must stay generic and standards-first
5. `player` remains the single real playback state source

In practice this means:

- parsing and classification belong in `player/ingress`
- playback policy belongs in `policy_*`
- DLNA protocol code must not become a vendor hack layer
- if a change alters runtime playback behavior, it should be isolated and tested separately from structural cleanup

## Current Architecture

Main areas:

- `source/protocol/dlna/`
- `source/player/core/`
- `source/player/ingress/`
- `source/player/backend/`
- `source/player/render/`

Read these first before larger changes:

1. [README_CN.md](README_CN.md)
2. [docs/Player层设计.md](docs/Player层设计.md)
3. [docs/DMR实现细节.md](docs/DMR实现细节.md)
4. [docs/源兼容性.md](docs/源兼容性.md)

## Pull Requests

Before submitting:

1. make sure the project builds
2. keep structural and behavioral changes separate when possible
3. update docs when architecture or current status changes
4. describe what changed, why, and how it was tested

## Areas Where Help Is Useful

- generic DMR interoperability
- mixed/local-proxy transport handling
- player/backend hardening
- media toolchain work (`nvtegra`, future custom `mpv/ffmpeg`)
- documentation
