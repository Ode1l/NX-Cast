# Plan: Suspend Discovery During Media Playback

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
Add a BSD8 diagnostic profile that keeps AirPlay/DLNA discovery active while idle, atomically suspends mDNS and SSDP while any media owner is active, and resumes discovery after final release.

## Assumptions
- The diagnostic profile is experimental and does not change discovery behavior in existing profiles.
- Up to 200 ms for mDNS and 100 ms for SSDP workers to observe a suspension request is acceptable because the coordinator callback itself must remain nonblocking.
- AirPlay RTSP, DLNA HTTP/SOAP/event services, nxlink, and active player connections must remain running while discovery is suspended.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] The first successful media ownership claim suspends both discovery worker loops without joining threads or closing their sockets.
- [x] Ownership takeover does not briefly resume discovery; stale release attempts do not resume it.
- [x] Final release, abort, or observed ownership reset resumes discovery, and workers re-announce from their own threads.
- [x] `full-discovery-suspend-bsd8` is selectable from VS Code, logs suspension state, and completes a strict clean Switch build.

### Non-goals
- Replacing libnx socket management, changing player buffering logic, or stopping AirPlay/DLNA control servers.
- Claiming that BSD8 alone guarantees smooth DLNA playback; the device test determines whether removing discovery traffic resolves the remaining stalls.

### Edge Cases
- Protocol takeover must keep discovery suspended continuously.
- Direct player ownership reset must be reconciled by the coordinator tick and resume discovery.
- Service shutdown must clear suspension state without requiring worker joins from the UI thread.

## Design Decisions

| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Suspension boundary | Stop entire protocol services; close discovery sockets; pause only discovery polling | Pause only mDNS/SSDP polling and announcements; keep sockets/control services alive | yes — user proposed stopping discovery during the single active playback and approved implementation |
| Coordination point | Player-specific hooks; UI state; central media ownership | Central `ProtocolCoordinator` ownership transitions | yes — matches the one-media-owner invariant discussed with the user |
| Rollout | Change all profiles; new diagnostic profile | New `full-discovery-suspend-bsd8` profile only | yes — user will test the experiment before broader adoption |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Add and test the coordinator discovery-suspension lifecycle contract. |
| Step 2 | `steps/step-2.md` | COMPLETED | Implement nonblocking atomic suspension in mDNS and SSDP workers. |
| Step 3 | `steps/step-3.md` | COMPLETED | Wire the BSD8 profile, diagnostics, VS Code task, documentation, and strict build. |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Coordinator test | `make test-protocol-coordinator` | Existing make target and `scripts/test_protocol_coordinator.c` | yes |
| Host regression tests | `make test-host` if present, otherwise relevant existing host test targets | Makefile discovery during execution | best available |
| Switch build | `make clean && make TOPDIR="$PWD" THIS_MAKEFILE="$PWD/makefile" NXCAST_DIAG_PROFILE=full-discovery-suspend-bsd8 NXCAST_USE_IMGUI_UI=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 NXCAST_REQUIRE_AIRPLAY_ED25519=1 -j4` under devkitPro MSYS2 | Existing strict diagnostic build workflow | yes |
| Profile consistency | `rg "full-discovery-suspend-bsd8|NXCAST_SUSPEND_DISCOVERY_WHILE_MEDIA" makefile .vscode/tasks.json source docs` | Config/source cross-file contract | yes |

## Context & Learnings
### Key Decisions
- Use the coordinator's media ownership state as the single source of truth so IPTV, DLNA, and AirPlay receive identical discovery behavior.
- Call a nonblocking callback outside the coordinator mutex; workers acknowledge through atomic state and sleep instead of being joined or having sockets closed.
- Resume announcements inside the discovery worker threads, not in the coordinator or UI caller.
- Keep the NX-Cast home/video renderer boundary unchanged: upstream comparison confirms its mutually exclusive view rendering already follows the relevant Switch-player pattern, while the AirPlay-off baseline proves home-state coordination is not the regression variable.

