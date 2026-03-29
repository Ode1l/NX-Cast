FROM devkitpro/devkita64:latest

WORKDIR /workspace

# Keep this explicit for Makefile checks.
ENV DEVKITPRO=/opt/devkitpro

RUN dkp-pacman -Sy --noconfirm --needed \
    switch-pkg-config \
    switch-ffmpeg \
    switch-libmpv \
    switch-libjpeg-turbo && \
    dkp-pacman -Scc --noconfirm

# Default command can be overridden by docker run/compose.
CMD ["bash", "-lc", "make clean && make -j$(nproc)"]
