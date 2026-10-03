# NX-Cast v0.3.2 Release Notes

This release fixes iPhone screen-mirroring sound and crashes, improves AirPlay stream transitions, and uses the corrected Switch FFmpeg 7.1-4 dependency. The visible application version is 0.3.2.

## Release Highlights

NX-Cast combines its DLNA media receiver and IPTV player with experimental AirPlay video playback for Nintendo Switch homebrew.

This release includes experimental AirPlay URL/HLS playback and iPhone screen mirroring with PIN pairing. These paths now have automated host/Switch build coverage and real-device testing, but remain experimental and are not a complete AirPlay 2 implementation.

### New In 0.3.2

- Working mirrored audio when the phone adds an audio stream after video has started, without reloading video or waiting for another keyframe.
- Corrected NTP audio/video clock handling, FFmpeg Switch random-seed generation and nvtegra H.264 long-term reference selection.
- Per-stream teardown/re-setup preserves ongoing video mirroring across audio transitions.
- Screen mirroring hides seek timelines and IPTV channel-menu hints; channel controls remain limited to IPTV playback.
- Safer deko3d frame-resource transitions between Home and playback.
- Formal builds use normal diagnostics with Trace disabled. Continuous downloads carry an explicit development-only warning.

### IPTV

- Import local or remote M3U/M3U8 playlists and open direct media URLs.
- Add, remove, and refresh remote sources with persistent SD-card caching.
- Browse channels by playlist group, search by channel metadata, and keep Favorites and Recent lists.
- Read plain or gzip XMLTV programme guides and show current/next programme information.
- Cache and display channel logos alongside current programme information.
- Open the channel browser during playback and switch channels without returning to Home.
- Use either stick, controller or touch navigation throughout the channel and source screens.
- Browse dynamically sized channel libraries with continuous scrolling, category/source filters, Chinese/English controls and CJK channel/programme names. Capacity is bounded by memory budgets rather than a fixed channel count.

### Playback And UI

- Hardware-accelerated playback through libmpv, FFmpeg, nvtegra, and deko3d.
- DLNA controls for play, pause, stop, seek, and volume.
- Controller and touch playback overlay with timeline seeking.
- Safer IPTV channel replacement that tears down the previous stream before loading the next one.
- Single-owner arbitration prevents stale DLNA, IPTV, or AirPlay sessions from controlling a newer playback source.
- Cleaner release diagnostics: expected decoder tail messages after Stop no longer appear as persistent Home-screen errors.

### Experimental AirPlay

- Native DNS-SD discovery, persistent RTSP/HTTP control, PIN pairing, and trusted-client storage.
- Direct URL/HLS playback, pause, seek, rate, status, and stop commands through the existing hardware player.
- Experimental H.264/AAC screen mirroring through an isolated, fixed-source GPL PlayFair compatibility backend and the existing nvtegra/deko3d player.
- Session and media lifecycles are separated so reconnects, explicit stops, and replacement videos do not reuse stale playback ownership.
- Runtime identity and pairing files remain private on the SD card and are excluded from release packages.
- AirPlay 2 multi-room/audio-only playback, AWDL, commercial FairPlay/DRM content, and Apple certification are not supported.

### Install

Download `NX-Cast-sdmc.zip` and extract it directly to the root of the Switch SD card. The package includes the NRO, DLNA runtime files, fonts, IPTV configuration examples, the AirPlay storage skeleton, and dependency notices.

The install package is distributed as one ZIP, not a separate NRO download. Keep the complete `switch/NX-Cast/` folder together. AirPlay device identities and pairing records are generated on each user's Switch and are not bundled.

Back up customized configuration before extracting an update. **End users do not install FFmpeg separately**: the corrected dependency is statically linked into the NRO. The separate `toolchain-ffmpeg-7.1-4` release is for developers, not an SD-card runtime file.

NX-Cast does not provide subscription channels, credentials, DRM bypass, or regional access. Use playlists and streams that you are authorized to access.
