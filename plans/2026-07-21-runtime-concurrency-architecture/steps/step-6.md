# Step 6: Validate And Promote

> Status: IN_PROGRESS
> Created: 2026-07-21

## Goal
Prove the concurrency architecture across automated regression and a controlled physical protocol-switching matrix before it replaces the release baseline.

## Prerequisites
- Steps 1-5 completed with no known host/Switch build failures.
- Physical Switch and representative DLNA/IPTV/AirPlay senders are available for the final gate.

## Deliverables
- Automated tests and strict release/trace builds pass from clean state.
- Physical matrix covers cold start, X input during service startup, DLNA/IPTV/AirPlay switching, home during load, reconnect, and exit.
- After this step: release promotion is based on actor/supervisor health evidence, not absence of visible crashes.

## Plan
- [x] `bash` clean focused/full host tests and available sanitizers.
- [x] `bash` clean strict trace and release Switch builds; record hashes and feature attestation.
- [ ] `test` physical matrix — collect persistent runtime logs and verify command/service heartbeats.
- [x] `edit` plan/docs — record automated results and residual physical-test risk.

## Quality Checklist
- [x] Evidence-before-edit: validation commands and physical scenarios fixed before run.
- [x] Existing pattern / reuse checked: current Full Trace tasks and persistent SD logs used.
- [x] Contract understood: promotion requires all actor/supervisor invariants, not just successful playback once.
- [x] Risk reviewed: hardware-only decoder/render/network behavior and long-session races.
- [x] Mitigation recorded: protocol-switch matrix, reconnect repetitions, and soak run.

## Validation Checklist
- [x] All clean builds/tests pass and hashes are recorded.
- [ ] Physical matrix has no main-loop stall, stale command execution, or teardown hang.

## Test Checklist
- [ ] Automated suite plus physical protocol-switch/soak matrix pass.

## Implementation Notes
Focused actor/coordinator/log/shutdown tests, the full AirPlay host suite,
ASan/UBSan, TSan, formatting, and the strict nvtegra/deko3d/Ed25519 trace build
pass. Physical validation remains intentionally open because hardware-only
render, decoder, controller, and network scheduling cannot be proven on host.

The first physical matrix attempts exposed a logger regression before protocol
validation: live nxlink output stopped within seconds and the main frame later
stalled. The runtime heartbeat called `log_get_runtime_stats()`, which waited on
the sink mutex while the logger held it across SD flush and socket send. History
also shows commit `ce0257b` disabling Switch file I/O for this freeze class.
Health snapshots now use memory-only state, nxlink uses zero-timeout `poll()`
before nonblocking send, and live nxlink is the primary sink with fully buffered
SD fallback. Automated validation passes, but the physical matrix remains open.
Sink configuration now completes before `log_runtime_init()`, leaving the
worker as the sole runtime sink owner and removing the sink mutex entirely.
An intermediate follow-up stopped redirecting `stderr` and gave the raw socket
to the logger alone. The subsequent physical result below superseded that
experiment because it did not preserve live output.

The next two physical runs established that the host's `Connection reset by
peer` arrived only during application shutdown, not when live output stopped.
The UI also remained responsive, confirming the sink-lock removal, but the Full
Trace mirror became silent after its first heartbeat. The former zero-timeout
`poll()` preflight could classify a platform-specific/transient condition as a
hard failure, and `log.c` then permanently disabled mirroring. The mirror now
uses only the socket's nonblocking `send()` result, treats backpressure and
resource pressure as transient drops, retries after hard failures, and restores
the v0.2.0 `nxlinkStdioForDebug()` stderr descriptor path. Playback validation
remains open until a physical Full Trace run confirms continuous heartbeats and
identifies the first libmpv stage after `MPV_EVENT_START_FILE`.

- Release NRO SHA-256: `7528ddd7ef29aaf666de7009fd374944bbd4036a1722b86000107cc76b9e1b1d`
- Full Trace NRO SHA-256: `3f81bc4f43cb5e860077802136526e70dc5989fdbc2e31855daf9b2c96d58b4a`
- Logger-isolation Full Trace NRO SHA-256: `2f5c63c6b170c0bd05d55b0eca453d1a6856a874c3e711ffea0e0971404c361f`
- Final single-writer logger Full Trace NRO SHA-256: `56d67606ffd6a378fead72b3b6af53c552759d053cdf4668427180b0dc0710aa`
- Retry-safe stderr-mirror Full Trace NRO SHA-256: `68f783f22a0535f5720838f62c24088650b8453293b75f74a49fb459a88e4c6d`
- mpv-info Full Trace NRO SHA-256: `5043617addd5bea1bbc2614e53ea605b4da70d32fdc5d91e8af3dbb9b9caf60b`
- Release attestation: libmpv, deko3d, Ed25519, libnx randombytes, and PlayFair enabled.

## Physical Matrix

| Scenario | Action | Pass evidence |
|---|---|---|
| Cold start | Repeatedly press `X` while the player/backend and services initialize. | Input heartbeat frames advance; the action is observed; service start does not block main. |
| DLNA load | Start DLNA, press `B` during loading, then start it again. | Home returns; old generation is rejected if it arrives late; the new session plays. |
| IPTV switch | Open a playlist and change channels repeatedly with buttons, stick, and touch. | One active generation; no UI stall, corrupted frame loop, or queue growth. |
| Protocol takeover | Run DLNA -> IPTV -> AirPlay -> DLNA. | Ownership generation increases; stale callbacks are rejected rather than executed. |
| Reconnect | Pair/connect/disconnect AirPlay three times. | Each service/actor heartbeat recovers and no stream bridge survives teardown. |
| Exit under load | Exit once idle, once loading, and once playing. | Persistent shutdown phases reach logger shutdown; no render/backend use after detach. |
| Soak | Play for at least 20 minutes and switch protocol twice. | Queue depth returns to zero, heartbeat ages recover, and high-water marks remain bounded. |

## Files Changed
- `docs/threading-design.md`
- `docs/player-layer.md`
- `plans/2026-07-21-runtime-concurrency-architecture/`
- `source/log/log.c`
- `source/log/log.h`
- `source/log/mirror.c`
- `source/main.c`
- `scripts/test_log_mirror.c`
