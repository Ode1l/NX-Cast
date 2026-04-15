FROM devkitpro/devkita64:20260219

WORKDIR /workspace

ENV DEVKITPRO=/opt/devkitpro
ARG NXCAST_MPV_VARIANT=deko3d
ARG WILIWILI_RELEASE=v0.1.0
ARG LIBUAM_PKG=libuam-f8c9eef01ffe06334d530393d636d69e2b52744b-1-any.pkg.tar.zst
ARG SWITCH_FFMPEG_PKG=switch-ffmpeg-7.1-1-any.pkg.tar.zst
ARG SWITCH_LIBMPV_PKG=switch-libmpv_deko3d-0.36.0-2-any.pkg.tar.zst

RUN if [ "${NXCAST_MPV_VARIANT}" = "deko3d" ]; then \
      if dkp-pacman -Q switch-libmpv >/dev/null 2>&1; then dkp-pacman -Rdd --noconfirm switch-libmpv >/dev/null; fi; \
      if dkp-pacman -Q switch-ffmpeg >/dev/null 2>&1; then dkp-pacman -Rdd --noconfirm switch-ffmpeg >/dev/null; fi; \
      base_url="https://github.com/xfangfang/wiliwili/releases/download/${WILIWILI_RELEASE}"; \
      for pkg in "${LIBUAM_PKG}" "${SWITCH_FFMPEG_PKG}" "${SWITCH_LIBMPV_PKG}"; do \
        curl -fsSL -o "/tmp/${pkg}" "${base_url}/${pkg}"; \
        dkp-pacman -U --noconfirm "/tmp/${pkg}"; \
      done; \
      rm -f /tmp/*.pkg.tar.*; \
    fi && \
    dkp-pacman -Q switch-pkg-config switch-libjpeg-turbo switch-zlib switch-bzip2 switch-libass switch-libfribidi switch-freetype switch-harfbuzz switch-mbedtls switch-liblua51 deko3d >/dev/null && \
    test -f /opt/devkitpro/portlibs/switch/include/mpv/client.h && \
    test -f /opt/devkitpro/portlibs/switch/lib/libmpv.a

CMD ["bash", "-lc", "make clean && make -j$(nproc)"]
