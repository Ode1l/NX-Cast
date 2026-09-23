# Changelog

All notable user-facing changes to NX-Cast are documented here.

## [0.3.1] - 2026-09-23

### Added

- Bilingual Home with persistent language selection, passive casting status and a prominent Live TV entry.
- Full-screen channel library and transparent playback drawer with shared C input state, accelerated continuous scrolling, touch inertia and draggable scrollbars.
- Category and source selectors alongside Favorites, Recent and Search; touch-accessible source management with delete confirmation.
- Dynamically sized channels, sources, groups and favorites, with explicit memory budgets and atomic catalog replacement instead of fixed entry-count truncation.

### Changed

- IPTV live controls omit the playback timeline and local seeking; tap outside the playback channel drawer to dismiss it. Home feature icons now use matching thin-line graphics.
- Language preferences use SD-compatible replacement with recovery on interrupted writes and detailed storage failure logging.
- Lighter player overlay with a white timeline, smaller outlined key hints, translucent center controls, compact loading animation and single-line scrolling titles. Unknown-duration streams no longer show a fake timeline; non-seekable media has no seek thumb/hint.
- Distribution attachments contain the complete SD-card ZIP rather than a standalone NRO; AirPlay runtime identity files are never copied from development assets.
- Local M3U/M3U8 files beside `iptv/sources.txt` are discovered at startup and can be rescanned from Sources.
- Channel selection and playback are separate actions; menu input cannot seek or pause the underlying video.

## [0.3.0] - 2026-09-19

### Added

- Experimental AirPlay DNS-SD discovery, PIN pairing, URL/HLS controls, and private SD-card identity storage.
- Experimental H.264/AAC screen mirroring with a fixed-source GPL PlayFair compatibility backend, MPEG-TS bridge, media clock, and nvtegra/deko3d playback integration; representative real iPhone/Switch paths have been validated, but compatibility remains experimental.
- Deterministic PlayFair stage-one/key tests, receiver capability smoke coverage, provenance, and packaged GPL license text.
- Generation-safe player ownership across DLNA, IPTV, AirPlay remote video, and AirPlay mirroring.
- AirPlay host protocol/media tests and strict Ed25519 release-build enforcement.
- Protocol-neutral media ownership and lifecycle coordination across AirPlay, DLNA, and IPTV.
- Full CJK UI glyph coverage with a packaged Source Han font and Switch shared-font fallback.

### Changed

- Shutdown now stops AirPlay/DLNA workers before player, UI, logs, and network teardown.
- Release packages include AirPlay storage, libsodium, and PlayFair notices while rejecting private identities, pairings, keys, logs, traces, dumps, and captures.
- IPTV channel capacity increased from 1,024 to 4,096 entries (up to 586 seven-row pages).
- Media caching, initial rendering, replacement playback, and controller-session handling were hardened for long-running network streams.

### Fixed

- Fixed stale AirPlay sessions preventing subsequent videos from replacing the current media.
- Fixed player ownership races during cross-protocol takeover and asynchronous stop/reload paths.
- Fixed benign FFmpeg tail errors after Stop appearing as persistent Home-screen failures.
- Fixed Chinese IPTV channel names rendering as question marks when packaged or optional Switch fonts were unavailable.

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
