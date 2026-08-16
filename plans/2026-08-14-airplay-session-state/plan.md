# Plan: AirPlay Logical Session And Pipeline Boundaries

> Status: COMPLETED
> Created: 2026-08-14
> Last Updated: 2026-08-14

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
Make AirPlay connection/session ownership and media-pipeline stages explicit and testable without changing the working DLNA/IPTV playback architecture.

## Assumptions
- UxPlay is used only as a protocol-behavior reference; NX-Cast keeps an independent C implementation and its existing Switch media backend.
- The current DLNA and IPTV player paths are the regression baseline and must not be rerouted through AirPlay code.
- Real-device success still requires a final iPhone-to-Switch test after host tests and the strict Switch build pass.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] AirPlay TCP connection identity is separate from logical remote-video session identity.
- [x] Multiple AirPlay HTTP connections with the same exact `X-Apple-Session-ID` share one logical media session and closing one connection cannot stop the others.
- [x] Pairing, transport setup, stream setup, media ownership, first media bytes, and player loading have distinct trace boundaries.
- [x] Stale AirPlay runtime callbacks cannot release a newer player lease.
- [x] Existing AirPlay host tests, DLNA/IPTV ownership tests, smoke tests, and strict Switch build pass.

### Non-goals
- Porting UxPlay/RPiPlay source, GStreamer, Linux networking, or a generic framework for future protocols.
- Implementing AirPlay 2 multi-room audio or replacing the existing protocol coordinator.
- Claiming real-device playback success without a new hardware trace.

### Edge Cases
- No-CSeq secondary pairing connections may exist before an Apple session ID is available.
- A connection that changes its Apple session ID, mixes incompatible connection kinds, or uses remote-video routes while unbound must fail without disturbing other protocols.
- Closing the first of several connections in one logical session must not emit a logical-session close.

## Design Decisions

| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Global playback arbitration | Replace coordinator vs extend existing coordinator | Reuse `protocol_coordinator` and existing owner leases | yes |
| AirPlay identity | Raw TCP id vs hashed header vs exact logical-session registry | Exact bounded `X-Apple-Session-ID` registry; RAOP mirror remains connection-scoped | yes |
| State representation | One universal enum vs independent states plus derived phase | Preserve pairing/runtime states and add a small derived protocol phase | yes |
| Reference implementation use | Source port vs behavior comparison | Independently implement observed UxPlay connection semantics | yes |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Add a tested logical-session registry and protocol behavior contract. |
| Step 2 | `steps/step-2.md` | COMPLETED | Bind receiver and handler callbacks to logical identities and safe close semantics. |
| Step 3 | `steps/step-3.md` | COMPLETED | Add explicit per-connection phase validation and low-noise stage traces. |
| Step 4 | `steps/step-4.md` | COMPLETED | Harden AirPlay lease generation and media-boundary diagnostics. |
| Step 5 | `steps/step-5.md` | COMPLETED | Run full regression, strict Switch build, and global quality reflection. |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Focused session test | `make test-airplay-session` | Existing host-test makefile convention | yes |
| Protocol regression | `make test-airplay` | Existing makefile target | yes |
| Discovery smoke | `python3 scripts/smoke_airplay_mdns.py` | Existing smoke script | yes |
| Whitespace/syntax review | `git diff --check` | Git | yes |
| Strict Switch build | `source /opt/devkitpro/switchvars.sh && make PORTLIBS_PREFIX=<staged-prefix> TRACE_MEDIA=1 TRACE_INPUT=1 TRACE_AIRPLAY=1 NXCAST_USE_IMGUI_UI=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 NXCAST_REQUIRE_AIRPLAY_ED25519=1 NXCAST_REQUIRE_AIRPLAY_MUXER=1 -j4` | Existing release-equivalent build flags | yes |

