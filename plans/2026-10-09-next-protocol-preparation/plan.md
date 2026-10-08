# Plan: Next Protocol Documentation And Push

> Status: ACTIVE
> Created: 2026-10-09
> Last Updated: 2026-10-09

## Goal
Document feasibility-first Miracast and Google Cast development, then validate and push the accumulated approved work on main without a release tag.

## Assumptions
- Switch Wi-Fi Direct/WFD access and acceptance by stock Google Cast senders remain unverified.
- Native Windows adapter validation remains pending.

## Open Questions
None.

## Spec-Lite
N/A - covered by Goal, Deliverables, and Validation.

### Acceptance Criteria
- [x] New protocols clearly marked not implemented with feasibility gates and staged acceptance criteria.
- [x] Existing actor/ownership/threading boundaries reused, no runtime protocol changes.
- [ ] Reviewed approved changes committed and pushed without force or new tags.

### Non-goals
- Implement protocols, expand CI checks, fix the deferred coordinator race or publish a stable version.

### Edge Cases
- Remote divergence must not be resolved by force-pushing.
- Push success is not CI success; main may trigger the existing Continuous workflow.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Next protocol preparation | Miracast, Google Cast, FCast, DIAL | Miracast first; Google Cast candidate; FCast/DIAL deferred | User request |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Write protocol development preparation and update navigation/roadmap. |
| Step 2 | `steps/step-2.md` | IN_PROGRESS | Validate accumulated approved changes and commit/push main. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Whitespace | `git diff --check` | Git | yes |
| Workflow regression | `python3 scripts/test_dev_workflow.py` | Existing script | yes |
| Installer regression | `python3 scripts/test_ffmpeg_install.py` | Existing script | yes |
| IPTV regression | Existing test_iptv_data.c host command | Prior work | yes |
| Remote synchronization | `git fetch origin` and `git push origin main` | User request | yes |

## Context & Learnings
### Key Decisions
- Keep one development preparation document; do not scaffold another protocol framework.
### Gotchas & Warnings
- Preserve all approved uncommitted work; exclude generated binaries, logs and private data.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| docs/README.md, ROADMAP.md | Navigation and status | Existing documents read |
| source/app/protocol_media_session.h, source/app/protocol_coordinator.h | Integration boundary | Header reads |
| docs/threading-design.md | Thread ownership guidance | Read |
| Existing modified scripts and IPTV source | Approved work to publish | git status/diff |
### Verified Facts
- Current branch is main with origin https://github.com/Ode1l/NX-Cast.git.
- Roadmap still labels AirPlay unstarted and IPTV planned despite release records and implemented code.
- A narrow search in installed libnx nifm/wlaninf/ldn headers found no P2P/WFD symbol names; this is not proof of platform impossibility.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-10-09 | 1 | Added feasibility gates, staged media work, resource/lifecycle constraints and device matrix; local links and diff passed. |
