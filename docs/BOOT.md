# Boot images (D-38, D-40)

A USB stick or SD card that boots a PC or a Raspberry Pi straight into Continuum 93: no installed OS, no
desktop, nothing on the internal disk is touched. Three images:

| Image | Runs on | Firmware |
|-------|---------|----------|
| `dist/continuum93-boot-x64.zip` (7.5 MB) | 64-bit PCs and laptops | UEFI (`BOOTX64.EFI`) and legacy BIOS |
| `dist/continuum93-boot-x86.zip` (8 MB) | 32-bit PCs with SSE2 (Pentium 4, Pentium M, Atom, Athlon 64 on) | BIOS and 32-bit UEFI (`BOOTIA32.EFI`) |
| `dist/continuum93-boot-rpi.zip` (14 MB) | Raspberry Pi 3, 3B+, Zero 2 W, 4, 400, 5, 500 (and their Compute Modules) | the Pi's own firmware (`kernel8.img`) |

The whole system is the kernel file: ~7 MB (x64) / 7.5 MB (x86) / 10.5 MB (Pi), initramfs and emulator included. In QEMU/KVM
the kernel reaches init in 0.33 s and has the stick mounted at ~0.9 s; the emulator's boot menu is up ~0.5 s later.

Build: `build boot` (all three) or `build boot x64|x86|rpi` (Windows), `./build.sh boot [x64|x86|rpi]`
elsewhere. Docker only; the first run downloads the kernels and takes a few minutes per arch. The Pi is built in
two containers: its emulator in an emulated arm64 one (slow the first time), its kernel cross-built by clang on x64.

## Using a stick

1. Unzip, write the `.img` to a USB stick or SD card (Rufus in DD mode, balenaEtcher, Raspberry Pi Imager
   ("Use custom"), or `dd`).
2. PC: boot from it (boot menu key: F12, F11, F8 or Esc, depending on the maker). **Secure Boot must
   be off**: the kernel is not signed. Pi: put the card in and power on (a Pi 3B+, 4 or 5 also boots from
   a USB stick when no card is in).
3. The first boot gives the partition the whole stick or card (a few seconds, "Do not unplug" on screen):
   the image is 64 MB, the rest of the device is yours for files. It happens once; nothing else is touched.
4. Quitting Continuum (the guest's shutdown) powers the PC off; a Pi 3/4 halts (unplug it), a Pi 5 powers off.
5. Picture squashed or stretched (the monitor stretches a mode of another shape)? Press **Shift+F11** until it
   looks right: auto, 4:3, 5:4, 16:10, 16:9, 21:9. It is saved in `Data/init.cfg` (`screenAspect`, D-39).

The stick runs without the CRT filter (`crtFilter=false` in its `Data/init.cfg`; set it to `true` for the look).

The stick is a plain FAT32 volume named `C93` that any OS can open:

```
EFI/BOOT/BOOTX64.EFI   PC: the kernel, with everything else inside it (UEFI runs it directly)
syslinux/              PC: BIOS boot; syslinux.cfg picks the BIOS screen mode (vga=)
kernel8.img            Pi: the kernel, everything inside; config.txt and cmdline.txt beside it
bootcode.bin, start*.elf, fixup*.dat, *.dtb, overlays/   Pi: its boot firmware and device trees
Data/                  the emulator's Data folder: OS, programs, init.cfg, the guest's files
continuum93/logs/      written at each boot: continuum.log (the emulator), kernel.log (dmesg)
```

## How it works

- **Kernel**: Linux 6.18.60 LTS, every driver built in, no modules. `boot/kernel.config` is common.
  - PCs: `tinyconfig` + `kernel-pc.config` (+ `kernel-x86.config`), no firmware files. Its own EFI loader
    (EFI stub); the initramfs is inside the kernel file, compressed once with it (zstd).
  - Pi: Raspberry Pi's kernel tree at the same version (tag `stable_20261103`: their SoC drivers and device
    trees), their `bcm2711_defconfig` with every module dropped and its server/debug features off,
    + `kernel-rpi.config`. One arm64 `kernel8.img` (gzip, the firmware unpacks it) for every Pi from the 3 on.
    The firmware files (`bootcode.bin`, `start*.elf`, `fixup*.dat`) are Raspberry Pi's, release 1.20260915.
- **Initramfs**: `boot/init` (busybox sh as PID 1), the emulator, glibc, libdrm, libasound and ALSA's
  config. Init mounts the FAT partition that has `Data/os` (USB storage needs ~0.5 s to appear; it
  retries for 10 s), grows it on first boot (below), waits up to 5 s for the display (`/dev/dri/card0`: vc4 can finish after the card is up),
  links `/c93/Data` to it, runs `/c93/continuum`, then unmounts and powers off.
- **Display, PCs**: no GPU drivers. The firmware sets a screen mode (UEFI: the EFI stub switches to the largest GOP
  mode, `video=efifb:auto`, normally the panel's native one; BIOS: VESA through `vga=`), the kernel's `simpledrm` exposes it, and SDL draws into it with its software renderer. The emulator
  composites on the CPU anyway (the GPU path only saves time on big windows).
- **Display, Pi**: KMS through `vc4` (`dtoverlay=vc4-kms-v3d`, as Raspberry Pi OS): HDMI at the monitor's own
  mode, read from its EDID. The emulator's 480x270 picture is scaled up by the display hardware (an overlay
  plane, nearest neighbour), so the CPU only draws 480x270; no 3D driver.
