# Step 1: Build An Isolated Platform Probe

> Status: COMPLETED
> Created: 2026-10-16

## Goal
Create a standalone probe that can collect the missing device service-access evidence later.

## Prerequisites
- Refactoring checkpoint 2def19a saved; current branch codex/miracast.
- libnx and Atmosphere IPC contracts inspected; user testing currently unavailable.

## Deliverables
- Probe NRO/ELF and a repeatable SDK-only build entry.
- Capability inventory, branch lineage and device instructions.

## Plan
- [x] Add standalone C probe for registry lookup, user-triggered service acquisition and wlan child creation.
- [x] Add independent build script/Makefile and document evidence.
- [x] Compile with warnings as errors; verify no application build/packaging changes.
- [x] Record outputs; include this record in the branch checkpoint commit.

## Quality Checklist
- [x] Evidence-before-edit: read SDK service headers, standard build rules and upstream service contracts.
- [x] Reuse: existing SDK environment script; no new runtime framework or application scheduler.
- [x] Contract: registry presence and access are observations, not proof of WFD support.
- [x] Risks: synchronous service acquisition and undocumented radio APIs.
- [x] Mitigation: isolated optional diagnostic; no radio setup, frame injection or guessed IPC layouts.

## Validation Checklist
- [x] Probe builds with libnx only.
- [x] Shell syntax and diff checks pass.
- [x] Production source and packaging unchanged relative to refactoring checkpoint.

## Test Checklist
- Device checks pending; no synthetic tests pretending to validate Horizon service support.

## Implementation Notes
M0 will remain incomplete until real sender discovery and connection are demonstrated. This step only prepares and compiles the diagnostic.

Standalone output: build/miracast-probe/NX-Cast-Miracast-Probe.nro and matching ELF. Report path: sdmc:/switch/NX-Cast/miracast-probe.log. The report overwrites only its own previous run and checks write/close failures. No private network/profile data is read. Failed registry queries are UNKNOWN; absent registrations are skipped. A registered service can still fail acquisition. Synchronous IPC is explicitly documented as not cancellable here.

main remains 0efdba4; refactoring checkpoint is 2def19a. Application NRO/ELF SHA256 values remain the same as the prior handoff. The standalone probe is outside release staging and requires no changes to CI. No branch has been merged or pushed as part of this step.

## Files Changed
- tools/miracast_probe/main.c
- tools/miracast_probe/Makefile
- tools/miracast_probe/README.md
- scripts/build_miracast_probe.sh
- docs/miracast-platform-feasibility.md
- docs/casting-protocol-development.md
- docs/README.md
- plans/2026-10-16-miracast-m0/plan.md
- plans/2026-10-16-miracast-m0/steps/step-1.md
