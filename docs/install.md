# Install NX-Cast

`NX-Cast` is distributed as a Switch homebrew SD card package. The recommended release asset is `NX-Cast-sdmc.zip`.

## Recommended Install

1. Download `NX-Cast-sdmc.zip` from the GitHub Release.
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
    airplay/
      README.txt
    licenses/
      LICENSE.NX-Cast.txt
      LICENSE.Dear-ImGui.txt
      LICENSE.libsodium.txt
      THIRD-PARTY-NOTICE.txt
```

`NX-Cast.nro` is the application. The `dlna/` directory contains runtime device and service description files. The `fonts/` directory contains the packaged UI font. Copy local `.m3u` or `.m3u8` playlists into the `iptv/` directory. The `airplay/` directory initially contains only a privacy notice; NX-Cast creates private identity and pairing files there at runtime.

## NRO-Only Install

The release also provides `NX-Cast.nro` as a standalone file. This is useful for quick updates, but the SD package is safer for normal users.

If installing manually:

1. Create `sdmc:/switch/NX-Cast/`.
2. Copy `NX-Cast.nro` to `sdmc:/switch/NX-Cast/NX-Cast.nro`.
3. Copy the release `dlna/`, `fonts/`, `iptv/`, `airplay/`, and `licenses/` folders if you are not using `NX-Cast-sdmc.zip`.

## Why There Is No Installer

Switch homebrew apps normally run from the SD card through `hbmenu`. A separate installer would need to copy files on-device and handle partial installs, file permissions, and rollback. That adds failure modes without much benefit.

The release zip is therefore the installer: it is already laid out exactly as the SD card should look. Extracting it to the SD root installs the app and its runtime assets in one step.

## Troubleshooting

- If `NX-Cast` does not appear in `hbmenu`, check that the final path is `sdmc:/switch/NX-Cast/NX-Cast.nro`.
- If the app starts but DLNA discovery fails, confirm the Switch and sender device are on the same Wi-Fi.
- If the UI font looks wrong, reinstall with `NX-Cast-sdmc.zip` so `sdmc:/switch/NX-Cast/fonts/` is present.
- If playback fails, use the latest release package rather than copying an older `NRO` over a mismatched SD layout.
- If experimental AirPlay startup fails, reinstall the full current build and preserve write access to `sdmc:/switch/NX-Cast/airplay/`. Deleting `identity.bin` and `pairings.bin` resets the AirPlay identity and trusted devices.