## Context & Learnings
### Key Decisions
- Keep protocol control, media ownership, bridge/mux, and decode as separate fault domains so a trace identifies the first missing boundary.
- Add only an AirPlay-specific logical-session registry because cross-connection Apple session identity is protocol policy, not global player policy.
### Gotchas & Warnings
- `Connection reset by peer` appears when the Switch application closes and is not treated as the playback root cause.
- The worktree contains unrelated in-progress changes; no existing change may be reverted or reformatted incidentally.
- Pairing/FairPlay contexts remain per TCP connection even when remote-video ownership is shared.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `source/protocol/airplay/protocol/rtsp.h` | Per-TCP connection state | `read` showed raw `id` is the only identity field. |
| `source/protocol/airplay/receiver.c` | Pairing/handler dispatch boundary | `read` showed no connection classification or logical-session registry. |
| `source/protocol/airplay/protocol/handlers.c` | AirPlay route and media callbacks | `rg` showed callbacks and close handling use raw `session->id`. |
| `source/protocol/airplay/media/remote_video.c` | URL/HLS session lifetime | `read` showed ownership keyed by the raw owner session id. |
| `source/protocol/airplay/integration.c` | Coordinator lease integration | `read` showed existing coordinator reuse and a stale mirror-generation risk. |
| `source/app/protocol_coordinator.c` | Existing global arbitration | `rg`/`read` verified this is already the central protocol ownership mechanism. |
| `makefile` | Host and strict Switch validation | `rg` verified `test-airplay` compiles focused C tests. |
| `source/protocol/airplay/protocol/logical_session.[ch]` | Logical connection/session identity | Step 1 host and sanitizer tests verified classification, binding, refcounts, rejection, and capacity; Step 5 renamed the files to avoid devkitPro's flat `session.o` collision. |
| `source/protocol/airplay/receiver.c` | Session classification and close owner | Step 2 `make test-airplay` verified composed receiver build/smoke and deterministic route gating. |
| `source/protocol/airplay/protocol/handlers.c` | Media callback identity consumer | Step 2 handler test verified logical id use and removal of raw-close remote stop. |
| `scripts/test_airplay_session.c` | Focused session behavior regression | `make test-airplay-session` passed normally and with ASan/UBSan. |
| `source/protocol/airplay/protocol/handlers.c` | Derived protocol phase and request-order validation | Step 3 transcript test and full host regression verified legal transitions and deterministic invalid-order handling. |
| `scripts/test_airplay_handlers.c` | Handler transcript regression | Step 3 assertions verified control, transport-ready, record-pending, recording, and closed phases. |
| `source/protocol/airplay/integration.c` | Runtime generation to coordinator lease binding | Step 4 review verified every asynchronous mirror callback must match both generations before player or lease mutation. |
| `source/protocol/airplay/media/mirror_runtime.c` | Generation-tagged player handoff | Step 4 runtime test verified bind/open/play/stop callbacks retain their originating session generation. |
| `source/protocol/airplay/media/stream_bridge.c` | Mux-to-player byte boundary | Step 4 bridge/audio tests verified first-write and first-read diagnostics without changing ring-buffer behavior. |
| `docs/AIRPLAY_PROTOCOL_COMPATIBILITY.md` | Protocol behavior and fault-boundary contract | Step 1 review verified connection classes, ownership rules, and explicit non-goals. |
| `/tmp/nxcast-airplay-reference.0kbncV/UxPlay/lib/raop.c` | Protocol behavior reference | `read` verified distinct RAOP/AirPlay connection classification and stable Apple session id per connection. |

