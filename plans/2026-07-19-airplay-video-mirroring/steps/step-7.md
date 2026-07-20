# Step 7: iPhone Control Handshake Milestone

> Status: IN_PROGRESS
> Created: 2026-07-19

## Goal
让真实 iPhone 从发现、配对进入 `/info`、`SETUP`、`RECORD` 和 `TEARDOWN` 完整控制会话，但暂不播放媒体。

## Prerequisites
- Step 6 completed — iPhone can discover NX-Cast and open the advertised control port.
- Sanitized `TRACE_AIRPLAY=1` logging policy is defined; no keys/PIN/body dumps are allowed.

## Deliverables
- `source/protocol/airplay/protocol/handlers.[ch]` 返回当前阶段所需设备信息和会话响应。
- `source/protocol/airplay/security/fairplay.[ch]` 仅实现镜像会话建立所需的非内容 DRM 兼容握手；若无法独立验证则停止并记录阻塞证据。

## Plan
- [x] `rg` UxPlay first and RPiPlay second for `/info`, `/fp-setup`, SETUP/RECORD/TEARDOWN state/field differences — create a behavior matrix.
- [x] `write` `source/protocol/airplay/protocol/handlers.[ch]` — implement endpoints with capability-gated fields and explicit session errors.
- [x] `write` `source/protocol/airplay/security/fairplay.[ch]` — isolate required session-key negotiation from pairing and media code.
- [x] `edit` `Makefile` and logging configuration — add opt-in `TRACE_AIRPLAY` with mandatory secret redaction.
- [x] `edit` `scripts/smoke_airplay.py` and fixtures — replay sanitized control transcript through teardown.
- [x] `vendor` GPL PlayFair from a fixed UxPlay commit — preserve source, copyright, license and provenance without importing unrelated server/media code.
- [x] `edit` `source/protocol/airplay/security/fairplay.[ch]` — adapt stage-one replies and wrapped-key decrypt through the isolated backend with bounded inputs and secure teardown.
- [x] `test` deterministic FairPlay fixtures, malformed modes, state ordering and receiver capability advertisement with the real backend.
- [ ] `bash` tests/build, then capture iPhone state/length/sequence trace — accept only a clean RECORD-to-TEARDOWN flow.

## Quality Checklist
- [x] Evidence-before-edit: endpoint matrix distinguishes current UxPlay behavior from RPiPlay legacy behavior.
- [x] Existing pattern / reuse checked: Step 2 plist, Step 3 RTSP, Step 5 pairing and existing trace flags.
- [x] Contract understood: handler order is session-bound; unsupported methods return protocol errors without crashing.
- [x] Risk reviewed: undocumented compatibility, proprietary handshake boundary, secret leakage and false success.
- [x] Mitigation recorded: isolated module, deterministic transcripts, real-device gate and explicit block condition.

## Validation Checklist
- [x] `make test-airplay` and local control smoke exit 0.
- [x] Strict Switch build exits 0 with `TRACE_AIRPLAY=0` and `=1`.
- [x] GitHub Actions run `29728903616` passes host tests, the official devkitPro Docker build, strict Switch build, package checks, artifact upload and continuous Release publication for commit `f5e21b2`.
- [x] The first physical-test log is explained before protocol traffic: the uploaded local NRO lacks Ed25519, and strict VS Code/nxlink build gates now prevent that artifact from being uploaded again.
- [x] Normal and ASan/UBSan host suites, JSON/shell checks, expected missing-libsodium gate and a traced Switch development compile pass after startup diagnostic changes.
- [x] GitHub Actions run `29745733797` passes the strict release pipeline for `c6fd4ca`; its 25,461,434-byte NRO replaces the incompatible local artifact for the next physical test.
- [x] Switch libsodium startup no longer uses its fatal `/dev/urandom` sysrandom path; the strict NRO registers the libnx `randomGet()` implementation before `sodium_init()`.
- [x] GitHub Actions run `29748063877` passes the complete strict release pipeline and publishes the corrected 25,461,434-byte NRO to the continuous Release.
- [x] Hardware attempt 3 startup is moved off the first-frame path, TRACE startup markers bypass the async logger, and mDNS parity adds the pairing identity plus truthful audio/no-separate-RAOP feature bits.
- [x] GitHub Actions run `29750313369` passes the full host/container/package/release pipeline for `25cc6a3` and publishes the corrected 25,461,434-byte NRO.
- [ ] iPhone trace reaches RECORD and TEARDOWN without retry loop or leaked secret data.

## Test Checklist
- [ ] Host transcript and real-device matrix cover first pair, reconnect, wrong PIN, cancel and teardown. Host transcripts pass; the real-device matrix is blocked.

