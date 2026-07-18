# NX-Cast Documentation

This directory keeps design boundaries, implementation notes, toolchain guidance, and future product plans for `NX-Cast`.

All Markdown documents in this directory use English file names and English content. The UPnP PDF files are protocol references and are intentionally kept as-is.

## Recommended Reading

1. [install.md](install.md)
2. [dmr-implementation.md](dmr-implementation.md)
3. [player-layer.md](player-layer.md)
4. [render-design.md](render-design.md)
5. [scpd-module.md](scpd-module.md)
6. [soap-module.md](soap-module.md)
7. [threading-design.md](threading-design.md)
8. [source-compatibility.md](source-compatibility.md)

## Build And Media Toolchain

- [libmpv-dependencies.md](libmpv-dependencies.md)
- [ffmpeg-mpv-toolchain.md](ffmpeg-mpv-toolchain.md)

Use these when debugging `libmpv`, `FFmpeg`, `deko3d`, `hos-audio`, or `nvtegra` support.

## Protocol

- [dmr-implementation.md](dmr-implementation.md)
- [scpd-module.md](scpd-module.md)
- [soap-module.md](soap-module.md)
- [connection-manager-sink-protocol-info.md](connection-manager-sink-protocol-info.md)

Use these when working on DLNA discovery, device/service description, SOAP actions, GENA, or protocol state sync.

## Player And UI

- [iptv.md](iptv.md)
- [player-layer.md](player-layer.md)
- [player-open-path.md](player-open-path.md)
- [render-design.md](render-design.md)
- [threading-design.md](threading-design.md)

Use these when working on playback control, `libmpv` backend integration, renderer lifecycle, or the player overlay.

## Product Planning

- [iptv-gui-plan.md](iptv-gui-plan.md)
- [desktop-shortcut.md](desktop-shortcut.md)
- [source-compatibility.md](source-compatibility.md)

Use these when planning future GUI, IPTV, source compatibility, or Switch desktop integration work.

## Reference PDFs

The UPnP PDFs are protocol references. They are not required for normal development, but they are useful when checking AVTransport, RenderingControl, ContentDirectory, or MediaRenderer behavior.

## Maintenance Rule

If documentation and source code disagree, current source code wins. Update the affected document in the same change whenever behavior or architecture changes.
