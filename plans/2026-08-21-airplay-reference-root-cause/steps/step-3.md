# Step 3: Consolidate Root Causes and Next Gate

> Status: COMPLETED
> Created: 2026-08-21

## Goal
Turn the direct-play and reverse-HLS comparisons into a minimal ordered fix plan or one targeted device test if evidence remains missing.

## Prerequisites
- Step 1 lifecycle comparison completed.
- Step 2 reverse-HLS comparison completed.
- All conclusions separated into verified facts, inferences, and unknowns.

## Deliverables
- Ranked root causes with affected paths and expected behavior.
- A minimal implementation boundary that avoids unrelated player/FFmpeg changes.
- After this step: either implementation can begin directly, or the user has one precise test script and expected evidence.

## Plan
- [x] `read` completed Step 1 and Step 2 notes — consolidate only evidence-backed findings.
- [x] `rg` `source/protocol/airplay`, `source/app/protocol_coordinator.c`, and `source/player` — identify the smallest ownership, HLS, test, and logging change set.
- [x] `write` the follow-up implementation boundary in this step — the next change spans lifecycle, HLS, compatibility handlers, tests, and diagnostics and therefore requires a separate implementation plan.
- [x] `bash` `git diff --check -- plans/2026-08-21-airplay-reference-root-cause` — validate planning artifacts.

## Quality Checklist
- [x] Evidence-before-edit: both comparison steps complete and affected callers searched.
- [x] Existing pattern / reuse checked: coordinator leases, generation checks, and HLS tests preferred over new abstractions.
- [x] Contract understood: direct URL, reverse-HLS, mirror, DLNA fallback, and takeover remain distinct flows.
- [x] Risk reviewed: regressions to DLNA/IPTV, stale sessions, resource leaks, and false-positive timeouts.
- [x] Mitigation recorded: vertical fixes with host tests plus a bounded device matrix.

## Validation Checklist
- [x] Recommended changes map one-to-one to verified failures.
- [x] No speculative FFmpeg or decoder change is included.

## Test Checklist
- [x] `git diff --check -- plans/2026-08-21-airplay-reference-root-cause` exits 0.

## Implementation Notes
Implementation should proceed in four evidence-backed slices: (1) keep direct URL media alive across generic AirPlay HTTP connection closure and stop it only on explicit `/stop`, EOS, replacement/takeover, or user action; (2) retain remote-video transaction identity separately from socket bindings, including generation protection and playback UUID needed for reconnect/replacement; (3) accept and store valid raw FCUP media playlists, perform bounded condensed expansion lazily at local HLS GET time, and emit redacted substage/size/count diagnostics; (4) return benign compatibility responses for expected `/getProperty` polling instead of 501 and add realistic large/non-empty condensed playlist tests. A repeat test of the current binary would not distinguish the YouTube subcondition. The next useful device test is one run after instrumentation/refactoring, covering YouTube reverse-HLS and Bilibili direct URL in that order.

## Files Changed
- `plans/2026-08-21-airplay-reference-root-cause/plan.md`
- `plans/2026-08-21-airplay-reference-root-cause/steps/step-3.md`
