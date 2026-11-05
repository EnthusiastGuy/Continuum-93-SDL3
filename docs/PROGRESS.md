# Progress

Read this first in every session. Update it at the end of every work block.

**Current phase:** 10 — boot images (D-38, docs/BOOT.md); phase 9 packaging done apart from its open checks below
**Next action:** the user tries the images on real hardware and sends back `continuum93/logs/` (continuum.log,
kernel.log) from the stick/card. PCs: `dist/continuum93-boot-x64.zip` (and x86), Secure Boot off; stretched picture:
Shift+F11 (D-39). Pi: `dist/continuum93-boot-rpi.zip` on an SD card, Pi 3/4/5; its display (vc4), input and sound
are untested (QEMU has no vc4), so check those first (flicker fix, first-boot resize and the scaled picture D-42: retest on the Zero 2 W, Pi 4, Pi 5; send the fps lines). Fix what real hardware shows (likely: GOP mode on big screens,
the ALSA card choice, laptop touchpads; on the Pi, vc4 bring-up). Still open from phase 9: asm/BASIC suites on
Windows after D-37 (expect 129/129, 341/341), the linux-arm64 zip on a real Pi. Branch `feature/bootimage`
(PC images and D-39 committed as 642c678; the Pi image is not committed yet).

### 2026-11-05 — Pi mouse lag: hardware-scaled picture, frame-rate log (D-42)
- User, Pi 5: card resize works. Plane found but unused (DirtySurfaces resized the window; fixed), 22.5 fps before
  the fix; no sound (HDMI 0's card, monitor on the other port; init now picks the connected port's card). Rebuilt;
  PCs rechecked in QEMU (x64 BIOS: same path, sound opens, guest shutdown). Pi retest pending.
- SDL patch: overlay plane scaling for windows smaller than the mode (nearest neighbour), probed at window creation;
  emulator: `C93_WINDOW_SCALE` (boot init: 1) and a 30 s frame-rate log line. PCs (no overlay on simpledrm) keep
  the full-mode path: QEMU x64 BIOS 60.0 fps, slowest 16.8 ms, mouse clicks and guest shutdown work; desktop
  Windows unchanged (3x window). The Pi path is untested until real hardware: the user reports the log lines
  "KMSDRM: the display scales ..." and "Video: N fps" on the Zero 2 W / Pi 4 / Pi 5, and whether the lag is gone.

### 2026-11-05 — first-boot resize (D-41)
- init grows partition 1 to the whole device on first boot (RAM copy, MBR size, mkdosfs, copy back, extlinux on
  PCs). Verified in QEMU: x64 BIOS 512 MB (direct kernel) → 511 MB, then a real BIOS→syslinux boot of the grown
  disk and a UEFI boot of it reach Halcyon; x64 UEFI 1 GB first boot: grow 0.65 s, Halcyon up; Pi (raspi3b)
  512 MB: grown, files intact, hidden sectors 2048. init steps are in kernel.log ("c93 init: ..."), timestamped.

### 2026-11-04 — Raspberry Pi boot image (D-40)
- User, Pi Zero 2 W: boots, Halcyon runs (slow); a black flicker at the frame rate; one boot of four had
  "kmsdrm not available". Fixed: SDL patch double-buffers with page flips (D-40), init waits up to 5 s for
  /dev/dri/card0. Checked in QEMU on PCs (x64 BIOS + UEFI, x86 UEFI32): picture, mouse, guest shutdown; the log says
  "KMSDRM: 1024x768 dumb buffers, page-flipped at vblank". The Pi side needs the user's retest.
- `build boot rpi` → `dist/continuum93-boot-rpi.zip` (14 MB; 64 MB FAT32 card image): Pi firmware 1.20260915,
  `kernel8.img` (10.4 MB gzip, Raspberry Pi kernel `stable_20261103` = 6.18.60, initramfs inside), the bcm2710/
  2711/2712 device trees, the vc4-kms-v3d overlays, config.txt, cmdline.txt. The userland (steps 1-3) builds in
  linux/arm64 (emulated), the kernel and card (4-5) on amd64 with clang; `boot/build.sh <arch> [userland|image]`.
- Kernel fragments split: `kernel.config` (common) + `kernel-pc.config` / `kernel-rpi.config`. The x64 kernel
  .config is byte-identical to before the split (diffed). The initramfs files are now staged in `/cache/<arch>/root`;
  the duplicate libdrm/libasound list entries are gone. init skips loop/ram block devices.
- Verified in QEMU 7.2 `raspi3b` (TCG): kernel boots, SD card mounts, emulator starts and stops at "kmsdrm not
  available" (QEMU applies no overlay: expected), powers down at 3.7 s; dwc_otg finds QEMU's USB keyboard.
  `boot/test/run.sh ... rpi3` does this. Not verified: vc4 display, HDMI/jack sound, input, Pi 4/5 at all.

