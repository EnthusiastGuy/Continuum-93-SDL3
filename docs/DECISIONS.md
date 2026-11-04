# Decisions and open questions

Status: **DECIDED** (with date) or **OPEN** (waiting on the user). Never reopen a decided
item without new information; add a new entry instead.

## D-1 Language and stack — DECIDED 2026-11-03
C++20, SDL 3.4.x statically linked, static C runtime on Windows, one executable plus `Data/`.
Multiplatform from day one (user, 2026-11-03).

## D-2 Service mode / debugger — DECIDED 2026-11-04 (user): faithful recreation
Recreate the reference's service UI pixel for pixel, or rebuild it with Dear ImGui
(faster to write, looks modern, adds ~1 MB)? Proposal: Dear ImGui, last phase.

## D-3 ContinuumTools remote debugger and the debug server — DECIDED 2026-11-04 (user): dropped for now
The reference starts a TCP debug server at boot for ContinuumTools. Port it, drop it, or
port it bound to localhost only? Proposal: drop until asked; if ported, localhost only.

## D-4 Log location — DECIDED 2026-11-03 (revisit if the user objects)
Per-user data folder (`%LOCALAPPDATA%\Continuum 93\logs`), rotating, like
Last-Known-Reality. The reference wrote next to the exe. Alternative: a "portable"
mode writing beside the exe when a `portable` file exists there.

## D-5 SDL source — DECIDED 2026-11-03 (revisit if the user objects)
FetchContent pinned to `release-3.4.20` (downloaded once at configure). Alternative:
vendor as a git submodule for offline builds.

## D-6 Opus audio — DECIDED 2026-11-04 (accepted for now)
The reference's `.ogg` files are Opus. libopus 1.5.2 + libogg 1.3.6, static via FetchContent, library
only (no programs/tests/docs), app target only: +195 KB on the Windows exe (whole audio port included).
Converting the sounds to WAV/QOA instead would break guest programs that load their own `.ogg`.

## D-7 Reference quirks — DECIDED 2026-11-03
Port quirks faithfully; list each here so they can be fixed deliberately later.
- `VCL` is wired to the `VDL` handler (`InstructionSet.cs`): VCL *updates* layers
  instead of clearing them. Kept.
- Visibility bits index back buffers (top first) while auto/manual bits index pages.
  Kept.
- Out-of-range RAM (> 16 MB + 3), register indices 26–31 for 16+ bit views, more than
  8 video pages: the reference threw and halted. The port wraps or clamps (no UB).
  Programs that relied on the crash are not a concern.

## D-8 Reference bug: unchecked memory fill — FOUND 2026-11-03, needs the user's call
`Memory.SetBytesToRAM` / `SetBytesToRAMRepeated` (used by the `LD (dest),value,n[,repeat]`
fill variants, 110+ of them) write through an unsafe pointer without bounds checks. A count
or address that runs past 16 MB writes outside the RAM array: the oracle crashed the .NET
process with an AccessViolation. The port will clamp/wrap at 24 bits like every other
access. Worth fixing in the C# project too.

## D-9 Unknown operand shapes are an assembler error — DECIDED 2026-11-04 (user)
The reference assembler, for a line whose operand shape has no opcode-table entry (e.g. `LD (rrr), n`)
or whose mnemonic is unknown (`//INT ...`: `//` is not a comment; stray `DR`/`ER`), emits only the primary
opcode byte (0x00 for an unknown mnemonic) with no error; the byte then swallows the start of the next
instruction. The port makes such a line an error, counted like the reference's "unrecognized operand"
errors (one per line), and emits nothing for it:
`Assembler: <path> line N: 'LD (BCD), 0': no LD form takes operands (rrr),nnn` / `...: unknown mnemonic //INT`.
Shipped sources fixed: font-test (`//INT`, `//WAIT` -> `;`), isometric-demo and oldOS os/os v2
(`LD (x), 0` -> `LD (x), 0, 1`; `DR`/`ER` commented out); they assemble byte-identical to the reference.
conformance --asm/--basic count such cases as "expected divergence (D-9)" (all but the bytes must still
match). The reference BASIC compiler's generated code also hit it (`CP A, HI`, `INT ABCD`, ...); fixed
with D-18.

