# Step 6: Integrated validation and hardware handoff

> Status: COMPLETED
> Created: 2026-07-22

## Goal
Prove the completed implementation is host-clean, sanitizer-clean, shutdown-safe, and Switch-buildable, then hand off a minimal physical test sequence with decisive log evidence.

## Prerequisites
- Steps 1-5 completed with no known validation failures.
- All plan acceptance criteria have an automated or physical-test evidence source.
- Workspace dirty changes have been reviewed without discarding unrelated work.

## Deliverables
- Complete validation record in step notes and the plan implementation log.
- No compile warnings, host regressions, sanitizer failures, shutdown-order violations, or patch whitespace errors introduced by this task.
- Hardware checklist covers repeated playback, Stop/Play, seek, cross-protocol rejection, Home restore, discovery recovery, and final shutdown.
- After this step: plan is marked completed if all non-hardware gates pass; physical-only observations are clearly identified for the user.

## Plan
- [x] `bash make test-protocol-coordinator test-network-diagnostics test-airplay test-c-safety-sanitize test-shutdown-order` — ran all targets; direct relevant tests pass, with the pre-existing Cygwin log-mirror assertion and unavailable sanitizer runtimes recorded explicitly.
- [x] `bash make clean && make NXCAST_DIAG_PROFILE=full-owner-exclusive-bsd12 NXCAST_USE_IMGUI_UI=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 NXCAST_REQUIRE_AIRPLAY_ED25519=1 -j4` — final clean strict Switch build succeeds; Profile 12 legacy branch also clean-builds.
- [x] `bash git diff --check` — patch hygiene passes.
- [x] `read git diff --stat` and targeted diffs — reviewed scope, profile gates, bounded diagnostic content, cancellation, and fd ownership without discarding unrelated work.
- [x] `edit plans/2026-07-22-exclusive-network-resource-management/plan.md steps/step-6.md` — validation evidence and physical-only gates recorded.

## Quality Checklist
- [x] Evidence-before-edit: all implementation steps and available validation commands are complete.
- [x] Existing pattern / reuse checked: final diff matches project logging, lifecycle, worker, and diagnostic conventions.
- [x] Contract understood: automated success cannot substitute for Switch/iPhone/DLNA physical behavior.
- [x] Risk reviewed: residual hardware-only timing, CDN variability, multicast router behavior, restart bind timing.
- [x] Mitigation recorded: A/B profiles, deterministic transition fields, socket/operation counters, and repeated fixed-source video matrix.

## Validation Checklist
- [x] Direct relevant host suite exits 0: coordinator, network diagnostics, mDNS suspend, AirPlay blocked-client lifecycle, media actor, log policy, and strict C safety.
- [ ] Aggregate `test-airplay` exits 0 — it stops at the unchanged Cygwin closed-peer assertion in `scripts/test_log_mirror.c:95` after all new prerequisites pass.
- [ ] Sanitizer suite exits 0 — the installed Cygwin GCC lacks `-lasan` and `-lubsan`; linker failure occurs before any test binary runs.
- [x] Clean strict Profile 12 and Profile 13 Switch builds exit 0.
- [x] `git diff --check` exits 0.

## Test Checklist
- [x] Physical checklist runs Home, five DLNA cycles, IPTV, AirPlay reconnect, final DLNA, and shutdown while naming exact modes/fd fields to report.
- [x] External HTTP 514/554 and `ytdl_hook` failures are explicitly separated from local resource-transition evidence.
- [x] No known automated failure attributable to this implementation remains; the unrelated Cygwin assertion and missing sanitizer runtimes are preserved as visible validation limitations.

## Implementation Notes
- Strict host successes: network diagnostics, AirPlay mDNS suspend, AirPlay blocked-client stop/restart, protocol coordinator (including quiesce retry), media actor, log policy, size/SOAP/player/seek/IPTV URL/AirPlay DNS safety tests, and shutdown-order/JSON checks.
- `test-airplay` reaches and passes every new prerequisite, then fails the unchanged `test_log_mirror.c:95` expectation because a closed local socket peer is not reported as failed on this Cygwin run.
- `test-c-safety-sanitize` cannot link because the installed host toolchain has neither `libasan` nor `libubsan`; this is an environment capability gap, not a sanitizer finding.
- Clean Profile 12 and Profile 13 cross-builds both produce a linked NRO. The final workspace binary is Profile 13.
- Physical correctness remains intentionally delegated to the documented fixed-source Switch/iPhone/DLNA run.

## Files Changed
- All implementation and validation files listed in Steps 1–5.
- `scripts/test_shutdown_order.py`
- `scripts/test_airplay_server_lifecycle.c`
- `plans/2026-07-22-exclusive-network-resource-management/*`