### 2026-11-04 — boot images for PCs (D-38)
- `build boot [x64|x86]` → `dist/continuum93-boot-<x64|x86>.zip` (7.5 / 8 MB): a 64 MB FAT32 stick image. Linux
  6.18.60 from tinyconfig + `boot/kernel.config`, initramfs in the kernel (busybox init `boot/init`, the emulator,
  glibc, libdrm, libasound + config, amixer), EFI stub for UEFI, syslinux for BIOS. Builds in Docker
  (`boot/Dockerfile`, x86 as linux/386); kernel tree and SDL checkout in the `continuum93-boot` volume.
- `boot/sdl-bare.patch` (SDL 3.4.20, boot builds only): kmsdrm dumb-buffer framebuffer when libgbm is missing;
  evdev scans /dev/input when built without libudev.
- Verified in QEMU 7.2/KVM (`boot/test/`): x64 UEFI + BIOS (syslinux), x86 BIOS + 32-bit UEFI → Halcyon up;
  USB mouse (moves, clicks the dock's power button), USB keyboard (F1 → service mode), HDA audio (beep in the
  captured wav), Halcyon's Shut down → "shut down cleanly", stick unmounted, VM powers off. Stick found on 5/5
  repeated boots. Timing (x64): /init at 0.33 s, stick mounted 0.9 s. Halcyon reports ~640 MHz (x64) and
  ~370–530 MHz (x86) under nested KVM.
