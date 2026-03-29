# NX-Cast

**NX-Cast** is an open-source wireless display receiver for the Nintendo Switch running Atmosphère.

The project aims to turn the Nintendo Switch into a wireless media receiver capable of accepting streams via protocols such as **DLNA** and **AirPlay-style media streaming**, without requiring Linux on Switch.

NX-Cast runs  on Atmosphère using the homebrew development stack based on **devkitPro** and **libnx**.

---

## Project Goals

NX-Cast aims to provide:

- Atmosphère implementation
- Wireless media receiving capability
- Modular architecture for protocol extensions
- Hardware-accelerated decoding where possible
- Community-maintainable codebase

The long-term goal is to provide a reusable infrastructure for media streaming applications on Nintendo Switch homebrew.

---

## Current Status

Active prototype stage.

Current progress:

- Application bootstrap, logging, and network initialization are in place.
- Device discovery is working with SSDP responder and mDNS probe support.
- DLNA DMR core path is largely implemented: SCPD, SOAP routing, AVTransport/RenderingControl/ConnectionManager, and smoke-test coverage are all in place.
- Remaining DLNA items are mainly optional features such as `NOTIFY ssdp:alive/byebye` and richer eventing.
- Real playback/rendering is the next major milestone after protocol completion.

---

## Milestone 0 Bootstrap

The initial milestone focuses on getting the project to boot reliably inside Atmosphère and preparing the network stack for later protocol work.

Completed items:

- Repository + build system skeleton targeting devkitPro/libnx
- Homebrew entry point that initializes console output and controller input
- Creation of `.nro/.nacp` artifacts for deployment
- Basic network stack bootstrap via `socketInitializeDefault()` with runtime diagnostics

How to verify Milestone 0 locally:

1. Install the devkitPro toolchain (devkitA64 + libnx), then run `make`.
2. Copy the generated `NX-Cast.nro` to `/switch/nx-cast/` on your SD card.
3. Connect the Switch to Wi-Fi, launch NX-Cast from the Homebrew Menu, and check the console log for the `[net]` initialization messages.
4. Press `+` to exit once you see the successful network message.

---

## Milestone 1 Device Discovery

Milestone 1 introduces SSDP-based device discovery to detect DLNA/UPnP senders on the local network.

Highlights:

- Minimal SSDP `M-SEARCH` client that sends discovery probes to `239.255.255.250:1900`.
- Logs sender IP/port and raw response headers to the console so you can verify compatible devices.
- Added an initial SSDP responder so NX-Cast can answer incoming `M-SEARCH` requests; periodic `NOTIFY` broadcasts are not implemented yet.
- Introduced a dedicated `network/discovery` module that caches basic device metadata (USN/ST) and will host future mDNS/AirPlay discovery code.
- Added a real mDNS probe that multicasts `_airplay._tcp.local` queries and prints PTR answers.
- Clarifies that NX-Cast’s DLNA target is the Digital Media Renderer (DMR) role, relying on third-party DMC/DMS apps for control and content.
- Added a placeholder DLNA control module that consumes cached results and logs which devices will get control sessions next.

How to verify Milestone 1 locally:

1. Build and deploy the latest `.nro` as in Milestone 0.
2. Ensure your Switch and the DLNA/UPnP sender are on the same Wi-Fi network.
3. Launch NX-Cast and watch for `[ssdp]` logs showing responses; press `+` after confirming discovery output.

---

## Planned Features

### Phase 1
- Application framework
- Network stack
- Device discovery
- Status: largely complete

### Phase 2
- DLNA Digital Media Renderer (DMR) implementation
- SCPD + SOAP control path
- Status: largely complete, with optional `NOTIFY ssdp:alive/byebye` and richer event/eventing work still pending

### Phase 3
- Basic playback pipeline
- On-device rendering path

### Phase 4
- Hardware accelerated decode

### Phase 5
- AirPlay-style video streaming

### Phase 6
- UI and configuration
- Home menu desktop shortcut entry (add-to-desktop style launch)

### Optional Phase
- Sysmodule-based resident/background service
- Suspended or persistent discovery/listening beyond normal foreground homebrew lifecycle

