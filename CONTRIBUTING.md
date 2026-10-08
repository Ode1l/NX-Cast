# Contributing to NX-Cast

Thank you for contributing to `NX-Cast`.

## Development Setup

Follow the [media toolchain guide](docs/ffmpeg-mpv-toolchain.md) to install
devkitPro, devkitA64, libnx and the required media dependencies, including
`libuam` and `switch-libmpv_deko3d`.

Use NX-Cast's pinned FFmpeg package, not an arbitrary upstream or wiliwili
FFmpeg build. Normal build entry points automatically install the required
package when it is missing or outdated, using the pinned GitHub Release asset
and SHA-256 verification. An already current installation is reused without
downloading or reinstalling. This updates the shared devkitPro Switch prefix;
it does not automatically install the other project dependencies.

See the [macOS and Windows developer workflow](docs/developer-workflow.md) for
SDK setup, the four VS Code launches, Full Trace logging and publication.
Builds default to four jobs. From macOS or a configured MSYS2 Bash shell:

```bash
bash scripts/dev.sh build
```

Windows PowerShell uses `scripts/dev.ps1` to start the configured MSYS2 Bash;
it does not require WSL or Docker. The adapter still needs native Windows
validation; local macOS compilation and script-level tests have passed.

Publication is separate from local packaging: the publish launch pushes a
committed, clean `main` and a new version tag, then GitHub Actions builds the
release. It does not commit changes or overwrite existing releases. Follow the
[release steps](docs/developer-workflow.md#github-release) before using it.

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
- `assets/dlna/`

Read these first before larger changes:

1. [README.md](README.md)
2. [docs/player-layer.md](docs/player-layer.md)
3. [docs/dmr-implementation.md](docs/dmr-implementation.md)
4. [docs/scpd-module.md](docs/scpd-module.md)

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
- `deko3d` and custom media toolchain hardening
- documentation
