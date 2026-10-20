# CI toolchain images

The source-rebuild validation history is documented in
[`toolchain-candidate.md`](toolchain-candidate.md). Candidate validation,
Build and Release now call the same reusable toolchain workflow.

Build and Release use a precompiled Switch toolchain from GHCR. The reusable
`.github/workflows/toolchain.yml` workflow resolves the image before compilation.
An existing image is reused without recompiling media dependencies.

The image tag hashes the current `devkitpro/devkita64:latest` image digest plus
`Dockerfile`, `.dockerignore`, the FFmpeg/mpv build scripts, patches, libuam cross
configuration, verifier, and toolchain workflow. Dependencies and build
defaults live in `Dockerfile`; application changes do not change the image tag.
When the base image or recipe changes, the workflow builds and publishes the missing version
once. Both normal builds and releases use the same resolution process.

Images are stored at `ghcr.io/ode1l/nx-cast-toolchain`. Actions authenticate using
`GITHUB_TOKEN`, with package write permission for publication. Keep old image
versions so older commits can reuse their toolchains. The Switch toolchain
workflow can also be run manually to prepare the image before a release.

The first build compiles libuam, custom FFmpeg, and deko3d mpv from pinned,
checksum-verified sources using that base's compiler and SDK. The resolved base
digest is passed into Docker so a moving latest tag cannot change mid-build.
Subsequent jobs resolve and download the complete image, then compile NX-Cast.
Application auto-install of binary FFmpeg packages is disabled inside the image.
Native non-container builds retain their existing dependency installation path.

Local `scripts/docker_build_release.sh` uses the same Dockerfile and `--pull`.
Compose builds also pull the latest base. There is no separate candidate
Dockerfile. `NXCAST_BASE_IMAGE` overrides the local script's base for diagnosis.
No scheduled rebuild is required: each invoked workflow resolves latest again.

The integrated path passed [run 39382939902](https://github.com/Ode1l/NX-Cast/actions/runs/39382939902)
at commit `3ada50a`, including the existing AirPlay host tests and release
packaging. Its cached rerun skipped image publication: toolchain preparation
took 23 seconds and tests/application packaging took 93 seconds. These are
observed job durations, not a runtime guarantee.

Keep old GHCR hashes and FFmpeg release assets for rollback. Older commits keep
their original Dockerfile/workflow behavior; reverting this migration restores
the prebuilt dependency path. Do not delete working images during migration.

Revision 4 corrects H.264 long-term reference selection in the nvtegra backend,
while retaining the Matroska muxer and libnx random implementation.

Fork PRs cannot publish missing image versions with their read-only token. A
maintainer must build proposed toolchain changes from a trusted branch first.
For fork PRs to pull existing images, configure the GHCR package as public.