## D-10 CP sets comparison flags, not carry/zero — NOTE
Programs branch after CP with `EQ/NE/LT/GT/LTE/GTE`; `C`/`NZ` after CP test stale flags.
Not a bug, a convention worth remembering when writing test programs.

## D-11 Keyboard: SDL keycodes, not scancodes — DECIDED 2026-11-04
The reference (MonoGame DesktopGL on SDL2) maps SDL *keycodes* to `Keys` (Windows VK numbers),
so layouts behave as there (AZERTY's physical Q gives `Keys.A`). The port does the same. SDL3
differences: no `MAIL` key; `MEDIA_PLAY` and `MEDIA_PLAY_PAUSE` both map to MediaPlayPause.

## D-12 Input callback timing — DECIDED 2026-11-04
The reference rewrote IP from the UI thread with no synchronisation. The port raises a pending
flag and the CPU pushes IP and jumps at the next instruction boundary (same observable effect,
no race). Phase 9 folded `running` into the same word (kStopBit, D-29): one check per instruction.

## D-13 Guest paths cannot leave the file system root — DECIDED 2026-11-04
The reference passed `..`, `C:\...` and `\...` straight to the host, giving guest programs the
whole disk. The port treats such paths as missing and refuses saves (logged). Security at a
trust boundary; no legitimate program needs it.

## D-14 LoadPalette (INT 4, 0x32) — reference bug, port does the intent — DECIDED 2026-11-04
The reference wrote a null pixel buffer, crashing its game thread and never resuming the CPU.
The port writes the palette at the destination and the colour count to the register.

## D-15 Number formatting is fixed en-US — DECIDED 2026-11-04
INT 5 used .NET `ToString(format)` in the current culture; the reference's tests and machines are
en-US (`12.45%`, `$`, `∞`). The port implements .NET's standard + custom numeric formats in en-US
on every host (425,880 cases checked against real .NET 8, 0 mismatches).

## D-16 Where the reference threw, the port halts — DECIDED 2026-11-04
An exception escaping the C# CPU loop stopped the machine; the port stops it too (and logs the
reason) for file-system errors, invalid formats, chars > 255 in the key buffer, etc. Cases are
kept in `tests/throw/` (the oracle confirms each one throws).

## D-17 "CPU MHz" is a real-hardware-equivalent clock — DECIDED 2026-11-04 (user: "as close to actual hardware MHz as possible")
The reference's cycle counts (`Tools/InstructionCycleBenchmark`) were calibrated on the C# emulator
itself (fastest instruction = 2 ticks), so its "MHz" was self-referential (the port showed ~3800).
The MHz is computed only by the guest's CPU benchmark (cycles per loop x runs / time); no emulator
code involves cycles. The benchmark now counts the cycles real hardware would take: an in-order,
single-issue pipelined CPU with on-chip cache, the i486 class (1993, the machine's era): register
ALU/move 1 cycle, taken branch 3 (not taken 1), +1 per memory access. Its LD/DEC/CP/JR loop is
6 cycles (was 28). The figure is the clock such a chip would need to keep up with the emulator.
It also times with the 100 ns clock (INT 0 0x03 mode 1) over 16 M runs instead of 1 ms over 10 M
(the loop now ends in ~0.12 s), and reloads DrawText's registers per line (DrawText returns its box
in them, so the designation and messages used to draw off-screen). Result here: ~800 MHz ("Odin I"),
3 runs within 1%. Not done: a throttled fixed-clock mode (possible later on the same cycle model).

