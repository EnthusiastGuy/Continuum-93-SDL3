# Linux build image (x64 and arm64). Debian bookworm: glibc 2.36, so the binary also runs on
# Raspberry Pi OS bookworm, Ubuntu 22.04+ and anything newer. clang 16 for musttail (GCC 12 has none, D-29).
# SDL dlopens X11/Wayland/ALSA/Pulse/PipeWire at run time; their headers are only needed to build.
FROM debian:bookworm
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      clang-16 lld-16 cmake ninja-build git ca-certificates pkg-config \
      libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev \
      libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev \
      libegl1-mesa-dev libgles2-mesa-dev libgl1-mesa-dev libdrm-dev libgbm-dev \
      libasound2-dev libpulse-dev libpipewire-0.3-dev libdbus-1-dev libudev-dev \
    && rm -rf /var/lib/apt/lists/*
# lld: clang's -flto needs an LTO-capable linker.
ENV CC=clang-16 CXX=clang++-16 LDFLAGS=-fuse-ld=lld HOME=/tmp
