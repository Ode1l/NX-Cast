# Step 1: Identity-Correlated Terminal Lifecycle

> Status: COMPLETE
> Created: 2026-09-02

## Goal
Correlate separate RAOP and HTTP sessions by verified sender identity and terminate active video only after explicit transport teardown plus final control close.

## Prerequisites
- The latest Bilibili trace proves both event orders occur across real sessions.
- Files to modify: pairing API, logical session manager, receiver routing, remote video lifecycle, and focused tests.
- Design: preserve layered IDs and avoid peer-IP identity, confirmed by the user.

## Deliverables
- A bounded sender identity copied from verified pairing state into the session manager.
- An order-independent terminal decision returned by the session manager.
- After this step: focused session/receiver behavior tests cover detach versus terminal release.

## Plan
- [x] `edit` `source/protocol/airplay/security/pairing.{h,c}` — expose verified client identity without exposing mutable internal storage.
- [x] `edit` `source/protocol/airplay/protocol/logical_session.{h,c}` — associate layered sessions by client identity and track teardown/final-close terminal conditions.
- [x] `edit` `source/protocol/airplay/receiver.c` and `source/protocol/airplay/media/remote_video.{h,c}` — route terminal decisions to the matching media owner.
- [x] `edit` `scripts/test_airplay_session.c` and focused media tests — cover both event orders, unrelated senders, and transient close.
- [x] `bash` focused AirPlay session and remote-video test commands — expect zero failures.

## Quality Checklist
- [x] Evidence-before-edit: targets read, impact searched with `rg`, validation targets discovered from existing scripts.
- [x] Existing pattern / reuse checked: existing session manager and remote-video release path will be extended.
- [x] Contract understood: identity bytes are copied; manager owns bounded state; terminal result names one retained media session.
- [x] Risk reviewed: lifecycle regression, stale identity, cross-sender termination, lock/callback ordering.
- [x] Mitigation recorded: no callback under manager mutex; exact identity match; focused order and isolation tests.

## Validation Checklist
- [x] Focused host compilation exits 0.
- [x] Existing AirPlay handler and remote-video tests remain green.

## Test Checklist
- [x] RAOP teardown then final HTTP close releases matching media.
- [x] Final HTTP close then RAOP teardown releases matching media.
- [x] Final close without teardown remains detached.
- [x] Different verified identities never cross-terminate.

## Implementation Notes
The session manager stores copied verified identities at connection and logical-session layers. A terminal media ID is emitted only when transport teardown and final media-control close have both occurred. Receiver callbacks stop only that matching remote-video owner and never execute under the session-manager mutex.

## Files Changed
- `source/protocol/airplay/security/pairing.h`
- `source/protocol/airplay/security/pairing.c`
- `source/protocol/airplay/protocol/logical_session.h`
- `source/protocol/airplay/protocol/logical_session.c`
- `source/protocol/airplay/media/remote_video.h`
- `source/protocol/airplay/media/remote_video.c`
- `source/protocol/airplay/receiver.c`
- `scripts/test_airplay_pairing.c`
- `scripts/test_airplay_session.c`
- `scripts/test_airplay_remote_video.c`
