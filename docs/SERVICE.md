# Service mode (F1): how it is built and how to add a tool window

Faithful recreation of the reference's ServiceModule (D-2), drawn with the SDL renderer at host
resolution. Same fonts, theme, layout file, chrome, keys and quirks; verified pixel for pixel against
the running reference (phase 8, wave 1).

## Files (`src/service/`)

| File | Ports | What |
|------|-------|------|
| `service.h/.cpp` | Service, ServiceState, Controls/ServiceInput, ServiceGraphics, ServiceLayoutManager, UI/EmulatorWindow, InputMouse (service part) | F1 toggle, key routing, zoom animation, `layout.json`, the Emulator window, guest mouse mapping. The only header main.cpp includes. |
| `ui.h/.cpp` | ServiceFont, Fonts, Themes/* | colours, fonts, theme, drawing helpers, the framework API below |
| `window.cpp` | UI/Window, UI/WindowManager, UI/Taskbar, UI/TextWindow | chrome, drag/snap/resize, focus, popups routing, taskbar |
| `json.h/.cpp` | System.Text.Json (the part used) | tiny reader/writer |
| `disasm.h/.cpp` | CodeAnalysis/Debugger, DInstruction, DArg, DUtils, DFormat | disassembler, text identical to the reference (`conformance --disasm`) |
| `state.h/.cpp` | Parsers/CPUState, Disassembled, StepHistory, RegistryUtils, Operations | snapshots for the windows; step/continue/toggle |
| `win_disassembler.cpp`, `win_registers.cpp`, `win_flags.cpp`, `win_status.cpp` | UI/DisassemblerWindow, RegisterWindow, FlagWindow, StatusBarWindow | the wave-1 tool windows; copy their shape |
| `win_floatregs.cpp`, `win_stacks.cpp`, `win_palettes.cpp` (+ `video_parser.h`), `win_3dview.cpp` | UI/FloatRegWindow, StacksWindow + Parsers/Stacks, PalettesWindow + Parsers/Video, VirtualScreen3DWindow | wave 2: identical pixels except 3D VIEW's rocking angle (time based) and a few line/edge pixels |
| `win_memory.cpp`, `win_memorymap.cpp` | UI/MemoryWindow + MemoryHoverPopup + Parsers/Memory, UI/MemoryMapWindow + MemoryMapHoverPopup | wave 2: identical pixels; the map reads `Machine::consumeActivity` |
| `cpu_tracked.cpp` | RAM/MemoryActivityTracker (the CPU side) | the instruction loop while service mode is open (D-26) |
| `editor.h` | the text editor ImmediateWindow and WatcherWindow each carry | lines, caret, keys, scrolling, drawing; both windows derive from `EditorWindow` |
| `win_immediate.cpp`, `immediate.h` | UI/ImmediateWindow | assembly snippets; Ctrl+Enter assembles them at 0xC00000 and runs them (`conformance --immediate`) |
| `win_watcher.cpp`, `watch.h` | UI/WatcherWindow | conditions that stop the CPU; their language and evaluation (`conformance --watch`) |

The debugger itself (DebugState) lives in `Machine` (`machine.h`): `stepByStep`, `moveNext`, `debugSet()`,
`debugTrap()`. Step-by-step arms `Machine::kTrapBit` in `callbacksPending`, the one check the run loop
already makes, so the loop pays nothing while the debugger is off; the stopped CPU sleeps on
`debugSignal` (C++20 atomic wait) with `parked` set. `DEBUG` sets `debugRequest`; the main thread opens service mode.

The WATCHER checks its conditions after every instruction, as the reference did, through the same check:
while service mode is on and the text holds conditions, `Machine::kWatchBit` is armed and `serveCallbacks`
calls `watchHook` (the list is parsed on the main thread when the text changes and handed over through an
atomic pointer). Nothing runs when it is off; armed, `conformance --bench "A == 1"` runs at ~55% speed (D-27).
The IMMEDIATE window runs its snippet on the main thread only while the CPU is `parked` in `debugTrap`
(it stops it there first when the machine runs).

The MEMORY MAP's read/write tracking is armed by F1 (`Machine::kTrackBit`): `serveCallbacks` then hands the CPU to a
second copy of the instruction tables (`cpu_tracked.cpp`: `src/cpu_ops.inc` built against `TrackedMachine`,
whose fetch/rd/wr also mark 256-byte pages) until F1 leaves service mode (D-26). That loop calls
`serveCallbacks` whenever any other bit is set, so the trap and the WATCHER work there too. Bits:
kTrapBit 31, kTrackBit 30, kWatchBit 29 (`kModeBits`); the low bits count input callbacks.

## Writing a tool window

One window = one new file `src/service/win_<name>.cpp`. CMake globs `src/service/*.cpp`, so no shared
file changes. Port the C# line by line; the framework keeps the C# names.

```cpp
// UI/FooWindow.cs: what it shows.
#include "service/state.h"
#include "service/ui.h"

namespace c93::svc {
namespace {
class FooWindow : public Window {
public:
    // The arguments ServiceGraphics.Initialize passes: title, x, y, width, height, spawn delay, canResize, canClose.
    FooWindow() : Window("FOO", 1160, 16, 300, 300, 0.2f, true, false) {}
protected:
    void UpdateContent(float dt) override { /* per frame, when visible and not spawn-delayed */ }
    void DrawContent(const Rect& c) override {
        DrawText(*theme.PrimaryFont, "Hello", c.X + Padding, c.Y + Padding, c.Width - Padding * 2,
                 theme.TextTitle, theme.TextOutline, DrawOutline, 0xFF);
    }
};
} // namespace
C93_SERVICE_WINDOW(40, FooWindow);   // creation order, see the table below
} // namespace c93::svc
```

### Hooks (virtuals of `svc::Window`)
| C# | C++ |
|----|-----|
| `UpdateContent(GameTime)` | `UpdateContent(float dt)` (seconds) |
| `DrawContent(SpriteBatch, Rectangle)` | `DrawContent(const Rect&)`; already clipped to the content rect inset by 4 px |
| `Update(GameTime)` override | `Update(float dt)`; call `Window::Update(dt)` first, as the C# called `base.Update` |
| `HandleInput(mouse, prev)` | same, `MouseState`; return true when it captured the mouse |
| `OnResized()`, `ShouldDrawBackground` | same names |
| `HoverPopup` / `GetHoverPopup()` | `Window* HoverPopup()`: return the popup (a `Window`, owned by the window). WindowManager gives a visible popup under the mouse all input, updates it and draws it on top. |
| `MemoryWindow.DrawHoverPopup()` called from ServiceGraphics | `DrawOverlay()`: after everything |
| ImmediateText / WatcherText in layout.json | `LayoutTextKey()` returns `"ImmediateText"` or `"WatcherText"`, plus `GetText()` / `SetText()` |
| (none: CheckConditionsSynchronously needed it) | `Tick()`: every frame, visible or not, in service mode or not |

Members keep the C# names: `X, Y, Width, Height, Visible, IsFocused, IsOnTop, Title, Manager`,
`Bounds(), TitleBarRect(), ContentRect()`, constants `Padding, TitleBarHeight, BorderWidth...`.

### Drawing (SpriteBatch equivalents)
- `fill(Rect, Color)` = `spriteBatch.Draw(pixel, rect, color)`.
- `DrawText(font, text, x, y, maxWidth, color, outline, flags, pattern)` = `ServiceGraphics.DrawText`;
  `font.MeasureText(text, maxWidth, flags)` returns `{width, height, lines}`; flags `Monospace,
  MonospaceCentering, DisableKerning, Center, Wrap, DrawOutline`.
- Fonts: `fonts.ModernDOS_9x15, _10x15, _10x16, _12x18, _12x18_thin`; `*theme.PrimaryFont` (12x18 thin).
- Colours: `theme.<C# name>` (all Theme.cs fields, defaults and JSON overrides), `lerp(a, b, t)` =
  `Color.Lerp`, `colors::Black/White/Transparent`. Colours are straight RGBA; the blend is premultiplied
  like SpriteBatch's AlphaBlend, so `{80,160,255,22}` adds, exactly as in the reference.
