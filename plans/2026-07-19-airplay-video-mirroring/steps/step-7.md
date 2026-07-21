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
- [x] Hardware attempt 4 is localized before all control traffic; normal and ASan/UBSan host suites, Clang analysis and a strict TRACE Switch build pass after removing the isolated NIFM lifecycle and adding mDNS/control boundaries.
- [x] Hardware attempt 5 proves mDNS and TCP accept succeed but the first parser call exceeded the client stack; heap staging and a production frame-size gate reduce the parser frame from 102,672 to 224 bytes with all automated checks passing.
- [x] Hardware attempt 6 reaches the visible PIN flow; its request sequence proves Pair Setup reconnects between `/pair-pin-start` and the SRP challenge, so Setup state now survives RTSP connection replacement with a cross-connection regression test.
- [x] Hardware attempt 7 completes all PIN/SRP stages and reaches Pair Verify twice; the missing empty-body content type and the blocked cross-thread startup announcement are corrected with protocol and lifecycle coverage.
- [x] Hardware attempt 8 completes Pair Verify twice but stops before `/fp-setup`; common UxPlay RTSP response headers, response-send diagnostics and logging-independent readiness are added and pass automated validation.
- [x] Hardware attempt 9 confirms every Pair Verify response is sent successfully but iOS still closes before `/fp-setup`; dual AirPlay/RAOP DNS-SD identity, startup scheduling parity and safe player-to-home teardown are added and pass automated validation.
- [x] Hardware attempt 10 confirms readiness and dual discovery are healthy but iOS still closes immediately after Pair Verify; AirPlay TXT, RAOP TXT and `/info` now share UxPlay's `0x5A7FFEE6` legacy-PIN mirroring profile with regression coverage.
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
- Hardware attempt 4 reached mDNS socket bind and multicast join, then reset before the caller could report mDNS thread completion. No accepted control connection or AirPlay request was logged, so the observed device entry may have been a TTL-cached advertisement and the log does not yet implicate pairing or PlayFair.
- The earlier NIFM address lookup is superseded: AirPlay now uses the same UDP route lookup as the stable DLNA path and no longer initializes/exits the process-global NIFM service during background startup. TRACE builds synchronously mark `threadCreate`, `threadStart`, mDNS worker entry, accepted control sockets and parsed method/body sizes without logging request bodies or secrets.
- Normal and ASan/UBSan `make test-airplay`, focused Clang analysis, `git diff --check`, and a strict `TRACE_AIRPLAY=1` Switch build pass. The resulting NRO is 25,461,434 bytes with SHA-256 `b0ade7586b5fd157052acd9f3228eb96a20e63fb90d7e7f529d665e4a9adce63`; physical retest remains required.
- Hardware attempt 5 reaches `mdns worker entered`, successful `threadStart`, `control accepted`, and `control client-worker entered`, then resets before the first parsed request. This excludes discovery startup and local-address lookup from the immediate crash boundary.
- `AirPlayRtspRequest` is 102,480 bytes because it owns 48 fixed 2,112-byte header slots. The parser previously instantiated this object locally, producing a measured 102,672-byte AArch64 frame on a 65,536-byte control-client stack; it now stages on the heap and transfers ownership only after complete validation, preserving the existing output contract.
- `make test-airplay` now compiles the production RTSP parser with `-Wframe-larger-than=32768`; AArch64 `-fstack-usage` reports 224 bytes. Normal and ASan/UBSan suites plus the strict `TRACE_AIRPLAY=1` Switch build pass; physical first-request/pairing retest remains required.
- Hardware attempt 6 no longer crashes in request parsing. Session 1 receives `/pair-pin-start`, while sessions 2-5 reconnect before sending the 86-byte SRP challenge; the old connection-owned pairing object was therefore destroyed before the challenge and each request returned to `IDLE`, causing iOS to request the PIN again.
- PIN/SRP Pair Setup is now a serialized service-owned transaction, while Pair Verify and its shared secret remain connection-owned. The host test closes the RTSP session between PIN start, challenge, proof and key exchange; normal, ASan/UBSan, Clang analysis and a strict TRACE Switch build pass.
- The next physical-test NRO is 25,461,434 bytes with SHA-256 `4d4d66de383df18828717eb939790f8fa660e3a492ec74ca3be9c2197eef2038`. Secret-safe traces now include request URI and Setup state transitions so any remaining failure is attributable to challenge, proof or key exchange.
- Hardware attempt 7 proves Pair Setup succeeds through `pin-started -> srp-challenge -> srp-verified -> paired`. Each connection then sends both 68-byte Pair Verify stages but iOS reconnects instead of advancing to `/fp-setup`; NX-Cast's successful Verify finish omitted the empty `application/octet-stream` response used by the audited UxPlay behavior.
- The same trace shows the background integration worker remains inside the caller-thread initial mDNS announcement, explaining the persistent red `AirPlay: WAIT` even though the worker can answer discovery queries and the control server can pair. Initial announcement now belongs to the mDNS worker, so `airplay_mdns_start()` can complete without concurrent socket sends.
- Pair Verify finish now returns `Content-Type: application/octet-stream`, its tests assert that response contract, and traces expose both Verify state transitions without payloads or keys. Normal and ASan/UBSan suites, focused Clang analysis and a strict TRACE Switch build pass.
- The next physical-test NRO is 25,461,434 bytes with SHA-256 `2edbecd5675260f4bb243b5dcec828d266194ab1fd1ab863e8760be57cf19b8a`.
- Hardware attempt 8 reaches `verify-challenge -> verified` with status 200 on two separate control sessions, but iOS emits neither `/fp-setup` nor `SETUP`. This excludes PIN/SRP and Pair Verify cryptography from the current failure boundary.
- UxPlay commit `acfb5494fb2b52ca358e62ef59d6ee0ab20dec49` adds `Server: AirTunes/220.68` to control responses and `Audio-Jack-Status: connected; type=digital` to CSeq RTSP responses except RECORD. NX-Cast now applies the same common-header contract in the central dispatcher and tests it across session transitions.
- The hardware trace records mDNS completion at `305513559 ms` and integration completion at `305535445 ms`; source inspection localizes the delay to the receiver's final asynchronous trace call. Receiver startup no longer enters the logger after becoming ready, and integration commits `running/status` before optional trace output.
- TRACE control diagnostics now pair each request with status/body/header counts, send success and connection close reason. Normal and ASan/UBSan `make test-airplay`, focused Clang analysis, `git diff --check`, and a strict TRACE Switch build pass.
- The next physical-test NRO is 25,461,434 bytes with SHA-256 `d05c1b71ce8dd8859e5c28a37b3fe2107b8e2c3ee5fa7da641d0ba75bcef73fe`.
- Hardware attempt 9 supersedes the prior logging diagnosis: after that log call was removed, mDNS still completed at `307244911 ms` while integration remained pending until a control connection at `307275960 ms`. The startup worker ran at priority `0x2c`, below the receiver/mDNS/player workers at `0x2b`; it now uses `0x2b`, allowing the home AirPlay indicator to reflect readiness without depending on unrelated network activity.
- Both real control attempts returned status 200 for `/info`, Pair Verify challenge and Pair Verify completion with `sent=1`, then iOS closed the socket before `/fp-setup`. UxPlay commit `acfb5494fb2b52ca358e62ef59d6ee0ab20dec49` advertises both `_airplay._tcp` and `_raop._tcp` on the control port, so NX-Cast now publishes the same dual identity with bounded service-specific DNS records and RAOP TXT metadata instead of relying only on feature bit 30.
- Returning home while libmpv was still `LOADING` switched deko3d ownership immediately and was followed by the device reset. B, touch Home and PIN-forced Home now dispatch stop, retain the video view for a 250 ms teardown grace, and only then apply a manual Home override that bypasses the normal stopped-frame hold.
- Normal and ASan/UBSan `make test-airplay`, dual-service UDP lifecycle smoke, focused Clang analysis, `git diff --check`, and a strict TRACE Switch build pass. The next physical-test NRO is 25,461,434 bytes with SHA-256 `71c9f715c8a5607ae3d63a3883f561166ff004e48846bb7e3cbedd0eb40ff7ea`.
- Hardware attempt 10 completes startup before the iPhone connects, publishes both AirPlay and RAOP, returns `/info`, completes Pair Verify challenge and finish with status 200 and `sent=1`, then receives a peer close before any `/fp-setup` or SETUP request. This excludes the readiness indicator, discovery transport, PIN/SRP and Pair Verify response delivery from the current boundary.
- The production mask `0x48000391` omitted FairPlay/authentication bits 2, 12, 14 and 22 while advertising legacy video, HLS and rotation bits that UxPlay keeps clear for screen mirroring. NX-Cast now publishes UxPlay's legacy-PIN mirroring profile `0x5A7FFEE6` through `_airplay._tcp` `features`, `_raop._tcp` `ft` and `/info`; receiver capability gating also removes the FairPlay/authentication group if its backend or mirror callbacks are unavailable.
- Normal and ASan/UBSan `make test-airplay`, `git diff --check`, and a strict `TRACE_AIRPLAY=1 TRACE_INPUT=1` Switch build pass. The next physical-test NRO is 25,461,434 bytes with SHA-256 `fe5a04bb901b198ed0c8a73252e2380f703aebc338010fe32079c4e428f5537d`; the startup log now prints the final feature mask for wire confirmation.

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
- `source/protocol/airplay/server.c`
- `source/protocol/airplay/protocol/rtsp.c`
- `source/protocol/airplay/integration.c`
- `source/protocol/airplay/receiver.c`
- `scripts/test_airplay_rtsp.c`
- `source/protocol/airplay/discovery/dns.[ch]`
- `scripts/test_airplay_dns.c`
- `source/player/render/view.c`
