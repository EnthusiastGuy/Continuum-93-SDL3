# Boot image builder, one per arch (linux/amd64 -> x64, linux/386 -> x86): the kernel, the emulator
# built without udev/X11/Wayland, busybox, and the tools that lay out the FAT32 stick. See boot/build.sh.
FROM debian:bookworm
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      clang-16 lld-16 cmake ninja-build git ca-certificates pkg-config \
      libdrm-dev libgbm-dev libegl-dev libgles-dev libasound2-dev \
      gcc make bc flex bison libelf-dev cpio xz-utils zstd curl \
      alsa-utils busybox-static syslinux syslinux-common mtools fdisk zip \
    && rm -rf /var/lib/apt/lists/*
# lld: clang's -flto needs an LTO-capable linker. The kernel build unsets these (it uses gcc).
ENV CC=clang-16 CXX=clang++-16 LDFLAGS=-fuse-ld=lld HOME=/tmp MTOOLS_SKIP_CHECK=1
