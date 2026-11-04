// The Continuum 93 machine state and its hot-path accessors.
// Everything the CPU touches per instruction is inline here so cpu.cpp compiles to tight code.
//
// Byte order is big-endian everywhere (RAM, register pairs, stacks), as in the original.
// Registers A..Z (0..25) chain into 16/24/32-bit views: AB, ABC, ABCD ... ZABC wraps around.
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>

namespace c93 {

constexpr uint32_t kRamSize  = 16u << 20;          // 16 MB general RAM (code, data, palettes, VRAM)
constexpr uint32_t kRamMask  = kRamSize - 1;
constexpr uint32_t kRsSize   = 4u << 20;           // register stack (PUSH/POP), bytes
constexpr uint32_t kCsSize   = 1u << 20;           // call stack, 32-bit cells
constexpr uint32_t kHmemSize = 64u << 10;          // hardware memory, 32-bit cells
constexpr int kW = 480, kH = 270;
constexpr uint32_t kVSize = kW * kH;               // one video page, one byte per pixel
constexpr uint32_t kPalSize = 768;                 // 256 x RGB
constexpr int kRegs = 26, kBanks = 256, kFRegs = 16;

// Sign-extends the low W bits of v.
template <int W> constexpr int32_t sx(uint32_t v) { return int32_t(v << (32 - W)) >> (32 - W); }
template <int W> constexpr uint32_t maskOf() { return uint32_t((uint64_t(1) << W) - 1); }

// Next register in the A..Z ring; 32 entries because operand bytes are masked with 0x1F
// and the original never validated 26..31 (it threw). Those map into the padding.
struct Ring {
    uint8_t n[32];
    constexpr Ring(int d) : n{} { for (int i = 0; i < 32; ++i) n[i] = uint8_t(i < 26 ? (i + d) % 26 : i + d); }
};
inline constexpr Ring kNext1{1}, kNext2{2}, kNext3{3};

struct Machine {
    // --- CPU ------------------------------------------------------------------------------
    uint8_t* r = regs;            // current register bank (bank * 26)
    uint32_t ip = 0;              // instruction pointer
    uint32_t spr = 0;             // register stack pointer (bytes into rs)
    uint32_t spc = 0;             // call stack pointer (cells into cs)
    bool f[8]{};                  // Z C SN OV PO EQ GT LT (see flag())
    uint8_t bank = 0, fbank = 0;
    // The machine runs while kStopBit is clear in callbacksPending: one word then holds everything the run loop
    // (and a chained handler, ops.h) tests per instruction. Reads and writes like the std::atomic<bool> it was.
    struct RunFlag {
        std::atomic<uint32_t>& bits;
        bool load(std::memory_order o = std::memory_order_seq_cst) const { return !(bits.load(o) & kStopBit); }
        void store(bool on, std::memory_order o = std::memory_order_seq_cst) { on ? bits.fetch_and(~kStopBit, o) : bits.fetch_or(kStopBit, o); }
        RunFlag& operator=(bool on) { store(on); return *this; }
        operator bool() const { return load(); }
    } running{callbacksPending};

    // --- Memory ---------------------------------------------------------------------------
    // shortcut: addresses wrap at 24 bits; the original threw (and halted) past 16 MB + 3.
    uint8_t ram[kRamSize + 4]{};
    uint8_t regs[kRegs * kBanks + 8]{};   // +8: register indices 26..31 land in padding
    float fregs[kBanks][kFRegs]{};
    uint8_t rs[kRsSize + 4]{};
    uint32_t cs[kCsSize]{};
    uint32_t hmem[kHmemSize]{};

    // --- Video (read by the presenter on the main thread) ----------------------------------
    std::atomic<uint8_t> vramPages{2};
    std::atomic<uint8_t> layerVisible{0xFF};   // bit i -> back buffer i (0 = top-most page)
    std::atomic<uint8_t> layerAuto{0xFF};      // bit i -> page i copied every frame
    std::atomic<uint8_t> backDirty{0};         // back buffers changed since the last present
    // Back buffers hold what is on screen. Index 0 is the top-most layer, which is the page
    // at the lowest address (page vramPages-1). Visibility bits index back buffers; the auto
    // and manual update bits index pages. Both quirks come from the original.
    uint8_t back[8][kVSize]{};
    uint8_t backPal[8][kPalSize]{};
    void updateBack(int b) {   // snapshot one page and its palette into back buffer b
        int page = vramPages - 1 - b;
        std::memcpy(back[b], ram + pageAddr(page), kVSize);
        std::memcpy(backPal[b], ram + paletteAddr(page), kPalSize);
        backDirty.fetch_or(uint8_t(1u << b), std::memory_order_release);
    }
    void manualUpdate(uint8_t pageBits) {   // VDL: present the pages named by pageBits now
        for (int i = 0; i < vramPages; ++i) if (pageBits >> i & 1) updateBack(vramPages - 1 - i);
    }