- Anything else (textures, palettes, 3D): `renderer()` is the SDL renderer; draw with
  `SDL_BLENDMODE_BLEND_PREMULTIPLIED` (or `NONE` for opaque) and nearest scaling. `setClip()` for scissors.
- `screenW()/screenH()` = Viewport size; `intersect()` = `Rectangle.Intersect` (keeps its quirks).

### Input and state
- `mouse()` = `Mouse.GetState()` this frame (`X, Y, Left, Right, Middle, ScrollWheelValue`). Windows that
  compare with the previous state keep their own copy, as in the C#.
- `keyDown(SDLK_x)`, `keyPressed(SDLK_x)` = `InputKeyboard` current/previous states (Keys.A -> SDLK_A).
  The guest gets no key downs or text while service mode is open (D-23).
- `machine()` is the live `Machine` (read freely; the CPU thread may run). `cpuState`, `disassembled`,
  `stepHistory`, `ops::*`, `regName/regHex` in `state.h`. `serviceMode()`, `stepByStep()`, `saveLayout()`.
- Log as `Service: ...` (docs/LOGGING.md); never per frame.

## Creation order (`C93_SERVICE_WINDOW` numbers)

Initial z-order and taskbar order are the reference's creation order; layout.json matches windows by title.

| Order | Window (title) | C# | Status |
|------:|----------------|----|--------|
| — | Emulator | EmulatorWindow | wave 1, built into service.cpp (always first) |
| 10 | IMMEDIATE | ImmediateWindow (420x220 at 1160,560, delay 0.15, resize, no close) | wave 2, done |
| 20 | DISASSEMBLER | DisassemblerWindow | wave 1 |
| 30 | REGISTERS | RegisterWindow | wave 1 |
| 40 | FLOAT REGISTERS | FloatRegWindow | wave 2, done |
| 50 | FLAGS | FlagWindow | wave 1 |
| 60 | STACKS | StacksWindow | wave 2, done |
| 70 | MEMORY | MemoryWindow | wave 2, done |
| 80 | MEMORY MAP | MemoryMapWindow | wave 2, done |
| 90 | STATUS | StatusBarWindow | wave 1 |
| 100 | PALETTES | PalettesWindow | wave 2, done |
| 110 | 3D VIEW | VirtualScreen3DWindow | wave 2, done |
| 120 | WATCHER | WatcherWindow | wave 2, done |

