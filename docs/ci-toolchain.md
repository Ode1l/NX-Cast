# CI toolchain images

Build and Release use a precompiled Switch toolchain from GHCR. The reusable
`.github/workflows/toolchain.yml` workflow resolves the image before compilation.
An existing image is reused without running Docker Buildx or compiling FFmpeg.

The image tag is a content hash of `Dockerfile`, `.dockerignore`, the FFmpeg
fetch/build/verification scripts and patch, and the toolchain workflow. Dependencies and build
defaults live in `Dockerfile`; application changes do not change the image tag.
When those inputs change, the workflow builds and publishes the missing version
once. Both normal builds and releases use the same resolution process.

Images are stored at `ghcr.io/ode1l/nx-cast-toolchain`. Actions authenticate using
`GITHUB_TOKEN`, with package write permission for publication. Keep old image
versions so older commits can reuse their toolchains. The Switch toolchain
workflow can also be run manually to prepare the image before a release.

The first build of a new toolchain downloads the fixed
`toolchain-ffmpeg-7.1-3` GitHub Release asset and verifies its SHA-256 rather
than recompiling FFmpeg. Subsequent jobs only resolve and download the image,
then compile NX-Cast. The FFmpeg source-build target remains available for
maintainers, not normal CI jobs.

Fork PRs cannot publish missing image versions with their read-only token. A
maintainer must build proposed toolchain changes from a trusted branch first.
For fork PRs to pull existing images, configure the GHCR package as public.