---

## Architecture Overview

NX-Cast uses a layered architecture:

```text
Application Layer
│
Protocol Layer
(AirPlay / DLNA)
│
Media Processing
│
Decoder
│
Renderer
│
Platform Layer (libnx)
```

---

## Repository Structure

```text
nx-cast
│
├── docs
│
├── src
│ ├── app
│ ├── network
│ ├── protocol
│ ├── media
│ ├── decoder
│ └── render
│
├── include
│
├── examples
│
└── tests
```

---

## Build Requirements

Install the Switch homebrew toolchain:

- devkitPro
- devkitA64
- libnx

Build using:
```text
make
 This produces a `.nro` executable for the Nintendo Switch.
```

### Build in Docker (optional)

If you want a reproducible build environment without installing devkitPro locally:

```text
./scripts/docker_build.sh
```

Equivalent docker compose command:

```text
docker compose build nx-cast-build
docker compose run --rm nx-cast-build
```

Notes:

- Set `NO_CLEAN=1` to skip clean (`NO_CLEAN=1 ./scripts/docker_build.sh`).
- Docker build covers compilation only; runtime verification still needs real Switch hardware.

---

## Running

Place the `.nro` in:
```text
/switch/nx-cast/
Launch using the Homebrew Menu.
```

---

## Contributing

Contributions are welcome.

Please read `CONTRIBUTING.md` before submitting pull requests.

Areas where help is needed:

- Protocol implementation
- Media pipeline optimization
- Hardware decode integration
- UI improvements
- Documentation

---

## Release Readiness

To publish NX-Cast as a Switchbrew project, we still need the following project-wide assets in addition to the core implementation:

- Clear licensing info: keep the GPLv3 license file, mention it in `README`, and document third-party licenses referenced by the code and any shipped assets (icons, fonts, etc.).
- Documentation set: developer environment setup, module-level design notes, coding standards, contribution guide, and reproducible build/test instructions.
- Release metadata: semantic versions, changelog, release notes, `.nro/.nacp` metadata (title, author, version, description), plus optional screenshots or videos for the project page.
- Compliance statements: note that no copyrighted firmware/keys are included and list runtime dependencies and security considerations.
- Community and process scaffolding: issue/PR templates, a code of conduct, maintainer contact info, and expectations for reviews.

---

## CI/CD Expectations

Having basic automation increases trust when distributing on Switchbrew:

- Continuous integration that runs formatting/lints, builds the `.nro`, and executes available tests on each pull request.
- Optional static analysis (clang-tidy, scan-build) and license checks to prevent regressions.
- Release workflows that package artifacts and populate release notes automatically to reduce manual steps when publishing.

Current workflows:

- `CI`: `.github/workflows/ci.yml` (runs on push/PR, builds and uploads artifacts).
- `Release`: `.github/workflows/release.yml` (runs on `v*` tags, publishes GitHub Release with `NX-Cast.nro`).

Tag example:

```text
git tag v0.1.0
git push origin v0.1.0
```

---

## Acknowledgements

NX-Cast is independently implemented. Parts of the protocol architecture, player layering, and Switch media backend direction were informed by the following open-source projects:

- [gmrender-resurrect](https://github.com/hzeller/gmrender-resurrect): DLNA/UPnP DMR service modeling, SCPD/SOAP/service separation, and renderer-side control flow references.
- [pPlay](https://github.com/Cpasjuste/pplay): player facade design and `libmpv` backend separation ideas.
- [NXMP](https://github.com/proconsule/nxmp): Switch media backend direction, including `libmpv`, `hos` audio, `deko3d`, and hardware-decoding integration strategy.
- [PlayerNX](https://github.com/XorTroll/PlayerNX): minimal FFmpeg decode-path reference used for software-decoding study and validation.

Thanks to the maintainers and contributors of these projects.

---

## License

NX-Cast is licensed under GNU GPLv3. See `LICENSE` for full terms.
Copyright (c) 2026 Ode1l.

---

## Disclaimer

NX-Cast is an independent open-source project and is not affiliated with Nintendo.

Use at your own risk.
