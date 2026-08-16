# Step 3: Validate And Publish Hardware Test Contract

> Status: COMPLETED
> Created: 2026-08-13

## Goal
Run integrated validation and document a single-variable Switch test matrix for cache, DLNA/IPTV, and AirPlay resource behavior.

## Prerequisites
- Steps 1 and 2 completed.
- Host dependencies and devkitPro packages are used when available; unavailable commands must be reported rather than bypassed.
- Files to modify: `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md` or the nearest existing source-of-truth diagnostics guide, plus plan bookkeeping.

## Deliverables
- Integrated host and release-build evidence.
- A hardware matrix separating stable LAN DLNA, IPTV, AirPlay mirror, protocol switching, and soak tests.
- Explicit expected log markers and failure interpretations.
- After this step: the user has an ordered test script that can identify cache starvation versus BSD/service contention.

## Plan
- [x] `bash make test-airplay` — aggregate host suite passed.
- [x] `bash make release-build RELEASE_JOBS=4` — production NRO and feature attestation passed.
- [x] `edit docs/AIRPLAY_FREEZE_DIAGNOSTICS.md` — added cache/budget markers and the ordered physical A/B matrix.
- [x] `bash git diff --check` — patch hygiene passed.
- [x] `read plans/2026-08-13-media-cache-network-runtime/plan.md` — results and residual risks recorded.

## Quality Checklist
- [x] Evidence-before-edit: diagnostics source read, commands discovered from Makefile, validation attempted
- [x] Existing pattern / reuse checked: extend the current AirPlay freeze guide instead of creating duplicate documentation
- [x] Contract understood: host tests cannot replace physical Switch playback validation
- [x] Risk reviewed: false attribution from CDN errors or mixed test variables
- [x] Mitigation recorded: stable LAN source first and separate AirPlay sender modes

## Validation Checklist
- [x] Available aggregate host tests pass
- [x] Available release build passes and embeds expected settings
- [x] `git diff --check` exits 0

## Test Checklist
- [x] Hardware matrix includes cold start, five-cycle DLNA, IPTV switching, AirPlay mirror, Home recovery, mixed-protocol switching, and soak

## Implementation Notes
- `make test-airplay` passed the aggregate host suite, including cache policy, network diagnostics, player actor, protocol coordinator, pairing, RTSP, mirror, audio, and HLS smoke coverage.
- `make release-build RELEASE_JOBS=4` produced a 24 MiB `NX-Cast.nro` and passed the existing libmpv/deko3d/Ed25519/randombytes/PlayFair feature attestation.
- The release build retains two pre-existing warnings where AirPlay `failure_stage` variables are compiled out with release logging. They are not caused by this cache/network change.
- Physical Switch playback remains the required final behavior check; the ordered matrix is now documented rather than claimed as host-tested.

## Files Changed
- `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md`
- `source/main.c`
