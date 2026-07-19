# Plan: AirPlay Video Mirroring for NX-Cast

> Status: ACTIVE
> Created: 2026-07-19
> Last Updated: 2026-07-20

## Goal
在不引入 UxPlay/RPiPlay 源码的前提下，用 NX-Cast 自有 C 模块和现有播放器接口实现同一局域网内 iPhone 到 Switch 的非 DRM H.264 屏幕镜像、AAC 同步音频以及 AirPlay URL/HLS 视频投送。

## Assumptions
- 首个支持场景是 iPhone 与 Switch 连接同一 Wi-Fi；AWDL/点对点 AirPlay 不在本计划范围内。
- 配对采用可在 Switch UI 显示的 PIN 流程，设备身份和长期密钥保存在 `sdmc:/switch/NX-Cast/airplay/`。
- 现有 switch-libmpv、FFmpeg、nvtegra 和 deko3d 构建保持不变；实时镜像通过内部流桥接复用该播放链路。
- 真机网络抓包和 iPhone 行为验证需要用户提供 Switch/iPhone 测试结果；主机端协议、解析、加密向量和媒体夹具由自动测试覆盖。
- 仅依据公开协议资料、网络行为和两个参考项目的接口/状态机进行独立实现，不复制其源码、常量表或平台渲染代码。

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [ ] iPhone 的“屏幕镜像”列表能稳定发现 NX-Cast，并能完成 PIN 配对、重连和取消连接。
- [ ] 第一阶段在 Switch 上通过 nvtegra/deko3d 连续播放 iPhone H.264 镜像视频，支持方向/分辨率变化和关键帧恢复。
- [ ] 第二阶段播放 AAC 音频，音画同步可在 30 分钟真机测试中保持可接受且不会持续漂移。
- [ ] 第三阶段接收 AirPlay URL/HLS 的播放、暂停、进度和停止命令，并复用 `renderer_set_uri()` 与现有播放器控制接口。
- [ ] DLNA、IPTV 与 AirPlay 不并发争用播放器；返回主页、断连、退出程序均能有序释放线程、套接字、密钥和流缓冲。
- [ ] 连续连接/断开至少 10 次不崩溃；日志不输出会话密钥、PIN、完整配对载荷或解密后的媒体数据。

### Non-goals
- 完整 AirPlay 2、多房间/多设备同步、HomeKit、独立音乐/纯音频接收器、Apple DRM/FairPlay 商业内容和 MFi 认证。
- 引入 UxPlay、RPiPlay、GStreamer、OpenSSL、libplist 或其平台渲染器源码。
- 第一版支持 AWDL、蓝牙发现、互联网中继或跨子网自动发现。

