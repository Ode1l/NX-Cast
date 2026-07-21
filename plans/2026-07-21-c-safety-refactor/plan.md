# Plan: C Safety And Ownership Refactor

> Status: COMPLETE
> Created: 2026-07-21
> Last Updated: 2026-07-21

## Goal
Strengthen NX-Cast's C memory, string, and buffer contracts without changing normal playback or protocol behavior.

## Assumptions
- Existing broad AirPlay/player worktree changes are user-owned and must not be reset, stashed, or reformatted wholesale.
- Display-only text may remain deliberately truncated, while protocol identifiers, paths, and media URLs must reject truncation.
- This task is a staged safety refactor, not a rewrite or a request to replace C with a managed runtime.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] Shared dynamic buffers reject size overflow before allocation or pointer arithmetic.
- [x] Owned player values have explicit replacement/clear behavior and focused lifecycle tests.
- [x] IPTV URL resolution reports truncation instead of producing a malformed playable URL.
- [x] Network/parser boundaries validate capacity invariants before subtraction, addition, or multiplication.
- [x] Host tests, sanitizer checks, strict normal build, and strict Full Trace build pass.

### Non-goals
- Rewriting all modules or replacing existing fixed-size display models.
- Changing DLNA, IPTV, or AirPlay wire behavior except rejecting malformed/oversized data safely.
- Introducing a garbage collector, C++ ownership framework, or third-party utility dependency.

### Edge Cases
- `SIZE_MAX` additions/multiplications and corrupted buffer state where offset exceeds capacity.
- Allocation failure during replacement, growth, and asynchronous command submission.
- Null, empty, self-copy, overlong URL, and repeated init/deinit inputs.

## Design Decisions

| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Safety mechanism | New dependency; ad-hoc checks; header-only checked-size helpers | Small header-only helpers reused at real allocation boundaries | yes — matches the requested C-focused refactor |
| Truncation policy | Truncate everything; reject everything; classify by boundary | Reject protocol/path/URL truncation, retain display-text truncation | yes — preserves UI behavior while preventing invalid transport data |
| Ownership model | Global allocator wrapper; reference counting everywhere; typed clear/copy contracts | Keep explicit typed clear/copy APIs and make replacement failure-atomic | yes — fits current player architecture |
| Delivery strategy | One repository-wide rewrite; staged vertical slices | Five independently tested safety slices | yes — minimizes regression risk in the dirty worktree |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Add checked size arithmetic and harden DLNA dynamic writers. |
| Step 2 | `steps/step-2.md` | COMPLETED | Make player owned-value replacement contracts explicit and tested. |
| Step 3 | `steps/step-3.md` | COMPLETED | Extract and test non-truncating IPTV URL resolution. |
| Step 4 | `steps/step-4.md` | COMPLETED | Harden network, download, and collection capacity boundaries. |
| Step 5 | `steps/step-5.md` | COMPLETED | Add aggregate safety validation, document contracts, and build both variants. |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Focused safety tests | `make test-c-safety` | Planned aggregate Makefile target | yes |
| Existing host regressions | `make test-airplay` | Existing Makefile target | yes |
| ASan/UBSan | `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 make test-c-safety HOST_CFLAGS='-std=c11 -Wall -Wextra -Werror -pedantic -Isource -Ithird_party/playfair -fsanitize=address,undefined -fno-omit-frame-pointer'` | Existing host compiler pattern plus platform-compatible sanitizer flags | yes |
| Normal Switch build | `make NXCAST_USE_IMGUI_UI=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 NXCAST_REQUIRE_AIRPLAY_ED25519=1 -j4` | Existing release contract | yes |
| Full Trace Switch build | `make TRACE_MEDIA=1 TRACE_INPUT=1 TRACE_AIRPLAY=1 NXCAST_USE_IMGUI_UI=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 NXCAST_REQUIRE_AIRPLAY_ED25519=1 -j4` | Existing trace contract | yes |
| Diff hygiene | `git diff --check` | Repository convention | yes |

