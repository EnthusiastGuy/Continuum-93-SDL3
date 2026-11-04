# Port plan

Source of truth for behaviour: `C:\Projects\Continuum-93\Continuum Emulator` (C#, .NET 8,
MonoGame), version 2.2.72, commit `fc52ef8`. Paths below named `Emulator/...` are there.

## Goals

| Goal | Target | Reference today |
|------|--------|-----------------|
| Files shipped | `continuum(.exe)` + `Data/` | 26 files + .NET runtime, 19 MB |
| Exe size | < 4 MB, static (SDL3 + CRT inside) | — |
| CPU speed (Release) | ≥ 3× the reference on simple instructions | 729 MHz eff., ~104 M instr/s |
| Platforms | Windows x64, Linux x64/arm64 (incl. Raspberry Pi), macOS arm64/x64 | Windows, Linux, macOS, ARM |
| Behaviour | Bit-identical CPU results; same pixels; same files written | — |

## Architecture

```
main.cpp     SDL window + event loop (main thread); starts the CPU thread
machine.h    all machine state + inline hot accessors (RAM, registers, flags, stacks)
cpu.cpp      reset, run loop, opcode tables; instruction families in ops_*.inc
video.*      presenter: GPU indexed textures, or CPU compositor
log.*        rotating log (docs/LOGGING.md)
--- to come ---
int_*.cpp    interrupts: machine, video (+ drawing accelerators), input, filesystem, strings, random
asm.cpp      assembler (Emulator/Interpreter) — needed to boot Q OS
cc/*.cpp     C compiler (Compilers/C93C) — needed for boot.c and Halcyon
basic/*.cpp  BASIC compiler (Compilers/C93Basic)
audio*.cpp   APU (audio.cpp): WAV, Ogg/Opus streaming, SDL streams; XSound synth (audio_xsound.cpp)
service/     service mode / debugger, drawn with SDL (D-2; docs/SERVICE.md)
tests/       conformance runner (C++) + oracle (C#, outside the shipped build)
```

**Threads.** Main thread: SDL events, input state, presenting at vsync. CPU thread: runs
free (`Machine::run`), exactly like the reference's machine thread. Audio: SDL's callback
thread. Shared state is plain memory plus a few atomics (`running`, video bits, dirty
masks). Torn frames are possible and accepted, as in the reference.

**CPU.** Two-level dispatch, as in the reference: `gTop[first byte]`, then per family a
256-entry table keyed by the sub-op byte. Handlers are templates over width (8/16/24/32)
and operand kind, so one C++ template replaces dozens of C# lambdas. All memory access is
byte-wise big-endian (portable on any host), addresses masked to 24 bits.
Later, measured: fused 16-bit dispatch (first+sub byte → one table) and a
threaded-code cache are the two "crazy" speedups worth trying.

**Video.** 480×270, up to 8 layers, 256-colour palette per layer, index 0 transparent.
The reference composites on the CPU each frame. Here each back buffer is an `INDEX8`
SDL texture with its palette (SDL 3.4), so the GPU does lookup, transparency and
scaling; the CPU uploads 130 KB per changed layer. With no GPU renderer (or
`SDL_RENDER_DRIVER=software`), `presentSoftware` composites into one RGB texture.
Both paths produce the same pixels (checked by screenshot, phase 1).

Layer bookkeeping copies the reference exactly, including its quirks: back buffer 0 is
the top layer = page `vramPages-1`; visibility bits index back buffers; auto/manual update
bits index pages; the bottom visible layer shows its colour 0 as background.

**Drawing accelerators** (`Emulator/GraphicsAccelerators`, video interrupts): CPU
implementations first (they write guest RAM, which the guest can read back, so the GPU
cannot own them). SIMD only behind portable fallbacks.

**Audio.** One SDL playback device; every sound or stream is an `SDL_AudioStream` bound to it
(SDL mixes). XSound synthesis ported as-is, bit-exact (`oracle/conformance --xsound`). WAV uses the
reference's own RIFF parser, not `SDL_LoadWAV` (which converts formats the reference did not). The
reference's `.ogg` files are **Opus**: libogg + libopus, static via FetchContent (D-6, D-21).

**Data.** `Data/` (OS sources, fonts, filesystem) next to the exe, found through
`SDL_GetBasePath()` (works inside a macOS bundle). Copied from the reference at
`fc52ef8`, minus `Data/lib` (Windows DLLs the port does not need).

## Conformance oracle (how "bit-identical" is proven)

