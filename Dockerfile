FROM devkitpro/devkita64:latest

WORKDIR /workspace

# Keep this explicit for Makefile checks.
ENV DEVKITPRO=/opt/devkitpro

# The official devkitPro Switch image already includes libnx and all available
# Switch portlibs, so CI should not hit pkg.devkitpro.org at build time.
RUN dkp-pacman -Q \
    switch-pkg-config \
    switch-ffmpeg \
    switch-libmpv \
    switch-libjpeg-turbo >/dev/null

# Default command can be overridden by docker run/compose.
CMD ["bash", "-lc", "make clean && make -j$(nproc)"]