### Edge Cases
- 不受支持的 DRM、HEVC/高位深、损坏加密包、乱序/丢包、无关键帧、SPS/PPS 变化和突然断电。
- iPhone 旋转屏幕、锁屏、来电、暂停、应用切换、Wi-Fi 漫游以及控制连接先于媒体连接断开。
- 配对身份文件缺失/损坏/只读、重复 mDNS 名称、端口占用、旧配对记录和错误 PIN。
- DLNA/IPTV 正在播放时 AirPlay 连接，或 AirPlay 会话期间用户返回主页/退出程序。

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| 参考实现策略 | 引入源码；仅参考 UxPlay；双实现等权参考 | UxPlay 当前行为为主，RPiPlay 仅校验旧镜像流程；全部用 NX-Cast C 代码重写 | yes |
| 控制服务边界 | 扩展 DLNA HTTP server；独立 AirPlay server | 独立的持久 RTSP/HTTP 控制服务，避免改变 DLNA 的短连接语义 | yes |
| plist 与加密依赖 | libplist+OpenSSL；自有 plist+mbedTLS | 实现所需最小 binary plist 子集，并复用 devkitPro mbedTLS | yes |
| 实时媒体接入 | 新播放器；GStreamer；libmpv 自定义流 | H.264/AAC 复用 FFmpeg MPEG-TS 封装、有限环形缓冲和 `mpv_stream_cb_add_ro()` | yes |
| 能力广播 | 一次声明完整 AirPlay 2；按阶段声明 | 只广播已通过真机验收的功能位，阶段完成后再增加能力 | yes |
| 播放器仲裁 | 多协议同时控制；最后命令获胜 | 单一活动媒体所有者，AirPlay/DLNA/IPTV 显式获取与释放会话 | yes |
| Ed25519 后端 | 自行实现曲线；mbedTLS PSA；官方 libsodium | 使用 devkitPro/Homebrew libsodium；mbedTLS 2.28 不提供 Ed25519，禁止自写密码学替代 | yes |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | 建立 AirPlay 生命周期外壳、状态契约和主机测试入口 |
| Step 2 | `steps/step-2.md` | COMPLETED | 实现最小 binary plist 编解码并以夹具锁定行为 |
| Step 3 | `steps/step-3.md` | COMPLETED | 实现独立持久 RTSP/HTTP 控制服务器和解析器 |
| Step 4 | `steps/step-4.md` | COMPLETED | 实现持久设备身份、随机数和 mbedTLS 加密原语 |
| Step 5 | `steps/step-5.md` | COMPLETED | 实现 Pair Setup/Pair Verify 与加密控制会话 |
| Step 6 | `steps/step-6.md` | IN_PROGRESS | 实现原生 mDNS 广播并让 iPhone 可发现设备 |
| Step 7 | `steps/step-7.md` | PENDING | 打通 iPhone 控制握手至 SETUP/RECORD/TEARDOWN |
| Step 8 | `steps/step-8.md` | PENDING | 实现镜像 H.264 接收、解密、重组和关键帧恢复 |
| Step 9 | `steps/step-9.md` | PENDING | 建立 MPEG-TS 环形缓冲与 libmpv 自定义流桥接 |
| Step 10 | `steps/step-10.md` | PENDING | 完成第一阶段 H.264 硬解镜像真机闭环 |
| Step 11 | `steps/step-11.md` | PENDING | 增加镜像 AAC 接收和音轨输出 |
| Step 12 | `steps/step-12.md` | PENDING | 增加时钟、抖动缓冲和音画同步 |
| Step 13 | `steps/step-13.md` | PENDING | 增加 AirPlay URL/HLS 投送与远程控制 |
| Step 14 | `steps/step-14.md` | PENDING | 完成协议仲裁、UI 状态与安全退出集成 |
| Step 15 | `steps/step-15.md` | PENDING | 完成兼容性、稳定性、CI、文档和发布验收 |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| AirPlay host tests | `make test-airplay` | Step 1 adds target following `scripts/test_iptv_channel_list.c` host-test style | yes |
| Protocol smoke | `python3 scripts/smoke_airplay.py --host 127.0.0.1 --port 7000` | Planned local persistent-session smoke test | yes |
| Strict Switch build | `make clean && make NXCAST_USE_IMGUI_UI=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 -j4` | Existing Makefile/toolchain contract | yes |
| Release package | `./scripts/package_release.sh` | Existing release packaging path | final step |
| Media fixture probe | `ffprobe -v error -show_streams build/tests/airplay-mirror.ts` | Planned Step 9 MPEG-TS fixture | yes |
| Real-device acceptance | `TRACE_MEDIA=1 TRACE_AIRPLAY=1` build + iPhone/Switch test matrix | Planned protocol-specific trace mode; secrets redacted | phase gates |

## Context & Learnings
### Key Decisions
- Clean-room implementation: reference projects define expected wire behavior only; NX-Cast owns all code, threading, storage, player integration and tests.
- UxPlay-first reference: its protocol core is current and includes remote-video handling; RPiPlay is retained only to detect regressions against older mirroring flows.
- Separate control server: AirPlay requires persistent, encrypted, stateful sessions that conflict with the current DLNA server's one-request/close model.
- Existing media path first: a custom libmpv stream avoids introducing GStreamer or a second renderer and preserves nvtegra/deko3d behavior.
- Binary plist scope: implement only dict/array/string/data/uint/real/bool with fixed resource limits; fixtures are checked in as reviewable hex.
- Binary plist integers preserve their raw encoded bit pattern as `uint64_t`, matching the reference API rather than applying a signed interpretation.
- Persistent control transport uses a separate four-slot server; each worker serializes one connection/session and exposes route plus disconnect cleanup callbacks.

