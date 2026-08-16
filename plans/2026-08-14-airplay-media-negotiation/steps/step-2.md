# Step 2: Advertise Implemented Remote Video Capabilities

> Status: COMPLETED
> Created: 2026-08-14

## Goal
Make iOS select NX-Cast's existing URL/HLS route when casting from a video app while retaining screen mirroring discovery.

## Prerequisites
- Step 1 completed with a compatible RTSP control transcript.
- Files to modify: `source/protocol/airplay/integration.c`, `scripts/airplay_mdns_smoke_server.c`, and `scripts/smoke_airplay_mdns.py`.
- Design: video/HLS bits are advertised only because `remote_video` is installed and receiver filtering remains authoritative.

## Deliverables
- Integration requests feature mask `0x5A7FFEF7` through named feature constants rather than a magic number.
- Discovery smoke coverage verifies both mirroring and remote-video feature bits.
- After this step: iOS may choose either type-110 mirroring or `/play` URL/HLS according to its entry point.

## Plan
- [x] `read` `source/protocol/airplay/integration.c`, `source/protocol/airplay/receiver.c`, `scripts/airplay_mdns_smoke_server.c`, and `scripts/smoke_airplay_mdns.py` — confirm feature ownership and smoke invocation.
- [x] `edit` `source/protocol/airplay/integration.c` — OR `AIRPLAY_MDNS_FEATURE_VIDEO` and `AIRPLAY_MDNS_FEATURE_HLS` into the configured compatibility profile.
- [x] `edit` `scripts/airplay_mdns_smoke_server.c` — exercise the same effective feature profile as production integration.
- [x] `edit` `scripts/smoke_airplay_mdns.py` — assert the effective video/HLS/mirroring feature record.
- [x] `bash` `make test-airplay && python3 scripts/smoke_airplay_mdns.py` — expect all tests and discovery smoke checks to pass.

## Quality Checklist
- [x] Evidence-before-edit: target reads listed above, impact search `rg -n "MIRROR_COMPAT|features=" source scripts`, validation commands listed above.
- [x] Existing pattern / reuse checked: reuse named mDNS feature constants and receiver capability filtering.
- [x] Contract understood: feature bits affect sender route selection but not player ownership directly.
- [x] Risk reviewed: over-advertising and discovery regressions.
- [x] Mitigation recorded: retain receiver-side clearing when `remote_video` is absent and test exact TXT output.

## Validation Checklist
- [x] `make test-airplay` exits 0.
- [x] `python3 scripts/smoke_airplay_mdns.py` exits 0.
- [x] `git diff --check` exits 0 for changed files.

## Test Checklist
- [x] mDNS smoke confirms video bit 0, HLS bit 4, and mirroring bit 7 are set.

## Implementation Notes
The smoke assertion first failed against the old `0x5A7FFEE6` profile. Production integration and its mDNS fixture now OR named video and HLS bits into the existing mirror profile, yielding `0x5A7FFEF7`. `airplay_receiver_start()` still clears those bits when no remote-video handler is supplied.

## Files Changed
- `source/protocol/airplay/integration.c`
- `scripts/airplay_mdns_smoke_server.c`
- `scripts/smoke_airplay_mdns.py`