### Gotchas & Warnings
- An mDNS `select` already in progress may take up to its 200 ms timeout to return; SSDP may take up to 100 ms.
- The callback must not run for owner-to-owner takeover or a stale ownership token release.
- Existing diagnostic profiles must retain their current behavior.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `source/app/protocol_coordinator.h` | Coordinator operations and snapshot contract | Prior `read` identified operations table and active-media snapshot. |
| `source/app/protocol_coordinator.c` | Ownership begin/release/abort/tick transitions | Prior targeted `read` found all central ownership transitions and direct-reset reconciliation. |
| `scripts/test_protocol_coordinator.c` | Focused host contract tests | Prior `read` found `FakeRuntime` and media takeover coverage. |
| `source/protocol/airplay/discovery/mdns.c` | mDNS worker select/receive/announce loop | Prior `read` verified 200 ms select loop and atomic running/socket state. |
| `source/protocol/dlna/discovery/ssdp.c` | SSDP worker select/respond/notify loop | Prior `read` verified 100 ms select loop and 30-second alive notification. |
| `source/main.c` | Runtime operation wiring and diagnostics | Prior `rg`/`read` found coordinator operations initialization and mDNS heartbeat logging. |
| `makefile` | Profile definitions and validation targets | Prior `read` verified profiles through ID 10 and the coordinator test target. |
| `.vscode/tasks.json` | Device test profile picker | Existing BSD8/BSD16 entries verified during the prior diagnostic step. |
| `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md` | Source-of-truth test procedure | Existing diagnostic matrix already contains BSD8/BSD16 results and instructions. |
| `xfangfang/wiliwili@88e5876` (external study) | Activity-stack and player/DLNA lifecycle comparison | `MainActivity`, `BasePlayerActivity`, `DLNAActivity`, and activity helper reads show one top activity, a shared MPV core, and teardown on player/DLNA activity destruction. |
| `Cpasjuste/pplay@0399546` (external study) | Explicit browser/player visibility comparison | `Player::setFullscreen` hides the file browser/status bar while retaining their objects, then restores them after playback. |
| `proconsule/nxmp@44229fe` (external study) | ImGui state-machine and UPNP discovery comparison | `GUI::HandleLayers` renders exactly one menu/player state; UPNP discovery is created on demand and uses a bounded receive loop. |

### Verified Facts
- `SocketInitConfig.num_bsd_sessions` defaults to a small value and BSD8 made previously unusable playback work but left periodic DLNA stalls — verified by local libnx header inspection and device test logs, 2026-07-22.
- BSD16 performed worse than BSD8 while main/logger/mDNS heartbeats remained live — verified from `logs/run_nxlink-20260722-024028.log` and user device observations, 2026-07-22.
- The protocol coordinator owns all normal media claim/release transitions and reconciles direct player ownership reset during tick — verified by targeted source reads, 2026-07-22.
- mDNS and SSDP discovery loops use bounded select timeouts, allowing atomic suspension without socket closure or synchronous joins — verified by targeted source reads, 2026-07-22.
- NX-Cast already renders either home or video per frame: `player_view_sync()` chooses one desired view and `frontend_render()` dispatches only that active view — verified in `source/player/render/view.c` and `source/player/render/frontend.c`, 2026-07-22.
- pPlay hides its filer/status UI on fullscreen playback; NXMP dispatches one ImGui screen from a single state switch; wiliwili uses top-activity lifecycle rather than running a second homepage renderer — verified from the pinned upstream commits in the Working Set, 2026-07-22.
- The coordinator callback is idempotent across takeover/stale release and is invoked outside its mutex — verified by a reentrant snapshot callback and passing `test-protocol-coordinator`, 2026-07-22.
- mDNS suspension freezes select and sent-packet counters, resumes with a worker-owned AirPlay/RAOP announcement, and stops cleanly while suspended — verified by the new strict host C regression, 2026-07-22.
- SSDP suspension and resume paths compile cleanly for Switch with strict warnings; no host SSDP implementation exists because this worker depends directly on libnx threads — verified by `aarch64-none-elf-gcc -fsyntax-only`, 2026-07-22.
- `full-discovery-suspend-bsd8` completed a strict clean Switch build; the NRO embeds the profile marker and hashes to `0287d7c573fe8e67e71ef9c1f0c755c7a870a8250f6311ab4b031817eba36e60` — verified locally, 2026-07-22.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-07-22 | Step 1 | Added and host-tested ownership-driven, nonblocking discovery suspension transitions. |
| 2026-07-22 | Step 2 | Added atomic worker suspension/resume to mDNS and SSDP plus a focused mDNS regression test. |
| 2026-07-22 | Step 3 | Wired profile ID 11, runtime evidence, VS Code picker/docs, and completed a strict clean NRO build. |
