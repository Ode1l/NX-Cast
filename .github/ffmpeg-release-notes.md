# Build Dependency: Switch FFmpeg 7.1-4

This package is for developers compiling NX-Cast. It is **not** a Switch application or an SD-card runtime file. Ordinary users should download the [latest NX-Cast installation ZIP](https://github.com/Ode1l/NX-Cast/releases/latest) instead.

## Package

- Asset: `switch-ffmpeg-7.1-4-any.pkg.tar.zst`
- SHA-256: `bc6068b8dfee02356aa571fcc126143bb718d9b900d61f4813190039c82a81d6`
- Includes the Matroska muxer, libnx-backed random bytes and the nvtegra H.264 long-term reference fix.
- Maintains Switch hardware decoding; do not replace it with an upstream desktop FFmpeg build.

## Install And Build

From NX-Cast source with devkitPro configured:

```bash
make install-airplay-ffmpeg
make verify-airplay-ffmpeg
make release-build
```

The installer and CI image use the same checksum-pinned package. FFmpeg is not recompiled by normal application CI jobs; GHCR caches the toolchain image by its inputs.

## Source And Reproduction

The [source-build recipe](https://github.com/Ode1l/NX-Cast/blob/toolchain-ffmpeg-7.1-4/scripts/build_switch_ffmpeg_airplay.sh) records the exact WiliWili upstream revision, FFmpeg source archive checksums and patches. The [Switch random-seed patch](https://github.com/Ode1l/NX-Cast/blob/toolchain-ffmpeg-7.1-4/scripts/ffmpeg_switch_random.patch) and [nvtegra reference patch](https://github.com/Ode1l/NX-Cast/blob/toolchain-ffmpeg-7.1-4/scripts/ffmpeg_nvtegra_long_ref.patch) are included in the tagged repository. Maintainers can reproduce the package with `make build-airplay-ffmpeg`.
