# NX-Cast v0.3.0 Release Notes

## Release Highlights

NX-Cast v0.3.0 combines its DLNA media receiver and IPTV player with experimental AirPlay video playback for Nintendo Switch homebrew.

This release includes experimental AirPlay URL/HLS playback and iPhone screen mirroring with PIN pairing. These paths now have automated host/Switch build coverage and real-device testing, but remain experimental and are not a complete AirPlay 2 implementation.

### IPTV

- Import local or remote M3U/M3U8 playlists and open direct media URLs.
- Add, remove, and refresh remote sources with persistent SD-card caching.
- Browse channels by playlist group, search by channel metadata, and keep Favorites and Recent lists.
- Read plain or gzip XMLTV programme guides and show current/next programme information.
- Cache `tvg-logo` metadata and image files for future UI use.
- Open the channel browser during playback and switch channels without returning to Home.
- Use controller or left-stick navigation throughout the channel and source screens.
- Load up to 4,096 channels (586 seven-row pages) and render full CJK channel/programme names.

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

NX-Cast does not provide subscription channels, credentials, DRM bypass, or regional access. Use playlists and streams that you are authorized to access.
