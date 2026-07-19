# NX-Cast Release Notes

## Release Highlights

NX-Cast now combines its DLNA media receiver with an integrated IPTV player for Nintendo Switch homebrew.

This build also includes an experimental AirPlay URL/HLS receiver path with PIN pairing. It has automated host and Switch build coverage but is not yet claimed as real-device compatible. Standard iPhone screen mirroring remains disabled at discovery because the FairPlay unwrap boundary is unresolved.

### IPTV

- Import local or remote M3U/M3U8 playlists and open direct media URLs.
- Add, remove, and refresh remote sources with persistent SD-card caching.
- Browse channels by playlist group, search by channel metadata, and keep Favorites and Recent lists.
- Read plain or gzip XMLTV programme guides and show current/next programme information.
- Cache `tvg-logo` metadata and image files for future UI use.
- Open the channel browser during playback and switch channels without returning to Home.
- Use controller or left-stick navigation throughout the channel and source screens.

### Playback And UI

- Hardware-accelerated playback through libmpv, FFmpeg, nvtegra, and deko3d.
- DLNA controls for play, pause, stop, seek, and volume.
- Controller and touch playback overlay with timeline seeking.
- Safer IPTV channel replacement that tears down the previous stream before loading the next one.
- Single-owner arbitration prevents stale DLNA, IPTV, or AirPlay sessions from controlling a newer playback source.

### Experimental AirPlay

- Native DNS-SD discovery, persistent RTSP/HTTP control, PIN pairing, and trusted-client storage.
- Direct URL/HLS playback, pause, seek, rate, status, and stop commands through the existing hardware player.
- Runtime identity and pairing files remain private on the SD card and are excluded from release packages.
- AirPlay 2 multi-room/audio-only playback, AWDL, DRM, and screen mirroring are not supported.

### Install

Download `NX-Cast-sdmc.zip` and extract it directly to the root of the Switch SD card. The package includes the NRO, DLNA runtime files, fonts, IPTV configuration examples, the AirPlay storage skeleton, and dependency notices.

NX-Cast does not provide subscription channels, credentials, DRM bypass, or regional access. Use playlists and streams that you are authorized to access.
