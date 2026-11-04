// CPU core: power-on state, the instruction loop and the opcode tables.
// Instruction families live in ops_*.inc and register themselves into the tables below.
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <random>
#include <thread>

#include "audio.h"
#include "log.h"
#include "ints.h"
#include "netrandom.h"
#include "ops.h"

namespace c93 {

#include "cpu_ops.inc"

// --- Default palette: 3-3-2 RGB, index 0 = white, reversed like the original Pal8Bit --------
static void defaultPalette(uint8_t* out) {
    for (int i = 0; i < 256; ++i) {
        int c = 255 - i;
        auto ramp = [](int steps, int v) { int x = steps * v - 1; return uint8_t(x < 0 ? 0 : x); };
        out[i * 3 + 0] = ramp(32, (c >> 5) & 7);
        out[i * 3 + 1] = ramp(32, (c >> 2) & 7);
        out[i * 3 + 2] = ramp(64, c & 3);
    }
}

void Machine::setVramPages(uint8_t pages) {
    if (pages > 8) pages = 8;               // the original threw past 8 (fixed-size tables)
    vramPages.store(pages);
    std::memset(back, 0, sizeof back);      // the original reallocated its back buffers here
    std::memset(backPal, 0, sizeof backPal);
    backDirty.store(0xFF);
    uint8_t pal[kPalSize];
    defaultPalette(pal);
    for (int p = 0; p < pages; ++p) std::memcpy(ram + paletteAddr(p), pal, kPalSize);
}

void Machine::reset() {
    running.store(false);
    std::memset(ram, 0, sizeof ram);
    std::memset(regs, 0, sizeof regs);
    std::memset(fregs, 0, sizeof fregs);
    std::memset(rs, 0, sizeof rs);
    std::memset(cs, 0, sizeof cs);
    std::memset(hmem, 0, sizeof hmem);
    std::memset(f, 0, sizeof f);
    ip = spr = spc = 0;
    setBank(0);
    fbank = 0;
    layerVisible = 0xFF;
    layerAuto = 0xFF;
    setVramPages(2);
}

// One instruction at `at` (first and second byte -> one handler); returns where the next one starts.
static uint32_t exec(Machine& m, uint32_t at) { return gSub[m.ram[at & kRamMask]][m.ram[(at + 1) & kRamMask]](m, at + 2); }

void Machine::step() {
    if constexpr (kChain) callbacksPending.fetch_or(kStepBit);   // the handler must not chain on (ops.h)
    ip = exec(*this, ip);
    if constexpr (kChain) callbacksPending.fetch_and(~kStepBit);
}

void Machine::run() {
    // callbacksPending is the only cross-thread traffic and the one check per instruction (callbacks, trap,
    // WATCHER, tracking, stop); a relaxed load is a plain mov. ip lives in `at`; Machine::ip is stored once per
    // instruction (here or in a chained handler, ops.h), so other threads always see a boundary.
    uint32_t at = ip;
#if C93_STACK_GUARD
    constexpr uintptr_t kChainStack = 16 << 10;   // of the CPU thread's 1 MB; interrupts (the C compiler) need the rest
    chainFloor = uintptr_t(_AddressOfReturnAddress()) - kChainStack;
#endif
    for (;;) {
        if (uint32_t p = callbacksPending.load(std::memory_order_relaxed)) [[unlikely]] {
            if (p & kStopBit) break;   // running cleared
            bool tracked = serveCallbacks();   // may move ip (callbacks, IMMEDIATE); tracked: that loop ran (D-26)
            at = ip;
            if (tracked) continue;
        }
        ip = at = exec(*this, at);
    }
}

// Waits at an instruction boundary until the debugger lets one instruction through, step mode ends,
// or the machine stops. The reference polled with Sleep(1); this sleeps on debugSignal.
void Machine::debugTrap() {
    parked.store(true);
    for (uint32_t s = debugSignal.load(); !moveNext.load() && stepByStep.load() && running.load(); s = debugSignal.load())
        debugSignal.wait(s);
    parked.store(false);
    moveNext.store(false);
}

// Builds the tables once at startup, before any machine runs.
static const bool gTablesReady = [] { initInterrupts(); return buildTables(); }();

} // namespace c93
