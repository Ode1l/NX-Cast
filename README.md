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

The project is now in a "generic DMR foundation is working, complete playback backend still pending" stage.

Current progress:

- Application bootstrap, logging, and network initialization are in place.
- Device discovery now covers SSDP responder, service-type replies, and mDNS probe support.
- The DLNA DMR core path is largely implemented: SCPD, SOAP routing, AVTransport/RenderingControl/ConnectionManager, GENA eventing, and smoke-test coverage are all in place.
- `player` is now the real state source, and the `SOAP -> player -> libmpv` control path is working end to end; the main `SetURI / Play / Pause / Stop / Seek / GetTransportInfo / GetPositionInfo` flow has passed on-device smoke tests.
- `Step 2.1 / Step 2.2` have landed: foreground-display ownership, the main-thread render-loop skeleton, and a minimal real video path via `software render + libnx framebuffer`.
- `ingress` has been refactored into a first-pass `evidence -> classify -> resource_select -> policy` pipeline, with `http` URL preflight, expanded `SinkProtocolInfo`, metadata resource selection, and `local_proxy` transport policy.
- `mp4`, `bilibili`, `mgtv-family`, Tencent Video including parts of its phone-acceleration path, and parts of `youku/cctv` are now verified on device; `iqiyi` is still not working and currently looks more like request-context or system media-stack completeness than a basic DMR failure.
- The current main work is no longer "can the protocol path work", but two deeper tracks:
  1. finishing the generic DMR compatibility baseline
  2. completing the real playback backend: audio output, an `OpenGL/libmpv render API` path, and hardware decode integration
- `ao=hos` and the `OpenGL/libmpv render API` path are now both verified on device. The backend gap has narrowed to `hwdec=nvtegra` not actually becoming active under the current official `dkp` `libmpv` toolchain, plus remaining instability in some mixed/local-proxy transports.

Current backend direction:

1. The currently landed and verified path is `ao=hos + OpenGL/libmpv render API`
2. `hwdec=nvtegra` remains the desired direction, but the current official `dkp` `libmpv` toolchain does not expose a working explicit `nvtegra` hwdec backend
3. `deko3d` remains a future capability instead of the immediate next step
4. If the project later switches to a custom media toolchain, it can move to the `libuam + FFmpeg(nvtegra) + mpv(deko3d + hos-audio)` route

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
- The DLNA control skeleton introduced in this phase has since evolved into the current full `SOAP + GENA` control layer.

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
- Status: largely complete; `SSDP responder + service-type response`, `SOAP`, `GENA eventing`, `SinkProtocolInfo`, metadata return, and the current compatibility baseline are already landed

### Phase 3
- Basic playback pipeline
- On-device rendering path
- Player-ingress resource selection from `CurrentURIMetaData` `DIDL-Lite res/protocolInfo` candidates
- Step 2.1: define foreground-display ownership and add a main-thread render-loop skeleton
- Step 2.2: integrate the `libmpv render API` and establish a minimal on-device video path via `software render + libnx framebuffer`
- Step 2.3: finish log/UI switching, screen ownership, and the `OpenGL/libmpv render API` path
- Status: Step 1 and Step 2.1 / 2.2 / 2.3 are landed, and the first version of player-ingress resource selection is also landed; `ao=hos + OpenGL/libmpv render API` is now verified on device, and the next priority is transport stability plus hardware-decode toolchain work

### Phase 4
- Hardware accelerated decode
- `hwdec=nvtegra`
- Status: `ao=hos` and `OpenGL/libmpv render API` are already working under the current toolchain; `hwdec=nvtegra` is still in the toolchain-validation and later integration stage

### Phase 5
- AirPlay-style video streaming

### Phase 6
- DMP expansion
- Source-side VOD browsing and playback
- Program lists, detail pages, and on-demand entry points for source-backed content
- Introduce source-native adapters only if the product scope explicitly moves beyond generic receiver behavior

### Phase 7
- UI and configuration
- Home menu desktop shortcut entry (add-to-desktop style launch)

### Optional Phase
- Custom `mpv` toolchain
- `deko3d` render backend
- `render_dk3d` / `libuam` route
- Sysmodule-based resident/background service
- Suspended or persistent discovery/listening beyond normal foreground homebrew lifecycle

---

## Architecture Overview

NX-Cast uses a layered architecture:

```text
Application entry (main)
│
Protocol layer
(DLNA / AirPlay placeholder)
│
Control and description
(SSDP / SCPD / SOAP / GENA)
│
player
(core / ingress / backend / render)
│
Platform and dependencies
(libnx / libmpv / networking / future deko3d)
```

---

## Repository Structure

```text
nx-cast
│
├── docs
├── scripts
├── source
│   ├── log
│   ├── player
│   │   ├── core
│   │   ├── ingress
│   │   ├── backend
│   │   └── render
│   ├── protocol
│   │   ├── airplay
│   │   ├── dlna
│   │   │   ├── discovery
│   │   │   ├── description
│   │   │   └── control
│   │   └── http
│   └── main.c
├── xml
├── README.md / README_CN.md
└── ROADMAP.md
```

Recommended reading order:

1. [Player层设计.md](/Users/ode1l/Documents/VSCode/NX-Cast/docs/Player层设计.md)
2. [源兼容性.md](/Users/ode1l/Documents/VSCode/NX-Cast/docs/源兼容性.md)
3. [DMR实现细节.md](/Users/ode1l/Documents/VSCode/NX-Cast/docs/DMR实现细节.md)
4. [render设计.md](/Users/ode1l/Documents/VSCode/NX-Cast/docs/render设计.md)

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
