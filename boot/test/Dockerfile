# QEMU with UEFI (64- and 32-bit OVMF) and BIOS for boot/test/run.sh; mtools reads the logs off the stick.
FROM debian:bookworm
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      qemu-system-x86 ovmf ovmf-ia32 socat mtools && rm -rf /var/lib/apt/lists/*