## D-18 Reference BASIC string runtime crashes — FIXED 2026-11-04 (user: "yes, fix it")
The reference emitter's string helpers (UCASE, LCASE, TRIM, STRING, INSTR, `+`, and also LEN, ASC,
LEFT, RIGHT, MID, CHR, VAL, LTRIM, RTRIM) and much of its other output faulted. Causes: 32-bit temps
used where 8/16/24-bit operands are needed (they assembled to a lone opcode byte, D-9), helpers
overwriting their own inputs, and results lost at `POP A, U`. The port emits fixed code by default;
`compileBasic(..., reference = true)` keeps the reference's text byte for byte (all fixes sit in `!ref_`
branches of `src/basic.cpp`). New runtime `src/basic_helpers_d18.inc`: one register convention, inputs
through the register stack, numeric results survive the restore. A helper result holds at most 31
characters, a variable at most 15 (assignment copies 16 bytes and terminates; short literal-initialised
strings reserve 16). STR$ uses format `""` (`"{0}"` printed its braces). Every line the corpus could not
encode is fixed (operand widths, int/float conversions, MOD/`\`/NEG/WAIT r, `LD .label, r`, float hex
without 0x, GOTO/GOSUB targets, `#DB` control characters): the 341-program corpus assembles with 0 errors.
Tests: `conformance --basic` compares reference mode with the oracle and requires the fixed output to keep
errors/log and assemble cleanly ("expected divergence (D-18)"); `conformance --basicrun tests/basicrun`
runs 7 programs that check results (all fail in reference mode).
Not changed (valid code, wrong semantics; follow-ups): PRINT of a non-literal string expression prints an
integer; string compares compare addresses; INPUT into an integer stores the char in the high byte;
MOUSE/TIME/TICKS clobber live temps in A..J; DIV is unsigned; array element assignment ignores the index;
WAIT is in milliseconds (the spec says frames); `#ORG 080000` is read as decimal.

## D-19 Build interrupts read sources under Data/ — DECIDED 2026-11-04 (user confirmed): builds may read Data/
The dual-boot selector (`boot.c`) builds `../os/q.asm` and `../os/halcyon/halcyon.c`, relative to the
guest file system root; under D-13 both were refused, so it could boot neither. INT 0 0xC0/0xC2 (and
the C compiler's `#include`s during a guest build) now accept paths anywhere under `Data/` (the root's
parent), still nothing outside it; every other file interrupt (and INT 0 0xC1 BuildBasic) keeps D-13.
The boot program itself is chosen by the host (`init.cfg`), so its includes are not sandboxed.

## D-20 C compiler: where the reference threw — DECIDED 2026-11-04
The C# compiler lets some .NET exceptions escape `Compile` (int.MinValue / -1 in a constant, a token
read past the end, unsized stores). The port returns `CcResult.fatal` instead (`Loaded.fatal`, shared
with assembler throws); a guest build (INT 0 0xC2) then halts the machine (D-16), a boot program is not
loaded. `tests/cc/throw_*.c` keep the candidate cases (the oracle records which ones really throw).

## Decisions confirmed by the user on 2026-11-04
- D-2: Service mode is a faithful recreation of the reference's look and behaviour, drawn with SDL
  (no Dear ImGui).
- D-3: no debug server / ContinuumTools listener for now (no network port, no firewall prompt).
- D-19: builds (INT 0 0xC0/0xC2 and their #includes) may read anything under Data/; every other
  guest file access stays inside Data/filesystem (D-13).

## D-21 Audio: what the port reproduces and what it does not — DECIDED 2026-11-04
Reproduced (they are what a guest hears on the reference): XSound PCM bit for bit; PLAY plays the
*last* PLAYed definition when several pile up before the APU thread runs (the C# lambda read a field
late); the APU thread dies, sound off until restart, where the reference's threw on it (empty sound,
sample rate outside 8000..48000, a 257th concurrent sound, a delay index made negative); a sound that
never ends (envelope float clock stalls at 2^24 samples) counts as such a death instead of hanging a core;
WAV streams lose their last 250-500 ms when not looped (Stop on EOF drops queued chunks); bytes after
the WAV data chunk play as audio; mono is panned centre at -3 dB (OpenAL Soft). PLAY definitions the
reference failed to parse (bad wave type, COMPLEX type listed twice) halt the machine (D-16).
Not reproduced: the reference's Ogg player decoded the OpusHead/OpusTags packets as audio, forced mono
and submitted whole 120 ms frames (it could not play a real file and no guest path reaches it); the port
decodes Ogg Opus properly. OpenAL Soft's output limiter is not emulated (SDL clips at full scale).
No guest instruction or interrupt reaches the WAV/Ogg channels in the reference; the API is kept for
when one does.

