FROM devkitpro/devkita64:latest

WORKDIR /workspace

# Keep this explicit for Makefile checks.
ENV DEVKITPRO=/opt/devkitpro

# Default command can be overridden by docker run/compose.
CMD ["bash", "-lc", "make clean && make -j$(nproc)"]