### Gotchas & Warnings
- AirPlay compatibility contains undocumented and version-sensitive behavior; each advertised feature must be backed by iPhone packet traces and a real-device acceptance gate.
- FairPlay/DRM content is explicitly unsupported; do not infer that protocol pairing support permits protected commercial streams.
- Binary plist and RTSP lengths are attacker-controlled network input; every allocation and frame length needs a fixed upper bound.
- Media callbacks, player commands and UI rendering run on different threads; direct player/UI calls from network threads are forbidden.
- Trace logs must record sequence/state/length/hash only and must never log PINs, private keys, session keys or full decrypted payloads.
- Generic plist readers can display a high-bit 64-bit integer as negative even though NX-Cast intentionally exposes the same bits as `uint64_t`.
- macOS Control Center can own TCP 7000; host smoke tries the requested port first and uses an ephemeral port only for local test isolation.
- Ed25519 is absent from mbedTLS 2.28; release builds must install `switch-libsodium` and set `NXCAST_REQUIRE_AIRPLAY_ED25519=1` rather than silently using the unavailable backend.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `source/protocol/airplay/discovery/mdns.c` | Existing AirPlay placeholder to replace | `rg -n mdns_discover_airplay source` |
| `source/protocol/http/http_server.c` | DLNA short-connection server boundary | read/search showed global handler and client-close lifecycle |
| `source/main.c` | Network/player startup and shutdown ordering | `rg -n mdns_discover_airplay source/main.c` and lifecycle read |
| `source/player/backend/libmpv.c` | Register custom stream and preserve hardware playback | existing libmpv backend read/search |
| `source/player/core/session.c` | Thread-safe player commands and media ownership integration | `rg -n player_set_uri source/player` |
| `source/player/renderer.h` | URL/HLS phase public playback facade | `renderer_set_uri()` maps to player API |
| `makefile` | mbedTLS linkage, trace flag and host tests | `rg -n TRACE_MEDIA\|NXCAST_REQUIRE makefile` |
| `scripts/` | Host protocol tests and smoke tooling | existing DLNA/IPTV test scripts inspected |
| `scripts/test_iptv_channel_list.c` | Existing dependency-light C host-test convention for Step 1 | direct source read, 2026-07-19 |
| `scripts/test_airplay.c` | AirPlay lifecycle host test entry point | normal and ASan/UBSan test runs, Step 1 |
| `source/protocol/airplay/airplay.h` | Public bounded lifecycle/status/callback contract | source read and strict Switch build, Step 1 |
| `source/protocol/airplay/airplay.c` | Dependency-free lifecycle implementation | host tests and strict Switch build, Step 1 |
| `/opt/devkitpro/portlibs/switch` | Dependency inventory for plist decision | focused `find` found no libplist installation, Step 2 |
| `/tmp/nxcast-airplay-ref.0dZrly/UxPlay` | Primary behavior reference only | shallow clone metadata and protocol file inventory |
| `/tmp/nxcast-airplay-ref.0dZrly/RPiPlay` | Legacy behavior cross-check only | shallow clone metadata and protocol file inventory |
| `source/protocol/airplay/protocol/plist.[ch]` | Owned bounded binary plist value tree and codec | full source read, host tests, static analysis and strict Switch build, Step 2 |
| `scripts/test_airplay_plist.c` | Happy-path, limits, malformed-input and mutation coverage | normal and ASan/UBSan `make test-airplay`, Step 2 |
| `scripts/fixtures/airplay/plist/*.bplist.hex` | Reviewable valid and malformed binary plist inputs | fixture read and host test execution, Step 2 |
| `build/tests/airplay-plist-roundtrip.bplist` | Generated interoperability artifact, ignored by git | Python `plistlib` assertions and `plutil -lint`, Step 2 |
| `source/protocol/airplay/protocol/rtsp.[ch]` | Bounded RTSP/HTTP parse, response and connection-state contract | full source read, unit tests, ASan/UBSan and static analysis, Step 3 |
| `source/protocol/airplay/server.[ch]` | Portable persistent listener/client ownership and cancellation | TCP smoke, ASan/UBSan, static analysis and strict Switch build, Step 3 |
| `scripts/test_airplay_rtsp.c` | Parser, response and session edge/error coverage | normal and sanitizer `make test-airplay`, Step 3 |
| `scripts/smoke_airplay.py` | Real TCP fragmentation, pipelining, timeout, capacity and stop smoke | local runs on requested/ephemeral ports, Step 3 |
| `source/protocol/airplay/security/crypto.[ch]` | Bounded mbedTLS primitives and libsodium Ed25519 facade | RFC vectors, ASan/UBSan, static analysis and strict Switch build, Step 4 |
| `source/protocol/airplay/security/identity.[ch]` | Versioned atomic SD identity with secret-preserving signing API | create/reload/corrupt/unwritable tests and source review, Step 4 |
| `scripts/test_airplay_crypto.c` | Published crypto vectors and identity persistence coverage | normal and sanitizer `make test-airplay`, Step 4 |
| `source/protocol/airplay/security/srp.[ch]` | Apple-compatible bounded SRP server transcript | fixed independent transcript, negative tests and static analysis, Step 5 |
| `source/protocol/airplay/security/pairing.[ch]` | PIN Pair Setup, persisted client trust and Pair Verify state | unit transcript, reconnect smoke, sanitizers and strict build, Step 5 |
| `source/protocol/airplay/security/pairing_store.[ch]` | Versioned and checksummed trusted-client persistence | reload/corruption/update tests and static analysis, Step 5 |
| `scripts/smoke_airplay_pairing.py` | Real TCP authorization close/reconnect smoke | local requested/ephemeral port runs, Step 5 |

