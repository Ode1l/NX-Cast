# Step 1: Refresh and publish bilingual documentation

> Status: COMPLETED
> Created: 2026-10-04

## Goal
Give users matching, current 0.3.2 README pages and correct linked installation guidance.

## Prerequisites
- Both README pages, install/IPTV docs, release notes and actual build/publishing recipes read.
- Clean main worktree; v0.3.2 is already published.

## Deliverables
- Updated README.md, README_CN.md and docs/install.md pushed to GitHub without changing binaries or tags.

## Plan
- [x] Update both README pages with release highlights, user-facing capabilities, installation, controls and developer instructions.
- [x] Correct the linked install guide's outdated standalone-NRO claim.
- [x] Check relative links, stale claims and whitespace; review diff.
- [x] Commit documentation with [skip ci], push main and verify remote README content.

## Quality Checklist
- [x] Evidence-before-edit: both targets read; impact checked against Dockerfile/makefile/workflows/docs/iptv.md.
- [x] Reuse: retain existing logo, media-center identity and technical documentation links.
- [x] Contract: documentation only; no app behavior or release artifact changes.
- [x] Risk: stale build commands, overwritten configuration or exaggerated AirPlay claims.
- [x] Mitigation: match published recipe, warn to back up settings and retain explicit experimental exclusions.

## Validation Checklist
- [x] Local Markdown targets exist; no old version/install/CI claims remain in edited pages.
- [x] git diff --check passes; GitHub readback matches committed README files.

## Test Checklist
N/A: documentation-only. No new CI tests; accepted 0.3.2 code/binary remains unchanged.

## Implementation Notes
Both landing pages now lead with features, the stable download and language navigation, followed by practical casting/IPTV setup, aligned controls and concise developer guidance. Removed obsolete unsupported-AirPlay, old pagination, baseline FFmpeg and arbitrary-branch/standalone-NRO release claims. Added 0.3.2 mirror audio notes and accurate experimental exclusions. The installation guide warns about overwriting customized sources and keeping pairing identities private.

Validation: 24 relative links/images exist; Markdown fences are balanced; six Bash blocks pass syntax checks (no package installations/builds executed); stale-claim checks and whitespace check pass. Documentation commit 442df87 was pushed. GitHub content API blob hashes match local committed README.md (e72319d91359c21c531f171051b64b5a35b98f6e), README_CN.md (c19de6bb57d0b5d2510c87ae7fa71c5a189c6ac2) and docs/install.md (ff8b25d748b585d529d66cab32fa08e7b999e07a). Used [skip ci] to avoid a redundant binary build; v0.3.2 and its ZIP remain unchanged.

## Files Changed
- README.md
- README_CN.md
- docs/install.md
- plans/2026-10-04-readme-0-3-2/plan.md
- plans/2026-10-04-readme-0-3-2/steps/step-1.md
