# New home UI device test

Branch: `new-ui`. This build changes the home screen and its input handling.
It does not yet implement the new channel library, continuous scrolling,
category selector or dynamic IPTV capacity discussed in the design session.
Only the home screen is bilingual in this first implementation slice.

## Install

Extract `dist/NX-Cast-sdmc.zip` into the SD card root, keeping the full
`switch/NX-Cast/` directory, including fonts. Preserve personal IPTV presets
when merging an existing installation. Alternatively use the existing VS Code
Upload task after the packaged assets are installed. The root `NX-Cast.nro`
is the newly compiled home test build; no trace rebuild is required.

## Home interaction

1. Startup focuses Live TV. Press A or X: the channel library opens, but no
   channel plays automatically. Close with B and repeat with a touch on the
   Live TV card, including the former channel-list coordinates inside it.
2. Tap the Cast title, illustration and status: none is a button. DLNA and
   AirPlay should still receive automatically without touching the home UI.
3. Move either stick or the directional buttons up to focus the language
   control; A switches language. Down returns focus to Live TV. Tapping
   `中文 / EN` switches immediately without opening the channel library.
4. Test a single Joy-Con: existing SR confirm and SL back mappings remain.
5. With a playing session available while home is visible, B returns to the
   player. A continues to mean the focused action, not return to playback.

## Language and visual states

1. Without `sdmc:/switch/NX-Cast/ui-language.txt`, the system language selects
   Simplified Chinese for Chinese systems, otherwise English.
2. Switch language and restart: the selected language persists. The file only
   contains `en` or `zh-CN`; it contains no credentials.
3. Check Chinese glyphs, English alignment, long recently-watched channel
   names, and the bottom-right hints in handheld and docked modes.
4. Confirm an AirPlay PIN replaces only the left status area. Check startup,
   offline, and error states without expecting an always-green readiness label.
5. Smoke-test existing IPTV, DLNA and AirPlay playback. Media protocol and
   decoder code is unchanged in this UI slice.

## Local verification

`make RELEASE_JOBS=4 release-build` and `scripts/package_release.sh` passed.
`scripts/test_home_ui.c` covers preference round-trips, invalid preferences,
unavailable storage, passive receiver hit areas and active hit boundaries.
The existing channel-list navigation test also passed. Real Switch rendering
and controller feel still require the checks above.
