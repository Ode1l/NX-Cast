# Plan: Sanitizer And Log Mirror Host Fixes

> Status: COMPLETED
> Created: 2026-07-22
> Last Updated: 2026-07-22

## Goal
Make host safety diagnostics truthful on the devkitPro Cygwin environment and make the log-mirror disconnect test portable without weakening production disconnect detection.

## Assumptions
- The Switch-target compiler/runtime is out of scope; these failures occur only in host-side tests.
- Installing or replacing a system compiler is not performed unless the repository cannot provide a safe local fix and the user explicitly chooses that system change.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] The repository identifies an unavailable host sanitizer runtime before attempting a long test build and reports an actionable compiler/runtime diagnostic.
- [x] Compiler and sanitizer-flag overrides remain supported, and a successful link probe proceeds into the existing test suite.
- [x] `test-log-mirror` passes on devkitPro Cygwin while still proving that a closed peer is eventually reported as `LOG_MIRROR_WRITE_FAILED`.
- [x] The normal host test aggregate and relevant Switch build remain unaffected.

### Non-goals
- Installing third-party Cygwin/MSYS toolchains or changing Switch-target sanitization.
- Changing the log-mirror wire format, socket ownership, or production retry policy.

### Edge Cases
- A local socket may accept one or more writes after the peer closes because close notification is asynchronous.
- A compiler may accept `-fsanitize` at compile time but lack the linkable ASan/UBSan runtimes.

## Design Decisions
None — no design-sensitive changes.

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Diagnose sanitizer ownership/search paths and make the sanitizer target fail early with an actionable capability check. |
| Step 2 | `steps/step-2.md` | COMPLETED | Replace the Cygwin-sensitive immediate-close assertion with a bounded eventual-failure assertion and validate host tests. |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Sanitizer capability | `make test-c-safety-sanitize` | `makefile` target | yes |
| Focused mirror test | `make test-log-mirror` | `makefile` target | yes |
| Host aggregate | `make test-airplay` | `makefile` target | yes |
| Switch build | `make NXCAST_DIAG_PROFILE=full-owner-exclusive-bsd12 ... -j4` | existing Profile 13 workflow | yes if host edits touch shared build configuration |

## Context & Learnings
### Key Decisions
- Keep compiler selection overrideable through `HOST_CC`; only add host capability detection and diagnostics.
- Test eventual disconnect observation because stream-socket close propagation is asynchronous; retain a strict bounded deadline.

### Gotchas & Warnings
- The current `/usr/bin/cc` targets `x86_64-pc-cygwin`, while devkitA64 libraries target the Switch and cannot satisfy host links.
- The worktree contains unrelated user and prior diagnostic changes that must be preserved.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `makefile` | Defines host compiler flags and sanitizer target. | `read` showed `HOST_CC`, `HOST_SANITIZER_FLAGS`, and `test-c-safety-sanitize`. |
| `scripts/test_log_mirror.c` | Contains the failing immediate closed-peer assertion. | `read` showed `close(sockets[1])` followed by one required failed write. |
| `source/log/mirror.c` | Production classification contract for send outcomes. | `read` showed transient errors map to DROPPED and terminal socket errors map to FAILED. |
| `scripts/test_log_mirror.c` | Portable disconnect timing assertion. | `make test-log-mirror` plus ten direct reruns passed on Cygwin after the bounded eventual-failure change. |

### Verified Facts
- `/usr/bin/cc` is GCC 15.3.0 targeting `x86_64-pc-cygwin` — verified by `command -v`, `cc -dumpmachine`, and `cc -dumpversion` on 2026-07-22.
- The selected compiler returns unresolved names for `libasan.a`, `libasan.dll.a`, `libubsan.a`, and `libubsan.dll.a` — verified by `cc -print-file-name` on 2026-07-22.
- Installed `gcc` and `gcc-libs` packages contain no ASan/UBSan files, and configured package databases expose no matching sanitizer package — verified by `pacman -Ql`, `pacman -Ss`, and `pacman -Sl` on 2026-07-22.
- The production function already distinguishes transient backpressure from terminal send errors; the observed failure is the test's first-write timing assumption — verified by `read source/log/mirror.c` and the Cygwin assertion failure.
- No ASan/UBSan runtime file exists anywhere under `D:\devkitPro`, and no alternative `gcc`/`clang` executable is currently on the Windows PATH — verified by bounded `rg --files`, `Get-Command`, and `where.exe` checks on 2026-07-22.
- The configured repository offers `clang` and `compiler-rt`, but neither is installed; the current Cygwin GCC itself cannot link sanitizers — verified by `pacman -Si`, `pacman -Q`, and the new link probe on 2026-07-22.
- The sanitizer preflight accepts overrideable flags/compiler and leaves no repo artifact; its success path completed all `test-c-safety` cases with an empty test flag override and the existing Cygwin feature macro — verified by `make ... test-c-safety-sanitize HOST_SANITIZER_FLAGS=` on 2026-07-22.
- A peer close is not synchronously visible to the first Cygwin `send`; the original one-shot assertion fails, while a bounded retry observes the terminal error — verified by before/after `make test-log-mirror` runs on 2026-07-22.
- All aggregate C prerequisites through `test-c-safety` pass with the Cygwin feature macro; the remaining aggregate blockers are missing host `python3` and host mbedTLS files, not changed code — verified by two `make test-airplay` runs on 2026-07-22.
- Profile 13 (`full-owner-exclusive-bsd12`) still builds `NX-Cast.nro` successfully with all required release feature gates enabled — verified by the Switch build on 2026-07-22.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-07-22 | Step 1 | Added an actual sanitizer link preflight and established that the current devkitPro Cygwin GCC installation has no ASan/UBSan runtime. |
| 2026-07-22 | Step 2 | Made closed-peer testing asynchronous-but-bounded, passed repeated Cygwin runs, and rebuilt Profile 13. |
