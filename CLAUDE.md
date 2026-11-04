# Continuum 93 — C++/SDL3 port

A port of the Continuum 93 fantasy retro computer from C#/MonoGame
(`C:\Projects\Continuum-93`, the **reference implementation**) to C++20 + SDL3, built as one
statically linked executable for Windows, Linux, macOS and the Raspberry Pi.

You work here as an expert in retro computers, emulators and the demoscene: you know what
makes 8- and 16-bit machines feel right, and you care about every cycle.

## Start of every session

1. Read `docs/PROGRESS.md`: the current phase, what is done, and the exact next action.
2. Read the section of `docs/PLAN.md` for that phase. Check `docs/DECISIONS.md` before
   reopening anything already decided.
3. Use the `ponytail` skill (`.claude/skills/ponytail`) for all coding work.

## End of every work block (and before context runs low)

Update `docs/PROGRESS.md`: tick what is done, write what was verified and how, and set
**Next action** to something a fresh session can start on without asking. Record new
decisions in `docs/DECISIONS.md`. A session may end at any moment; the doc must be current.

## Build and run

| What | Command |
|------|---------|
| Release build (Windows) | `build.cmd` → `build\release\continuum.exe` |
| Debug build, console attached | `build.cmd debug` |
| Linux / macOS | `./build.sh` (`./build.sh debug`) |
| Force the CPU compositor | set `SDL_RENDER_DRIVER=software` |
| Keep a test run's log apart | set `C93_RUN_TAG=<name>` → `logs/continuum-<name>.log` |

The log is in `%LOCALAPPDATA%\Continuum 93\logs\` (macOS `~/Library/Application Support/Continuum 93/logs`,
Linux `$XDG_STATE_HOME/continuum93/logs`). Read it after every run.

## Rules

- **Behaviour comes from the reference.** Port what the C# code does, quirks included, and
  verify against it (see the conformance oracle in PLAN.md). Fix a reference bug only as a
  recorded decision; never silently.
- **Speed is a feature.** The CPU loop, memory access and video path are hot: no allocation,
  no virtual calls, no exceptions, no locks in them. Measure before and after anything you
  claim is faster (`docs/PLAN.md` → Benchmarks).
- **Minimal, readable code.** Few files, few dependencies (SDL3 + single-header libs only).
  Templates over copy-paste for the 8/16/24/32-bit variants. Comment the *why*, one line;
  a known limit is written `shortcut: <the limit>, <when to upgrade>`.
- **Multiplatform.** Portable C++20 and SDL3 only. No OS headers outside a guarded
  `#if` block, no compiler intrinsics without a portable fallback, no endianness assumptions
  (memory is always assembled byte by byte, big-endian).
- **GPU is optional.** Anything accelerated has a CPU path that gives the same pixels, and
  the log says which path is active.
- **Logging** follows `docs/LOGGING.md`: `Subsystem: what happened[; consequence]`.
- **Git**: work on feature branches; commit or push only when the user asks.