    // --- Host requests (set by interrupts on the CPU thread, served by the main thread) -----
    const std::chrono::steady_clock::time_point powerOn = std::chrono::steady_clock::now();
    std::atomic<bool> isFullscreen{false};        // kept current by the main thread
    std::atomic<bool> fullscreenRequest{false};   // toggle fullscreen
    std::atomic<int8_t> hostCursorRequest{-1};    // -1 none, 0 hide, 1 show the host pointer
    std::atomic<bool> shutdownRequest{false};

    // --- Input callbacks (raised by the main thread, served by the CPU between instructions) --
    // Low bits count pending callbacks; kTrapBit arms the debugger trap. Both share the one check
    // the run loop already makes, so step-by-step costs nothing while it is off.
    static constexpr uint32_t kTrapBit = 1u << 31;
    // kWatchBit: service mode's WATCHER checks its conditions after every instruction (watchHook, CPU thread).
    static constexpr uint32_t kWatchBit = 1u << 29;
    void (*watchHook)(Machine&) = nullptr;
    std::atomic<uint32_t> callbacksPending{kStopBit};
    std::atomic<uint32_t> callbackTarget{0xFFFFFF};
    // ints_input.cpp: push IP on the register stack, jump; the WATCHER; then the trap. True when it ran service
    // mode's tracked loop instead (kTrackBit, D-26): that loop stops at an instruction boundary, run() just goes on.
    bool serveCallbacks();
    void servePending(uint32_t pending);   // the body, also called by the tracked loop

    // --- Debugger (CodeAnalysis/DebugState.cs; driven by service mode on the main thread) -------
    std::atomic<bool> stepByStep{false};    // the CPU stops at every instruction boundary
    std::atomic<bool> moveNext{false};      // ...and lets one instruction through when this is set
    std::atomic<uint32_t> debugSignal{0};   // bumped on every change; the stopped CPU sleeps on it
    std::atomic<bool> debugRequest{false};  // a DEBUG instruction ran: the main thread opens service mode
    std::atomic<bool> parked{false};        // the CPU sleeps in debugTrap: the main thread may run code (IMMEDIATE)
    void debugSet(bool step, bool next) {
        moveNext.store(next);
        stepByStep.store(step);
        if (step) callbacksPending.fetch_or(kTrapBit); else callbacksPending.fetch_and(~kTrapBit);
        debugWake();
    }
    void debugWake() { debugSignal.fetch_add(1); debugSignal.notify_all(); }   // also after clearing `running`
    void debugTrap();                   // cpu.cpp: Computer.DebuggerTrap, sleeping instead of polling

    // --- Memory activity (RAM/MemoryActivityTracker.cs, for service mode's MEMORY MAP; D-26) ----------
    // Armed only while service mode is open: kTrackBit in callbacksPending sends run() into a second copy
    // of the instruction loop (service/cpu_tracked.cpp, TrackedMachine below) whose accessors also mark the
    // 256-byte pages they touch. Unarmed, the normal loop and accessors are exactly what they were.
    // kStepBit: step() runs one instruction; handlers that chain (ops.h, C93_NEXT) stop after it. kStopBit: `running`.
    static constexpr uint32_t kTrackBit = 1u << 30, kStepBit = 1u << 28, kStopBit = 1u << 27;
    static constexpr uint32_t kModeBits = kTrapBit | kTrackBit | kWatchBit | kStepBit | kStopBit;   // flags, not callbacks
    static constexpr int kPageShift = 8, kPageCount = 0x1000000 >> kPageShift;
    static constexpr uint8_t kReadFlag = 1, kWriteFlag = 2;
    mutable uint8_t activity[kPageCount]{};                    // flags per page; only via atomic_ref
    inline static void (*trackedLoop)(Machine&) = nullptr;     // set by service/cpu_tracked.cpp (the app only)
    void trackActivity(bool on) {                              // ActivityTracker.Enabled
        if (on && trackedLoop) callbacksPending.fetch_or(kTrackBit); else callbacksPending.fetch_and(~kTrackBit);
    }
    bool trackingActivity() const { return callbacksPending.load(std::memory_order_relaxed) & kTrackBit; }
    // MarkRange: the first and the last page of the access (the ones between are not marked, as there).
    void markActivity(uint32_t a, uint32_t n, uint8_t flag) const {
        markPage(a >> kPageShift, flag);
        if (uint32_t e = (a + (n ? n - 1 : 0)) >> kPageShift; e != a >> kPageShift) markPage(e, flag);
    }
    void markPage(uint32_t page, uint8_t flag) const {   // shortcut: page 0x10000 (the 3 bytes past 16 MB, where the reference threw) wraps to 0
        std::atomic_ref<uint8_t> p(activity[page & (kPageCount - 1)]);
        if (!(p.load(std::memory_order_relaxed) & flag)) p.store(p.load(std::memory_order_relaxed) | flag, std::memory_order_relaxed);
    }
    void consumeActivity(uint8_t* out) {   // ConsumeActivity: copy and clear (the UI thread, at 30 Hz)
        for (int i = 0; i < kPageCount; ++i) {
            std::atomic_ref<uint8_t> p(activity[i]);
            out[i] = p.load(std::memory_order_relaxed);
            if (out[i]) p.store(0, std::memory_order_relaxed);
        }
    }
    void clearActivity() { for (uint8_t& b : activity) std::atomic_ref<uint8_t>(b).store(0, std::memory_order_relaxed); }

