# Step 3: Non-Truncating IPTV URL Resolution

> Status: COMPLETED
> Created: 2026-07-21

## Goal
Move IPTV URL resolution into a tested boundary module that rejects malformed or truncated transport URLs.

## Prerequisites
- Step 2 completed.
- Current `iptv_resolve_url()` callers and base/reference forms read.
- Files to modify: IPTV URL module, `iptv.c`, tests, and `makefile`.

## Deliverables
- A bool-returning URL resolver with defined absolute, scheme-relative, root-relative, and path-relative behavior.
- Overlong results are rejected and never submitted as playable URLs.
- After this step: URL edge tests and IPTV parsing/build callers pass.

## Plan
- [x] `write` `source/iptv/url.h` and `source/iptv/url.c` — extract bounded resolver with explicit success/failure.
- [x] `edit` `source/iptv/iptv.c` — replace static resolver and handle failures at EPG/channel/logo call sites.
- [x] `write` `scripts/test_iptv_url.c` — test URL forms, empty inputs, exact-fit, and one-byte overflow.
- [x] `edit` `makefile` — add `test-iptv-url` to the safety aggregate.
- [x] `bash` focused tests and `git diff --check` — verify no silent transport truncation remains.

## Quality Checklist
- [x] Evidence-before-edit: resolver and callers read; impact search recorded; focused command known.
- [x] Existing pattern / reuse checked: no existing general URL resolver found.
- [x] Contract understood: output is empty on failure and complete/NUL-terminated on success.
- [x] Risk reviewed: behavior drift for relative paths and signed query URLs.
- [x] Mitigation recorded: table-driven tests preserving current valid forms.

## Validation Checklist
- [x] `make test-iptv-url` exits 0.
- [x] Strict compilation of affected Switch objects succeeds.

## Test Checklist
- [x] Sanitized URL tests pass.

## Implementation Notes
- The resolver is a pure module with no FFmpeg, libnx, filesystem, or global-state dependency.
- Absolute URI, scheme-relative, root-relative, remote path-relative, and local path-relative forms preserve existing valid behavior.
- Remote bases such as `https://host?token=1` now resolve a relative segment to `https://host/segment` instead of appending after the query.
- Playable URL failure skips the channel; optional logo/EPG failure leaves the optional field empty.
- Focused normal/sanitized tests and an incremental strict Switch build pass.

## Files Changed
- `source/iptv/url.h`
- `source/iptv/url.c`
- `source/iptv/iptv.c`
- `scripts/test_iptv_url.c`
- `makefile`
