// Host input as the guest sees it, shared by the main thread (SDL events, main.cpp) and the CPU
// thread (INT 2, ints_input.cpp). One small mutex: each side holds it only to copy a few bytes.
// The values are already in the reference's terms: MonoGame Keys codes, machine pixels, and
// gamepads as MonoGame's GamePad.GetState / GetCapabilities report them.
#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace c93 {

struct Machine;

// One gamepad as GamePad.GetState(i) returns it: dead zones applied, sticks Y-up, all in -1..1 / 0..1.
enum : uint16_t {
    kPadUp = 1 << 0, kPadDown = 1 << 1, kPadLeft = 1 << 2, kPadRight = 1 << 3,
    kPadA = 1 << 4, kPadB = 1 << 5, kPadX = 1 << 6, kPadY = 1 << 7,
    kPadLB = 1 << 8, kPadRB = 1 << 9, kPadStart = 1 << 10, kPadBack = 1 << 11, kPadBig = 1 << 12,
    kPadLStick = 1 << 13, kPadRStick = 1 << 14,
};
struct PadState {
    bool connected = false;
    uint16_t buttons = 0;                  // kPad* bits
    float lx = 0, ly = 0, rx = 0, ry = 0;  // thumbsticks
    float lt = 0, rt = 0;                  // triggers
};
// GamePadCapabilities, already as the guest's three "has" bytes plus the type byte.
struct PadCaps {
    bool connected = false;
    uint8_t has[3]{};
    uint8_t type = 0;
    std::string name;   // UTF-8, as SDL gives it
};

// MonoGame's SDL backend (GamePad.SDL.cs), minus SDL itself so the self-test can check them.
enum { kAxLX, kAxLY, kAxRX, kAxRY, kAxLT, kAxRT };
PadState padFromSdl(const int16_t axes[6], uint16_t buttons);            // PlatformGetState
PadCaps padCapsFromMapping(const char* mapping, bool rumble, const char* name);   // PlatformGetCapabilities

struct Input {
    std::mutex mu;                 // guards everything below
    bool keys[256]{};              // KeyboardStateExt.state, indexed by Keys code
    std::vector<uint16_t> chars;   // InterruptCallbacks.CharBuffer (UTF-16 units, as C# chars)
    uint16_t mouseX = 0, mouseY = 0;
    uint8_t mouseButtons = 0;      // bit 0 left, 1 right, 2 middle
    PadState pads[4];
    PadCaps caps[4];               // refreshed only when a slot connects (reference)
    uint32_t keyboardCallback = 0xFFFFFF, mouseCallback = 0xFFFFFF, controllerCallback = 0xFFFFFF;
};
extern Input gInput;

// Main thread entry points (the self-test injects through the same calls).
void inputKey(uint8_t key, bool down);    // KeyboardStateExt.RegisterKeyDown / RegisterKeyUp
void inputChar(Machine& m, uint16_t c);   // InterruptsInput.TriggerKeyboardHandler

// Next code point of a UTF-8 string (U+FFFD for a malformed byte); advances p.
inline uint32_t utf8Next(const char*& p) {
    auto b = [&](int i) { return uint8_t(p[i]); };
    uint32_t c = b(0);
    int n = c < 0x80 ? 1 : c >> 5 == 6 ? 2 : c >> 4 == 14 ? 3 : c >> 3 == 30 ? 4 : 0;
    if (!n) { ++p; return 0xFFFD; }
    if (n > 1) c &= 0x3F >> (n - 1);
    for (int i = 1; i < n; ++i) {
        if ((b(i) & 0xC0) != 0x80) { p += i; return 0xFFFD; }
        c = c << 6 | (b(i) & 0x3F);
    }
    p += n;
    return c;
}

} // namespace c93