### Verified Facts
- Player submissions already converge through the media actor and ownership coordinator; no second global state machine is needed — verified by `rg player_submit_command source`, 2026-08-14.
- AirPlay currently uses one TCP `session->id` as connection id, callback owner id, and remote-video lifetime key — verified by `rg session->id source/protocol/airplay`, 2026-08-14.
- UxPlay distinguishes CSeq RAOP connections from no-CSeq `X-Apple-Session-ID` AirPlay connections and requires the session header to remain stable on a connection — verified by `read /tmp/nxcast-airplay-reference.0kbncV/UxPlay/lib/raop.c`, 2026-08-14.
- Mirror transport setup is connection-scoped while remote URL playback uses a persistent Apple session identity; combining both into one universal lifetime would be incorrect — verified by UxPlay handler and mirror call paths, 2026-08-14.
- Existing `make test-airplay` already covers pairing, RTSP, handlers, mirror runtime, audio, bridge, remote video, and coordinator behavior — verified by `rg test-airplay makefile`, 2026-08-14.
- Exact Apple session values can be bounded to 128 printable ASCII bytes and kept out of public snapshots/logs while generated high-bit tokens avoid raw TCP id collisions — verified by Step 1 implementation and host/sanitizer tests, 2026-08-14.
- An 8-entry fixed registry exceeds the server's current 4 simultaneous client slots without adding dynamic per-request allocation — verified by `AIRPLAY_SERVER_MAX_CLIENTS` search and Step 1 capacity test, 2026-08-14.
- Pairing and FairPlay remain connection-scoped while transport/media callbacks can safely use `logical_session_id`; default RTSP initialization preserves existing RAOP behavior by setting both ids equal — verified by Step 2 code review and `make test-airplay`, 2026-08-14.
- Remote-video cleanup now occurs at the receiver boundary only after the session manager reports the final bound connection; handler context cleanup no longer conflates TCP closure with logical media closure — verified by Step 2 handler/session tests, 2026-08-14.
- AirPlay handler phases are now derived from existing RTSP/stream state, invariant-checked at route boundaries, and logged only when the phase changes — verified by Step 3 transcript assertions and `make test-airplay`, 2026-08-14.
- Mirror player operations now carry the runtime generation and integration maps it to exactly one coordinator lease; stale callbacks cannot use or clear a newer lease — verified by Step 4 callback assertions, ownership tests, and code review, 2026-08-14.
- First configuration, keyframe, bridge write/read, and player bind/load/play are distinct event-driven trace boundaries; periodic bridge counters remain rate limited — verified by Step 4 trace-enabled host tests, 2026-08-14.
- The devkitPro build flattens source basenames into object names; renaming AirPlay `session.c` to `logical_session.c` removed the only duplicate C basename and the strict Switch build then linked successfully — verified by the repository basename scan and strict build, 2026-08-14.
- The full AirPlay regression, composed receiver smoke, mDNS lifecycle smoke, DLNA/IPTV ownership checks, and release-equivalent Switch build all pass; `NX-Cast.nro` SHA-256 is `e3cb4ba9ad3c74b516adf60e02b5e8d06869bfe078eb9b63258325cc49438bba` — verified locally, 2026-08-14.
- Global reflection confirmed that player mutation remains centralized in `source/protocol/airplay/integration.c`; the protocol coordinator, AirPlay logical-session registry, derived handler phase, and mirror runtime generation each own one non-overlapping concern — verified by targeted ownership/state searches, 2026-08-14.
- The earlier `session->id` fact records the pre-Step 2 baseline; current code keeps raw TCP identity in `id` and media identity in `logical_session_id`, with last-reference cleanup owned by the receiver — verified by final code review, 2026-08-14.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-08-14 | Step 1 | Added and verified AirPlay connection classification, exact logical-session binding, shared close lifetime, and protocol compatibility contract. |
| 2026-08-14 | Step 2 | Separated TCP and media identities through receiver/handlers and verified full host/smoke regression. |
| 2026-08-14 | Step 3 | Added derived protocol phase validation and transition-only diagnostics without creating duplicate mutable state. |
| 2026-08-14 | Step 4 | Bound asynchronous mirror callbacks to runtime and coordinator generations and added exact media handoff diagnostics. |
| 2026-08-14 | Step 5 | Completed host/smoke/strict Switch validation, removed the sole flat-object basename collision, and recorded the remaining real-device AirPlay validation boundary. |
