# Plan: AirPlay ALAC And Matroska Bridge

> Status: COMPLETED
> Created: 2026-08-13
> Last Updated: 2026-08-13

## Goal
Accept AirPlay `ct=2` ALAC streams and deliver H.264/AAC/ALAC through a reproducible Switch-compatible media bridge.

## Assumptions
- AirPlay audio-only playback remains out of scope; ALAC is accepted and bridged when a video mirror stream follows.
- The existing wiliwili FFmpeg patch set remains the source of Switch `nvtegra` support and is pinned before reuse.
- The current libmpv ABI remains compatible with a same-version FFmpeg package that only adds a muxer.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [x] `ct=2`, 44.1 kHz, stereo, 352 samples/frame produces a valid ALAC codec configuration.
- [x] The bridge identifies AAC as `AV_CODEC_ID_AAC` and ALAC as `AV_CODEC_ID_ALAC` in a streamable container.
- [x] The Switch FFmpeg build contains both `ff_alac_decoder` and the selected output muxer.
- [x] Host AirPlay tests and a strict Switch release build pass.

### Non-goals
- Standalone AirPlay music UI, metadata, cover art, or multi-room audio.
- Reimplementing the ALAC codec or replacing FFmpeg/libmpv.

### Edge Cases
- Unsupported compression types and non-44.1-kHz formats remain rejected.
- Audio setup before mirror setup is retained and attached when the mirror bridge is created.
- A Switch FFmpeg installation without the required muxer fails the strict build contract with an actionable message.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| ALAC decoding | Reimplement codec vs FFmpeg decoder | Reuse `AV_CODEC_ID_ALAC` from FFmpeg | yes, user approved 2026-08-13 |
| Bridge container | MPEG-TS vs Matroska vs separate audio output | Matroska for H.264/AAC/ALAC compatibility | yes, user approved proposed route 2026-08-13 |
| Switch dependency | Vendor FFmpeg fork vs pinned wiliwili recipe build | Pin wiliwili source and enable only the required muxer | yes, user delegated implementation 2026-08-13 |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Implement and host-test ALAC Matroska bridging |
| Step 2 | `steps/step-2.md` | COMPLETED | Make the Switch FFmpeg muxer build reproducible in local and CI builds |
| Step 3 | `steps/step-3.md` | COMPLETED | Validate the complete host, package, and Switch build contract |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| AirPlay host tests | `make test-airplay` | existing make target | yes |
| Focused bridge tests | `make test-airplay` with generated bridge artifacts inspected by tests | existing test binaries | yes |
| FFmpeg package symbols | `aarch64-none-elf-nm` against staged `libavcodec.a` and `libavformat.a` | installed devkitPro tools | yes |
| Switch release build | `make clean && make RELEASE_JOBS=4 release-build` | existing release target | yes |

## Context & Learnings
### Key Decisions
- Use one FFmpeg/libavformat bridge rather than adding a second Switch audio renderer, preserving the existing player ownership model.
- Pin external source and verify its archive checksum so CI does not silently track upstream changes.

### Gotchas & Warnings
- The currently installed `switch-ffmpeg 7.1-1` contains `ff_alac_decoder` but was configured with `--disable-muxers`.
- Existing host tests use Homebrew FFmpeg and therefore did not expose the missing Switch muxer.
- The worktree contains unrelated in-progress AirPlay diagnostics and cache changes; they must be preserved.
- Local installation of the generated package requires an interactive sudo password; strict linkage can be reproduced without root by overlaying the package onto a copied portlibs tree.

### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `source/protocol/airplay/mirror/audio.h` | Audio format contract | `read` showed only ct=4/8 and a four-byte codec configuration |
| `source/protocol/airplay/mirror/audio.c` | Codec negotiation | `read` showed AAC-only format construction |
| `source/protocol/airplay/media/stream_bridge.c` | Container and codec mapping | `read` showed hard-coded `mpegts` and `AV_CODEC_ID_AAC` |
| `scripts/test_airplay_audio.c` | Focused audio/bridge coverage | `read` showed ct=2 is explicitly rejected today |
| `scripts/test_airplay_stream_bridge.c` | Matroska video bridge coverage | Generated output reopens through libavformat and reports H.264 |
| `scripts/test_airplay_mirror_runtime.c` | Audio-before-video integration coverage | Runtime test now opens ct=2 before the mirror stream |
| `scripts/smoke_airplay.py` | Remote HLS regression coverage | Generated AAC Matroska is remuxed to an independent MPEG-TS HLS fixture |
| `Dockerfile` | CI Switch dependency image | `read` showed the prebuilt wiliwili FFmpeg package installation |
| `../others/wiliwili-dev/scripts/switch/ffmpeg/PKGBUILD` | Upstream Switch FFmpeg recipe reference | `read` showed `--disable-muxers` and pinned FFmpeg commit |
| `scripts/build_switch_ffmpeg_airplay.sh` | Reproducible custom FFmpeg package | Local build produced version 7.1-2 and verified ALAC/H.264/Matroska symbols |
| `scripts/package_release.sh` | Public SD package contract | Strict smoke test proved one directory-local NRO and intact IPTV presets |

### Verified Facts
- The latest iPhone test negotiates `ct=2`, `spf=352`, `sr=44100` and fails at `stage=format` — verified by `rg` on `logs/run_nxlink-20260813-203040.log`, 2026-08-13.
- UxPlay maps the same parameters to ALAC with a 36-byte magic cookie — verified by upstream `renderers/audio_renderer.c`, 2026-08-13.
- The installed Switch archive exports `ff_alac_decoder` — verified by `aarch64-none-elf-nm`, 2026-08-13.
- The installed Switch FFmpeg configuration includes `--disable-muxers` and exports no Matroska/MPEG-TS muxer — verified by `strings`, `ar`, and `aarch64-none-elf-nm`, 2026-08-13.
- The pinned wiliwili commit `88e5876bea9502d06f46a8656e3530684d3aaf7d` contains the matching recipe and patches — verified by archive inspection and recipe SHA-256, 2026-08-13.
- FFmpeg's H.264 parser reports the fixture as 64x64 and Matroska regenerates 38-byte H.264 CodecPrivate — verified by focused host test and `ffprobe`, 2026-08-13.
- Generated AAC and ALAC Matroska files report 44.1-kHz stereo audio; ALAC demuxes with 36-byte extradata — verified by `ffprobe`, 2026-08-13.
- Complete `make test-airplay` passes after updating the remote HLS fixture contract — verified 2026-08-13.
- `switch-ffmpeg-7.1-2-any.pkg.tar.zst` builds from pinned verified inputs and exports `ff_alac_decoder`, `ff_h264_parser`, and `ff_matroska_muxer` — verified by the package build script, 2026-08-13.
- The old installed FFmpeg is rejected by `NXCAST_REQUIRE_AIRPLAY_MUXER=1`, proving release builds cannot silently use the muxer-less package — verified by `make -n`, 2026-08-13.
- The release package smoke test contains exactly `switch/NX-Cast/NX-Cast.nro` and preserves `iptv/sources.txt` — verified by `scripts/package_release.sh`, 2026-08-13.
- Strict rootless Switch linkage completed and produced a 25,572,026-byte NRO with the full release attestation — verified by `make release-build`, 2026-08-13.
- The final strict SD zip is 19,810,075 bytes, contains one NRO, and includes `switch/NX-Cast/iptv/sources.txt` — verified by `zipinfo`, 2026-08-13.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-08-13 | Step 1 | Added ct=2 ALAC negotiation, Matroska H.264/AAC/ALAC bridging, parser-derived video metadata, time-base rescaling, and host regression coverage. |
| 2026-08-13 | Step 2 | Added a pinned custom Switch FFmpeg package, strict release symbol contract, Docker/CI integration, and hbmenu-safe SD zip validation. |
| 2026-08-13 | Step 3 | Passed the complete host suite, rootless strict Switch link, release attestation, and final SD package inspection. |
