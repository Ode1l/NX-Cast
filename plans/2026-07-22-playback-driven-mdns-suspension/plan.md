# Plan: Playback-Driven AirPlay mDNS Suspension

> Status: COMPLETED
> Created: 2026-07-22
> Last Updated: 2026-07-22

<!--
  Plan-level status (lifecycle):
    DRAFT     — awaiting approval after clarification
    ACTIVE    — execution in progress
    COMPLETED — all steps done, verified
    ARCHIVED  — optional long-term archival state
  This is distinct from step-level status (PENDING|IN_PROGRESS|COMPLETED|BLOCKED)
  in `steps/step-N.md`. The pre-edit gate checks step status, not plan status.
-->

## Goal
Add Profile ID 12 that keeps SSDP online and suspends only AirPlay mDNS while the player is actively loading, buffering, seeking, playing, or paused, then restores mDNS for stopped, idle, or error states.

## Assumptions
- Profile ID 11 and its ownership-driven mDNS+SSDP suspension remain unchanged as the comparison baseline.
- Profile ID 12 is named `full-mdns-playback-suspend-bsd8` and retains eight BSD service sessions.
- Disabling mpv's `ytdl_hook` is outside this requested change and will not be bundled silently.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] ID 12 does not suspend mDNS merely because a DLNA/IPTV/AirPlay ownership lease exists.
- [x] ID 12 suspends mDNS for loading, buffering, seeking, playing, and paused snapshots.
- [x] ID 12 resumes mDNS for stopped, idle, and error snapshots even when ownership and the media URI remain retained.
- [x] ID 12 never suspends SSDP, so DLNA discovery and SOAP control remain available during playback.
- [x] ID 11 retains its existing ownership-driven mDNS+SSDP behavior.
- [x] Focused host tests and a strict clean Switch build pass.

### Non-goals
- Changing player ownership lifetime, DLNA SOAP semantics, media buffering, FFmpeg/mpv options, or the ID 11 diagnostic baseline.

### Edge Cases
- Repeated identical player states must not duplicate callbacks or resume announcements.
- Ownership takeover while player activity remains true must keep ID 12 mDNS suspended.
- Error or stop with `has_media=1` must resume immediately; shutdown must leave the worker unsuspended.

## Design Decisions

| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Coordinator policy contract | Replace ownership behavior; drive directly from main; explicit policy enum | Add explicit none/ownership/playback policy so ID 11 and ID 12 can coexist | yes — user explicitly requires ID 11 retained and ID 12 playback-driven |
| ID 12 discovery target | Suspend mDNS+SSDP; suspend mDNS only | Suspend AirPlay mDNS only; leave SSDP untouched | yes — user explicitly requested SSDP always running |
| Active-playback predicate | View mode; ownership; player snapshot state | Reuse `main_snapshot_playback_active()` and its exact five active states | yes — user explicitly listed active and recovery states |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Add and host-test explicit ownership- versus playback-driven coordinator policies. |
| Step 2 | `steps/step-2.md` | COMPLETED | Wire ID 12 to player snapshots and mDNS-only suspension while preserving ID 11. |
| Step 3 | `steps/step-3.md` | COMPLETED | Update the device procedure and complete regression/build/artifact verification. |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Coordinator regression | `make PROTOCOL_COORDINATOR_TEST_BIN=/f/VSCODE~1/NX-Cast/build/tests/test_protocol_coordinator test-protocol-coordinator` | Existing focused target | yes |
| mDNS worker regression | `make AIRPLAY_MDNS_SUSPEND_TEST_BIN=/f/VSCODE~1/NX-Cast/build/tests/test_airplay_mdns_suspend test-airplay-mdns-suspend` | Existing focused target | yes |
| Task JSON | `Get-Content .vscode/tasks.json -Raw \| ConvertFrom-Json` | Existing VS Code configuration | yes |
| Switch build | `make clean && make TOPDIR="$PWD" THIS_MAKEFILE="$PWD/makefile" NXCAST_DIAG_PROFILE=full-mdns-playback-suspend-bsd8 NXCAST_USE_IMGUI_UI=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 NXCAST_REQUIRE_AIRPLAY_ED25519=1 -j4` | Existing strict build workflow | yes |
| Profile consistency | `rg "full-mdns-playback-suspend-bsd8|NXCAST_SUSPEND_AIRPLAY_MDNS_WHILE_PLAYBACK" makefile .vscode/tasks.json source docs` | Cross-file contract | yes |

