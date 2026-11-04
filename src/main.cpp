// Continuum 93 entry point: window, event loop, and the CPU thread.
//
// Threads: the main thread owns SDL (events, presenting); the machine runs free on its own
// thread, as in the original. The presenter reads RAM while the CPU writes it: torn frames
// are possible and accepted, the same trade the original made.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <thread>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "audio.h"
#include "input.h"
#include "ints.h"
#include "log.h"
#include "machine.h"
#include "service/service.h"
#include "tools.h"
#include "video.h"

using namespace c93;

namespace {

// SDL's own diagnostics go into our log under the "SDL" subsystem.
void sdlLog(void*, int, SDL_LogPriority prio, const char* msg) {
    if (prio >= SDL_LOG_PRIORITY_ERROR) LOGE("SDL: %s", msg);
    else if (prio >= SDL_LOG_PRIORITY_WARN) LOGW("SDL: %s", msg);
    else LOGI("SDL: %s", msg);
}

// A PAL-style test card shown when no program is loaded: proves palettes, layer order and
// transparency on whichever renderer was picked.
void drawTestCard(Machine& m) {
    uint8_t* bottom = m.ram + m.pageAddr(0);   // page 0 -> lowest layer with 2 pages
    uint8_t* top = m.ram + m.pageAddr(1);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            int cx = (x - 112) / 16, cy = (y - 7) / 16;   // 16x16 swatches of all 256 colours
            bool grid = x >= 112 && x < 368 && y >= 7 && y < 263;
            bottom[y * kW + x] = uint8_t(grid ? cy * 16 + cx : ((x / 8 + y / 8) & 1) ? 0xFF : 0xDA);
            bool frame = x < 2 || y < 2 || x >= kW - 2 || y >= kH - 2 || x == kW / 2 || y == kH / 2;
            top[y * kW + x] = frame ? 1 : 0;   // colour 1 (near white); 0 lets the bottom show
        }
}

