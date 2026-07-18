## Release Highlights

NX-Cast now combines its DLNA media receiver with an integrated IPTV player for Nintendo Switch homebrew.

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

### Install

Download `NX-Cast-sdmc.zip` and extract it directly to the root of the Switch SD card. The package includes the NRO, DLNA runtime files, fonts, and IPTV configuration examples.

NX-Cast does not provide subscription channels, credentials, DRM bypass, or regional access. Use playlists and streams that you are authorized to access.
