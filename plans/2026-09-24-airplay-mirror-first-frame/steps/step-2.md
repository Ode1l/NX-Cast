# Step 2: Correct shared-secret media key derivation

> Status: COMPLETED
> Created: 2026-09-24

## Goal
Derive the paired media key with SHA-512 as both reference receivers do.

## Prerequisites
- Read handlers.c, receiver shared-secret callback, reference crypto and mirror buffer implementations.
- Prior user edits and previous diagnostic fixes preserved.

## Deliverables
- Correct 64-byte digest buffer and SHA-512 derivation.
- Fixed independent expected key vector catches the original SHA-256 error.
- Full Trace NRO ready for device verification.

## Plan
- [x] Replace self-computed test expectation with a fixed SHA-512 vector; verify failure before fix.
- [x] Correct production derivation and add non-secret algorithm trace.
- [x] Run AirPlay tests and Full Trace build.

## Quality Checklist
- [x] Evidence-before-edit: both reference algorithms read, receiver callback confirmed installed.
- [x] Existing pattern / reuse checked: existing SHA-512 wrapper and handler tests reused.
- [x] Contract understood: hash unwrapped key then shared secret, truncate to 16 bytes.
- [x] Risk reviewed: shared audio/video key derivation affects mirrored media.
- [x] Mitigation recorded: fixed reference vector and no key material in logs.

## Validation Checklist
- [x] `make full-trace-build BUILD_JOBS=4`
- [x] `git diff --check`

## Test Checklist
- [x] Fixed-vector handler test fails before fix and passes after fix.
- [x] `make test-airplay`

## Implementation Notes
Device playback is still unverified; AES-CTR success reports only API success, not plaintext validity.
The prior test repeated the same SHA-256 mistake as production. The replacement asserts a fixed independently calculated SHA-512 prefix for inputs 00..0f and 20..3f. It failed on both downstream key consumers before the production fix. Full Trace NRO has been built for the next test.

## Files Changed
- `source/protocol/airplay/protocol/handlers.c`
- `scripts/test_airplay_handlers.c`