// --- Input ---------------------------------------------------------------------------------
// The reference ran on MonoGame DesktopGL (SDL2), whose KeyboardUtil.SDL.cs maps SDL *keycodes*
// (layout-dependent, unshifted) to Keys; the numbers below are Keys values = Windows VK codes.
// Anything else is Keys.None (0), which the reference registers as key 0.
uint8_t toKeys(SDL_Keycode k) {
    if (k >= SDLK_A && k <= SDLK_Z) return uint8_t(65 + (k - SDLK_A));
    if (k >= SDLK_0 && k <= SDLK_9) return uint8_t(48 + (k - SDLK_0));
    if (k >= SDLK_F1 && k <= SDLK_F12) return uint8_t(112 + (k - SDLK_F1));
    if (k >= SDLK_F13 && k <= SDLK_F24) return uint8_t(124 + (k - SDLK_F13));
    if (k >= SDLK_KP_1 && k <= SDLK_KP_9) return uint8_t(97 + (k - SDLK_KP_1));
    switch (k) {
    case SDLK_BACKSPACE: return 8;      case SDLK_TAB: return 9;            case SDLK_RETURN: return 13;
    case SDLK_ESCAPE: return 27;        case SDLK_SPACE: return 32;         case SDLK_APOSTROPHE: return 222;
    case SDLK_PLUS: return 107;         case SDLK_COMMA: return 188;        case SDLK_MINUS: return 189;
    case SDLK_PERIOD: return 190;       case SDLK_SLASH: return 191;        case SDLK_SEMICOLON: return 186;
    case SDLK_LESS: return 226;         case SDLK_EQUALS: return 187;       case SDLK_LEFTBRACKET: return 219;
    case SDLK_BACKSLASH: return 220;    case SDLK_RIGHTBRACKET: return 221; case SDLK_GRAVE: return 192;
    case SDLK_DELETE: return 46;        case SDLK_CAPSLOCK: return 20;      case SDLK_PRINTSCREEN: return 44;
    case SDLK_SCROLLLOCK: return 145;   case SDLK_PAUSE: return 19;         case SDLK_INSERT: return 45;
    case SDLK_HOME: return 36;          case SDLK_PAGEUP: return 33;        case SDLK_END: return 35;
    case SDLK_PAGEDOWN: return 34;      case SDLK_RIGHT: return 39;         case SDLK_LEFT: return 37;
    case SDLK_DOWN: return 40;          case SDLK_UP: return 38;            case SDLK_NUMLOCKCLEAR: return 144;
    case SDLK_KP_DIVIDE: return 111;    case SDLK_KP_MULTIPLY: return 106;  case SDLK_KP_MINUS: return 109;
    case SDLK_KP_PLUS: return 107;      case SDLK_KP_ENTER: return 13;      case SDLK_KP_0: return 96;
    case SDLK_KP_PERIOD: return 110;    case SDLK_KP_DECIMAL: return 110;   case SDLK_APPLICATION: return 93;
    case SDLK_MUTE: return 173;         case SDLK_VOLUMEUP: return 175;     case SDLK_VOLUMEDOWN: return 174;
    case SDLK_KP_CLEAR: return 254;     case SDLK_LCTRL: return 162;        case SDLK_LSHIFT: return 160;
    case SDLK_LALT: return 164;         case SDLK_LGUI: return 91;          case SDLK_RCTRL: return 163;
    case SDLK_RSHIFT: return 161;       case SDLK_RALT: return 165;         case SDLK_RGUI: return 92;
    case SDLK_MEDIA_NEXT_TRACK: return 176; case SDLK_MEDIA_PREVIOUS_TRACK: return 177; case SDLK_MEDIA_STOP: return 178;
    case SDLK_MEDIA_PLAY: return 179;   case SDLK_MEDIA_PLAY_PAUSE: return 179; case SDLK_MEDIA_SELECT: return 181;
    case SDLK_AC_SEARCH: return 170;    case SDLK_AC_HOME: return 172;      case SDLK_AC_BACK: return 166;
    case SDLK_AC_FORWARD: return 167;   case SDLK_AC_STOP: return 169;      case SDLK_AC_REFRESH: return 168;
    case SDLK_AC_BOOKMARKS: return 171; case SDLK_SLEEP: return 95;
    default: return 0;
    }
}

// C# (ushort)(float) on x64: truncate to int32 (out of range -> 0x80000000), keep the low 16 bits.
uint16_t toUshort(float v) { return uint16_t(v > -2147483648.0f && v < 2147483648.0f ? int32_t(v) : INT32_MIN); }

SDL_Gamepad* gPads[16];   // MonoGame's slots: a new pad takes the lowest free one; the guest sees 0..3

void padAdded(SDL_JoystickID id) {
    int slot = 0;
    while (slot < 16 && gPads[slot]) ++slot;
    SDL_Gamepad* p = slot < 16 ? SDL_OpenGamepad(id) : nullptr;
    if (!p) { LOGW("Input: gamepad %u could not be opened (%s); ignored", unsigned(id), slot < 16 ? SDL_GetError() : "all 16 slots taken"); return; }
    gPads[slot] = p;
    LOGI("Input: gamepad '%s' connected (slot %d)", SDL_GetGamepadName(p), slot);
}
void padRemoved(SDL_JoystickID id) {
    for (int slot = 0; slot < 16; ++slot)
        if (gPads[slot] && SDL_GetGamepadID(gPads[slot]) == id) {
            LOGI("Input: gamepad '%s' disconnected (slot %d)", SDL_GetGamepadName(gPads[slot]), slot);
            SDL_CloseGamepad(gPads[slot]);
            gPads[slot] = nullptr;
        }
}