1. `tests/oracle/` — a small C# console project referencing the reference emulator. For
   every instruction variant in `Mnem.OPS` (1216 of them, with their bit formats), it
   generates N random encodings, random registers/flags, RAM filled by a fixed hash of the
   address, runs **one** instruction, and writes a vector: input state + instruction bytes
   → output registers, flags, IP, stack pointers, and every changed RAM/stack byte.
2. `tests/conformance.cpp` — replays the vectors through the C++ core and reports
   every mismatch with the disassembly-style variant name.
3. Interrupts and whole programs: run the same `.asm`/`.c` program on both and compare
   RAM and VRAM hashes at fixed instruction counts.

## Phases

| # | Phase | Exit condition |
|---|-------|----------------|
| 1 | Skeleton: build, log, window, GPU/CPU presenters, machine state | Test card identical on GPU and CPU paths; log as specified |
| 2 | CPU: all instruction families + oracle | 0 mismatches over ≥ 100 vectors per variant |
| 3 | Interrupts: machine, video, input, filesystem, strings, random | Reference test programs give identical RAM/VRAM hashes |
| 4 | Assembler | Assembles `Data/os/q.asm` byte-identical to the reference; Q OS boots |
| 5 | C compiler + BASIC | Same output bytes as the reference on its test corpus; Halcyon + Neon Ricochet run |
| 6 | Audio | XSound, WAV, Opus play; same samples as the reference for XSound |
| 7 | Settings, fullscreen, gamepad, host cursor, shutdown | Feature parity with the reference's window/input |
| 8 | Service mode / debugger | See DECISIONS D-2 |
| 9 | Speed pass + packaging for all platforms | Benchmarks table filled; CI-style build on 3 OSes |
| 10 | Boot images: a stick that boots a PC (x64, x86) straight into the emulator; Raspberry Pi later (D-38, docs/BOOT.md) | Boots in QEMU on UEFI and BIOS with video, keyboard, mouse, sound, shutdown; then on real PCs |

## Benchmarks

Measured the same way as the reference (`RunSingleInstructionRepeatedly`, 1M runs):

| Build | Simple instr/s | Notes |
|-------|----------------|-------|
| Reference C# Release | ~104 M | 2026-10, this machine |
| C++ port, `step()` called in a loop (first bench) | ~750 M | 2026-11-03; layout-sensitive, see below |
| C++ port, real `Machine::run` loop, all families | ~428 M (×4.1) | 2026-11-04, `conformance --bench`, Ryzen 9 9950X3D |
| C++ port, phase 9 (MSVC) | ~786 M (×7.6) | 2026-11-04; ip in a register, fused dispatch, threaded handlers (D-29) |
| C++ port, phase 9 (GCC 15, WSL) | ~922 M (×8.9) | same machine; musttail-guaranteed threading |

Phase 9, `conformance --bench` mixes (M instr/s, each through `Machine::run`, counted with `step()`;
same bench source before and after, Ryzen 9 9950X3D, 2026-11-04):

| Mix | MSVC before | MSVC after | GCC 15 before | GCC 15 after |
|-----|------------:|-----------:|--------------:|-------------:|
| ADD r,n (straight line, as the reference's bench) | 436 | 786 | 442 | 922 |
| loop: LD/DEC/CP/JR (Data's CPU-benchmark.asm) | 360 | 548 | 364 | 571 |
| memory: LD r,(rrr) / (rrr),r / 32-bit, INC, DJNZ | 275 | 447 | 370 | 576 |
| CALL / CALLR / RET / DJNZ | 476 | 700 | 478 | 886 |
| compiled C (C compiler output: frames, locals, MUL, CALL) | 253 | 409 | 285 | 532 |
| ADD r,n, tracked loop (`--tracked`, service mode open) | 368 | 440 | 310 | 407 |
| ADD r,n, WATCHER armed (`"A == 1"`) | 218 | 206 | 183 | 206 |

Layout robustness: two builds with unrelated code added (cpu.cpp's reset, ints.cpp) measured within
±2% of the plain build on every mix, on both compilers (the old loop swung 428 ↔ 765 on such changes);
the memory mix alone also varies ±8% from run to run (MSVC 406–450).
Not reached: 1 G instr/s (≈6–7 host cycles per simple guest instruction remain). Tried and dropped:
an arithmetic register ring instead of the kNext tables (−25% on the 24-bit mixes), an unrolled loop
(+3%), unmasked fetches (+6% in a micro-benchmark, but not exact at the 16 MB wrap). What is left is the
per-byte 24-bit mask, one byte store per flag and the register file in memory.