    // --- RAM ------------------------------------------------------------------------------
    template <int W> uint32_t rd(uint32_t a) const {
        const uint8_t* p = ram + (a & kRamMask);
        if constexpr (W == 8)  return p[0];
        if constexpr (W == 16) return uint32_t(p[0]) << 8 | p[1];
        if constexpr (W == 24) return uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
        if constexpr (W == 32) return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
    }
    template <int W> void wr(uint32_t a, uint32_t v) {
        uint8_t* p = ram + (a & kRamMask);
        if constexpr (W >= 32) *p++ = uint8_t(v >> 24);
        if constexpr (W >= 24) *p++ = uint8_t(v >> 16);
        if constexpr (W >= 16) *p++ = uint8_t(v >> 8);
        *p = uint8_t(v);
    }
    // Floats live in RAM as big-endian IEEE-754 bits. Reads wrap per byte at 16 MB (as the
    // original); writes run into the 3 bytes of slack past the end, also as the original.
    float rdF(uint32_t a) const {
        uint32_t b = 0;
        for (int k = 0; k < 4; ++k) b = b << 8 | ram[(a + k) & kRamMask];
        float v; std::memcpy(&v, &b, 4); return v;
    }
    void wrF(uint32_t a, float v) { uint32_t b; std::memcpy(&b, &v, 4); wr<32>(a, b); }
    // Operand fetches advance the caller's ip, which the instruction handlers keep in a register (ops.h).
    template <int W> uint32_t fetch(uint32_t& at) const { uint32_t v = rd<W>(at); at += W / 8; return v; }
    uint8_t fetch8(uint32_t& at) const { return ram[at++ & kRamMask]; }
    uint8_t fetchReg(uint32_t& at) const { return fetch8(at) & 0x1F; }   // register operand: low 5 bits

    // --- Registers ------------------------------------------------------------------------
    template <int W> uint32_t get(uint8_t i) const {
        if constexpr (W == 8)  return r[i];
        if constexpr (W == 16) return uint32_t(r[i]) << 8 | r[kNext1.n[i]];
        if constexpr (W == 24) return uint32_t(r[i]) << 16 | uint32_t(r[kNext1.n[i]]) << 8 | r[kNext2.n[i]];
        if constexpr (W == 32) return uint32_t(r[i]) << 24 | uint32_t(r[kNext1.n[i]]) << 16 |
                                      uint32_t(r[kNext2.n[i]]) << 8 | r[kNext3.n[i]];
    }
    template <int W> void set(uint8_t i, uint32_t v) {
        if constexpr (W == 8)  { r[i] = uint8_t(v); }
        if constexpr (W == 16) { r[i] = uint8_t(v >> 8);  r[kNext1.n[i]] = uint8_t(v); }
        if constexpr (W == 24) { r[i] = uint8_t(v >> 16); r[kNext1.n[i]] = uint8_t(v >> 8); r[kNext2.n[i]] = uint8_t(v); }
        if constexpr (W == 32) { r[i] = uint8_t(v >> 24); r[kNext1.n[i]] = uint8_t(v >> 16);
                                 r[kNext2.n[i]] = uint8_t(v >> 8); r[kNext3.n[i]] = uint8_t(v); }
    }
    void setBank(uint8_t b) { bank = b; r = regs + b * kRegs; }
    float& fr(uint8_t i) { return fregs[fbank][i & 15]; }