// Once per frame, as the reference's Update(): the global mouse (Mouse.GetState is global and
// keeps tracking outside the window) and gamepads 0..3.
void pollInput(SDL_Window* window, SDL_Renderer* renderer) {
    float gx, gy, lx, ly;
    int wx = 0, wy = 0;
#ifdef __EMSCRIPTEN__
    // A page has no "outside the window"; its global mouse is in page units that miss the canvas scaling.
    SDL_MouseButtonFlags mb = SDL_GetMouseState(&gx, &gy);
    (void)window;
#else
    SDL_MouseButtonFlags mb = SDL_GetGlobalMouseState(&gx, &gy);
    SDL_GetWindowPosition(window, &wx, &wy);
#endif
    // Letterbox-aware; the reference divided by window/480 because it kept the window 16:9.
    SDL_RenderCoordinatesFromWindow(renderer, gx - float(wx), gy - float(wy), &lx, &ly);
    int lw, lh;   // screenAspect makes the canvas 480 x lh: back to the guest's 270 lines
    if (SDL_GetRenderLogicalPresentation(renderer, &lw, &lh, nullptr) && lh > 0) ly *= float(kH) / float(lh);

    static const SDL_GamepadButton kButtons[15] = {   // kPad* bit order
        SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
        SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
        SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_START,
        SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_GUIDE, SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK};
    static bool was[4];
    PadState pads[4];
    PadCaps caps[4];
    bool fresh[4]{};
    for (int i = 0; i < 4; ++i) {
        if (SDL_Gamepad* p = gPads[i]) {
            int16_t ax[6];   // SDL_GamepadAxis order is kAx* order
            for (int a = 0; a < 6; ++a) ax[a] = SDL_GetGamepadAxis(p, SDL_GamepadAxis(a));
            uint16_t b = 0;
            for (int k = 0; k < 15; ++k) b |= uint16_t(SDL_GetGamepadButton(p, kButtons[k]) << k);
            pads[i] = padFromSdl(ax, b);
            if (!was[i]) {   // capabilities are read only when a slot connects, as the reference
                char* map = SDL_GetGamepadMapping(p);
                bool rumble = SDL_GetBooleanProperty(SDL_GetGamepadProperties(p), SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
                if (map) { caps[i] = padCapsFromMapping(map, rumble, SDL_GetGamepadName(p)); fresh[i] = true; }
                else LOGW("Input: gamepad in slot %d has no mapping; its capabilities are not updated", i);
                SDL_free(map);
            }
        }
        was[i] = pads[i].connected;
    }

    // The service view maps the mouse into its Emulator window (InputMouse.ReadMachineMouse).
    float density = SDL_GetWindowPixelDensity(window);
    uint16_t sx = 0, sy = 0;
    int svc = service::guestMouse((gx - float(wx)) * density, (gy - float(wy)) * density, sx, sy);

    std::lock_guard l(gInput.mu);
    gInput.mouseX = svc < 0 ? toUshort(lx) : svc ? sx : gInput.mouseX;
    gInput.mouseY = svc < 0 ? toUshort(ly) : svc ? sy : gInput.mouseY;
    gInput.mouseButtons = svc == 0 ? 0 : uint8_t((mb & SDL_BUTTON_LMASK ? 1 : 0) | (mb & SDL_BUTTON_RMASK ? 2 : 0) | (mb & SDL_BUTTON_MMASK ? 4 : 0));
    for (int i = 0; i < 4; ++i) {
        gInput.pads[i] = pads[i];
        if (fresh[i]) gInput.caps[i] = std::move(caps[i]);   // kept after a disconnect (reference)
    }
}

// Data/init.cfg: "key=value" lines, '#' comments (Settings/SettingsManager.cs). Written with
// the reference's defaults when missing. enableDebugging/serverPort are kept for compatibility
// but unused until D-3 is decided.
struct Settings {
    std::map<std::string, std::string> kv;
    bool flag(const char* k) const { auto i = kv.find(k); return i != kv.end() && i->second == "true"; }
    std::string str(const char* k) const { auto i = kv.find(k); return i == kv.end() ? std::string() : i->second; }
};
Settings loadSettings(const std::filesystem::path& data) {
    auto path = data / "init.cfg";
    // The reference default file, verbatim (raw string: the paths keep their backslashes), plus crtFilter.
    static const char* kDefaults = R"(# This provides the configuration to be loaded when Continuum starts.

# Controls whether the emulator enables network communication with any Continuum Tools listening.
enableDebugging=true

# Attempts to start in fullscreen. Usually this would be succesful.
fullscreen=false

# Disables the mouse API (some linux based OSs need this for Continuum to run).
disableMouse=false

# Shows the screen as a CRT monitor would: curved glass, scanlines, phosphor mask (true or false).
crtFilter=true

# The physical shape of the screen: auto (square pixels), 4:3, 5:4, 16:10, 16:9 or 21:9. For screen modes
# the monitor stretches (a 4:3 mode on a wide panel). Shift+F11 cycles it and saves it here.
screenAspect=auto

# Points to the source file (.asm or .c) that will be automatically loaded and ran on boot.
# The default is the dual-boot selector (Halcyon or Q OS); use Data\os\q.asm to boot Q OS directly.
bootProgram=Data\os\boot\boot.c

# The port for the Continuum Tools to connect through.
serverPort=21098

# Enable logging events
logging=true
)";
    std::stringstream in;
    if (std::ifstream f(path); f) in << f.rdbuf();
    else if (!std::filesystem::exists(path) && std::ofstream(path) << kDefaults) { LOGI("Settings: created %s with defaults", path.string().c_str()); in << kDefaults; }
    else { LOGW("Settings: cannot read or create %s; using the defaults", path.string().c_str()); in << kDefaults; }
    Settings s;
    for (std::string line; std::getline(in, line);) {
        while (!line.empty() && (line.back() == 13 || line.back() == ' ')) line.pop_back();   // CRLF files
        size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
        s.kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return s;
}

// Rewrites one key of init.cfg in place (appended if missing); the rest of the file is kept as written.
void saveSetting(const std::filesystem::path& data, const std::string& key, const std::string& value) {
    auto path = data / "init.cfg";
    std::ifstream f(path);
    std::string out, line;
    bool done = false;
    while (std::getline(f, line)) {
        if (line.rfind(key + "=", 0) == 0) { line = key + "=" + value + (line.back() == 13 ? "\r" : ""); done = true; }
        out += line + "\n";
    }
    if (!done) out += key + "=" + value + "\n";
    f.close();
    if (!(std::ofstream(path, std::ios::binary) << out)) LOGW("Settings: cannot write %s; %s not saved", path.string().c_str(), key.c_str());
}

// screenAspect values, in Shift+F11 order; "auto" means square pixels.
const char* const kAspects[] = {"auto", "4:3", "5:4", "16:10", "16:9", "21:9"};
float aspectValue(const std::string& s) {
    float a = 0, b = 0;
    return std::sscanf(s.c_str(), "%f:%f", &a, &b) == 2 && a > 0 && b > 0 ? a / b : 0;
}

} // namespace

