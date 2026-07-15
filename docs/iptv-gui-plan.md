# IPTV And GUI Plan

This document plans future IPTV and full-application GUI work. It is intentionally separated from the current DLNA DMR playback path because these features should be built incrementally.

## Product Direction

The current product is a DLNA DMR first. IPTV and GUI support should extend the product without breaking:

1. DLNA discovery.
2. SOAP control.
3. `libmpv` playback.
4. `deko3d` rendering.
5. Hardware decode.
6. The local player overlay.

## Scope

### IPTV

IPTV support means:

1. Add and manage M3U/M3U8 playlist sources.
2. Parse channel groups and channel metadata.
3. Open selected channel URLs through the existing player.
4. Cache playlist metadata on SD/save storage.
5. Keep stream playback delegated to `libmpv/FFmpeg`.

It does not mean:

1. Reimplementing HLS.
2. Restoring a generic HLS gateway/proxy.
3. Scraping paid IPTV services.
4. Bypassing DRM, login, or regional restrictions.

### GUI

GUI support means:

1. A home screen.
2. Source management.
3. Channel list and search.
4. Player page.
5. Settings page.
6. Error and loading states.

The player overlay remains part of `source/player/ui/`. A future full GUI can reuse the same input and drawing ideas, but it should not be mixed directly into `main`.

## Existing Playback Chain

The project already has the most important playback path:

```text
URL
  -> player/libmpv
  -> FFmpeg probe/network/demux/decode
  -> deko3d render
  -> overlay
```

The hard work for IPTV is not media playback. The hard work is data modeling, UI, input, persistence, and safe threading.

## Suggested Architecture

```text
source/app/
  home/
  settings/

source/iptv/
  playlist.*
  parser.*
  source_store.*
  channel_store.*

source/gui/
  navigation.*
  screen.*
  widgets.*

source/player/ui/
  overlay.*
  timeline.*
  controls.*
```

Keep GUI and IPTV optional enough that the current DLNA path can continue to compile and run.

## Data Model

Suggested IPTV types:

1. `IptvSource`: name, URL or file path, last refresh time, enabled flag.
2. `IptvChannel`: name, group, logo URL, stream URL, source ID.
3. `IptvPlaylist`: source metadata plus channel list.
4. `IptvStore`: persistent cache and source list.

Persistence should use SD/save storage, not romfs.

## Playlist Handling

Minimum parser support:

1. `#EXTM3U`
2. `#EXTINF`
3. `tvg-id`
4. `tvg-name`
5. `tvg-logo`
6. `group-title`
7. Absolute stream URLs

Relative stream URLs should be resolved against the playlist URL only when the playlist source makes that safe and deterministic.

## GUI Route

Preferred future route: `ImGui + deko3d`, only after auditing license compatibility and integration complexity.

Reasons:

1. The project already owns a `deko3d` path.
2. Switch homebrew projects have proven this route feasible.
3. It can support controller and touch input.
4. It can grow from a small tool UI into a full application shell.

Constraints:

1. GUI, mpv video, and player overlay must share one frame acquisition/present flow.
2. Do not acquire/present once for mpv and once for GUI.
3. Do not call `deko3d` or GUI drawing from background threads.
4. Keep player overlay and app GUI state separate until the rendering order is well defined.

## Frame Order With GUI

Future frame order:

```text
acquire frame
  -> mpv video
  -> player overlay
  -> app GUI
  -> present frame
```

If a full GUI is shown without video, skip the mpv video pass but keep the same acquire/present ownership.

## Input

Minimum input model:

1. `A`: select / play-pause on player page.
2. `B`: back.
3. `+`: exit or app menu depending on context.
4. `L/R`: seek on player page, page switch in lists if needed.
5. D-pad / left stick: navigate lists.
6. Touch: select, scroll, drag timeline.
7. On-screen keyboard: use libnx system keyboard for URL input.

## Implementation Phases

### Phase 1: Playlist Parser

Deliverables:

1. M3U parser with unit-style local tests.
2. Channel model.
3. Parser error reporting.
4. No GUI dependency.

### Phase 2: Source Store

Deliverables:

1. Add/remove/list IPTV sources.
2. Persist source list on SD/save storage.
3. Cache parsed channels.
4. Refresh sources manually.

### Phase 3: Minimal Channel Browser

Deliverables:

1. Text or simple overlay list.
2. Select a channel.
3. Send channel URL to `player`.
4. Return to list from playback.

### Phase 4: GUI Foundation

Deliverables:

1. GUI render backend.
2. Navigation stack.
3. Home screen.
4. Settings screen.
5. Channel browser screen.

### Phase 5: Product Polish

Deliverables:

1. Search and filters.
2. Logos and async image cache.
3. Better loading/error states.
4. Favorite channels.
5. Import/export settings.

## Risks

1. GUI and mpv fighting over frame ownership.
2. Blocking network refresh on the render thread.
3. Large playlists causing memory pressure.
4. Controller/touch state leaking into player commands.
5. License risk when copying GUI backend code from other projects.

## Rule For Future Work

IPTV URLs should still be opened by the existing player. Do not rebuild a parallel playback stack for IPTV.