## Context & Learnings
### Key Decisions
- Keep player ownership as the protocol command authority, but make discovery suspension policy independent because stopped media intentionally retains ownership and URI for replay.
- Reuse the existing nonblocking callback and mDNS worker resume edge; do not add socket close/join operations.

### Gotchas & Warnings
- The main loop currently logs coordinator transitions before reading the latest player snapshot; ID 12 must apply player activity before transition logging/heartbeat capture.
- ID 11 must still call both `airplay_mdns_set_suspended()` and `ssdp_set_suspended()` on ownership changes.
- The worktree already contains the prior diagnostic implementation and unrelated user changes; edits must be surgical.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `source/app/protocol_coordinator.h` | Policy enum/config and playback activity API | `rg`/targeted read found the current callback and config contract. |
| `source/app/protocol_coordinator.c` | Ownership synchronization and suspension callback | Targeted read found every ownership-driven suspend/resume call. |
| `scripts/test_protocol_coordinator.c` | Policy regression coverage | Existing ID 11 ownership lifecycle test and reentrant callback are reusable. |
| `source/main.c` | Player state predicate and callback wiring | `main_snapshot_playback_active()` already exactly matches the requested five active states. |
| `makefile` | Profile registry and ID definitions | ID 11 is the current last profile and uses BSD8. |
| `.vscode/tasks.json` | Device profile picker | Existing ID 11 option can be extended with ID 12 exactly once. |
| `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md` | Device test procedure and profile matrix | Existing ID 11 procedure records both discovery flags and recovery expectations. |

### Verified Facts
- `main_snapshot_playback_active()` returns true only for loading, buffering, seeking, playing, and paused and already feeds Home state — verified by targeted source read, 2026-07-22.
- Stopped snapshots can retain `has_media=1` and ownership, so ownership is not a valid playback-activity proxy — verified in device log `run_nxlink-20260722-124754.log`, 2026-07-22.
- The mDNS worker already resumes and re-announces on its atomic suspend edge; SSDP has an independent setter, so ID 12 can omit it without low-level changes — verified by source read and prior focused regression, 2026-07-22.
- ID 11 is isolated behind `NXCAST_SUSPEND_DISCOVERY_WHILE_MEDIA`, allowing its behavior to remain unchanged while ID 12 gets a separate macro — verified by `main.c` and Makefile reads, 2026-07-22.
- ID 12 reads the current player snapshot and applies playback activity before coordinator transition logging and heartbeat capture — verified by final full-file review and strict Switch compilation, 2026-07-22.
- The clean ID 12 NRO embeds the correct profile name and has SHA-256 `e5bfb5c53d701a6752f1fc060998da128d8b7a63d318acf933bc74299e6dcb39` — verified from the generated artifact, 2026-07-22.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-07-22 | Step 1 | Added explicit none/ownership/playback discovery policies, a playback activity API, and passing edge/idempotence/stop host coverage. |
| 2026-07-22 | Step 2 | Added Profile ID 12, wired the existing five-state predicate before protocol logging, and isolated its callback to AirPlay mDNS while retaining ID 11's mDNS+SSDP callback. |
| 2026-07-22 | Step 3 | Documented the ID 11/12 comparison, passed both host regressions and the strict clean ID 12 build, and verified the NRO marker/hash and VS Code picker. |
| 2026-07-22 | Reflection | Re-read the implementation and corrected the ID 12 failure-classification tuple to require SSDP to remain zero. |