int main(int argc, char** argv) {
    log::open();
    LOGI("Continuum: version %s (C++/SDL %d.%d.%d), log at %s", C93_VERSION,
         SDL_VERSIONNUM_MAJOR(SDL_GetVersion()), SDL_VERSIONNUM_MINOR(SDL_GetVersion()),
         SDL_VERSIONNUM_MICRO(SDL_GetVersion()), log::path());
    SDL_SetLogOutputFunction(sdlLog, nullptr);
    if (int code = runTool(argc, argv); code >= 0) { log::close(); return code; }   // tools need no window

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        LOGE("Continuum: SDL init failed (%s)", SDL_GetError());
        return 1;
    }
    // Data/ sits next to the executable (inside the bundle on macOS); SDL knows where that is.
    std::filesystem::path data = std::filesystem::path(SDL_GetBasePath()) / "Data";
    if (!std::filesystem::exists(data)) {
        LOGW("Continuum: no Data folder at %s; using ./Data", data.string().c_str());
        data = "Data";
    }
    gFsRoot = (data / "filesystem").string();
    Settings settings = loadSettings(data);
    // Static from here on: on the web main() returns to the browser and the frame callback outlives its stack.
    static const bool mouseEnabled = !settings.flag("disableMouse");

    static SDL_Window* window = SDL_CreateWindow("Continuum 93", kW * 3, kH * 3,
                                                 SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                                 SDL_WINDOW_FILL_DOCUMENT |   // web: follow the page; elsewhere SDL drops it
                                                 (settings.flag("fullscreen") ? SDL_WINDOW_FULLSCREEN : 0));
    static Video video;
    if (!window || !video.init(window)) {
        LOGE("Continuum: no window (%s)", SDL_GetError());
        return 1;
    }
    if (settings.str("crtFilter") != "false") video.setCrt(true);   // on unless turned off (D-34)
    static const std::filesystem::path dataDir = data;
    static std::string aspect = settings.str("screenAspect");
    video.setAspect(aspectValue(aspect));
