# Plan: Miracast M0 Platform Probe

> Status: COMPLETED
> Created: 2026-10-16
> Last Updated: 2026-10-16

## Goal
Preserve the refactoring checkpoint and develop an isolated Switch service probe plus an evidence-backed Miracast capability inventory.

## Assumptions
- The user cannot run device tests now; all runtime results remain pending.
- RTSP refactoring has host/build validation but not device validation.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- main remains at its previous commit; refactoring and Miracast work have separate branches/commits.
- A standalone probe builds with libnx only, without changing NX-Cast playback or discovery.
- Reports distinguish service registration, permission to open a handle, object creation and actual WFD functionality.
- No claim of successful Wi-Fi Direct discovery or Miracast playback without device evidence.
### Non-goals
- Production receiver integration, undocumented radio configuration, firmware changes and Miracast certification.
### Edge Cases
- Non-Atmosphere environment, unavailable service, access denied, failed object creation and unwritable SD report.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Branch lineage | Modify main / isolate work | main -> codex/rtsp-refactor -> codex/miracast | User requested branch management and Miracast development |
| First deliverable | Premature receiver / platform probe | Standalone libnx diagnostic NRO | Consistent with approved M0 feasibility plan |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Build platform probe and document evidence and next gates |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Build probe | bash scripts/build_miracast_probe.sh | New standalone build entry | yes |
| Shell syntax | bash -n scripts/build_miracast_probe.sh | Bash | yes |
| Patch | git diff --check | Git | yes |
| Isolation | git diff 2def19a -- source makefile .github scripts/package_release.sh | Refactoring checkpoint | yes |

## Context & Learnings
### Key Decisions
- Device testing deferred by user on 2026-10-16; next development stays isolated from the application.
- Query Atmosphere HasService before attempting service acquisition; no waits for nonexistent services.
### Gotchas & Warnings
- Synchronous Horizon IPC has no application-level cancellation here. Isolate experiments from production UI and do not claim bounded IPC completion.
- Existence/access to wlan does not demonstrate P2P/WFD support.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| docs/casting-protocol-development.md | M0 requirements | Read |
| /opt/devkitpro/libnx/include/switch/services/sm.h | Available service manager API | Read |
| /opt/devkitpro/libnx/include/switch/services/wlaninf.h | Legacy-only wrapper | Read |
| /opt/devkitpro/libnx/switch_rules | Minimal NRO build | Read |
### Verified Facts
- main is 0efdba4; codex/rtsp-refactor checkpoint is 2def19a.
- Installed wlaninf wrapper is documented for HOS 1.0.0-14.1.2.
- Switchbrew documents wlan service creation and action-frame/vendor-IE commands for HOS 15+; no usable WFD implementation has been verified.
- Atmosphere source at 6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8 documents HasService as TIPC command 65100, input eight-byte service name and output low bit of a byte.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-10-16 | 1 | Saved previous work on a refactoring branch; created codex/miracast. |
| 2026-10-16 | 1 | Built independent libnx diagnostic NRO/ELF; documented service generations, remaining gates and pending device results. Completion covers probe preparation, not M0 feasibility. |
