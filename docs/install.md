# Install NX-Cast

`NX-Cast` is distributed as a Switch homebrew SD card package. The recommended release asset is `NX-Cast-sdmc.zip`.

## Recommended Install

1. Download `NX-Cast-sdmc.zip` from the [latest stable GitHub Release](https://github.com/Ode1l/NX-Cast/releases/latest), not the Continuous development build.
2. Power off the Switch or safely remove the SD card.
3. Extract the zip to the root of the SD card.
4. Confirm the SD card contains this path:

```text
sdmc:/switch/NX-Cast/NX-Cast.nro
```

5. Put the SD card back into the Switch.
6. Open `hbmenu`.
7. Launch `NX-Cast`.

The zip already contains the expected `switch/NX-Cast/` directory layout. Do not extract it into an extra nested folder such as `sdmc:/NX-Cast-sdmc/switch/...`.

Back up customized settings and `iptv/sources.txt` before extracting an update: files at matching paths may be overwritten. FFmpeg is statically linked into the application; users do not install the separate developer toolchain package on their SD card.

## Package Contents

```text
switch/
  NX-Cast/
    NX-Cast.nro
    dlna/
      AVTransport.xml
      ConnectionManager.xml
      Description.xml
      Presentation.html
      RenderingControl.xml
      SinkProtocolInfo.csv
      icon.jpg
    fonts/
      switch_font.ttf
      LICENSE.SourceFont.txt
      FREEWARE.ControllerFont.txt
    iptv/
      README.txt
      sources.txt
    airplay/
      README.txt
    licenses/
      LICENSE.NX-Cast.txt
      LICENSE.Dear-ImGui.txt
      LICENSE.libsodium.txt
      LICENSE.PlayFair.GPLv3.txt
      THIRD-PARTY-NOTICE.txt
```

`NX-Cast.nro` is the application. The `dlna/` directory contains runtime device and service description files. The `fonts/` directory contains the packaged Source Han font used by Chinese UI metadata and text subtitles. Copy local `.m3u` or `.m3u8` playlists into the `iptv/` directory. The `airplay/` directory initially contains only a privacy notice; NX-Cast creates private identity and pairing files there at runtime.

`hbmenu` scans directories below `sdmc:/switch/`. A data-only
`sdmc:/switch/NX-Cast/` directory can therefore appear as a folder during
`nxlink` development. The published SD zip is checked during packaging and
always places exactly one launchable NRO inside that directory, so normal users
see the NX-Cast application entry rather than an empty data folder.

## NRO-Only Install

Current releases provide one complete `NX-Cast-sdmc.zip`, not a separate NRO asset. NRO-only installation is a developer workflow for a locally compiled binary or a binary extracted from that ZIP; it is not recommended for a first install.

If installing manually:

1. Create `sdmc:/switch/NX-Cast/`.
2. Copy `NX-Cast.nro` to `sdmc:/switch/NX-Cast/NX-Cast.nro`.
3. Copy the matching package's `dlna/`, `fonts/`, `iptv/`, `airplay/`, and `licenses/` folders. Do not mix an older asset layout with a newer NRO.

Nxlink uploads only the binary, not these SD resources. Existing AirPlay identity/pairing files are private device data and should be preserved during updates, never copied from another user's device.

## Why There Is No Installer

Switch homebrew apps normally run from the SD card through `hbmenu`. A separate installer would need to copy files on-device and handle partial installs, file permissions, and rollback. That adds failure modes without much benefit.

The release zip is therefore the installer: it is already laid out exactly as the SD card should look. Extracting it to the SD root installs the app and its runtime assets in one step.

## Troubleshooting

- If `NX-Cast` does not appear in `hbmenu`, check that the final path is `sdmc:/switch/NX-Cast/NX-Cast.nro`.
- If the app starts but DLNA discovery fails, confirm the Switch and sender device are on the same Wi-Fi.
- If the UI font looks wrong, reinstall with `NX-Cast-sdmc.zip` so `sdmc:/switch/NX-Cast/fonts/` is present.
- If playback fails, use the latest release package rather than copying an older `NRO` over a mismatched SD layout.
- If experimental AirPlay startup fails, reinstall the full current build and preserve write access to `sdmc:/switch/NX-Cast/airplay/`. Deleting `identity.bin` and `pairings.bin` resets the AirPlay identity and trusted devices.

## Reporting A Problem

Report reproducible problems in [GitHub Issues](https://github.com/Ode1l/NX-Cast/issues).
Include the following rather than only saying that playback failed:

- The version shown in NX-Cast, the download page and whether it is a stable release, Continuous or your own build. For a local build, include the commit and build/Trace options.
- Switch system and Atmosphere versions, hbmenu launch mode, and whether custom clocks or other system modifications are enabled.
- The playback path: IPTV, DLNA, AirPlay from inside an app, or Control Center screen mirroring/audio. Include the sender device, OS and app version where relevant.
- Steps from starting NX-Cast to the failure, expected versus actual behavior, approximate failure time, and whether it affects one source or all sources. Distinguish buffering, loss of phone control, return to Home and an application crash.
- Any on-screen error and a relevant log or crash report if available. A screenshot or short recording can help with rendering problems; logs are not a prerequisite for opening a report.

Developers can capture one focused reproduction with
`NX-Cast: Full Trace & Upload + Logs`; see the
[developer workflow](developer-workflow.md). The upload script saves logs under
`logs/run_nxlink-*.log`. Keep the startup and surrounding failure context, not
just individual ERROR lines. Note when you deliberately closed NX-Cast:
`Connection reset by peer` at shutdown alone is not evidence of the playback cause.

For a crash, retain the matching crash report and, for a local build, the exact
`NX-Cast.elf` before rebuilding. A different build's ELF cannot reliably locate
the crashed instructions. Keep original evidence privately for follow-up.

Before posting publicly, inspect and redact signed media URLs, tokens, cookies,
playlist credentials, device identifiers, private network addresses and personal
file paths. Never upload AirPlay `identity.bin`, `pairings.bin`, private keys or
an entire SD-card data directory. Crash reports and memory dumps can also contain
sensitive data; do not publish raw dumps without review. Replace sensitive fields
consistently so event order and connection relationships remain understandable.
