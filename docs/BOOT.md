# Boot images (D-38)

A USB stick that boots a PC straight into Continuum 93: no installed OS, no desktop, nothing on the
internal disk is touched. Two images:

| Image | Runs on | Firmware |
|-------|---------|----------|
| `dist/continuum93-boot-x64.zip` (7.5 MB) | 64-bit PCs and laptops | UEFI (`BOOTX64.EFI`) and legacy BIOS |
| `dist/continuum93-boot-x86.zip` (8 MB) | 32-bit PCs with SSE2 (Pentium 4, Pentium M, Atom, Athlon 64 on) | BIOS and 32-bit UEFI (`BOOTIA32.EFI`) |

The whole system is the kernel file: ~7 MB (x64) / 7.5 MB (x86), initramfs and emulator included. In QEMU/KVM
the kernel reaches init in 0.33 s and has the stick mounted at ~0.9 s; the emulator's boot menu is up ~0.5 s later.

Build: `build boot` (both) or `build boot x64` / `build boot x86` (Windows), `./build.sh boot [x64|x86]`
elsewhere. Docker only; the first run downloads the kernel and takes a few minutes per arch.

## Using a stick

1. Unzip, write the `.img` to a USB stick (Rufus in DD mode, balenaEtcher, Raspberry Pi Imager, or `dd`).
2. Boot the PC from it (boot menu key: F12, F11, F8 or Esc, depending on the maker). **Secure Boot must
   be off**: the kernel is not signed.
3. Quitting Continuum (the guest's shutdown) powers the PC off.
4. Picture squashed or stretched (the monitor stretches a mode of another shape)? Press **Shift+F11** until it
   looks right: auto, 4:3, 5:4, 16:10, 16:9, 21:9. It is saved in `Data/init.cfg` (`screenAspect`, D-39).

The stick runs without the CRT filter (`crtFilter=false` in its `Data/init.cfg`; set it to `true` for the look).

The stick is a plain FAT32 volume named `C93` that any OS can open:

```
EFI/BOOT/BOOTX64.EFI   the kernel, with everything else inside it (UEFI runs it directly)
syslinux/              BIOS boot; syslinux.cfg picks the BIOS screen mode (vga=)
Data/                  the emulator's Data folder: OS, programs, init.cfg, the guest's files
continuum93/logs/      written at each boot: continuum.log (the emulator), kernel.log (dmesg)
```

## How it works

- **Kernel**: Linux 6.18 LTS, `tinyconfig` plus `boot/kernel.config` (+ `kernel-x86.config`), every driver
  built in, no modules, no firmware files. Its own EFI loader (EFI stub); the initramfs is inside the
  kernel file, compressed once with it (zstd).
- **Initramfs**: `boot/init` (busybox sh as PID 1), the emulator, glibc, libdrm, libasound and ALSA's
  config. Init mounts the FAT partition that has `Data/os` (USB storage needs ~0.5 s to appear; it
  retries for 10 s), links `/c93/Data` to it, runs `/c93/continuum`, then unmounts and powers off.
- **Display**: no GPU drivers. The firmware sets a screen mode (UEFI: the EFI stub switches to the largest GOP
  mode, `video=efifb:auto`, normally the panel's native one; BIOS: VESA through `vga=`), the kernel's `simpledrm` exposes it, and SDL draws into it with its software renderer. The emulator
  composites on the CPU anyway (the GPU path only saves time on big windows).
- **SDL patch** (`boot/sdl-bare.patch`, applied only for these images):
  - kmsdrm without libgbm: a window framebuffer in a DRM dumb buffer, flushed with DIRTYFB each frame,
    instead of requiring Mesa (GBM + EGL);
  - evdev without libudev: scans `/dev/input` and rescans when it changes (hotplug), guessing each
    device's class from its capabilities, as SDL's joystick code already does.
- **Input**: USB and PS/2 keyboards and mice, USB gamepads (generic HID, Xbox, PlayStation, Switch).
- **Sound**: Intel HDA (most PCs since 2005), AC'97, USB audio, through ALSA.

## Testing

`boot/test/` boots an image in QEMU (KVM through Docker Desktop) and screenshots it; see the header of
`boot/test/run.sh`. From Git Bash on Windows (`MSYS_NO_PATHCONV=1` keeps the paths as written):

```
docker build -t c93-qemu boot/test
docker run --rm --device /dev/kvm -v "$PWD/build/boot-x64:/img" -v "$PWD/boot/test:/q" -v "$PWD/build/qemu:/out" c93-qemu sh /q/run.sh /img/continuum93-boot-x64.img uefi 15
```

Verified so far (2026-11-04, QEMU 7.2): x64 on UEFI and BIOS, x86 on BIOS and 32-bit UEFI: Halcyon comes
up, USB keyboard (F1 opens service mode) and mouse work, HDA sound plays, Halcyon's Shut down powers the
machine off with the log saved on the stick. Not yet on real PCs.

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
