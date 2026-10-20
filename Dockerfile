# Shared source toolchain for Build, Release, and candidate validation.
ARG BASE_IMAGE=devkitpro/devkita64:latest
FROM ${BASE_IMAGE}
ARG BASE_IMAGE
SHELL ["/bin/bash", "-eo", "pipefail", "-c"]
ENV DEVKITPRO=/opt/devkitpro
ENV PORTLIBS_PREFIX=/opt/devkitpro/portlibs/switch
ENV PORTLIBS=/opt/devkitpro/portlibs/switch
ENV PATH=/opt/devkitpro/devkitA64/bin:/opt/devkitpro/tools/bin:${PATH}
ENV PKG_CONFIG_PATH=/opt/devkitpro/portlibs/switch/lib/pkgconfig
# Never let an application build replace source-built FFmpeg with a downloaded binary.
ENV NXCAST_AUTO_INSTALL_FFMPEG=0
ENV PYTHON=/usr/bin/python3

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential curl ca-certificates git patch perl sudo fakeroot \
      python3 python3-setuptools python3-mako meson ninja-build bison flex \
      pkg-config zstd libarchive-tools ffmpeg libavcodec-dev libavformat-dev \
      libavutil-dev libmbedtls-dev libsodium-dev && rm -rf /var/lib/apt/lists/*
RUN useradd --create-home candidate && mkdir -p /candidate /opt/nxcast-toolchain && \
    chown candidate:candidate /candidate && \
    printf '%s\n' "$BASE_IMAGE" > /opt/nxcast-toolchain/base-image.txt && \
    if dkp-pacman -Q switch-libmpv >/dev/null 2>&1; then dkp-pacman -Rdd --noconfirm switch-libmpv; fi
COPY --chmod=0755 scripts/build_switch_ffmpeg_airplay.sh scripts/verify_switch_ffmpeg_airplay.sh \
    scripts/build_switch_mpv_candidate.sh /candidate/scripts/
COPY scripts/ffmpeg_switch_random.patch scripts/ffmpeg_nvtegra_long_ref.patch \
    scripts/mpv_switch_cross.patch scripts/libuam_switch_cross.ini /candidate/scripts/

# Rebuild libuam as well: do not mix a legacy prebuilt C++ shader compiler into mpv.
RUN curl -fL --retry 3 \
      https://codeload.github.com/averne/libuam/tar.gz/f8c9eef01ffe06334d530393d636d69e2b52744b \
      -o /candidate/uam.tar.gz && \
    echo '71d94f33841b2ed5d5577a42cfbe28a988214f5485ba0bbe0e597f1d8eff8cc8  /candidate/uam.tar.gz' | sha256sum -c - && \
    tar -xf /candidate/uam.tar.gz -C /candidate && \
    source /opt/devkitpro/switchvars.sh && \
    meson setup /candidate/uam-build /candidate/libuam-f8c9eef01ffe06334d530393d636d69e2b52744b \
      --cross-file /candidate/scripts/libuam_switch_cross.ini --prefix="$PORTLIBS_PREFIX" && \
    meson compile -C /candidate/uam-build -j4 && meson install -C /candidate/uam-build && \
    mkdir -p "$PORTLIBS_PREFIX/share/licenses/libuam-candidate" && \
    cp /candidate/libuam-f8c9eef01ffe06334d530393d636d69e2b52744b/LICENSE \
      "$PORTLIBS_PREFIX/share/licenses/libuam-candidate/" && \
    printf '%s\n' 'source=https://github.com/averne/libuam' \
      'commit=f8c9eef01ffe06334d530393d636d69e2b52744b' \
      'archive_sha256=71d94f33841b2ed5d5577a42cfbe28a988214f5485ba0bbe0e597f1d8eff8cc8' \
      > /opt/nxcast-toolchain/libuam-candidate-build.txt

# makepkg must run without root. Installation is confined to this candidate image.
USER candidate
RUN bash /candidate/scripts/build_switch_ffmpeg_airplay.sh /candidate/ffmpeg
USER root
RUN dkp-pacman -U --noconfirm /candidate/ffmpeg/switch-ffmpeg-7.1-4-any.pkg.tar.zst
USER candidate
RUN bash /candidate/scripts/build_switch_mpv_candidate.sh /candidate/mpv
USER root
RUN archives=(/candidate/mpv/work.*/mpv-candidate.tar.gz) && \
    test "${#archives[@]}" -eq 1 && tar -xf "${archives[0]}" -C / && \
    bash /candidate/scripts/verify_switch_ffmpeg_airplay.sh "$PORTLIBS_PREFIX" && \
    dkp-pacman -Q > /opt/nxcast-toolchain/packages.txt && \
    cp /mpv-candidate-build.txt /opt/nxcast-toolchain/ && \
    cp /candidate/scripts/verify_switch_ffmpeg_airplay.sh /usr/local/bin/ && \
    rm -rf /candidate /mpv-candidate-build.txt
WORKDIR /workspace
CMD ["bash", "-lc", "make release-build"]