#ifndef _WIN32   // Windows takes the multi-size icon from the exe's resources (continuum.rc)
    static const uint8_t kIconPng[] = {
#include "icon_png.inc"
    };
    if (SDL_Surface* icon = SDL_LoadPNG_IO(SDL_IOFromConstMem(kIconPng, sizeof kIconPng), true)) {
        SDL_SetWindowIcon(window, icon);
        SDL_DestroySurface(icon);
    }
#endif

    audioInit();
    audioPlayInitializationSounds();   // Computer(initWithAudio: true) beeps before the boot program loads
    static auto machine = std::make_unique<TrackedMachine>();   // ~30 MB: heap, never the stack; Tracked: D-26
    machine->reset();

    // Boot (Run/Runner.cs): assemble the boot program, start at its first block. The setting is
    // relative to the exe folder and may use Windows separators.
    std::string boot = settings.str("bootProgram");
    for (char& c : boot) if (c == 92) c = '/';
    std::filesystem::path bootPath = data.parent_path() / boot;
    // C boot programs (the dual-boot selector) are compiled first; their includes resolve as in Runner.cs
    Loaded l = boot.empty() ? Loaded{} : bootPath.extension() == ".c" ? compileCInto(*machine, bootPath.string(), true, "")
                                                                      : assembleInto(*machine, bootPath.string());
    static std::thread cpu;
    // Sample host input once before the guest runs: its first mouse read must be the real position,
    // or the boot menu sees (0,0) -> pointer as "movement", selects a card and cancels its countdown.
    pollInput(window, video.renderer());
    if (l.loaded) {
        machine->ip = l.firstBlock;
        machine->running = true;
        cpu = std::thread([m = machine.get()] { m->run(); LOGI("Machine: halted at 0x%06X", m->ip & kRamMask); });
        LOGI("Machine: booted %s at 0x%06X", bootPath.string().c_str(), l.firstBlock);
    } else {
        drawTestCard(*machine);
        LOGW("Machine: no boot program could be loaded (%s); showing the test card", bootPath.string().c_str());
    }

    service::init(window, video, *machine, data);
    // Continuum.cs: the pointer shows when the guest wants it (INT 0 0x11) or the service view is up.
    static bool guestCursor = true, cursorShown = true;
    if (!mouseEnabled) { SDL_HideCursor(); cursorShown = false; }   // disableMouse also hid the pointer in the reference
    SDL_StartTextInput(window);   // MonoGame's SDL2 had text input on by default

    // One pass of the host loop; false once the machine shuts down. Captures nothing: a C callback on the web.
    static auto frame = [] {
        bool quit = false;
        for (SDL_Event e; SDL_PollEvent(&e);) {
            if (e.type == SDL_EVENT_QUIT) quit = true;
            else if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
                bool down = e.type == SDL_EVENT_KEY_DOWN;
                // While the service UI is open, typing belongs to it; releases still go through so no key sticks.
                if (!down || service::guestKeys()) inputKey(toKeys(e.key.key), down);
                // MonoGame also sent a key down's keycode as text when it is a control char
                // (Back 8, Tab 9, Enter 13, Esc 27, Del 127...), auto-repeat included.
                uint16_t c = uint16_t(e.key.key);
                if (down && (c < 32 || (c >= 127 && c < 160)) && service::guestKeys()) inputChar(*machine, c);
                if (down && !e.key.repeat && e.key.scancode == SDL_SCANCODE_F11 && (e.key.mod & SDL_KMOD_SHIFT)) {
                    int i = 0;   // the next shape; an unknown value counts as auto (i = 6 wraps to 4:3)
                    while (i < 6 && aspect != kAspects[i]) ++i;
                    aspect = kAspects[(i + 1) % 6];
                    video.setAspect(aspectValue(aspect));
                    saveSetting(dataDir, "screenAspect", aspect);
                } else if (down && !e.key.repeat && (e.key.scancode == SDL_SCANCODE_F11 ||
                    (e.key.scancode == SDL_SCANCODE_RETURN && (e.key.mod & SDL_KMOD_ALT)))) {
                    bool full = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
                    SDL_SetWindowFullscreen(window, !full);
                }
            } else if (e.type == SDL_EVENT_TEXT_INPUT && service::guestKeys()) {   // D-23
                for (const char* p = e.text.text; *p;)   // one C# char per code point below U+FFFF
                    if (uint32_t cp = utf8Next(p); cp < 0xFFFF) inputChar(*machine, uint16_t(cp));
            } else if (e.type == SDL_EVENT_GAMEPAD_ADDED) padAdded(e.gdevice.which);
            else if (e.type == SDL_EVENT_GAMEPAD_REMOVED) padRemoved(e.gdevice.which);
            service::event(e);
        }
        // Requests the guest made through INT 0 (fullscreen 0x10, host cursor 0x11, shutdown 0x20).
        if (machine->fullscreenRequest.exchange(false)) {
            bool leaving = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
            SDL_SetWindowFullscreen(window, !leaving);
            if (leaving) SDL_SetWindowSize(window, kW * 2, kH * 2);   // the reference came back at 2x (960x540)
        }
        machine->isFullscreen.store((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0);
        if (int8_t c = machine->hostCursorRequest.exchange(-1); c >= 0) guestCursor = c != 0;
        if (machine->shutdownRequest.load()) quit = true;

        service::update();
        if (bool show = mouseEnabled && (guestCursor || service::view()); show != cursorShown) {
            cursorShown = show;
            show ? SDL_ShowCursor() : SDL_HideCursor();
        }
        pollInput(window, video.renderer());
        service::view() ? service::draw() : video.present(*machine);
        return !quit;
    };
#ifdef __EMSCRIPTEN__
    // The browser owns the loop: a frame per display refresh. Never returns; a guest shutdown just stops drawing.
    LOGI("Continuum: running in the browser");
    emscripten_set_main_loop([] { if (!frame()) { emscripten_cancel_main_loop(); LOGI("Continuum: guest shut down"); } }, 0, true);
#endif
    while (frame()) {}

    machine->running.store(false);
    machine->debugWake();   // a CPU stopped by the debugger sees `running` and leaves
    service::shutdown();
    if (cpu.joinable()) cpu.join();
    audioShutdown();
    video.shutdown();
    SDL_DestroyRenderer(video.renderer());
    SDL_DestroyWindow(window);
    SDL_Quit();
    LOGI("Continuum: shut down cleanly");
    log::close();
    return 0;
}
