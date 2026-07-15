# NX-Cast Documentation

This directory keeps implementation notes, design boundaries, and future plans for `NX-Cast`.

Most documents are written in Chinese because the project design discussion is currently Chinese-first.

## Recommended Reading

1. [DMR实现细节.md](DMR实现细节.md)
2. [Player层设计.md](Player层设计.md)
3. [render设计.md](render设计.md)
4. [SCPD模块说明.md](SCPD模块说明.md)
5. [SOAP模块说明.md](SOAP模块说明.md)
6. [多线程设计.md](多线程设计.md)
7. [源兼容性.md](源兼容性.md)

## Build And Media Toolchain

- [libmpv依赖安装.md](libmpv依赖安装.md)
- [FFmpeg与mpv自编工具链教程.md](FFmpeg与mpv自编工具链教程.md)

Use these when debugging `libmpv`, `FFmpeg`, `deko3d`, `hos-audio`, or `nvtegra` support.

## Protocol

- [DMR实现细节.md](DMR实现细节.md)
- [SCPD模块说明.md](SCPD模块说明.md)
- [SOAP模块说明.md](SOAP模块说明.md)
- [ConnectionManager-SinkProtocolInfo设计.md](ConnectionManager-SinkProtocolInfo设计.md)

Use these when working on DLNA discovery, description, SOAP actions, GENA, or protocol state sync.

## Player And UI

- [Player层设计.md](Player层设计.md)
- [Player入口资源选择设计.md](Player入口资源选择设计.md)
- [render设计.md](render设计.md)
- [多线程设计.md](多线程设计.md)

Use these when working on playback control, `libmpv` backend integration, renderer lifecycle, or the player overlay.

## Product Planning

- [IPTV与GUI实施规划.md](IPTV与GUI实施规划.md)
- [桌面快捷方式方案.md](桌面快捷方式方案.md)
- [源兼容性.md](源兼容性.md)
- [../ROADMAP.md](../ROADMAP.md)

Use these when planning future GUI, IPTV, source compatibility, or Switch desktop integration work.

## Reference PDFs

The UPnP PDFs are protocol references. They are not required for normal development, but they are useful when checking AVTransport, RenderingControl, ContentDirectory, or MediaRenderer behavior.

## Maintenance Rule

If documentation and source code disagree, current source code wins. Update the affected document in the same change whenever behavior or architecture changes.