## D-22 Window resizing letterboxes — DECIDED 2026-11-04
The reference forced the window back to 16:9 (width from height) on every resize. The port lets the
window take any size and letterboxes the 480x270 canvas (SDL logical presentation); the picture is the
same and the window manager is not fought. Mouse mapping already accounts for the bars.

## D-23 Service mode keeps typed text from the guest too — DECIDED 2026-11-04 (task brief: "the guest sees no keys")
The reference stopped registering key downs while service mode was open (and cleared the held keys on F1),
but its TextInput handler still put typed characters into the guest's buffer and fired the keyboard
callback. The port blocks both; key releases still go through so nothing sticks.

## D-24 Leaving service mode with F1 resumes the machine — DECIDED 2026-11-04 (user)
The reference turned step-by-step on with F1 and never off on leaving, so the machine stayed frozen
(title ", debugging") until the user re-entered and pressed F8 or F5. The port now calls
`ops::ContinueExecution()` when F1 leaves service mode (log: "service mode off; CPU resumed at ...").
Verified live: F1 during the boot selector's countdown, F1 again, the countdown finishes and Halcyon boots.

## D-30 3D view on the CPU renderer stays approximate — DECIDED 2026-11-04 (user)
Without a GPU, ~1.9% of the 3D view's pixels differ from the GPU path (SDL's software triangle
rasterizer). The user accepted it; a custom exact rasterizer is not worth it for now.

## D-25 Service-mode quirks kept — NOTE (2026-11-04)
- Windows whose content area is smaller than the 8 px clip inset (STATUS, height 30) get a negative
  scissor; MonoGame's GL backend rejects it and keeps the full-screen scissor, so the status text draws
  past the window's bottom edge. Reproduced (`Window::DrawContentClipped`).
- Proportional text draws each glyph's ink width from the cell's left edge, not from the ink offset.
- `GetMemoryWrapped` (register memory column) wraps 0x1000000 to 1, not 0.
- DEBUG opens service mode without resizing the window or clearing held keys (only F1 does both).
- The disassembler stops where the reference threw (operands its tables cannot render, RAM end + 3).

