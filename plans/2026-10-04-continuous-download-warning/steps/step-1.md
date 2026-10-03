# Step 1: Warn against development downloads

> Status: COMPLETED
> Created: 2026-10-04

## Goal
Publish an unambiguous development-only warning now and preserve it across CI updates.

## Prerequisites
- Current release metadata and build/release workflows inspected; user request authorizes warning change.

## Deliverables
- Dedicated bilingual warning with stable link; updated CI name/body and README label; current remote release updated.

## Plan
- [x] Add .github/continuous-release-notes.md and update build.yml publisher and README release description.
- [x] Validate YAML, isolated notes shell generation and diff; edit remote title/body using dedicated notes.
- [x] Read back release metadata and confirm v0.3.1 remains latest with unchanged assets.

## Quality Checklist
- [x] Evidence-before-edit: build.yml read, release name/body usages searched, gh validation discovered.
- [x] Reuse: existing body_path notes generation and gh CLI retained.
- [x] Contract: rolling tag/assets unchanged; warning only on Continuous; latest false.
- [x] Risk: overwriting unrelated local changes or official release notes.
- [x] Mitigation: scoped patch, dedicated notes and metadata readback; no playback edits.

## Validation Checklist
- [x] YAML and git diff --check pass.
- [x] Remote title/body/prerelease/latest readback matches intent.

## Test Checklist
- [x] Isolated CI notes generation contains warning, stable link and commit metadata; no network or tag push in test.
- [x] Official release asset digest unchanged.

## Implementation Notes
- Ruby YAML load and isolated Bash notes generation passed, exit 0; warning first, stable URL and main/test-commit metadata verified. Full preparation script bash -n passed independently.
- Git diff --check passed, exit 0; actual workflow/README diff and dedicated notes re-read. Existing README edits preserved.
- gh release edit continuous succeeded; gh release view readback confirms DO NOT DOWNLOAD title, bilingual body and prerelease true. No asset upload or deletion.
- gh api releases/latest still returns v0.3.1. Stable asset SHA256 remains a16763ec60081513b5b7648abd782b004d99b0c270955ca2b7a7d879550454c4; Continuous asset SHA256 remains a7c8af6525977294ef5179d6e54d9f58222ebf6cc3f8857c5e10d0bd092a814a.
- Local stale continuous tag was excluded from analysis after detecting its mismatch. GitHub compare API confirms no player/IPTV source changes between published versions, but toolchain changes exist; periodic stutter remains undiagnosed without user trace/download/source details.
- CI change is local, not committed/pushed in this task. It must reach main before the next CI update to prevent the previous workflow overwriting the online warning.
- No device rebuild or new CI tests: only release metadata/documentation changed.

## Files Changed
- .github/continuous-release-notes.md
- .github/workflows/build.yml
- README.md (Continuous subsection only)
- plans/2026-10-04-continuous-download-warning/plan.md
- plans/2026-10-04-continuous-download-warning/steps/step-1.md
- Online GitHub Continuous release title/body metadata (assets/tags untouched).