## Implementation Notes
- `/info`, `/server-info`, `/fp-setup`, OPTIONS, SETUP, RECORD, FLUSH, parameter and TEARDOWN handling now use explicit per-connection state and never call the player/UI directly.
- The composed receiver owns pairing identity, TXT data, persistent control transport, lifecycle and optional mDNS in a deterministic start/stop order. Capability bits are removed when their callbacks are absent.
- The user-approved research route vendors only UxPlay's GPL PlayFair subset at commit `3ca7526387e894d6848b84c209de361c3bedd1ec`; the rest of the protocol, pairing, media, player, and UI remain NX-Cast implementations.
- Stage-one modes, stage-two ordering/mode bounds, response capacity, secure state reset, wrapped-key output, and receiver capability publication have deterministic host coverage. Invalid mode input is rejected before entering the legacy algorithm.
- Normal tests, ASan/UBSan, Clang static analysis, real TCP receiver/mDNS/pairing/transport smoke and strict Switch builds with trace disabled/enabled pass.
- GitHub Actions run `29728903616` produced artifact `8455447635` and updated the continuous Release to the PlayFair integration commit, proving the pinned release environment builds and packages the backend.
- The 2026-07-21 nxlink log contains only `integration unavailable` and no receiver/mDNS startup line. The uploaded NRO is 25,391,802 bytes and the local Switch pkg-config tree has no `libsodium.pc` or `libsodium.a`; therefore the failure is the known Ed25519 build dependency, not an observed mDNS or PlayFair protocol failure.
- VS Code build tasks and `scripts/run_nxlink.sh` now set `NXCAST_REQUIRE_AIRPLAY_ED25519=1`; the all-trace task also sets `TRACE_AIRPLAY=1`. Integration, receiver and mDNS failures expose only stage/errno metadata, and opt-in traces use the visible WARN channel.
- Run `29745733797` verifies the fix under the pinned release environment with Ed25519 enabled. The downloaded continuous NRO has SHA-256 `a07b9856af7cc33821e346f5a837f3c8b27d4a3cd649130d5c168213d1bceff7` and is ready for an upload-only discovery retest.
- The second hardware attempt reset nxlink before the first trace only after `switch-libsodium` was installed. Local archive/disassembly inspection verified `sodium_init()` stirred the default sysrandom backend, which opens `/dev/urandom` and terminates through `sodium_misuse()` when the device is unavailable on libnx.
- `crypto.c` now installs a process-wide `randombytes_implementation` backed by libnx `randomGet()` under an atomic one-time initialization gate. Integration and receiver startup expose pre/post stage markers so another pre-discovery failure can be localized without logging secrets.
- Normal and ASan/UBSan host suites pass. A strict TRACE Switch build is 25,461,434 bytes with SHA-256 `8f3e243501f39cbb86027b3a8f6d6ced0af10213aff2f2842a3e6e372314af7a`; NRO strings and ELF disassembly verify the libnx marker and registration-before-initialization order.
- The clean attested `release-build` and `scripts/package_release.sh` both pass with `airplay-randombytes=libnx`; the workspace was then rebuilt in strict TRACE mode without a release attestation for the physical retest.
- GitHub Actions run `29748063877` produced artifact `8463310854` (32,614,837 bytes) and updated the continuous Release to code commit `bca12bc`, independently verifying the official Docker/Switch build and package path.
- Hardware attempt 3 reached Ed25519, identity and TXT creation but reset nxlink before the queued handler completion marker. Since AirPlay startup ran synchronously before the first render loop and trace messages used the async logger, the log could neither distinguish the exact crash boundary nor avoid the observed black startup interval.
- AirPlay startup now runs in a cancellable 256 KiB worker and shutdown joins it before receiver/player/network teardown. TRACE startup boundaries write and flush directly to stderr with monotonic timestamps, including handler allocation, control server, local address, mDNS bind/join/thread and announcement stages.
- Switch mDNS now gets its address from libnx NIFM instead of probing an external `8.8.8.8` route. The AirPlay TXT record includes the identity-derived `pi`; advertised features include mirror audio and bit 30 because NX-Cast intentionally serves no separate RAOP/AirTunes music service.
- Normal, TRACE and ASan/UBSan host suites, the attested non-TRACE release build and a strict TRACE Switch build pass after the hardware-attempt-3 changes. Physical discovery remains the acceptance gate.
- GitHub Actions run `29750313369` produced artifact `8464258928` (32,613,962 bytes) and updated the continuous Release with a 25,461,434-byte NRO plus 19,768,260-byte SD package for commit `25cc6a3`.
- Remaining gate: automated compatibility and Switch development builds pass, but a real iPhone/Switch must still prove discovery through RECORD/TEARDOWN and H.264/AAC playback before the feature can be called compatible.
- The user selected the GPL open-source research route. Integration must identify UxPlay and PlayFair as upstream sources, retain GPL notices, and must not describe the imported algorithm as clean-room or Apple-authorized.

## Files Changed
- `makefile`
- `source/protocol/airplay/trace.h`
- `source/protocol/airplay/protocol/rtsp.h`
- `source/protocol/airplay/protocol/handlers.[ch]`
- `source/protocol/airplay/security/fairplay.[ch]`
- `source/protocol/airplay/security/pairing.[ch]`
- `source/protocol/airplay/security/crypto.c`
- `source/protocol/airplay/discovery/mdns.[ch]`
- `source/protocol/airplay/receiver.[ch]`
- `scripts/test_airplay_handlers.c`
- `scripts/airplay_receiver_smoke_server.c`
- `scripts/smoke_airplay.py`
- `scripts/smoke_airplay_receiver.py`
- `scripts/test_airplay_fairplay.c`
- `.vscode/tasks.json`
- `.vscode/launch.json`
- `scripts/run_nxlink.sh`
- `source/protocol/airplay/integration.c`
- `source/protocol/airplay/trace.h`
- `source/main.c`
- `source/protocol/airplay/integration.h`
- `source/protocol/airplay/protocol/handlers.c`
- `scripts/airplay_mdns_smoke_server.c`
- `scripts/smoke_airplay_mdns.py`
- `third_party/playfair/`