## D-26 Memory activity tracking only while service mode is open — DECIDED 2026-11-04
The MEMORY MAP needs the reference's MemoryActivityTracker (every RAM read/write/fetch marks its 256-byte
page). The reference armed it on F1 and disarmed it on leaving, but paid an `Enabled` check in every
accessor. The port keeps the normal CPU loop and the Machine accessors exactly as they were: F1 sets
`Machine::kTrackBit` in `callbacksPending` (the one check the loop already makes), and `serveCallbacks()` (out of line,
so the loop's code is unchanged) hands the CPU to a second copy of the instruction tables (`src/service/cpu_tracked.cpp`, the same `cpu_ops.inc` built
against `TrackedMachine`, whose fetch/rd/wr also mark pages) until the bit is cleared. `conformance --bench`
is unchanged (≈426 M instr/s before, ≈428 after); the tracked loop runs at ≈368 M (86%). A branch for it inside
run()'s loop cost the WATCHER's per-instruction path 25% (237 -> 178 M), hence serveCallbacks. As in the reference, DEBUG opening service mode
does not arm it (only F1 does). Interrupt handlers are compiled once and do not mark pages (the reference's
video/string interrupts did through MEMC.Set8bitToRAM); recorded as a shortcut in cpu_tracked.cpp.
Reference quirks the MEMORY / MEMORY MAP windows keep (verified on the running reference): the map's
`0x1000000d` is the hex literal 0x1000000D (≈16x RAM), so bricks hold 16x the bytes the area has room for
and MapRows (scrollbar, Ctrl+wheel step) counts 16x the bricks; both hover popups are drawn twice
(WindowManager, then ServiceGraphics' second pass), so their backgrounds are denser, and the MEMORY popup's
second pass draws even with its window minimised; the map popup's wheel position starts at 0, so it opens
scrolled by one when the wheel was ever used; popup positions are clamped with sizes they do not have
(200x100, 280x260); the MEMORY window's own reads mark pages 0-1 (it reads through Get8bitFromRAM).

## D-27 WATCHER and IMMEDIATE: the reference's thread races — DECIDED 2026-11-04
Both windows touched the machine from the UI thread while the CPU thread polled its trap with Sleep(1).
Measured on the running reference and settled to its usual outcome, deterministically:
- F5/F8 while every WATCHER condition holds: its per-frame check re-stopped the machine before the CPU ran
  (4 of 5 presses). The port checks the conditions once more when the trap is left by a resume
  (`serveCallbacks`), so the machine does not move. Conditions are also checked after every instruction (kept).
- IMMEDIATE Ctrl+Enter while stepping: the reference's CPU thread often slipped one instruction past the
  return address (3 of 4 runs). The port parks the CPU, so it stops at the return address (the other outcome).
- IMMEDIATE Ctrl+Enter while running: the reference wrote IP mid-instruction (the jump could be lost or tear
  an instruction). The port stops the CPU at an instruction boundary, jumps, and lets it run (as D-12).
- Where the reference threw out of its UI thread and the app died (an assembler fatal, a WATCHER address past
  RAM + 3), the port shows "Compile errors: n+1" / wraps the address; an illegal instruction in the snippet
  halts the machine ("Immediate did not return within limit").
- Bits: kTrapBit 31, kTrackBit 30 (D-26), kWatchBit 29; `serveCallbacks` keeps all three (`kModeBits`), and the WATCHER's conditions are checked in the tracked loop too (it calls `serveCallbacks` whenever any other bit is set).

## D-28 Command line — NOTE (2026-11-04)
- As the reference: the first `-help`/`-docs`/`-png2font` ends processing; unknown arguments are logged and
  the machine still does not start (the reference processed arguments in its first Update, then exited).
- Port differences: no window opens (the reference's "briefly shows up"); a failed `-png2font` (missing,
  not `.png`, undecodable, unwritable) exits with 1 and an ERROR line (the reference exited 0, or crashed
  on a bad PNG); `-docs` is not ported (HTML generated from C#-only metadata) and logs a WARN.
- `-help` goes to stdout and the log. A Windows release build is GUI-subsystem, so stdout is visible only
  when redirected (`continuum -help > help.txt`); no AttachConsole (Windows-only API, not worth it here).

## D-29 CPU dispatch: ip in a register, fused table, threaded handlers — DECIDED 2026-11-04
- Handlers are `uint32_t op(Machine&, uint32_t ip)`: ip arrives past the bytes dispatch read and the handler
  returns the next instruction's address; operand fetches advance the local. `Machine::ip` is stored once per
  instruction, so other threads (service mode, the log) still see a boundary, never a mid-instruction value.
  Interrupts get `Machine::ip` set before they run and may move it (INT returns it).
- `gSub[first][second]` is the one dispatch table of `run()`/`step()`: family rows hold the sub-op handlers,
  one-byte opcodes fill their row (through `one<>`, which gives back the byte). The tracked loop keeps the
  byte-wise `gTop` path, so the MEMORY MAP marks exactly the bytes fetched, as before.
- Threaded: a handler ends in `C93_NEXT(ip)`, which jumps straight to the next handler unless
  `callbacksPending` is non-zero. Clang/GCC 15+: `musttail` guarantees the jumps. MSVC turns them into jumps
  at /O2 but without a guarantee (a few float handlers keep a call), so the chain also returns to `run()`
  once the stack is 16 KB below its frame (`_AddressOfReturnAddress`): depth bounded in every build, Debug
  included. Other compilers, and the tracked copy: plain return to the loop.
- `running` is a view of kStopBit in `callbacksPending` (same load/store/= API), so the loop and the chain
  test one word. `step()` sets kStepBit around its instruction so a chained handler stops after it.
- Cost: WATCHER-armed runs ~5% slower on MSVC (every instruction leaves the chain); faster on GCC.
- Measured in PLAN.md → Benchmarks; behaviour checked by every suite plus `--bench` (each mix's end state
  under `run()` equals `step()`'s, plain, watched and `--tracked`).
