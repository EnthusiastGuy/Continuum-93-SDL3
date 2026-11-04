# Logging

One log file per run, plain text, readable by a human and greppable by a tool.

## Where

| OS | Folder |
|----|--------|
| Windows | `%LOCALAPPDATA%\Continuum 93\logs\` (Local, not Roaming: logs belong to the machine) |
| macOS | `~/Library/Application Support/Continuum 93/logs/` |
| Linux | `$XDG_STATE_HOME/continuum93/logs/` (default `~/.local/state/continuum93/logs/`) |

`continuum.log` is the current run; the last four runs are kept as `continuum.1.log` …
`continuum.4.log` (1 = newest). `C93_RUN_TAG=<tag>` writes `continuum-<tag>.log` with its own
rotation, so tests and tools running side by side do not rotate each other's logs away.
Every line is also mirrored to stderr (visible in Debug builds, which keep a console).

## Line format

```
22:56:07.357 INFO  Video: renderer 'direct3d11'; layers are composited on the GPU
22:56:07.360 WARN  Audio: no output device (WASAPI: ...); running silent
HH:MM:SS.mmm LEVEL Subsystem: what happened[; consequence]
```

- **Subsystem** is one capitalised word: `Continuum` (app lifecycle), `Machine`, `CPU`,
  `Video`, `Audio`, `Input`, `FS`, `Assembler`, `CCompiler`, `Basic`, `Service`, `SDL`
  (SDL's own messages, routed into our log).
- **INFO**: lifecycle and choices the user may want to know (which renderer, which file booted).
- **WARN**: something degraded but the machine keeps running. Always say the consequence,
  especially for fallbacks: `...; compositing on the CPU`, `...; running silent`.
- **ERROR**: something failed and a feature or the machine stopped:
  `CPU: illegal instruction at 0x00A3F0; machine halted`.
- Addresses are `0x` + six hex digits. Paths are absolute. No stack traces, no dumps:
  a line must make sense alone.
- Never log from the per-instruction or per-pixel paths. Log transitions, not states.

## Use

```cpp
#include "log.h"
LOGI("Video: renderer '%s'; layers are composited on the GPU", name);
LOGW("Video: vsync unavailable (%s); frames are paced by the event loop", SDL_GetError());
LOGE("CPU: illegal instruction at 0x%06X; machine halted", addr);
```
