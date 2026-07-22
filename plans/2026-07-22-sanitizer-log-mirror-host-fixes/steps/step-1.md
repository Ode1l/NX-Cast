# Step 1: Diagnose And Guard Host Sanitizer Capability

> Status: COMPLETED
> Created: 2026-07-22

## Goal
Determine where any installed sanitizer runtimes belong and make the host sanitizer target report missing link support before executing the test suite.

## Prerequisites
- Plan clarification is complete and `Open Questions` is `None.`.
- Files to inspect or modify: `makefile` and local compiler/package metadata.
- The current host compiler is verified as `/usr/bin/cc` targeting Cygwin.

## Deliverables
- Evidence identifying whether remembered ASan/UBSan files belong to this host compiler or another target/toolchain.
- A minimal `makefile` capability preflight that respects `HOST_CC` and `HOST_SANITIZER_FLAGS`.
- After this step: missing runtimes produce a concise actionable error; a capable override still proceeds.

## Plan
- [x] `bash` local toolchain inventory — inspect compiler search dirs, installed package contents, and bounded filesystem matches for sanitizer runtimes.
- [x] `rg` `makefile` and repository scripts — confirm no existing sanitizer probe can be reused.
- [x] `edit` `makefile` — add the smallest link-capability preflight to `test-c-safety-sanitize` while preserving overrides.
- [x] `bash` `make test-c-safety-sanitize` — verify the current unsupported compiler fails early with the intended message.

## Quality Checklist
- [x] Evidence-before-edit: target read `makefile`, impact search `rg sanitizer`, validation `make test-c-safety-sanitize`
- [x] Existing pattern / reuse checked: search make targets and helper scripts for compiler capability probes
- [x] Contract understood: host-only target, command-line overrides, nonzero failure when instrumentation is unavailable
- [x] Risk reviewed: build portability and false-positive compiler detection
- [x] Mitigation recorded: perform an actual compile+link probe with the selected compiler and flags

## Validation Checklist
- [x] Unsupported Cygwin GCC exits before recursive test compilation with an actionable message
- [x] Probe uses temporary output and leaves no repository artifact

## Test Checklist
- [x] `make test-c-safety-sanitize` — expected explicit unsupported-runtime failure on the current compiler

## Implementation Notes
The installed devkitPro tree contains no filename matching ASan/UBSan, and GCC reports the library names unchanged, proving they are not in its search paths. GCC's configure line targets Cygwin and does not provide libsanitizer. Added an actual compile-and-link probe before the recursive tests; it prints compiler target, flags, and a WSL/Linux or matching-runtime recovery path. The probe's supported path was exercised with sanitizer flags empty and the host suite passed after supplying the pre-existing Cygwin `_GNU_SOURCE` requirement. A protective commit was intentionally skipped because `makefile` already contains substantial uncommitted user/prior changes; the surgical diff was reviewed instead.

## Files Changed
- `makefile`
