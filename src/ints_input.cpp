// The input group (INT 2) and the guest-visible input state behind it.
// (Interrupts/InterruptsInput.cs, InterruptCallbacks.cs, Controls/KeyboardStateExt.cs,
// Controls/InputMouse.cs, Controls/GamepadStateExt.cs, and MonoGame 3.8.1 DesktopGL's
// GamePad.SDL.cs / GamePadThumbSticks.cs / GamePadTriggers.cs, which shaped the values.)
//
// Reference quirks kept:
//  - Any-modifier codes 16/17/18 follow 160..165, but only when a modifier itself changes.
//  - A typed char goes into the buffer twice while a keyboard callback is set, and each one
//    pushes IP (24-bit) on the register stack and jumps to the callback (no overflow check).
//    Mouse and controller callbacks are stored and never called.
//  - Reading a buffered char above 255 throws in the reference (Convert.ToByte): the machine
//    halts and the char stays in the buffer.
//  - The buffer read reports "chars left" after removing one, except at 255+ where it reports
//    the count before removing.
//  - Gamepad capabilities and names are taken when a slot connects and kept after it
//    disconnects; names are ASCII ('?' per non-ASCII char) and cut to 252 chars in proportion.
#include <cstring>

#include "input.h"
#include "ints.h"
#include "log.h"