### Verified Facts
- Current AirPlay implementation is only a `mdns_discover_airplay()` placeholder returning false — verified by `rg` and source read, 2026-07-19.
- Existing DLNA HTTP server owns one global listener and closes each accepted client after handling one request — verified by `source/protocol/http/http_server.c`, 2026-07-19.
- `renderer_set_uri()` delegates to `player_set_uri()` and is already used by IPTV and DLNA — verified by `rg`, 2026-07-19.
- Installed Switch libmpv exposes `mpv_stream_cb_add_ro()` and installed FFmpeg exposes avformat/avio APIs — verified from `/opt/devkitpro/portlibs/switch/include`, 2026-07-19.
- mbedTLS headers, static libraries and pkg-config metadata are installed; OpenSSL/libplist are not part of the current Switch toolchain — verified from `/opt/devkitpro/portlibs/switch`, 2026-07-19.
- UxPlay reference is newer and contains mirror plus remote-video protocol paths; RPiPlay is older and useful primarily for legacy cross-checks — verified from shallow clone history and file inventories, 2026-07-19.
- Neither reference repository provides a usable protocol test suite for NX-Cast, so fixtures and transcript tests must be created locally — verified by repository inventory, 2026-07-19.
- `source/main.c` does not call AirPlay startup and contains only a commented mDNS placeholder, so adding the Step 1 module cannot advertise a network service — verified by source read, 2026-07-19.
- The makefile already scans `source/protocol/airplay/*.c`, while host tests use small standalone C executables with `-Isource` — verified by makefile and `scripts/test_iptv_channel_list.c`, 2026-07-19.
- AirPlay lifecycle accepts only non-empty names up to 63 bytes and non-zero ports, copies configuration into bounded storage, emits isolated optional callbacks and clears state on stop — verified by `make test-airplay` plus ASan/UBSan, Step 1.
- Adding `airplay.c` compiles into the required libmpv/deko3d Switch build without enabling runtime discovery because `main.c` remains unchanged — verified by strict build and source/status review, Step 1.
- UxPlay and RPiPlay AirPlay handlers use only plist dict, array, string, data, unsigned integer, real and boolean constructors/getters — verified by focused `rg` over both reference `lib/` trees, Step 2.
- The installed Switch portlibs contain no libplist headers, library or pkg-config file — verified by focused `find /opt/devkitpro/portlibs/switch`, Step 2.
- The custom codec rejects invalid UTF-8, malformed tables/references, cycles, over-depth/over-size values and unsupported markers under fixed allocation limits — verified by fixture, truncation, mutation and ASan/UBSan tests, Step 2.
- Encoded dictionaries, extended arrays, UTF-16 surrogate pairs, empty data and raw 64-bit integers decode correctly in NX-Cast; the generated file is also accepted by Python `plistlib` and macOS `plutil` — verified by host assertions and independent readers, Step 2.
- `plist.c` compiles into the required libmpv/deko3d NRO without enabling AirPlay network behavior — verified by strict Switch build and unchanged runtime startup path, Step 2.
- Existing DLNA HTTP handling is one-request/close and cannot preserve AirPlay CSeq/security state — verified by complete `http_server.c` lifecycle read, Step 3.
- The RTSP parser caps headers at 32 KiB and bodies at 1 MiB, rejects ambiguous framing, and preserves pipelined bytes in order — verified by unit, truncation and real TCP smoke tests, Step 3.
- Server stop atomically takes socket-close ownership, unblocks partial reads, joins all fixed client workers and invokes a disconnect cleanup hook — verified by ASan/UBSan saturation/shutdown smoke and source review, Step 3.
- `server.c` and `rtsp.c` compile and link into the required libmpv/deko3d Switch NRO without starting a listener from `main.c` — verified by strict build and startup-path review, Step 3.
- Switch and host mbedTLS are version 2.28.10 with Curve25519, AES, ChaCha20-Poly1305, HKDF and platform entropy enabled; Ed25519 is not implemented — verified from headers, pkg-config and RFC vector execution, Step 4.
- The crypto facade passes SHA-2, HMAC, HKDF, X25519, AES-CTR, ChaCha20-Poly1305 and Ed25519 published vectors, rejects low-order X25519 and invalid AEAD tags, and clears failed outputs — verified by normal and ASan/UBSan tests, Step 4.
- Device identity creation/reload, checksum corruption recovery and unwritable paths preserve the opaque seed boundary and deterministic public identity — verified by host tests and static analysis, Step 4.
- The selected legacy PIN flow uses `/pair-pin-start`, three binary-plist `/pair-setup-pin` exchanges and two raw `/pair-verify` exchanges; later RTSP is authorized by verified session state rather than a separate encrypted record layer — verified from reference behavior inventory and transcript tests, Step 5.
- Pair Setup/Verify passes an independent SRP fixture, persistent reconnect, wrong proof, malformed plist, replay and disconnect tests; real TCP smoke confirms protected SETUP returns generic 470/close and the listener accepts an immediate new connection — verified by normal/sanitizer tests and local smoke, Step 5.
- Strict Switch builds pass with AirPlay trace disabled and enabled, but this workstation lacks `switch-libsodium`; release CI must enforce `NXCAST_REQUIRE_AIRPLAY_ED25519=1` after installing that official package — verified by build output and explicit dependency gate, Step 5.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-07-19 | Step 1 | Added and verified the AirPlay lifecycle/status facade and `make test-airplay`; no network behavior enabled. |
| 2026-07-20 | Step 2 | Added and verified a bounded clean-room binary plist codec, adversarial fixtures and independent interoperability checks. |
| 2026-07-20 | Step 3 | Added and verified the independent persistent RTSP/HTTP server, bounded parser, session state and socket smoke suite. |
| 2026-07-20 | Step 4 | Added and verified atomic device identity plus mbedTLS/libsodium crypto primitives and published-vector tests. |
| 2026-07-20 | Step 5 | Added Apple-compatible PIN Pair Setup/Verify, persistent trusted clients, authorization routing and real TCP reconnect validation. |
