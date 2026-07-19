# Changelog

All notable user-facing changes to NX-Cast are documented here.

## [Unreleased]

### Added

- Experimental AirPlay DNS-SD discovery, PIN pairing, URL/HLS controls, and private SD-card identity storage.
- Internal H.264/AAC mirror receive, MPEG-TS bridge, media clock, and nvtegra/deko3d playback integration; mirroring remains unadvertised pending the FairPlay boundary.
- Generation-safe player ownership across DLNA, IPTV, AirPlay remote video, and AirPlay mirroring.
- AirPlay host protocol/media tests and strict Ed25519 release-build enforcement.

### Changed

- Shutdown now stops AirPlay/DLNA workers before player, UI, logs, and network teardown.
- Release packages include an AirPlay storage notice and libsodium license while rejecting private identities, pairings, keys, logs, traces, dumps, and captures.

## [0.2.0] - 2026-07-19

### Added

- Integrated IPTV playback for local and remote M3U/M3U8 playlists and direct media URLs.
- Persistent SD-card source management, remote playlist caching, Favorites, Recent, groups, and channel search.
- Plain and gzip XMLTV programme-guide support with current/next programme details and progress.
- In-player channel drawer with controller, single Joy-Con, analog-stick, and touch navigation.
- Channel-logo caching and rendering through the ImGui/deko3d UI.
- Release packaging for IPTV presets, runtime DLNA assets, fonts, and installation instructions.

### Changed

- Reworked the player overlay around a Switch-style bottom information hierarchy and safe-area action hints.
- Unified the IPTV channel drawer and source manager around shared typography, spacing, transparency, rounding, and accent colors.
- Reduced loading and playback status chrome so video remains the visual focus.
- Improved DLNA stream startup, autoplay handoff, and player-state synchronization.
- Added strict deko3d/libmpv CI builds and IPTV channel-navigation smoke tests.

### Fixed

- Reduced corruption and instability when replacing an active IPTV channel stream.
- Fixed channel-list paging and selection across controller, stick, and touch input paths.
- Fixed inconsistent player controls, seek presentation, volume state, and bottom safe-area placement.
- Prevented incomplete release packages that omit `iptv/sources.txt`.

## [0.1.1] - 2026-07-16

- Introduced the ImGui/deko3d player overlay and static home renderer.
- Added controller and touch timeline controls with optional media/input tracing.
- Added Docker and GitHub Actions build/release workflows.
- Packaged the UI font with corrected runtime permissions.

## [0.1.0] - 2026-07-15

- Initial public DLNA receiver release for Nintendo Switch homebrew.