namespace c93 {

Input gInput;

namespace {

void store(Machine& m, uint32_t a, const uint8_t* src, size_t n) {   // Computer.LoadMemAt
    for (size_t i = 0; i < n; ++i) m.ram[(a + i) & kRamMask] = src[i];
}
uint32_t destOf(Machine& m, uint8_t r) { return m.get<24>(nextReg(r, 1)); }

// --- Keyboard ------------------------------------------------------------------------------
void keysAsBits(Machine& m, uint8_t r) {   // 0x00: 32 bytes, key 0 is bit 7 of byte 0
    uint8_t out[32]{};
    {
        std::lock_guard l(gInput.mu);
        for (int i = 0; i < 256; ++i) out[i >> 3] |= uint8_t(gInput.keys[i] << (7 - (i & 7)));
    }
    store(m, destOf(m, r), out, 32);
    m.set<8>(r, 32);
}
void keysAsCodes(Machine& m, uint8_t r) {   // 0x10: codes of held keys, ascending; r = count
    uint8_t out[256];
    int n = 0;
    {
        std::lock_guard l(gInput.mu);
        for (int i = 0; i < 256; ++i) if (gInput.keys[i]) out[n++] = uint8_t(i);
    }
    store(m, destOf(m, r), out, size_t(n));
    m.set<8>(r, uint8_t(n));
}

void readKeyboardBuffer(Machine& m, uint8_t r) {   // 0x01: r = char, r+1 = chars left
    uint8_t stat = nextReg(r, 1);
    std::lock_guard l(gInput.mu);
    auto& b = gInput.chars;
    if (b.empty()) { m.set<8>(r, 0); m.set<8>(stat, 0); return; }
    if (b[0] > 255) {   // Convert.ToByte throws before anything is written
        LOGE("Input: keyboard buffer holds U+%04X, which does not fit a byte; machine halted", b[0]);
        m.running.store(false, std::memory_order_relaxed);
        return;
    }
    m.set<8>(r, b[0]);
    size_t before = b.size();
    b.erase(b.begin());
    m.set<8>(stat, uint8_t(before == 1 ? 0 : before < 255 ? b.size() : before));
}

void setKeyboardCallback(Machine& m, uint8_t r) { std::lock_guard l(gInput.mu); gInput.keyboardCallback = destOf(m, r); }
void setMouseCallback(Machine& m, uint8_t r) { std::lock_guard l(gInput.mu); gInput.mouseCallback = destOf(m, r); }
void setControllerCallback(Machine& m, uint8_t r) { std::lock_guard l(gInput.mu); gInput.controllerCallback = destOf(m, r); }

// --- Mouse ---------------------------------------------------------------------------------
void readMouse(Machine& m, uint8_t r) {   // 0x03: r+1 = X (16), r+3 = Y (16), r+5 = buttons
    std::lock_guard l(gInput.mu);
    m.set<16>(nextReg(r, 1), gInput.mouseX);
    m.set<16>(nextReg(r, 3), gInput.mouseY);
    m.set<8>(nextReg(r, 5), gInput.mouseButtons);
}

// --- Gamepads ------------------------------------------------------------------------------
uint8_t toByte(float v) { return uint8_t(int32_t((v + 1.0f) * 127.5f)); }   // ConvertControllerFloatToByte
uint8_t toBytePositive(float v) { return uint8_t(int32_t(v * 255.0f)); }    // ...ToBytePositive

void readPads(Machine& m, uint8_t r) {   // 0x14: 41 bytes, GetGamepadsState
    uint8_t out[41]{};
    {
        std::lock_guard l(gInput.mu);
        for (int i = 0; i < 4; ++i) {
            const PadState& p = gInput.pads[i];
            if (!p.connected) continue;
            out[0] |= uint8_t(1 << i);
            uint8_t b1 = uint8_t(p.buttons);   // DPad U D L R, A B X Y: same bit order as kPad*
            uint8_t b2 = uint8_t((p.ly < 0) | (p.ly > 0) << 1 | (p.lx < 0) << 2 | (p.lx > 0) << 3 |
                                 (p.ry < 0) << 4 | (p.ry > 0) << 5 | (p.rx < 0) << 6 | (p.rx > 0) << 7);
            uint8_t b3 = uint8_t((p.buttons >> 8 & 3) | (p.lt > 0) << 2 | (p.rt > 0) << 3 | (p.buttons >> 10 & 7) << 4);
            uint8_t b4 = uint8_t(p.buttons >> 13 & 3);
            if (b1 | b2 | b3 | b4) out[0] |= uint8_t(1 << (4 + i));
            else b3 |= 0x80;                   // "none pressed", set before the summary bits below
            b4 |= uint8_t((b1 & 0xF0 ? 4 : 0) | (b1 & 0x0F ? 8 : 0) | (b3 & 0x03 ? 16 : 0) |
                          (b3 & 0x0C ? 32 : 0) | (b2 & 0x0F ? 64 : 0) | (b2 & 0xF0 ? 128 : 0));
            uint8_t* o = out + 1 + i * 10;
            o[0] = b1; o[1] = b2; o[2] = b3; o[3] = b4;
            o[4] = toBytePositive(p.lt); o[5] = toBytePositive(p.rt);
            o[6] = toByte(p.lx); o[7] = toByte(p.ly); o[8] = toByte(p.rx); o[9] = toByte(p.ry);
        }
    }
    store(m, destOf(m, r), out, sizeof out);
}

void readPadCaps(Machine& m, uint8_t r) {   // 0x15: 17 bytes, GetGamepadsCapabilities
    uint8_t out[17]{};
    {
        std::lock_guard l(gInput.mu);
        for (int i = 0; i < 4; ++i) {
            const PadCaps& c = gInput.caps[i];
            out[0] |= uint8_t(c.connected << i);
            std::memcpy(out + 1 + i * 4, c.has, 3);
            out[4 + i * 4] = c.type;   // BigButtonPad -> 128 in the reference; SDL pads are never that type
        }
    }
    store(m, destOf(m, r), out, sizeof out);
}

// 0x16: the four names as ASCII, each '\0'-terminated; r = byte count (256 wraps to 0).
// Lengths are C# string lengths (UTF-16 units); past 252 in all, each is cut in proportion.
void readPadNames(Machine& m, uint8_t r) {
    std::u16string names[4];
    {
        std::lock_guard l(gInput.mu);
        for (int i = 0; i < 4; ++i) {
            if (!gInput.caps[i].connected) continue;
            for (const char* p = gInput.caps[i].name.c_str(); *p;) {
                uint32_t c = utf8Next(p);
                if (c < 0x10000) names[i] += char16_t(c);
                else { c -= 0x10000; names[i] += char16_t(0xD800 + (c >> 10)); names[i] += char16_t(0xDC00 + (c & 0x3FF)); }
            }
        }
    }
    size_t total = 0;
    for (auto& n : names) total += n.size();
    uint8_t out[260];
    size_t len = 0;
    for (auto& n : names) {
        size_t keep = total > 252 ? n.size() * 252 / total : n.size();
        for (size_t k = 0; k < keep; ++k) {   // Encoding.ASCII: one '?' per non-ASCII char or surrogate pair
            char16_t u = n[k];
            if (u >= 0xD800 && u < 0xDC00 && k + 1 < keep && n[k + 1] >= 0xDC00 && n[k + 1] < 0xE000) ++k;
            out[len++] = u < 0x80 ? uint8_t(u) : '?';
        }
        out[len++] = 0;
    }
    m.set<8>(r, uint8_t(len));
    store(m, destOf(m, r), out, len);
}

} // namespace

// --- Host side -------------------------------------------------------------------------------
void inputKey(uint8_t key, bool down) {
    std::lock_guard l(gInput.mu);
    bool* s = gInput.keys;
    s[key] = down;
    if (key == 160 || key == 161) s[16] = s[160] || s[161];        // any Shift
    else if (key == 162 || key == 163) s[17] = s[162] || s[163];   // any Ctrl
    else if (key == 164 || key == 165) s[18] = s[164] || s[165];   // any Alt
}

void inputChar(Machine& m, uint16_t c) {
    std::lock_guard l(gInput.mu);
    gInput.chars.push_back(c);
    if (gInput.keyboardCallback == 0xFFFFFF) return;
    gInput.chars.push_back(c);
    // The reference rewrote IP from the UI thread; here the CPU does it between instructions.
    m.callbackTarget.store(gInput.keyboardCallback, std::memory_order_relaxed);
    m.callbacksPending.fetch_add(1, std::memory_order_release);
}

bool Machine::serveCallbacks() {
    uint32_t p = callbacksPending.load(std::memory_order_acquire);
    if (p & kTrackBit) [[unlikely]] { trackedLoop(*this); return true; }   // out of run(): its loop stays as it was
    servePending(p);
    return false;
}

void Machine::servePending(uint32_t p) {
    if (p & ~kModeBits) {   // no read-modify-write per instruction while only the mode bits (trap/track/watch) are armed
        uint32_t n = callbacksPending.fetch_and(kModeBits, std::memory_order_acquire) & ~kModeBits;
        uint32_t target = callbackTarget.load(std::memory_order_relaxed);
        for (; n; --n) { rsSet<24>(spr, ip); spr += 3; ip = target; }
    }
    if (p & kWatchBit) watchHook(*this);   // WatcherWindow.CheckConditionsSynchronously: before the trap
    while (stepByStep.load(std::memory_order_relaxed)) {
        debugTrap();
        // Resumed (F5/F8): the reference's per-frame watcher check usually stopped the machine again before its
        // CPU thread (polling every 1 ms) ran on, so a resume while the conditions hold does nothing.
        if (stepByStep.load() || !(callbacksPending.load() & kWatchBit)) break;
        watchHook(*this);
    }
}

// GetFromSdlAxis: SDL's -32768..32767 to -1..1.
static float sdlAxis(int16_t v) { return v < 0 ? v / 32768.0f : v / 32767.0f; }
// GamePadDeadZone.IndependentAxes (GetState's default), then the clamp to -1..1.
static float deadZone(float v, float dz) {
    if (v < -dz) v += dz;
    else if (v > dz) v -= dz;
    else return 0.0f;
    v /= 1.0f - dz;
    return v < -1.0f ? -1.0f : v > 1.0f ? 1.0f : v;
}
static float clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

PadState padFromSdl(const int16_t a[6], uint16_t buttons) {
    PadState p;
    p.connected = true;
    p.buttons = buttons;
    p.lx = deadZone(sdlAxis(a[kAxLX]), 0.24f);            // SDL dead zones from the XInput constants
    p.ly = deadZone(sdlAxis(a[kAxLY]) * -1.0f, 0.24f);    // SDL's Y grows down, XNA's up
    p.rx = deadZone(sdlAxis(a[kAxRX]), 0.265f);
    p.ry = deadZone(sdlAxis(a[kAxRY]) * -1.0f, 0.265f);
    p.lt = clamp01(sdlAxis(a[kAxLT]));                     // triggers: clamp only, no dead zone
    p.rt = clamp01(sdlAxis(a[kAxRT]));
    return p;
}

// The "has" bits come from the names in the SDL mapping string ("a:b0,leftx:a0,...").
PadCaps padCapsFromMapping(const char* mapping, bool rumble, const char* name) {
    static const struct { const char* key; uint8_t byte, bit; } kMap[] = {
        {"dpup", 0, 0}, {"dpdown", 0, 1}, {"dpleft", 0, 2}, {"dpright", 0, 3},
        {"a", 0, 4}, {"b", 0, 5}, {"x", 0, 6}, {"y", 0, 7},
        {"leftx", 1, 0}, {"lefty", 1, 1}, {"rightx", 1, 2}, {"righty", 1, 3},
        {"start", 1, 4}, {"back", 1, 5}, {"guide", 1, 6},   // bit 7 (voice) is never set
        {"leftshoulder", 2, 0}, {"rightshoulder", 2, 1}, {"lefttrigger", 2, 2}, {"righttrigger", 2, 3},
        {"leftstick", 2, 4}, {"rightstick", 2, 5},
    };
    PadCaps c;
    c.connected = true;
    c.type = 1;   // GamePadType.GamePad, always, on MonoGame's SDL backend
    c.name = name ? name : "";
    if (rumble) c.has[2] |= 0xC0;   // left and right vibration motors
    std::string s = mapping ? mapping : "";
    for (size_t at = 0; at <= s.size();) {
        size_t end = s.find(',', at);
        if (end == std::string::npos) end = s.size();
        std::string item = s.substr(at, end - at);
        size_t colon = item.find(':');
        if (colon != std::string::npos && item.find(':', colon + 1) == std::string::npos)   // Split(':') == 2 parts
            for (auto& k : kMap) if (item.compare(0, colon, k.key) == 0 && std::strlen(k.key) == colon) c.has[k.byte] |= uint8_t(1 << k.bit);
        at = end + 1;
    }
    return c;
}

void registerInputInts() {
    IntFn* t = gInt[2];
    t[0x00] = keysAsBits;
    t[0x01] = readKeyboardBuffer;
    t[0x02] = setKeyboardCallback;
    t[0x03] = readMouse;
    t[0x04] = setMouseCallback;
    t[0x06] = setControllerCallback;
    t[0x10] = keysAsCodes;
    t[0x14] = readPads;
    t[0x15] = readPadCaps;
    t[0x16] = readPadNames;
}

} // namespace c93
