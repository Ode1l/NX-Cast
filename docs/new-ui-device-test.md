# New UI device test

Branch: `new-ui`. Includes bilingual Home, the full-screen channel library,
dark playback drawer, source/category selectors, continuous input, dynamic
IPTV catalog storage and the lighter player overlay.

## Install

Extract `dist/NX-Cast-sdmc.zip` into the SD root. Keep the complete
`switch/NX-Cast/` directory and fonts. Back up personal `iptv/sources.txt`
before merging an existing installation. AirPlay identities/pairings are
not included or overwritten by the package.

The root `NX-Cast.nro` is the new test build. After installing the assets,
the existing VS Code Upload task is sufficient; Full Trace is not required.

## Home and language

1. A or X opens Live TV without automatically playing a channel.
2. Touching the left Cast area does nothing; phone-initiated casting still works.
3. Up focuses language; A switches Chinese/English. Down focuses Live TV.
   Touch language selection also works. Switch several times, restart and confirm
   the last selection persists. SD replacement retains a recovery `.bak` until
   installation succeeds; failures now log errno. Check read-only/full SD behavior.
4. Inspect long Chinese/English titles, offline status and AirPlay PIN display.

## Channel library and drawer

1. Test separately with handheld controls, one Joy-Con (SR confirm / SL back),
   either stick, and touch only.
2. Browse beyond eight rows, hold to accelerate, drag and release with inertia,
   and drag the scrollbar near the end of a large catalog.
3. Touch a channel only selects it. Touch Play or press A to play. No swipe
   should trigger a stream replacement. A successful playback request closes
   the list, including when switching channels from the player. X reopens it.
4. Exercise Categories, Favorites, Recent, Search and Sources, including no
   results. Clear search by submitting an empty query.
5. Sources > Manage sources exposes Add URL, Scan SD, Refresh, Programme guide
   and Delete. Verify Cancel and Confirm on deletion.
6. Put .m3u/.m3u8 files beside sources.txt, restart or Scan SD. A channel list
   expands; a direct HLS playlist stays one playable entry.
7. During IPTV playback X opens the dark drawer; X expands it without stopping
   playback. B collapses/closes it. Tap the exposed video beside the half-width
   drawer to dismiss it without pausing. An outside swipe must not dismiss it;
   outside taps on an open selector close only that selector.
8. Focus outline and currently-playing marker must remain distinct. Missing
   logos/EPG should not break row alignment. DLNA/AirPlay must not show X Channels.

## Player appearance and interaction

1. Normal playback with controls hidden: no persistent progress line.
2. Show controls: soft bottom gradient, white thin timeline, title above it,
   time/volume on the information row, smaller hints at bottom right.
   Long titles stay on one line and scroll slowly back and forth with pauses
   at both ends, without ellipsis. A new title starts at the beginning.
3. Pause: centered translucent circle/play triangle, no Paused box.
4. Drag timeline: centered target/total time, larger thumb while previewing;
   release commits the seek. Thin styling must not reduce touch target size.
5. Hold L/R: accumulated signed seconds; no changing pause text during preview.
6. Adjust volume: compact percentage and volume bar.
7. Loading: small circular animation without a large text panel.
8. Unknown-duration media: no fake draggable timeline; non-seekable content
   has no draggable thumb or seek hint.
9. Smoke-test IPTV, DLNA and AirPlay playback/return-home. No protocol or
   decoder behavior was intentionally changed.
10. IPTV uses live controls, even when HLS reports a finite sliding window:
    no timeline, elapsed/total time, touch seek or L/R seek hint. Volume, pause
    and channel selection remain. This also applies to direct URLs opened via
    IPTV; a separate VOD/timeshift interface is not implemented. EPG programme
    progress in channel rows is retained. DLNA/AirPlay seek controls are unchanged.
11. Home Cast/Live TV icons share thin strokes, size and vertical alignment.
    Cast has a circular broadcast dot; the NX-Cast brand mark stays unchanged.
12. Home and player use shared Switch-style button glyphs. Check both languages
    and IPTV/non-IPTV hint sets. Tap empty video to hide controls while playing
    and paused: text and geometry must disappear together without a black strip.
    The hidden ImGui path no longer invokes legacy framebuffer cleanup; verify
    whether this resolves the reported one-frame artifact on the device.

## Local validation

- `make test-ui`: browser state/input, language persistence, layout/hit geometry.
- `make test-iptv-data`: 10,041 channels, 81 sources, 100 groups, 10,001 favorites;
  failure injection verifies the previous catalog survives allocation failure.
- `make RELEASE_JOBS=4 release-build`
- `sh scripts/package_release.sh`

Host tests and cross-compilation do not establish real-device rendering,
controller ergonomics or streaming compatibility. Those need the checks above.