## Context & Learnings
### Key Decisions
- Fix proved boundary defects first; do not churn already-safe code merely to standardize style.
- Keep allocation ownership local to typed modules and use shared helpers only for arithmetic invariants.
### Gotchas & Warnings
- LeakSanitizer is unsupported by the installed macOS ASan runtime; use ASan/UBSan with `detect_leaks=0` and cover releases through lifecycle tests/audit.
- Switch code and host-test code use different thread/platform APIs, so every shared change needs both host and strict cross-build validation.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `source/protocol/dlna/control/soap_writer.c` | Bounded dynamic SOAP output | `rg`/read found unchecked `output_len + extra + 1` and `strlen * 6`. |
| `source/protocol/dlna/description/template_resource.c` | Dynamic XML template output | Read found unchecked escape multiplication and doubling growth. |
| `source/player/types.c` | Owned media/event/snapshot values | Read found failure-atomic copies but `player_media_copy(out, NULL)` zeroes without freeing the old value. |
| `source/iptv/iptv.c` | URL/path composition | Read found `iptv_resolve_url()` silently truncates fixed outputs and uses `strncat`. |
| `source/protocol/airplay/discovery/dns.c` | Packet writer | Read found capacity subtraction assumes `offset <= capacity`. |
| `source/iptv/fetch.c` | Bounded downloads and atomic cache install | Read found additive size check and fixed temporary path composition. |
| `makefile` | Host and strict Switch validation | Existing aggregate host and release build contracts verified by read. |
| `source/util/size.h` | Shared checked arithmetic | Step 1 adds add/multiply/growth helpers used by two independent DLNA writers. |
| `scripts/test_c_size.c` | Arithmetic boundary regression | Covers success, overflow, null output, geometric limit, and invalid initial capacity. |
| `scripts/test_soap_writer.c` | Dynamic XML regression | Covers valid escaped output, `SIZE_MAX`, oversized escape, clear/dispose, and invalid internal state. |
| `scripts/test_player_types.c` | Owned-value lifecycle regression | Repeats media/event/snapshot set, deep copy, self-copy, NULL replacement, and clear 1000 times. |
| `source/iptv/url.c` | Transport URL boundary | Pure resolver returns false and clears output when a complete result cannot fit. |
| `scripts/test_iptv_url.c` | URL compatibility regression | Covers absolute, scheme-relative, root-relative, path-relative, local, query-base, exact-fit, and overflow cases. |

### Verified Facts
- The repository contains 152 C/header files and about 38,602 source lines; no `strcpy`, `strcat`, `sprintf`, `vsprintf`, or `strncpy` calls were found — verified by `rg` inventory, 2026-07-21.
- Current source has 149 `snprintf`, 11 `realloc`, 39 `strdup`, 81 direct allocation calls, and 548 `free` calls; arithmetic and ownership are higher-value targets than replacing unsafe legacy APIs — verified by aggregate `rg`, 2026-07-21.
- Existing AirPlay/player/protocol host suite passes under ASan/UBSan with leak detection disabled; the first run failed only because macOS reports LeakSanitizer unsupported — verified by sanitized `make test-airplay`, 2026-07-21.
- AirPlay RTSP/plist code already has local checked-size and explicit maximum patterns that can guide shared helpers — verified by reads of `rtsp.c` and `plist.c`, 2026-07-21.
- Checked-size and SOAP writer focused tests plus the full host aggregate pass after hardening; `git diff --check` is clean — verified in Step 1, 2026-07-21.
- `player_media_copy` and `player_event_copy` previously cleared a NULL source with `memset`, leaking an initialized destination's dynamic members; both now delegate to typed clear functions — verified by source read and normal/sanitized lifecycle tests in Step 2, 2026-07-21.
- IPTV URL resolution previously silently truncated output and mishandled a remote authority followed directly by a query; the extracted resolver rejects truncation and handles that base form correctly — verified by focused/sanitized tests and strict incremental Switch build in Step 3, 2026-07-21.
- DNS/mDNS packet assembly, IPTV cache downloads, and DLNA state collection growth now reject invalid capacity arithmetic; the DLNA parser also frees a partially constructed state variable on every allocation failure — verified by host regressions, ASan/UBSan, and strict Switch link in Step 4, 2026-07-21.
- The final audit found no `strcpy`, `strcat`, `strncat`, `sprintf`, `vsprintf`, or `strncpy` calls in `source` or `scripts`; seek parsing now rejects arithmetic overflow and non-finite numeric input before conversion — verified by targeted `rg`, focused sanitizer tests, and both clean strict builds in Step 5, 2026-07-21.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-07-21 | Step 1 | Added reusable checked `size_t` operations and made SOAP/template allocation growth reject overflow and invalid capacity state before writes. |
| 2026-07-21 | Step 2 | Unified player media/event NULL replacement with typed cleanup and added repeated deep/self-copy lifecycle tests. |
| 2026-07-21 | Step 3 | Extracted non-truncating IPTV URL resolution, skipped invalid playable entries, and preserved optional logo/EPG failure behavior. |
| 2026-07-21 | Step 4 | Hardened packet/download/collection boundaries and removed a partial-construction leak in the DLNA XML state loader. |
| 2026-07-21 | Step 5 | Added the repeatable C safety/sanitizer gates, documented project contracts, hardened seek parsing, and validated normal plus Full Trace Switch builds from clean states. |
