# NX-Cast

<p align="center">
  <img src="assets/icon/switch-screencast-logo.svg" alt="NX-Cast logo" width="180">
</p>

**An open-source media center for Nintendo Switch homebrew.**

Cast from your phone, watch live TV, or mirror your iPhone screen. NX-Cast brings DLNA, IPTV and experimental AirPlay together in one hardware-accelerated player.

[Download v0.3.2](https://github.com/Ode1l/NX-Cast/releases/tag/v0.3.2) | [Latest Stable Release](https://github.com/Ode1l/NX-Cast/releases/latest) | [简体中文](README_CN.md)

## Features

| Mode | What you can do |
|---|---|
| DLNA | Cast media from compatible apps; control playback, pause, seek and volume from the sender. |
| Live TV / IPTV | Import local or remote M3U/M3U8 playlists; browse groups, search, Favorites and Recent; switch channels while watching. |
| AirPlay (experimental) | Cast compatible video URLs/HLS streams or mirror an iPhone screen with audio and PIN pairing. |

- Hardware-accelerated playback through libmpv, FFmpeg, nvtegra and deko3d.
- Chinese/English interface, Chinese channel/programme names and text subtitles.
- Controller and touch controls, including single Joy-Con navigation.
- Channel logos and current/next programme information when the playlist and programme guide provide them.
- SD-card source management, favorites, history and cache.
- Clean Home screen with casting status and the latest error, rather than a debug console.

### New In 0.3.2

iPhone screen mirroring now has working audio, including when audio starts after video. This release fixes mirroring crashes and audio/video clock handling, improves stream transitions, and includes the corrected Switch FFmpeg 7.1-4 dependency. Mirroring hides the seek timeline; channel-menu controls only appear for IPTV.

DLNA and IPTV are supported release features. **AirPlay remains experimental**, despite successful real iPhone/Switch testing: app behavior and media formats vary, and this is not a complete or Apple-certified AirPlay 2 implementation.

See [CHANGELOG.md](CHANGELOG.md) for the full release history.

## Install

Use a Nintendo Switch running an Atmosphère/homebrew environment with access to hbmenu.

1. Download **NX-Cast-sdmc.zip** from the [latest stable release](https://github.com/Ode1l/NX-Cast/releases/latest).
2. Extract it directly to the root of the Switch SD card.
3. Launch NX-Cast from hbmenu.

Keep the complete folder together:

```text
switch/
  NX-Cast/
    NX-Cast.nro
    dlna/
    fonts/
    iptv/
      sources.txt
    airplay/
    licenses/
```

Do not add an extra enclosing folder. Fonts and DLNA resources are included; **end users do not install FFmpeg separately**.

Back up customized settings and sources before extracting an update. AirPlay identity and trusted-pairing files are generated on your Switch in `switch/NX-Cast/airplay/`; keep them private and do not include them in shared packages or bug reports.

**Do not download Continuous for normal use.** It is a rolling development build for maintainer-requested testing, not the stable release.

More installation and troubleshooting details: [docs/install.md](docs/install.md).

## Getting Started

### Cast From Your Phone

Keep the Switch and phone on the same local network and leave NX-Cast running.

- **DLNA:** open a compatible app's cast/device selector and choose NX-Cast.
- **AirPlay video:** choose NX-Cast in a compatible app's AirPlay selector.
- **iPhone screen mirroring:** open Control Center, select Screen Mirroring and choose NX-Cast. Enter the PIN shown on the Switch if prompted.

The casting card on Home is a status display, not a button. Start casting on the phone. AirPlay audio-only/music playback is not supported; an app selecting an audio-only route is not equivalent to screen mirroring or video casting.

### Watch Live TV

Open **Live TV** on Home with `A`, `X` or touch. You can:

- Copy local `.m3u` / `.m3u8` playlists to `switch/NX-Cast/iptv/`; they are scanned at startup.
- Add a remote playlist URL through **Sources > Manage sources > Add URL**.
- Preconfigure long URLs in `switch/NX-Cast/iptv/sources.txt` instead of typing them on the Switch.

Example `sources.txt`:

```text
https://example.com/channels.m3u
My IPTV | https://example.com/channels.m3u | https://example.com/guide.xml
```

The third field is an optional programme-guide URL. Guides declared by the playlist can also be imported automatically. A guide supplies programme information, not the video streams themselves.

The packaged `sources.txt` includes public presets. Stream availability depends on the source, network and access rights. Channel libraries grow within memory budgets rather than a fixed page limit; use filters and search for large lists.

See [docs/iptv.md](docs/iptv.md) for formats, guide matching and SD-card storage details.

## Controls

| Screen | Controller | Touch |
|---|---|---|
| Home | `A`: activate focused control; `X` / stick click: Live TV; up/down: language / Live TV; `Y`: refresh sources; `-`: enter URL; `B`: return to active player | Tap Live TV or the language control |
| Channels | Direction buttons / either stick: browse; `L/R` or left/right: move between toolbar, list and actions; `A/SR`: confirm; `Y`: favorite; `B/SL`: back | Drag list with inertia or use scrollbar; tap a row, then Play |
| Player | `A`: play/pause; `L/R` or left/right: seek 10 seconds; up/down: volume; `-`: show controls; `B`: Home; `+`: exit | Tap to show/hide controls; tap center to play/pause; drag timeline to preview, release to seek |
| IPTV playback | `X` / stick click: channel drawer; `X`: full list; `A`: switch channel; `B`: back | Use the channel drawer and its full-list control |

During playback, either stick's horizontal axis seeks and its vertical axis changes volume. Hold seek buttons for larger jumps. Seek/timeline controls require a seekable stream; live TV and screen mirroring do not show an on-demand timeline. The IPTV channel action is not shown during DLNA/AirPlay playback.

Single horizontal Joy-Con, paired Joy-Cons, handheld controls, Pro Controller and touch can each browse and select channels independently. A single Joy-Con uses `SR` to confirm and `SL` to return. Home supports Chinese/English switching and saves the preference to the SD card.

## Compatibility And Limits

NX-Cast is a homebrew media receiver/player, not a DLNA media server, media controller or native app for any streaming platform.

- No subscription credentials, DRM bypass or regional-access bypass.
- No complete AirPlay 2 multi-room functionality, audio-only/music player, AWDL or HEVC mirroring.
- No guarantee that every app or protected stream supports casting.
- IPTV currently shows current/next programmes; recording, timeshift and full-day EPG grids are not implemented.

Use streams and playlists you are authorized to access. AirPlay capability and protocol details are documented in [docs/AIRPLAY_DEVELOPMENT.md](docs/AIRPLAY_DEVELOPMENT.md) and [docs/AIRPLAY_PROTOCOL_COMPATIBILITY.md](docs/AIRPLAY_PROTOCOL_COMPATIBILITY.md).

## Build

These instructions are for developers. Ordinary users only need the installation ZIP.

### Docker

Docker uses the same media dependency recipe as GitHub Actions:

```bash
./scripts/docker_build_release.sh
```

Output: `dist/NX-Cast-sdmc.zip`.

The image installs Wiliwili's libuam and deko3d libmpv packages, plus NX-Cast's pinned **Switch FFmpeg 7.1-4** and official devkitPro dependencies. The downloader verifies the FFmpeg SHA-256 before installation.

### Local devkitPro

First configure devkitPro/devkitA64, libnx and the Switch portlibs required by the media toolchain. The [toolchain guide](docs/ffmpeg-mpv-toolchain.md) covers setup and dependencies.

From the repository root, with the default `/opt/devkitpro` installation:

```bash
source /opt/devkitpro/switchvars.sh
sudo dkp-pacman -S --needed switch-libsodium

base_url="https://github.com/xfangfang/wiliwili/releases/download/v0.1.0"
sudo dkp-pacman -U \
  "$base_url/libuam-f8c9eef01ffe06334d530393d636d69e2b52744b-1-any.pkg.tar.zst"

make install-airplay-ffmpeg
make verify-airplay-ffmpeg

sudo dkp-pacman -U \
  "$base_url/switch-libmpv_deko3d-0.36.0-2-any.pkg.tar.zst"

make RELEASE_JOBS=4 release-build
NXCAST_MIN_NRO_SIZE=5000000 ./scripts/package_release.sh
```

The commands above set up a fresh toolchain. After that, **build/rebuild automatically prepares FFmpeg**: plain `make`, development, trace and release targets check the pinned package before compiling. The correct installed version skips downloading and installation entirely. A missing or outdated version is fetched from the [NX-Cast toolchain release](https://github.com/Ode1l/NX-Cast/releases/tag/toolchain-ffmpeg-7.1-4), SHA-256 checked and installed; only installation requests `sudo` on a normal user account. Existing VS Code Build/Rebuild tasks use this same path.

`make install-airplay-ffmpeg` remains available for initial setup or explicit dependency preparation. Downloaded packages are cached, and FFmpeg is not rebuilt. Do not substitute Wiliwili's older FFmpeg or a desktop upstream build: the release requires the Matroska muxer, Switch-native random bytes and nvtegra reference fix. Reproducing the dependency from its pinned sources is available separately through `make build-airplay-ffmpeg`.

For an intentionally separate, manually managed toolchain prefix, use `NXCAST_AUTO_INSTALL_FFMPEG=0`. Clean, host-test and dry-run commands do not automatically install packages.

For local development or diagnostic builds:

```bash
make dev-build BUILD_JOBS=4
make full-trace-rebuild BUILD_JOBS=4
```

The Full Trace target enables media, input and AirPlay diagnostics. Normal builds do not enable Trace; `release-build` explicitly disables it and validates the required hardware/crypto/media capabilities before packaging.

### GitHub Actions

- Pushes to `main` run tests/build/packaging and update the **DO NOT DOWNLOAD - NX-Cast Continuous (Development Build)** prerelease.
- Pull requests targeting `main` build without publishing a release.
- A new `v*` tag triggers the stable release workflow and uploads the complete SD ZIP.

The content-hashed GHCR toolchain image is reused until its dependency inputs change. Ordinary application builds download a prebuilt FFmpeg package; they do not compile FFmpeg from scratch.

Version metadata must be updated before tagging a new release. The published `v0.3.2` tag already exists; do not recreate or move it. See [docs/ci-toolchain.md](docs/ci-toolchain.md) for build/release infrastructure.

## Documentation And Licenses

Start with [docs/README.md](docs/README.md). Player, rendering and threading design documents are linked there; user-facing IPTV and installation instructions are linked above.

See [LICENSE](LICENSE) and [third_party/NOTICE.md](third_party/NOTICE.md) for licensing and bundled dependencies. The experimental AirPlay path includes a fixed-source GPL PlayFair compatibility backend; inclusion does not imply Apple certification.
