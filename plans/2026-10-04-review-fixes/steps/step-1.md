# Step 1: Preserve IPTV configuration on save failure

> Status: COMPLETED
> Created: 2026-10-04

## Goal
Protect favorites, recent IDs, source database and preset-source edits during replacement failures.
## Prerequisites
- User approved finding 1; coordinator changes excluded.
- Writers and existing language backup logic inspected.
## Deliverables
- Private replacement helper and focused failure tests.
## Plan
- [x] Replace delete-before-rename in configuration writers.
- [x] Add host fault injection and run IPTV tests.
## Quality Checklist
- [x] Evidence: writer callers and current test fixture read.
- [x] Reuse: follow home.c backup strategy, no framework.
- [x] Contract: save false leaves old data recoverable.
- [x] Risk: SD I/O and rollback failure.
- [x] Mitigation: preserve backup, test overwrite restrictions and failed rename.
## Validation Checklist
- [x] Diff excludes playback and protocol code.
## Test Checklist
- [x] make test-iptv-data passes including fault cases.
## Implementation Notes
All three configuration writers now share a private backup/rollback helper. Buffered write errors are checked before replacement. A failed rollback preserves .bak and a subsequent save cannot delete that only recovery copy. Host tests passed for first save, overwrite-rejecting SD rename, close, backup, installation and rollback failures, and the existing 10041-channel catalog tests. No download/decoder/coordinator behavior changed. This does not promise power-loss atomicity.
## Files Changed
- source/iptv/iptv.c
- scripts/test_iptv_data.c