    // --- Register stack (byte addressed, big-endian) ----------------------------------------
    template <int W> uint32_t rsGet(uint32_t a) const {
        const uint8_t* p = rs + (a & (kRsSize - 1));
        uint32_t v = 0;
        for (int k = 0; k < W / 8; ++k) v = v << 8 | p[k];
        return v;
    }
    template <int W> void rsSet(uint32_t a, uint32_t v) {
        uint8_t* p = rs + (a & (kRsSize - 1));
        for (int k = W / 8 - 1; k >= 0; --k, v >>= 8) p[k] = uint8_t(v);
    }

    // --- Flags ----------------------------------------------------------------------------
    // Condition codes 0..7 are the negated flags (NZ NC SP NO PE NE LTE GTE), 8..15 the flags.
    bool cond(uint8_t c) const { return c >= 8 ? f[c & 7] : !f[c & 7]; }
    uint8_t flagsByte() const { uint8_t b = 0; for (int i = 0; i < 8; ++i) b |= uint8_t(f[i]) << i; return b; }

    template <int W> void addFlags(uint32_t a, uint32_t b) {
        if constexpr (W == 32) {
            uint32_t s = a + b;
            f[0] = s == 0; f[1] = s < a; f[2] = (s >> 31) != 0;
        } else {
            uint32_t s = a + b, t = s & maskOf<W>();          // a, b < 2^W so s fits in 32 bits
            f[0] = t == 0; f[1] = s > maskOf<W>(); f[2] = (t >> (W - 1)) & 1;
        }
    }
    template <int W> void subFlags(uint32_t a, uint32_t b) {
        f[0] = a == b; f[1] = b > a; f[2] = ((a - b) >> (W - 1)) & 1;
    }

    // Float add/sub as FloatRegisters.cs: flags from the result; an infinite or NaN result
    // leaves the first operand unchanged. Carry is untouched.
    template <bool Sub> float faddsub(float a, float b) {
        float s = Sub ? a - b : a + b;
        f[0] = s == 0; f[2] = s < 0; f[3] = Sub ? s < -3.40282347e38f : s > 3.40282347e38f;
        return s <= 3.40282347e38f && s >= -3.40282347e38f ? s : a;
    }

    // --- Lifecycle ------------------------------------------------------------------------
    void reset();                       // power-on state: clear RAM/regs, 2 video pages, palettes
    void setVramPages(uint8_t pages);   // also reloads every page's default palette
    uint32_t pageAddr(int page) const { return 0x1000000 - kVSize * uint32_t(page + 1); }
    uint32_t paletteAddr(int page) const { return 0x1000000 - kVSize * vramPages - kPalSize * uint32_t(page + 1); }
    void run();                         // CPU thread body: executes until running is cleared
    void step();                        // one instruction
    uintptr_t chainFloor = 0;           // set by run(): MSVC's chained handlers return to it below this stack address (ops.h)
};

// The machine as the tracked instruction loop sees it: the same state, with the RAM accessors the
// instructions use also marking pages (MemoryController.TrackRead/TrackWrite). No data of its own; the app
// allocates its machine as one so the tracked loop's cast is exact.
struct TrackedMachine : Machine {
    template <int W> uint32_t rd(uint32_t a) const { markActivity(a & kRamMask, W / 8, kReadFlag); return Machine::rd<W>(a); }
    template <int W> void wr(uint32_t a, uint32_t v) { markActivity(a & kRamMask, W / 8, kWriteFlag); Machine::wr<W>(a, v); }
    float rdF(uint32_t a) const { markActivity(a & kRamMask, 4, kReadFlag); return Machine::rdF(a); }
    void wrF(uint32_t a, float v) { markActivity(a & kRamMask, 4, kWriteFlag); Machine::wrF(a, v); }
    template <int W> uint32_t fetch(uint32_t& at) const { uint32_t v = rd<W>(at); at += W / 8; return v; }
    uint8_t fetch8(uint32_t& at) const { markActivity(at & kRamMask, 1, kReadFlag); return Machine::fetch8(at); }
    uint8_t fetchReg(uint32_t& at) const { return fetch8(at) & 0x1F; }
};

} // namespace c93
