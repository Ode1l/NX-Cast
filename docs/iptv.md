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

`tvg-logo` files are cached asynchronously and displayed through a small deko3d texture cache. Channel names and programme metadata remain in the source's original language.

## Controls

The full-screen library shows eight rows; the playback drawer shows nine.
There are no page buttons. Hold a direction to scroll faster, or use Search,
Categories, Favorites, Recent and the independent source filter to narrow a list.

- Home: A opens the focused Live TV card; X or a stick click also opens the library.
- Home: up/down focuses language/Live TV; A or touch switches Chinese/English.
- Library: up/down or either stick moves selection. Left/right or L/R moves
  between the toolbar, list and action bar. A/SR activates the focused item.
- Touch: drag the list or scrollbar, tap a row to select, then tap Play.
  A scrolling gesture never triggers playback.
- Y or the Favorite action toggles the selected channel's favorite status.
- Search accepts an empty query to clear the search.
- Sources opens a selector with All sources and Manage sources.
- Manage sources: select a source, then choose Add URL, Scan SD, Refresh,
  Programme guide, or Delete. Delete requires confirmation.
- B/SL dismisses selectors, returns from source management, or closes the browser.
- During IPTV playback X or a stick click opens a dark left drawer. X or the
  Full list button expands it without stopping playback. B collapses the full
  list to the drawer, then closes the drawer. Switching channels preserves
  the browser so another channel can be selected.
- Menu input is isolated from underlying playback pause, seek and volume actions.

Handheld controls, either stick, paired Joy-Cons, Pro Controller, a single
Joy-Con (SR confirm / SL back), and touch can navigate independently.
Local sources are removed by deleting their M3U file from the SD card.

## Capacity

Channels, sources, groups and favorites now grow on demand instead of using
fixed 4,096-channel, 32-source and 64-group arrays. The channel storage budget
is 128 MiB per catalog; metadata arrays each have an 8 MiB budget. A rebuild
temporarily holds both the old and new catalog. Allocation or budget failures
are reported and leave the existing catalog usable; this is not infinite storage.

Recent history retains the last 32 channels. Download-size safety limits and
Switch memory still apply. Large libraries should use source/category filters
and search rather than traversing the entire list. Rendering reads only the
visible rows; filter counts are cached when the view is rebuilt.

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

## Remaining Scope

NXMP remains broader: network file systems, Enigma2, full-day EPG grids,
playlist editing, recording and timeshift are not implemented here.
The current guide displays the current/next programme when matching XMLTV
data is available; it cannot infer a channel's programme from video alone.

NX-Cast does not provide subscription credentials, DRM bypass or regional
access. Users must supply authorized playlists and streams.
