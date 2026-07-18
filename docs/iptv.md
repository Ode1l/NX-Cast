# IPTV Support

NX-Cast keeps IPTV source, playlist, cache, search, favorite, and EPG handling in C. The C++ ImGui/deko3d frontend only reads snapshots and draws the browser. Playback continues to use the same libmpv/FFmpeg session as DLNA.

## Current Flow

```text
local M3U on SD or remote M3U URL
  -> classify channel list versus direct HLS playlist
  -> download remote files to an atomic SD cache
  -> discover and download url-tvg / x-tvg-url programme guides on the first refresh
  -> parse EXTINF metadata and resolve relative URLs
  -> apply favorites, recent history, group, and search filters
  -> apply cached XMLTV current/next programme data by tvg-id
  -> select a channel
  -> existing libmpv/FFmpeg/deko3d playback path
```

Remote refresh and logo downloads run on one background worker. Rendering and controller state remain on the main thread.

## Supported

- Local and remote `.m3u` / `.m3u8` sources
- Persistent remote source add, remove, refresh, and XMLTV URL configuration
- SD-card preinstalled remote sources through a user-editable `sources.txt`
- Atomic playlist, XMLTV, and channel-logo caches on the SD card
- `#EXTINF`, `tvg-id`, `tvg-name`, `tvg-logo`, `group-title`, `url-tvg`, and `x-tvg-url`
- Relative local URLs, relative remote URLs, root-relative URLs, and protocol-relative URLs
- All, Favorites, Recent, and M3U group filters
- Channel search by name, group, or `tvg-id`
- Persistent favorites and recent playback history
- Plain or gzip XMLTV parsing with current and next programme display
- HLS media/master playlist detection through `#EXT-X-*` tags
- Direct `http`, `https`, `rtsp`, `rtmp`, `udp`, `rtp`, `mms`, `file`, and `sdmc` playback URLs

`tvg-logo` files are downloaded asynchronously when a channel is selected. The current panel reports cache state; decoding the cached image into a deko3d texture is a later UI task.

## Controls

Home:

- `A`: return to the active player when playback is running in the background
- `X` or either stick click: open the IPTV channel browser
- `Y`: refresh all local and remote sources in the background
- `-`: open a media or M3U URL. Recognized playlist URLs are saved as remote sources, refreshed in the background, and opened in Channels.

Channels:

- `A`: play the selected channel
- `Up` / `Down` or either stick vertically: move selection
- `Left` / `Right`, either stick horizontally, or `L` / `R`: move seven channels
- `Y`: add or remove favorite
- `ZL` / `ZR`: cycle All, Favorites, Recent, and playlist groups
- `L3`: enter search text
- `R3`: clear search
- `X`: switch to Sources
- `B`: close the browser
- touch a row to select it; tap it again or tap `PLAY CHANNEL` to play
- swipe vertically or horizontally over the list, or tap the page arrows, to change page

Sources:

- `A`: refresh selected source
- `Up` / `Down` or either stick vertically: move selection
- `Left` / `Right`, either stick horizontally, or `L` / `R`: move seven sources
- `Y`: add a remote M3U URL
- `ZR`: connect or replace the selected remote source's `guide.xml` / XMLTV URL
- `-`: remove the selected remote source

During playback, press `X` or either stick to open the same Channels/Sources panel over the video. Playback continues behind the panel; `A` switches to the selected channel and closes it, while `B` closes it without changing playback.
- `X`: switch to Channels
- `B`: close the browser

All connected standard controllers are merged into the same single-player input. A horizontal single Joy-Con uses its stick for browsing, stick click to open IPTV, `SR` as confirm, and `SL` as back. Handheld mode, paired Joy-Cons, Pro Controllers, separate player controllers, and touch can each operate the channel browser independently.

Local sources are removed by deleting their M3U file from the SD card.

## SD Card Data

```text
sdmc:/switch/NX-Cast/iptv/
  *.m3u / *.m3u8       local sources
  sources.txt           user-editable preinstalled HTTP/HTTPS sources
  sources.example.txt   packaged format example
  sources.tsv           generated remote source database; do not edit
  favorites.txt        stable channel IDs
  recent.txt           recent channel IDs
  cache/playlists/     downloaded remote M3U files
  cache/epg/           downloaded plain/gzip XMLTV files
  cache/logos/         downloaded tvg-logo files
```

The app owns the generated database and cache files. Manual playlists can still be copied directly into the root IPTV directory.

For long remote addresses, create `sources.txt` beside `sources.example.txt` and use either form:

```text
https://example.com/channels.m3u
My IPTV | https://example.com/channels.m3u | https://example.com/guide.xml
```

Blank lines and `#` comments are ignored. Fields must use the ASCII `|` separator. NX-Cast merges entries into its internal source database, deduplicates by playlist URL, and automatically refreshes new entries or entries whose EPG URL changed. Removing a remote source from the Sources screen removes its matching line from `sources.txt`, updates `sources.tsv`, and deletes its cached M3U and EPG files. If `sources.txt` is copied while NX-Cast is running, press `Y` on Home to reload it before refreshing.

Nxlink uploads only `NX-Cast.nro`; it does not synchronize the local `assets/` or `sdmc/` directories. For device testing, copy `sources.txt` to the physical Switch SD path shown above or install the complete `NX-Cast-sdmc.zip` package.

The standard release includes the public presets from `assets/iptv/sources.txt`. Release packaging fails if that file is missing, empty, or is not copied intact into `NX-Cast-sdmc.zip`. Personal URLs containing credentials or tokens should remain only on the physical SD card and must not be committed as distributor presets.

## NXMP Target Gap

The percentages below are engineering estimates, not release guarantees.

```text
Playback reuse and hardware decode     [#########-] 90%
Local and remote M3U/HLS handling      [########--] 80%
Source management and persistence      [#######---] 70%
Search, groups, favorites, and recent  [########--] 80%
Logo metadata and asynchronous cache   [######----] 60%
XMLTV current/next EPG                  [######----] 60%
Full logo texture/image UI              [#---------] 10%
NXMP-style IPTV/media-center target     [#####-----] 50%
```

NXMP remains broader: network file systems, Enigma2, richer EPG grids, playlist editing, settings, and multiple media browsers are not implemented here.

NX-Cast does not provide channels, credentials, DRM bypass, or regional access. Users must supply authorized playlists and streams.