- **Frame rate**: continuum.log gets `Video: N fps over 30 s, slowest frame M ms` every 30 s. The pointer is drawn
  by the guest OS inside the frame, so it lags by whole frames: below 60 fps the lag grows.
- **SDL patch** (`boot/sdl-bare.patch`, applied only for these images):
  - kmsdrm without libgbm: the window framebuffer in two DRM dumb buffers, page-flipped at vblank (vsync, no
    tearing; vc4 scans a buffer out as it is drawn, so one buffer flickered black on the Pi), instead of requiring
    Mesa (GBM + EGL). A driver without page flips gets one buffer and DIRTYFB; the log says which;
  - a window smaller than the mode (the images ask for 480x270, `C93_WINDOW_SCALE=1`) goes on an overlay plane
    that the display scales up, nearest neighbour, over a black full-mode buffer: the CPU draws 480x270 pixels a
    frame instead of the whole screen (D-42). Mouse speed is scaled to match. Drivers without a scaling overlay
    (simpledrm on PCs) give the window the mode's size instead, as before;
  - evdev without libudev: scans `/dev/input` and rescans when it changes (hotplug), guessing each
    device's class from its capabilities, as SDL's joystick code already does.
- **Input**: USB (and on PCs PS/2) keyboards and mice, USB gamepads (generic HID, Xbox, PlayStation, Switch).
- **Sound**: PCs: Intel HDA (most PCs since 2005), AC'97; Pi: HDMI and the 3.5 mm jack (Pi 3/4); USB audio
  everywhere. Through ALSA.

## First-boot resize

The image is 64 MB so it downloads and writes fast. FAT32 cannot grow in place (its allocation table would
have to move the data), so init does what installers do: when more than 64 MB lie unused after partition 1
(MBR, type 0x0C: only our own layout), it copies the partition's files to RAM (~20 MB), writes the new size into
the MBR, formats the partition again (busybox `mkdosfs`, label C93, the hidden-sectors field fixed for BIOS
boot), copies the files back and, on PCs, reinstalls syslinux with `extlinux`. ~0.7 s on a PC in QEMU. Each
step goes to the kernel log (`c93 init: ...` in kernel.log). Losing power mid-way leaves a stick to rewrite;
there is nothing of the user's on it yet. MBR caps the partition at 2 TiB.

## Testing

`boot/test/` boots an image in QEMU (KVM through Docker Desktop) and screenshots it; see the header of
`boot/test/run.sh`. From Git Bash on Windows (`MSYS_NO_PATHCONV=1` keeps the paths as written):

```
docker build -t c93-qemu boot/test
docker run --rm --device /dev/kvm -v "$PWD/build/boot-x64:/img" -v "$PWD/boot/test:/q" -v "$PWD/build/qemu:/out" c93-qemu sh /q/run.sh /img/continuum93-boot-x64.img uefi 15
```

`DISK=1G` (env) enlarges the test copy first, a bigger stick, and keeps it as `/out/<fw>/disk.img` for a second boot.

Verified so far (2026-11-04, QEMU 7.2): x64 on UEFI and BIOS, x86 on BIOS and 32-bit UEFI: Halcyon comes
up, USB keyboard (F1 opens service mode) and mouse work, HDA sound plays, Halcyon's Shut down powers the
machine off with the log saved on the stick. Not yet on real PCs.

Pi: `run.sh <img> rpi3 <seconds>` boots the card as a Pi 3B (QEMU `raspi3b`, no KVM; QEMU stands in for the
firmware, so the `vc4` overlay is not applied and there is no display). Verified: the kernel boots, the SD card
mounts, the arm64 emulator starts and stops at "kmsdrm not available" (expected), the machine powers down;
USB (`dwc_otg`) finds QEMU's keyboard. The display, input and sound on a Pi are untested until real hardware.

## Known limits

- UEFI runs at the panel's native size: the picture keeps its shape (letterboxed). With `crtFilter=true` the
  filter takes 7.9 ms at 1080p (`conformance --crt`, this dev PC); above 2.2 Mpx it runs at half size.
- BIOS boots 1024x768 (`vga=0x318`, 4:3): a wide monitor may stretch it (Shift+F11 fixes the shape).
  Edit `syslinux/syslinux.cfg` on the stick for another VESA mode.
- shortcut: no GPU drivers, so no vsync from the hardware; SDL paces frames to the mode's refresh rate.
- No I2C touchpads (most laptops since ~2016 use them for the touchpad, not the keyboard): use a USB mouse.
- Intel laptops whose sound needs SOF DSP firmware fall back to plain HDA (`snd_intel_dspcfg.dsp_driver=1`);
  some of those have no sound.
- No Wi-Fi, no network: not needed by the emulator.
- The power button does nothing (no ACPI button handling): shut down from the guest, or hold it to force off.
  Logs are only complete after a guest shutdown.
- The sound card is ALSA's default (card 0). On PCs where card 0 is HDMI-only audio, sound goes to the monitor.
  On a Pi 4/5 init picks the HDMI port with a monitor (each port is a card); a DVI monitor or one without speakers
  gives no HDMI sound: use the 3.5 mm jack (Pi 3/4) or USB audio.
- Pi: the boot firmware is Raspberry Pi's closed code (redistributable, `LICENCE.broadcom` on the card). The
  Pi 5 has no 3.5 mm jack: HDMI or USB audio. How fast the emulator runs on a Pi 3 is not measured yet.
