# NXCast

**NXCast** is an open-source wireless display receiver for the Nintendo Switch running Atmosphère.

The project aims to turn the Nintendo Switch into a wireless media receiver capable of accepting streams via protocols such as **DLNA** and **AirPlay-style media streaming**, without requiring Linux on Switch.

NXCast runs natively on Horizon OS using the homebrew development stack based on **devkitPro** and **libnx**.

---

## Project Goals

NXCast aims to provide:

- Native Horizon OS implementation
- Wireless media receiving capability
- Modular architecture for protocol extensions
- Hardware-accelerated decoding where possible
- Community-maintainable codebase

The long-term goal is to provide a reusable infrastructure for media streaming applications on Nintendo Switch homebrew.

---

## Current Status

Early development stage.

Planned first milestone:

- Basic application framework
- Network initialization
- Device discovery
- DLNA receiver prototype

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
2. Copy the generated `NXCast.nro` to `/switch/nxcast/` on your SD card.
3. Connect the Switch to Wi-Fi, launch NXCast from the Homebrew Menu, and check the console log for the `[net]` initialization messages.
4. Press `+` to exit once you see the successful network message.

---

## Planned Features

### Phase 1
- Application framework
- Network stack
- Device discovery

### Phase 2
- DLNA video receiver
- Basic playback pipeline

### Phase 3
- AirPlay-style video streaming

### Phase 4
- Hardware accelerated decode

### Phase 5
- UI and configuration

---

## Architecture Overview

NXCast uses a layered architecture:

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
nxcast
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
make This produces a `.nro` executable for the Nintendo Switch.
```

---

## Running

Place the `.nro` in:
```text
/switch/nxcast/
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

To publish NXCast as a Switchbrew project, we still need the following project-wide assets in addition to the core implementation:

- Clear licensing info: keep the MIT license file, mention it in `README`, and document third-party licenses referenced by the code and any shipped assets (icons, fonts, etc.).
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

---

## License

MIT License

---

## Disclaimer

NXCast is an independent open-source project and is not affiliated with Nintendo.

Use at your own risk.
