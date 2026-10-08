# NX-Cast Documentation

This directory keeps design boundaries, implementation notes, toolchain guidance, and future product plans for `NX-Cast`.

All Markdown documents in this directory use English file names and English content. The UPnP PDF files are protocol references and are intentionally kept as-is.

## Recommended Reading

1. [install.md](install.md)
2. [developer-workflow.md](developer-workflow.md)
3. [dmr-implementation.md](dmr-implementation.md)
4. [player-layer.md](player-layer.md)
5. [render-design.md](render-design.md)
6. [scpd-module.md](scpd-module.md)
7. [soap-module.md](soap-module.md)
8. [threading-design.md](threading-design.md)
9. [c-safety.md](c-safety.md)
10. [source-compatibility.md](source-compatibility.md)
11. [AIRPLAY_DEVELOPMENT.md](AIRPLAY_DEVELOPMENT.md)
12. [AIRPLAY_PROTOCOL_COMPATIBILITY.md](AIRPLAY_PROTOCOL_COMPATIBILITY.md)

## Build And Media Toolchain

- [developer-workflow.md](developer-workflow.md): current macOS/Windows setup, VS Code tasks, uploads, logs and release publication.
- [ci-toolchain.md](ci-toolchain.md)
- [libmpv-dependencies.md](libmpv-dependencies.md)
- [ffmpeg-mpv-toolchain.md](ffmpeg-mpv-toolchain.md)
- [c-safety.md](c-safety.md)

Use these when debugging `libmpv`, `FFmpeg`, `deko3d`, `hos-audio`, or `nvtegra` support.

## Historical Diagnostics And Handoffs

- [AIRPLAY_FREEZE_DIAGNOSTICS.md](AIRPLAY_FREEZE_DIAGNOSTICS.md)
- [MACOS_HANDOFF_2026-07-23.md](MACOS_HANDOFF_2026-07-23.md)
- [LOCAL_VSCODE_DIAGNOSTIC_WORKFLOW.md](LOCAL_VSCODE_DIAGNOSTIC_WORKFLOW.md)
- [Sanitized nxlink log samples](../sample/nxlink-logs/README.md)

These preserve earlier investigations and test setups. Their task names and
experimental options are not the current development entry points. Use
[developer-workflow.md](developer-workflow.md) for today's build, upload and
logging instructions.

The [sanitized nxlink log samples](../sample/nxlink-logs/README.md) preserve the
hardware test timelines referenced by the AirPlay/DLNA handoff without
publishing signed media URLs, LAN addresses, device identity material, or host
paths.

## Protocol

- [dmr-implementation.md](dmr-implementation.md)
- [scpd-module.md](scpd-module.md)
- [soap-module.md](soap-module.md)
- [connection-manager-sink-protocol-info.md](connection-manager-sink-protocol-info.md)
- [AIRPLAY_DEVELOPMENT.md](AIRPLAY_DEVELOPMENT.md)
- [AIRPLAY_PROTOCOL_COMPATIBILITY.md](AIRPLAY_PROTOCOL_COMPATIBILITY.md)

Use these when working on DLNA discovery, device/service description, SOAP actions, GENA, or protocol state sync.

## Player And UI

- [iptv.md](iptv.md)
- [player-layer.md](player-layer.md)
- [player-open-path.md](player-open-path.md)
- [render-design.md](render-design.md)
- [threading-design.md](threading-design.md)

Use these when working on playback control, `libmpv` backend integration, renderer lifecycle, or the player overlay.

## Product Planning

- [casting-protocol-development.md](casting-protocol-development.md): Miracast feasibility first, Google Cast candidate; neither is implemented yet.
- [iptv-gui-plan.md](iptv-gui-plan.md)
- [desktop-shortcut.md](desktop-shortcut.md)
- [source-compatibility.md](source-compatibility.md)

Use these when planning future GUI, IPTV, source compatibility, or Switch desktop integration work.

## Reference PDFs

The UPnP PDFs are protocol references. They are not required for normal development, but they are useful when checking AVTransport, RenderingControl, ContentDirectory, or MediaRenderer behavior.

## Maintenance Rule

If documentation and source code disagree, current source code wins. Update the affected document in the same change whenever behavior or architecture changes.