- Fixed on the way: CONFIG_BLOCK (tinyconfig drops it), LEDS for the PlayStation driver, SYSVIPC (ALSA dmix),
  /etc/group (dmix's audio group), amixer unmute (ALSA starts muted), COMPAT_32BIT_TIME (32-bit glibc futex),
  zstd -19 for the x86 kernel (a 32-bit zstd cannot do -22).
- Real wide screen (user): picture stretched, no scanlines: the firmware's small 4:3 boot mode. Now
  `video=efifb:auto` (largest GOP mode); QEMU switches to 2048x2048, letterboxed, scanlines back (x64 UEFI,
  x86 UEFI32). CRT cost added to `conformance --crt`: 7.9 ms at 1080p; above 2.2 Mpx video.cpp already runs
  the filter at 1/2 (1440p: 1280x720, 4K: 1920x1080), so no threading (user: keep it simple). BIOS
  boots stay 1024x768 4:3 (user: do nothing for now; options if it comes back: EDID via vesadrm +
  FIRMWARE_EDID and a pixel-aspect letterbox, or a manual aspect= line on the stick).
- D-39: `screenAspect` in init.cfg, Shift+F11 cycles and saves it; boot sticks ship `crtFilter=false`. Verified in
  QEMU BIOS (1024x768): 4 x Shift+F11 → 16:9, the picture fills the mode, the stick's init.cfg says
  `screenAspect=16:9`, the mouse lands where expected (5 x mouse_move 97 91 → pointer at 485,455). Desktop
  (Windows, D3D11) logs "square pixels (screenAspect=auto)". x64 and x86 images rebuilt.
- Seen, not fixed: two ALSA underruns at start; once (before the futex fix, not reproduced since) the x86 init
  did not find the stick within 10 s.
- Not checked: real PCs, gamepads (no QEMU gamepad), PS/2 laptop keyboards, AC'97/USB audio, BOOTIA32 on a real tablet.

### 2026-11-04 — macOS build and package (D-37)
- `./package-macos.sh` on an Apple silicon MacBook Air (macOS 26.6, Apple clang 17, CMake 4.5 from the .dmg,
  make, no Ninja) → `dist/continuum93-macos-arm64.zip`: 165 files, binary mode 755, arm64, minos 13.3, links
  only system frameworks, no init.cfg / serviceState / zz_ files.
- Two fixes: float `std::from_chars` (missing from Apple's libc++) → strtof in asm.cpp and basic.cpp; deployment
  target set to 13.3 (otherwise the SDK's 15.5 is the minimum).
- Ran: Metal renderer, CRT on, audio opened, boot.c → Halcyon, clean shutdown (log + screenshot);
  `conformance --ldfill` 6/6; `--bench` 425/264/229/295/224 M instr/s. Not run: asm/BASIC vector suites (the
  vectors come from the Windows oracle), x64 Macs, macOS 13–15.

### 2026-11-04 — CRT fixes and the two-column site
- CRT (D-34 updated): corners bend instead of being cut (per-axis bend); no moiré in small windows (scanline
  depth and mask ease off below 3 px per source pixel). Checked: Windows 1440x810 screenshot, Q OS in the browser
  at 1084x605 (zoomed: even faint lines, no bands), `--crt` on Windows and wasm.
- Site: the machine on the left (as tall as the window allows), downloads + Continuum RX card + notes in a 330 px
  column on the right; one column below 1000 px. Seen at 1440x810: everything visible without scrolling.

### 2026-11-04 — packages and the website (D-36)
- `package.cmd` / `package.sh` (build all, then zip in the emsdk container) → `dist/continuum93-<windows-x64|
  linux-x64|linux-arm64|web>.zip` + `dist/web/` (the site). `package-macos.sh` for a Mac. Full run: 9 min.
- Checked: each zip has 165 files (program, repo Data/, README, LICENSE), Linux binaries keep mode 755, no
  init.cfg / serviceState / zz_ files; the web zip has the 4 files + `.htaccess` + `_headers`.
- Site served locally both ways (scratchpad node server): with the headers, Halcyon runs in the iframe, sizes
  show, macOS greyed "not available yet"; without them, coi-sw.js reloads once and the page is isolated.
- `build.sh` was stored without its executable bit; fixed (`git add --chmod=+x`), as for the package scripts.
- Not checked: package-macos.sh (no Mac), the real host (continuum93.tape-drive.xyz).

### 2026-11-04 — CRT filter (D-34), processor speed in Halcyon (D-35), web mouse fix
- CRT: `src/crt.cpp` (own MIT filter, CPU), `Video::presentCrt`, `crtFilter` in init.cfg, on by default (a
  missing key = on; checked with an init.cfg without it). `conformance --crt`
  (black stays black, corners dark, white centre bright, ms/frame) passes on Windows, Linux x64 and wasm:
  ~4.4–4.9 ms at 1440x810. Looked at on Windows (direct3d11): curved, scanlines, rounded corners. Not looked at
  in the browser (same code path).
- Halcyon: `Data/os/halcyon/hal_bench.c` + top bar, Pulse, About. Measured 815 MHz natively (16 M runs in
  1,178,296 ticks), 246–262 MHz in headless Chrome. Seen in Chrome: count-up, Pulse line, About line,
  click-to-remeasure. cc oracle regenerated (542/542; old one kept as `build/cc.before-bench.txt`).
- Web mouse bug fixed: the pointer landed at the wrong place (SDL's "global" mouse is page units); the web
  build now reads `SDL_GetMouseState`. Checked: click at CSS (700,400) puts the guest pointer there.
- Gotcha: a build copies Data/ only when the exe relinks; after editing only Data/, copy it by hand (or touch a
  source) before testing.
- Seen, not fixed (also in the unmodified Halcyon): About's big rainbow "HALCYON" title does not draw.

### 2026-11-04 — Linux, Raspberry Pi and web builds (D-31, D-32)
`build linux|web|all` in `build.cmd` and `build.sh`, through Docker (`docker/linux.Dockerfile`, emsdk 6.0.13).
Code: the main loop is a `frame()` lambda over statics, so the browser can own the loop;
`SDL_WINDOW_FILL_DOCUMENT`; `init.cfg` falls back to the built-in defaults when Data/ is read-only (was: the
test card); json.h `Member` struct (libstdc++ 12); plain `copy_directory` (CMake 3.25 on bookworm). Page:
`web/shell.html` (canvas in its own div: SDL 3.4.20 fill-document restore bug); docs/WEB.md.
Verified:
- Windows: rebuilt, boots Halcyon, closes cleanly; vectors 1072/1072, programs 44/44.
- Linux x64 (in the container): programs 44, asm 132 + 5 D-9 (oracle paths rewritten to POSIX in
  `build/asm-posix.txt`), cc 541, basic 228 + 113, disasm, xsound 0 differ, basicrun, watch, immediate, input:
  all as Windows. Vectors: 373 of 103,487 wrong, all 1-ULP libm CBR/SIN/COS/TAN (D-31). Needs GLIBC_2.36,
  links libc/libm only. Bench (clang 16): ADD 1036, loop 435, memory 523, CALL 814, C 488 M instr/s.
  Under WSLg: OpenGL, audio, boots Halcyon; read-only Data/ → WARN + defaults, still boots.
- Web: every suite as Linux when node runs *inside the emsdk container* (node on Windows mangles `C:\`
  paths under NODERAWFS); same D-31 libm diffs. Bench (node): 160–216 M instr/s. Headless Chrome, driven over
  DevTools: boot menu, Halcyon, Q OS via Right+Enter, F1 service mode, letterboxed to the page.
- Linux arm64 (emulated in Docker): programs 44, asm 132 + 5 D-9, cc 541, xsound 0 differ. Vectors found
  D-33 (ARM's default NaN bits: 24 SQR/ISQR/POW variants); fixed with `x86Nan`; after it, only 1-ULP
  SIN/COS/TAN cases remain (D-31). Windows still 1072/1072, web unchanged after the fix.
- Gotcha: run program suites with a fresh `C93_FS` copy (tests/programs.cmd), never two at once.
- Not checked: the window icon on Linux (set without error, not looked at), the arm64 app on real hardware
  (only the test runner ran, emulated), macOS at all, sound in the browser (device opens; not listened to).
  Not re-run after D-33: the full arm64 vector set (only the float families) and the Linux x64 suites
  (x86Nan compiles to nothing there).

### 2026-11-04 — the user's decisions applied
D-24 (F1 exit resumes), D-17 (benchmark MHz = i486-class equivalent clock, ~800 MHz here), D-9 (unencodable
lines are assembler errors; 4 shipped programs fixed), D-18 (BASIC emits working code; string runtime
rewritten), D-30 (3D software path stays approximate). App icon: `icon/` (master, .ico resource, window PNG).
Verified after the merges: vectors 1072/1072 (0 of 103,487 wrong); programs 44/44; asm 132 ok + 5 D-9
(oracle regenerated from the fixed Data); basic 228 ok + 113 D-18; basicrun 7/7; cc 541; disasm 27,148;
watch 32,476; immediate 65 ok + 14 D-9 + 5 reference-threw; xsound 137 (0 samples differ); ldfill, input ok.
Live: F1 in/out during the boot countdown, Halcyon boots; benchmark 794–802 MHz over 3 runs; exe icon shown.

Tests: `tests\conf.cmd` (instructions), `tests\programs.cmd` (whole programs, regenerates the
reference results), `tests\png2font.cmd` (CLI font converter vs the reference's), `build\release\conformance.exe --input | --ldfill | --bench ["<watch condition>"|--tracked]`,
`--disasm|--asm|--basic|--cc build\<x>.txt`, `--basicrun tests\basicrun`, `--watch build\watch.txt`, `--immediate build\immediate.txt`.
After changing Data/ sources, regenerate the asm oracle: `tests\oracle\bin\Release\net8.0\oracle.exe --asm
<repo>\Data <repo>\tests\asm <repo>\tests\programs build\asm.txt`.

Test one family: `tests\conf.cmd AND OR` (all: no arguments). Core-only build without SDL:
`cmake -DC93_APP=OFF` (target `conformance`).

### How to regenerate the vectors (needs the reference checkout next to this repo)
```
cd tests/oracle && dotnet run -c Release -- ../../build/vectors.txt 100
cd tests/oracle && dotnet run -c Release -- --programs ../programs ../../build/programs.txt
build/release/conformance.exe --programs build/programs.txt
```
Program vectors: every `.asm`/`.c` in `tests/programs` is compiled by the reference, run from
a fresh machine for `; steps: N` instructions; registers, machine state and changed RAM compared.
1078 variants, ~103k cases, ~35 s. Excluded: INT, WAIT, RAND, PLAY, BREAK, DEBUG, and LD
with a byte count (`,n`), see DECISIONS D-8. Cases where the reference threw are skipped.

### Phase 6 — audio (2026-11-04)
`src/audio_xsound.cpp` (XSound generator + PLAY definition parser), `src/audio.cpp` (APU thread, SDL
device, WAV/Ogg channels), `src/ops_audio.inc` (PLAY). Verified: `oracle --xsound build/xsound.bin` then
`conformance --xsound build/xsound.bin`: 137 definitions (every wave, envelopes, ramps, filters, flanger,
phaser, repeat, COMPLEX, boot sounds, the reference's PLAY test definitions, edge and failure cases),
86,984,651 samples, 0 differ. PLAY decoding: `tests/programs/audio` (program oracle), throws in
`tests/throw/t12-13`. Live: captured with `SDL_AUDIO_DRIVER=disk`: boot sound correlates 0.9998 with the
reference PCM at -3 dB; stacked PLAYs play the last one; WAV/Ogg play/pause/stop/loop/volume timelines
match. No device: `Audio: no output device (...); running silent`. No guest program in Data/ uses PLAY.

## Phase checklist

- [x] 1 Skeleton — build, log, window, GPU + CPU presenters, machine state
- [x] 2 CPU families + oracle (1072/1072 testable variants, 0 wrong)
- [x] 3 Interrupts (all groups; build interrupts wait for phases 4–5, PLAY for 6)
- [x] 4 Assembler, Q OS boots (129/129 files byte-identical; q.asm 2.1 ms vs 33 ms)
- [x] 5 C compiler + BASIC, Halcyon boots
- [x] 6 Audio (XSound bit-exact, PLAY, WAV + Ogg Opus channels, boot sound; D-6, D-21)
- [x] 7 Settings, fullscreen, gamepad, cursor, command line (`-help`, `-png2font`; D-28)
- [x] 8 Service mode — wave 1 (framework, Emulator/Disassembler/Registers/Flags/Status, debugger) and
      wave 2 (every remaining window and popup, `docs/SERVICE.md`); D-23..D-27
- [ ] 9 Speed pass (done, D-29), app icon (done), builds: Windows, Linux x64/arm64, web (D-32) done;
      macOS and a run on a real Pi open

### Phase 2 — instruction families (C# file → status)

LD (3890 lines) · ADD · SUB · DIV · SDIV · MUL · SMUL · SL/SR/RL/RR · SET/RES/BIT ·
AND/OR/XOR/NAND/NOR/XNOR/IMPLY · INV · EX · CP/SCP · INC/DEC · CALL/CALLR/JP/JR/RET* ·
PUSH/POP · DJNZ · SETF/RESF/INVF/LDF · REGS/FREGS/LDREGS/STREGS · WAIT · VDL/VCL · FIND ·
GETBITS/SETBITS · MEMF/MEMC · RAND · MIN/MAX · POW/SQR/CBR/ISQR/ISGN · SIN/COS/TAN ·
ABS/ROUND/FLOOR/… · colour conversions (RGB2HSL etc.) · INT (phase 3)

**Done.** All 1072 oracle-testable variants pass (0 of 103,487 cases wrong). Files: `ops_addsub`,
`ops_logic`, `ops_shift`, `ops_math`, `ops_flow`, `ops_ld` (.inc); each header lists the reference
quirks it reproduces. Not oracle-tested: SETVAR/GETVAR (reference throws on random indices),
`LD …,n` fills (hand cases: `conformance --ldfill`), BREAK (programs), DEBUG (logs; phase 8),
INT (phase 3), RAND/WAIT/PLAY (host-dependent; G ports RAND/WAIT, PLAY is phase 6).

## Session log

### 2026-11-03 — session 1
- Repo scaffolded: `CMakeLists.txt` (SDL 3.4.20 static via FetchContent, /MT, LTO),
  `build.cmd` (Windows, finds VS via vswhere), `build.sh` (Linux/macOS).
- `src/log.*`: rotating log per docs/LOGGING.md, per-OS folder, `C93_RUN_TAG`.
- `src/machine.h`: full machine state (RAM, 256 register banks, float regs, flags,
  register/call stacks, HMEM, back buffers) with templated big-endian accessors and
  flag helpers mirroring `Flags.cs` exactly.
- `src/video.*`: GPU path with INDEX8 textures + per-layer palettes; CPU compositor
  fallback. Reference layer quirks reproduced (D-7).
- `src/cpu.cpp`: reset (default 3-3-2 palette as `Pal8Bit.cs`), run loop, top table
  (unassigned = no-op, as the reference).
- `CLAUDE.md`, `.claude/skills/ponytail` (MIT, verbatim), docs.
- `Data/` copied from reference `fc52ef8` without `Data/lib`.
- **Verified:** Release exe 2.9 MB, runs; test card screenshots on `direct3d11` and
  `software` renderers are identical (transparency, layer order, all 256 colours);
  log rotation and line format checked.
- Phase 2 started: `tests/oracle` (C# vector generator over the reference), `tests/conformance.cpp`
  (replayer + `--bench`), `src/ops.h` (opcodes, operand kinds), `src/ops_addsub.inc`.
- **Verified:** ADD/SUB 17,300 cases, 0 wrong. Bench `ADD r,n`: ~750 M instr/s vs the
  reference's ~104 M (×7.2) with plain two-level dispatch, before any dispatch tricks.
- Found in the reference: memory-fill LD writes through an unchecked pointer (D-8).

### 2026-11-03 — session 2
- INC/DEC, the seven logic ops and INV ported (`ops_logic.inc`); `binOp` + `put` generalised in ops.h.
- `C93_APP=OFF` CMake option: core + conformance only, no SDL download (7 s builds).
- **Verified:** 424 variants / 42,400 cases, 0 wrong.
- Parallel ports A/B/C merged (shift/bit/flag, flow/stack/CP, math/colour); shared helpers
  (`subByte`, `fbits`, `fromBits`) unified into ops.h. Conformance now builds with the app's exact
  optimisation flags (/Oi /GL /LTCG) so intrinsics cannot hide differences.
- Program oracle (`--programs`) + `tests/programs/{sieve.asm,crc.c,structs.c}` added.
- **Verified:** 906 variants / 90k cases, 0 wrong.
- Agent D merged (LD + memory ops); vectors now hash diffs > 256 cells (35 GB → 50 MB, 81 s run).
- BREAK/DEBUG, INT dispatch (`ints.h/.cpp`: machine + random groups, host request atomics).
- **Verified:** 1072/1072 variants, 0 wrong; programs 3/3 ok; `--ldfill` 6/6.
- Perf finding: ~428 M instr/s in the real run loop (×4.1 the reference); `ip` in memory is the
  bottleneck and makes timing layout-sensitive (see PLAN → Benchmarks). Fix in phase 9.
- Wave 2 merged: video primitives, text/sprites/paths, input. Oracle now loads the real default
  palettes (it never did, so palette bytes were unchecked); oracle `--programs` walks subfolders.
  `-ffp-contract=off` for GCC/Clang so ARM never fuses multiply-adds (pixel-exact float math).
- **Verified:** 25/25 programs ok (CPU + video + text), `--input` all ok, 1072/1072 variants.
- Wave 2 complete: file system + PNG + fonts + strings (`ints_fs.cpp`, stb_image for 0x30–0x32),
  RAND/WAIT (`ops_misc.inc`), CPU names. `tests/programs.cmd` runs the whole program suite with
  fresh guest file systems. Decisions D-13..D-16.
- **Verified:** 35/35 programs, 1072/1072 variants, `--input` ok. Exe 3.4 MB.
- main.cpp: settings (`Data/init.cfg`, reference defaults, created when missing), Data folder via
  `SDL_GetBasePath`, guest host requests (fullscreen, cursor, shutdown) served each frame.
- Phase 4 in flight: assembler agent in scratch copy `portI` → `src/asm.h/.cpp`, generated
  `src/asm_ops.inc`, oracle `--optable`/`--asm`, `conformance --asm`. If lost, redo that way.
- Phase 4 merged: `asm.h/.cpp` + generated `asm_ops.inc` (oracle `--optable`), oracle `--asm`,
  `conformance --asm`, corpus `tests/asm`. 129/129 files byte-identical (Data incl. q.asm, all test
  programs, C-compiler output of boot.c/halcyon.c/neon-ricochet.c, 36 edge-case files, every one of
  the 1212 variants). D-9 corrected (opcode byte only), since made an error by the user (D-9).
- Boot wired: `assembleInto()` (ints.cpp) loads a source into RAM; main.cpp boots `bootProgram`
  (C sources fall back to q.asm until phase 5) on the CPU thread; INT 0 0xC0 Build is real.
- **Verified live:** Q OS boots (3 ms assemble), keyboard navigation works, `bubble-universe.asm`
  builds via INT 0 0xC0 (0.4 ms) and renders correctly. 35/35 programs, 129/129 asm, 1072 variants.
- Phase 5 in flight: C compiler agent (scratch `portJ` → `src/cc*`, oracle `--cc`, `conformance --cc`,
  INT 0 0xC2, boot of .c) and BASIC agent (`portK` → `src/basic*`, oracle `--basic`, INT 0 0xC1).
  If lost, redo the same way (assembly text must equal the reference compiler's output).
- BASIC compiler merged (`basic.h/.cpp`, `basic_helpers.inc`, oracle `--basic`, `conformance --basic`,
  corpus `tests/basic`, programs `tests/programs/basic`): 341/341 sources identical (asm text, errors,
  log); INT 0 0xC1 real. Assembler throws inside Build/BuildBasic now halt the machine (D-16). D-18.
- **Verified:** basic 341/341, programs 38/38, asm 129/129.
- C compiler merged (`cc.h`, `cc_internal.h`, `cc_pp.cpp`, `cc_parse.cpp`, `cc_gen.cpp`, generated `cc_runtime.inc` =
  oracle `--ccruntime`; oracle `--cc` records every `.c` under the given paths plus every source the reference's 480
  C unit tests compile, via `CcHarness.cs` built into the oracle in place of their harness; `conformance --cc`
  compares assembly text, errors, log, #pragma origin/stack and include queries). Corpus `tests/cc` (129 edge cases),
  programs `tests/programs/c` (strings, dispatch, gfx, build_c.asm = INT 0xC2 on the game + Halcyon). INT 0 0xC2 real;
  `.c` boot programs compile (`compileCInto`); `Loaded.fatal` shared by assembler and C compiler throws. D-19, D-20.
- **Verified (main, full build):** conf 1072/1072 variants (103,487 cases, 0 wrong); programs 42/42; asm 133/133;
  basic 341/341; cc 541/541 (Data, tests/programs, tests/cc, 398 unit-test compiles); `--input` all ok; `--ldfill` 6/6.
  halcyon.c compiles in 7.7 ms (reference 9.3 ms, best of 5); its generated asm assembles in ~36 ms (reference ~825 ms).
  Live (scratch build): boot.c compiles in 2.5 ms, the selector boots, builds and starts Halcyon.
- **Verified live (2026-11-04):** default boot -> boot.c menu -> 3 s countdown -> Halcyon desktop
  renders correctly (43 ms compile+assemble+load). Fixed: main.cpp now samples host input once
  before the CPU starts (the guest's first mouse read was (0,0), which the boot menu took as
  movement and cancelled its countdown).
- User decisions: D-2 faithful service-mode recreation, D-3 no debug server for now, D-19 confirmed.
- Phase 6 (audio) agent resumed after the session restart (scratch `portL`).
- Audio merged (`audio.h/.cpp`, `audio_xsound.cpp`, `netrandom.h`, `ops_audio.inc`, oracle `--xsound`):
  137 sounds / 86,984,651 samples bit-identical; programs 44/44; 1072 variants; exe 4.6 MB.
- Phase 7 remaining (after the service-mode wave-1 merge, both touch main.cpp): window title with
  version (+ service-mode suffix, from the service work); restore a 2x window when leaving fullscreen;
  CLI `-help` and `-png2font <png>` (Utils/UtilsManager.cs, FontConvertor.cs, Kerning.cs; reuse the
  font extractor in ints_fs.cpp). `-docs` (HTML docs from C# metadata) deferred: developer tool.
  Resizing letterboxes instead of forcing 16:9 like the reference (D-22).

### 2026-11-04 — phase 8, wave 1 (service mode framework + debugger core; scratch `portM`)
- `src/service/`: framework (fonts, theme JSON, Window/WindowManager/Taskbar, layout.json, F1, key and
  mouse routing, zoom animation, Emulator window), disassembler (CodeAnalysis port), CPUState/Disassembled/
  StepHistory/Operations, Disassembler/Registers/Flags/Status windows. `docs/SERVICE.md` = how to add a window.
- Debugger in `Machine`: step-by-step arms a bit in `callbacksPending` (no new check in the run loop); the
  stopped CPU sleeps on an atomic wait. DEBUG opens service mode. Video: `frame()` (screen as a texture),
  `setLogical()`. `netFloat` exported from basic.cpp for float operands.
- **Verified:** `conformance --disasm` 27,148/27,148 (oracle `--disasm`: every Mnem.OPS variant with random
  operands, random streams, RAM end); side by side with the running reference at 1440x810 (DEBUG stop, 3 steps,
  hover, focus, drag + snap + grip resize, exit view, mouse mapping): pixel-identical apart from the Windows
  window corners; saved layout.json identical; F1 resize to MainWidth/Height and back; keys blocked in service
  mode; guest mouse = Emulator-view mapping (239,135) on both; software renderer within ±5 per channel.
  conf ADD 0 wrong, `--input` ok, `--bench` ~422 M instr/s (base build ~421).
- Service mode wave 1 merged (`src/service/*`, `docs/SERVICE.md`, oracle `--disasm`): pixel-identical to
  the reference at 1440x810; disasm 27,148/27,148; bench unchanged (424 M). D-23..D-25 (D-24 since decided: F1 exit resumes;
  the reference kept it stopped).
- Service mode wave 2 in flight, 4 agents in scratch copies portN (memory + memory map, tracking armed
  only in service mode), portO (stacks, float regs, palettes, 3D view), portP (watcher, immediate),
  portQ (disassembler/register/history popups + instruction docs). UI checks serialised via
  `scratchpad/uilock.ps1`. Each delivers a diff `scratchpad/svc-*.diff` against main.
- Phase 7: leaving fullscreen via the guest request restores a 2x window (as the reference). Still
  open: CLI `-help`, `-png2font` (port Utils/FontConvertor + Tools/FontTools BitFont; verify byte-
  identical against the PNG/.font pairs in Data/fonts).
- Wave 2 popups merged (portQ): `service/popups.h`, `popup_disassembler.cpp` (+ generated
  `asm_meta.inc`, oracle `--asmmeta`), `popup_registers.cpp`, `popup_history.cpp`; pixel-identical to
  the reference except values that depend on the wall clock. Remaining wave 2: portN, portO, portP.

### 2026-11-04 — phase 8, wave 2 complete (all service-mode windows and popups)
- Merged into main: memory + memory map (portN, `svc-memory.diff`), watcher + immediate (portP,
  `svc-watch.diff`), on top of the popups (portQ) and stacks/float regs/palettes/3D view (portO). Every
  window in ServiceGraphics now exists; `docs/SERVICE.md` lists them all as done.
- MEMORY / MEMORY MAP (`win_memory.cpp`, `win_memorymap.cpp`): pixel-identical to the running reference
  (hover popups, wheel/Shift scrolling, zoom, Ctrl+wheel and thumb scrolling, popup scrolling, clear,
  read/write colours while a program runs and held while stopped, F1 out/in). Reference quirks kept,
  notably `0x1000000d` being the hex literal 0x1000000D (bricks 16x too big) and the doubled popup draw.
- Read/write tracking (D-26): armed only by F1. `serveCallbacks()` hands the CPU to a second copy of the
  instruction tables (`src/cpu_ops.inc` built against `TrackedMachine` in `service/cpu_tracked.cpp`);
  the normal loop and accessors are unchanged. The app allocates a `TrackedMachine`.
- WATCHER / IMMEDIATE (`win_watcher.cpp`, `win_immediate.cpp`, `watch.h`, `immediate.h`, `editor.h`):
  `kWatchBit` + `watchHook` in `serveCallbacks`, `parked` for snippets; D-27 (the reference's thread
  races settled to their usual outcome). Mode bits: trap 31, track 30, watch 29 (`kModeBits`); the
  WATCHER is checked in the tracked loop too.
- **Verified in main (LF everywhere in src/, docs/, CMakeLists.txt):** conf 1072 variants, 0 of 103,487
  wrong; programs 44/44; `--input` ok; disasm 27,148/27,148; asm 133/133; basic 341/341; cc 541/541;
  watch 32,476 ok (260 where the reference threw); immediate 74 ok (+10 where the reference threw).
- **Bench (ADD r,n):** plain ≈429 M instr/s (wave 1: 424); WATCHER armed (`--bench "A == 1"`) ≈238 M;
  tracked loop (service mode open, MEMORY MAP tracking) ≈368 M; tracked + watched ≈211 M. A branch for the
  tracked loop inside `run()`'s loop had cost the watched path 25% (178 M); it lives in `serveCallbacks`.
- Known limits: interrupt handlers do not mark memory-map pages (the reference's video/string interrupts
  did); DEBUG opening service mode does not arm tracking (as the reference).

### 2026-11-04 — phase 7 done: command line (scratch `portR`)
- `src/tools.cpp/.h` (UtilsManager.cs, FontConvertor.cs, BitFont.cs): `runTool()` runs before SDL init.
  `-help` (stdout + log), `-png2font <png>` (writes `<dir>/<name>.font`), `-docs` logs that it is not
  ported; unknown arguments warn; any argument means the machine does not start (as the reference). D-28.
- **Verified:** `tests\png2font.cmd` (oracle `--png2font`: the reference's own ConvertFont through a bare
  MonoGame Game for its GraphicsDevice): 26 PNGs (Data/fonts, Data/filesystem/fonts, 9 crafted in
  `tests/png2font`) + the 3 committed reference .font files: 29/29 byte-identical. UTF-8 paths ok.

### 2026-11-04 — phase 9 speed pass (scratch `portT`, `speed.diff`)
- D-29: handlers `uint32_t op(Machine&, uint32_t ip)` (ip in a register, `Machine::ip` stored once per
  instruction); `gSub[first][second]` is the one dispatch table of `run()`/`step()`; handlers end in
  `C93_NEXT`, a tail jump to the next handler (musttail on Clang/GCC 15+, MSVC's own tail calls with a 16 KB
  stack guard); `running` lives in `callbacksPending` (kStopBit), `step()` uses kStepBit. The tracked loop
  keeps byte-wise dispatch and plain returns.
- `conformance --bench` now times five mixes (ADD r,n; Data's CPU-benchmark loop; LD (rrr) memory;
  CALL/RET; compiled C) and checks each one's end state under `run()` against `step()`;
  `--bench --tracked` runs them through service mode's tracked loop (cpu_tracked.cpp is linked in).
- **Bench (MSVC, M instr/s, before → after):** ADD 436 → 786, loop 360 → 548, memory 275 → 447,
  CALL/RET 476 → 700, C 253 → 409; tracked ADD 368 → 440; WATCHER armed ADD 218 → 206.
  GCC 15 (WSL, g++ -O3 by hand): ADD 442 → 922, loop 364 → 571, memory 370 → 576, CALL/RET 478 → 886,
  C 285 → 532. Two builds with unrelated code added: within ±2% (PLAN.md → Benchmarks).
- **Verified (MSVC):** conf 1072/1072 (0 of 103,487 wrong); programs 44/44; ldfill, input ok; asm 133;
  basic 341; cc 541; disasm 27,148; watch 32,476; immediate 74 (+10); xsound 137, 0 samples differ.
  Live: default boot → Halcyon desktop; F1 service mode, F9 steps (history), F5 runs (tracked), F1 back.
- **GCC 15 in WSL (core only; no cmake/ninja, no sudo):** builds with -Wall -Wextra, no warnings in the
  changed files; every suite gives exactly the baseline's results there (CBR vectors differ by one ulp
  from glibc's cbrt, asm.txt holds Windows paths, fs_a_exists needs a case-insensitive file system).
