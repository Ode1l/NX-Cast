# Step 1: Checked Size Arithmetic And DLNA Writers

> Status: COMPLETED
> Created: 2026-07-21

## Goal
Provide reusable checked size operations and make DLNA SOAP/template growth reject overflow before allocation or pointer arithmetic.

## Prerequisites
- Historical/current buffer patterns and direct callers have been read.
- Files to modify: `source/util/size.h`, DLNA writer/template sources, focused tests, and `makefile`.
- Design: use header-only helpers and preserve generated XML bytes for valid inputs.

## Deliverables
- Checked add/multiply/growth helpers with boundary tests.
- SOAP and template buffers fail safely on impossible sizes.
- After this step: focused size/DLNA writer tests and existing host suite pass.

## Plan
- [x] `write` `source/util/size.h` — add checked `size_t` add, multiply, and bounded geometric growth helpers.
- [x] `edit` `source/protocol/dlna/control/soap_writer.c` — replace unchecked reserve/escape arithmetic.
- [x] `edit` `source/protocol/dlna/description/template_resource.c` — replace unchecked XML allocation and growth arithmetic.
- [x] `write` `scripts/test_c_size.c` and `scripts/test_soap_writer.c` — cover zero, `SIZE_MAX`, capacity corruption, escaping, and valid output.
- [x] `edit` `makefile` — add focused host targets and run them.

## Quality Checklist
- [x] Evidence-before-edit: targets read; impact search used `rg -n "soap_writer|template_resource"`; validation commands recorded in plan.
- [x] Existing pattern / reuse checked: local `rtsp_size_add()` and `plist_size_add()` provide the model; no shared helper exists.
- [x] Contract understood: writers own reallocatable buffers and preserve old allocation on growth failure.
- [x] Risk reviewed: integer overflow, OOM, partial output, and dirty worktree.
- [x] Mitigation recorded: checked helpers, focused edge tests, aggregate host suite, strict cross-build later.

## Validation Checklist
- [x] `make test-c-size test-soap-writer` exits 0.
- [x] `git diff --check` exits 0.

## Test Checklist
- [x] `make test-airplay` — all existing host tests pass.

## Implementation Notes
- Protection commit/stash was skipped because the dirty worktree contains broad user-owned ongoing changes.
- The shared helper is header-only and has three operations only: checked add, checked multiply, and bounded geometric growth.
- SOAP writer now rejects corrupt `(buffer, length, capacity)` state and impossible output before `realloc`/`memcpy`.
- DLNA template escaping, appending, and file loading retain existing output bytes while checking every capacity calculation.
- Focused tests and the complete existing host suite pass.

## Files Changed
- `source/util/size.h`
- `source/protocol/dlna/control/soap_writer.c`
- `source/protocol/dlna/description/template_resource.c`
- `scripts/test_c_size.c`
- `scripts/test_soap_writer.c`
- `makefile`
