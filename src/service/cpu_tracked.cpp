// The instruction loop service mode runs while its MEMORY MAP tracks RAM activity (D-26): every
// instruction family compiled a second time against TrackedMachine, whose fetch/rd/wr also mark pages
// (MemoryController.TrackRead/TrackWrite). Machine::run enters it when kTrackBit is set and gets the CPU
// back when it is cleared; the normal loop never pays for tracking.
// shortcut: interrupt handlers (video drawing, strings, files) are compiled once and do not mark pages,
// unlike the reference's MEMC.Set8bitToRAM calls in them; mark in their pixel helpers if the map should show them.
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <random>
#include <thread>

#include "audio.h"
#include "log.h"
#include "ints.h"
#include "netrandom.h"

namespace c93::tracked { using Machine = TrackedMachine; }
#define C93_OPS_NS c93::tracked
#include "ops.h"

namespace c93::tracked {

#include "cpu_ops.inc"

// The app allocates its machine as a TrackedMachine (main.cpp), so the cast is exact.
static void loop(c93::Machine& base) {
    auto& m = static_cast<TrackedMachine&>(base);
    uint32_t ip = m.ip;
    while (m.running.load(std::memory_order_relaxed)) {
        uint32_t p = m.callbacksPending.load(std::memory_order_relaxed);
        if (p != c93::Machine::kTrackBit) [[unlikely]] {
            if (!(p & c93::Machine::kTrackBit)) return;   // disarmed: back to the normal loop
            m.servePending(p);   // callbacks, WATCHER, trap: as in the normal loop
            ip = m.ip;
        }
        uint8_t op = m.fetch8(ip);
        m.ip = ip = gTop[op](m, ip);
    }
}

static const bool gTablesReady = [] { c93::Machine::trackedLoop = loop; return buildTables(); }();

} // namespace c93::tracked