## Wave 2: done (phase 8)

| Work | File(s) | Result |
|------|---------|--------|
| MemoryWindow + MemoryHoverPopup + Parsers/Memory | `win_memory.cpp` | identical pixels; `DrawOverlay()` second popup pass like the C# |
| MemoryMapWindow + MemoryMapHoverPopup | `win_memorymap.cpp`, `cpu_tracked.cpp`, `src/cpu_ops.inc` | identical pixels incl. read/write colours while running; tracking armed only in service mode (D-26) |
| StacksWindow + Parsers/Stacks | `win_stacks.cpp` | done |
| FloatRegWindow | `win_floatregs.cpp` | done; values as .NET's float.ToString() (checked on 300k floats) |
| PalettesWindow + Parsers/Video | `win_palettes.cpp` | done; Parsers/Video is `video_parser.h` |
| VirtualScreen3DWindow ("3D VIEW"); Video3DWindow | `win_3dview.cpp` | done: XNA matrices on the CPU, quads cut into grids for `SDL_RenderGeometry`, frustum clipping, GPU-rule lines; same starfield (System.Random(42)). Video3DWindow not ported (never created) |
| WatcherWindow | `win_watcher.cpp`, `watch.h`, `editor.h` | done: `kWatchBit` + `watchHook` in `serveCallbacks`; `conformance --watch` 32,476/32,476 vs `oracle --watch`; screenshots identical (D-27) |
| ImmediateWindow | `win_immediate.cpp`, `immediate.h`, `editor.h` | done: `conformance --immediate` 74/74 (+10 where the reference threw: same machine state); screenshots identical (D-27) |
| DisassemblerHoverPopup (+ ASMMetaInfo docs) | `popup_disassembler.cpp`, `asm_meta.inc` | done |
| RegisterHoverPopup | `popup_registers.cpp` | done |
| HistoryHoverPopup (StepHistory viewer) | `popup_history.cpp` | done |

Popups are `Window` subclasses (as in the C#); their classes are declared in `popups.h` or next to the owner.
