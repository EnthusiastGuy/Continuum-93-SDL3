// Interrupt dispatch, the machine group (0) and the random group (3).
// (Interrupts/InterruptHandler.cs, InterruptsMachine.cs, InterruptsRandom.cs.)
#include "ints.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <random>

#include "asm.h"
#include "basic.h"
#include "cc.h"
#include "log.h"

namespace c93 {

IntFn gInt[kIntGroups][256];
std::string gFsRoot = "Data/filesystem";
std::optional<std::string> readText(const std::string& path);   // below

namespace {

// --- Machine (0) ---------------------------------------------------------------------------
void stop(Machine& m, uint8_t) { m.running.store(false, std::memory_order_relaxed); }
void clear(Machine& m, uint8_t) { m.reset(); m.running.store(true, std::memory_order_relaxed); }

// Mode 0: milliseconds since power-on (32-bit); mode 1: .NET ticks (100 ns, 64-bit big-endian).
// shortcut: the reference read MonoGame's per-frame game time (16.7 ms steps); this clock is
// continuous. Programs only ever compare or subtract it.
void readClock(Machine& m, uint8_t r) {
    uint32_t mode = m.get<8>(nextReg(r, 1));
    uint32_t dest = m.get<24>(nextReg(r, 2));
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - m.powerOn).count();
    if (mode == 0) m.wr<32>(dest, uint32_t(ns / 1'000'000));
    else if (mode == 1) {
        uint64_t ticks = uint64_t(ns / 100);
        m.wr<32>(dest, uint32_t(ticks >> 32));
        m.wr<32>(dest + 4, uint32_t(ticks));
    }
}

// Host requests: the main thread acts on them between frames.
void toggleFullscreen(Machine& m, uint8_t r) {
    m.set<8>(r, m.isFullscreen.load() ? 1 : 0);
    m.fullscreenRequest.store(true);
}
void hostCursor(Machine& m, uint8_t r) { m.hostCursorRequest.store(m.get<8>(nextReg(r, 1)) ? 1 : 0); }
void shutDown(Machine& m, uint8_t) { m.shutdownRequest.store(true); }

// Guest path of a build interrupt (reg+1) -> host path. Build sources may live anywhere under Data/
// (the file system root's parent): the boot selector builds ../os/q.asm and ../os/halcyon/halcyon.c.
// Nothing outside it (D-13, D-19). Empty when refused.
std::string buildPath(Machine& m, uint8_t n) {
    std::string rel;
    for (uint32_t a = m.get<24>(n); m.rd<8>(a); ++a) rel += char(m.rd<8>(a) == 92 ? '/' : m.rd<8>(a));
    auto full = std::filesystem::weakly_canonical(std::filesystem::weakly_canonical(gFsRoot) / rel);
    return within(full, buildRoot()) ? full.string() : "";
}

// INT 0 0xC0 Build: assemble a guest file system .asm and load it. reg+1 (24-bit): the path in,
// the run address out (0xFFFFFF when the file is missing); reg+4: error count (max 255).
// shortcut: the reference also wrote debug/<name>.log/.full.asm/.<addr> next to the source; skipped.
void build(Machine& m, uint8_t r) {
    uint8_t n = nextReg(r, 1);
    std::string full = buildPath(m, n);
    if (full.empty()) { m.set<24>(n, 0xFFFFFF); return; }
    Loaded l = assembleInto(m, full);
    if (l.fatal) { m.running.store(false); return; }   // D-16: the reference threw out of the CPU loop
    if (!l.found) { m.set<24>(n, 0xFFFFFF); return; }
    if (!l.loaded) return;   // no blocks: the reference returned before touching the registers
    m.set<24>(n, l.runAddress);
    m.set<8>(nextReg(r, 4), uint8_t(l.errors > 255 ? 255 : l.errors));
}

// INT 0 0xC1 BuildBasic (InterruptsMachine.BuildBasic): compile a guest .bas, assemble, load. reg+1 (24-bit):
// the path in, the run address out (0xFFFFFF when missing, on compile errors or with no code); reg+4: compile
// errors capped at 255, or after assembly the assembler's errors wrapped to 8 bits ((byte) cast, unlike Build).
// shortcut: the reference also wrote debug/<name>.basic.log/.generated.asm/.log/.full.asm/.<addr>; skipped.
void buildBasic(Machine& m, uint8_t r) {
    uint8_t n = nextReg(r, 1);
    std::string rel;
    for (uint32_t a = m.get<24>(n); m.rd<8>(a); ++a) rel += char(m.rd<8>(a) == 92 ? '/' : m.rd<8>(a));
    auto root = std::filesystem::weakly_canonical(gFsRoot);
    auto full = std::filesystem::weakly_canonical(root / rel);   // guest paths stay inside the root (D-13)
    auto src = full.string().rfind(root.string(), 0) == 0 ? readText(full.string()) : std::nullopt;
    if (!src) { m.set<24>(n, 0xFFFFFF); return; }
    std::string_view text = *src;
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);   // File.ReadAllText drops the BOM
    auto t0 = std::chrono::steady_clock::now();
    BasicResult b = compileBasic(text);
    LOGI("BASIC: %s: %d error(s), %.1f ms", full.string().c_str(), b.errors,
         std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    if (b.errors > 0) {
        std::istringstream log(b.log);
        for (std::string line; std::getline(log, line);) if (line.find("error") != std::string::npos) LOGW("BASIC: %s", line.c_str());
        m.set<8>(nextReg(r, 4), uint8_t(b.errors > 255 ? 255 : b.errors));
        m.set<24>(n, 0xFFFFFF);
        return;
    }
    // The reference assembled with the path of its debug copy: relative #includes resolve from there.
    auto asmPath = full.parent_path() / "debug" / (full.filename().string() + ".generated.asm");
    Loaded l = assembleText(m, b.asmText, asmPath.string());
    if (l.fatal) { m.running.store(false); return; }   // D-16
    if (!l.loaded) { m.set<24>(n, 0xFFFFFF); return; }
    m.set<24>(n, l.runAddress);
    m.set<8>(nextReg(r, 4), uint8_t(l.errors));
}

// INT 0 0xC2 Build C (InterruptsMachine.BuildC): compile, assemble, load. reg+1: path in, run address out
// (0xFFFFFF on any failure); reg+4: 0, or the compile errors (max 255), or the assembler's (at least 1).
// shortcut: the reference also wrote debug/<name>.c.log/.generated.asm/.log next to the source; skipped.
void buildC(Machine& m, uint8_t r) {
    uint8_t n = nextReg(r, 1), e = nextReg(r, 4);
    std::string full = buildPath(m, n);
    Loaded l = full.empty() ? Loaded{} : compileCInto(m, full, false, buildRoot().string());
    if (l.fatal) { m.running.store(false); return; }   // D-16, D-20
    m.set<24>(n, l.loaded ? l.runAddress : 0xFFFFFF);
    size_t errs = !l.found ? 1 : l.loaded ? 0 : l.compileFailed ? l.errors : std::max<size_t>(1, l.errors);
    m.set<8>(e, uint8_t(std::min<size_t>(errs, 255)));
}

// --- Random (3) ----------------------------------------------------------------------------
// The reference used an unseeded System.Random, so no program can depend on the sequence.
// Ranges mirror Random.Next(min, max): max exclusive, an empty range yields min.
std::mt19937_64 rng{std::random_device{}()};
int64_t next(int64_t lo, int64_t hi) { return hi <= lo ? lo : lo + int64_t(rng() % uint64_t(hi - lo)); }

template <int W> void rnd(Machine& m, uint8_t r) {
    constexpr int64_t top = W == 32 ? 0 : int64_t(maskOf<W>());
    m.set<W>(r, uint32_t(W == 32 ? next(-2147483647, 2147483647) : next(0, top)));
}
template <int W> void rndCustom(Machine& m, uint8_t r) {
    uint8_t i = nextReg(r, 1);
    uint32_t v = m.get<W>(i);
    if constexpr (W == 32) { int64_t half = v / 2; m.set<32>(i, uint32_t(next(int32_t(-half), int32_t(half)))); }
    else m.set<W>(i, uint32_t(next(0, W == 24 ? int32_t(v) : v)));
}

// GetCPUDesignationByFrequency: a marketing name for a speed in MHz, as a 0-terminated string.
void cpuDesignation(Machine& m, uint8_t r) {
    static const struct { uint16_t upTo; const char* name; } names[] = {
        {1, "Proto"}, {3, "Nova I"}, {5, "Nova II"}, {7, "Nova III"}, {10, "Comet"}, {12, "Comet II"},
        {15, "Comet III"}, {18, "Comet Prime"}, {22, "Meteor"}, {30, "Meteor II"}, {40, "Apollo"},
        {50, "Apollo II"}, {60, "Apollo III"}, {75, "Phoenix I"}, {90, "Phoenix II"}, {100, "Phoenix Keanu"},
        {120, "Titan"}, {150, "Titan II"}, {175, "Titan Keanu"}, {200, "Vulcan I"}, {250, "Vulcan II"},
        {300, "Vulcan III"}, {350, "Atlas"}, {400, "Atlas Keanu"}, {500, "Chronos I"}, {600, "Chronos II"},
        {700, "Chronos Keanu"}, {850, "Odin I"}, {1000, "Odin II"}};
    uint32_t mhz = m.get<16>(nextReg(r, 1)), dest = m.get<24>(nextReg(r, 3));
    std::string s = "Beyond Odin +" + std::to_string((mhz - 1000) / 100 + 1);
    for (auto& n : names) if (mhz <= n.upTo) { s = n.name; break; }
    for (size_t i = 0; i <= s.size(); ++i) m.ram[(dest + i) & kRamMask] = i < s.size() ? uint8_t(s[i]) : 0;
}

} // namespace

void registerMachineInts() {
    IntFn* t = gInt[0];
    t[0x00] = stop;
    t[0x01] = clear;
    t[0x03] = readClock;
    t[0x10] = toggleFullscreen;
    t[0x11] = hostCursor;
    t[0x20] = shutDown;
    t[0xC0] = build;
    t[0xC1] = buildBasic;
    t[0xC2] = buildC;
    t[0xF0] = cpuDesignation;

    IntFn* r = gInt[3];
    r[0] = rnd<8>;  r[1] = rndCustom<8>;
    r[2] = rnd<16>; r[3] = rndCustom<16>;
    r[4] = rnd<24>; r[5] = rndCustom<24>;
    r[6] = rnd<32>; r[7] = rndCustom<32>;
}

std::optional<std::string> readText(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

Loaded assembleInto(Machine& m, const std::string& path) {
    auto src = readText(path);
    return src ? assembleText(m, *src, path) : Loaded{};
}

Loaded assembleText(Machine& m, std::string_view src, const std::string& path) {
    Loaded l;
    l.found = true;
    auto t0 = std::chrono::steady_clock::now();
    AsmResult a = assemble(src, path, readText);
    for (auto& w : a.warnings) LOGW("Assembler: %s line %u: %s", path.c_str(), w.line, w.text.c_str());
    for (auto& e : a.errors) LOGW("Assembler: %s line %u: %s", path.c_str(), e.line, e.text.c_str());
    if (!a.fatal.empty()) LOGE("Assembler: %s: %s; nothing loaded", path.c_str(), a.fatal.c_str());
    for (auto& b : a.blocks) for (size_t i = 0; i < b.data.size(); ++i) m.ram[(b.start + i) & kRamMask] = b.data[i];
    l.loaded = !a.blocks.empty();
    l.firstBlock = l.loaded ? a.blocks[0].start : 0;
    l.runAddress = a.runAddress;
    l.errors = a.errors.size();
    l.fatal = !a.fatal.empty();
    LOGI("Assembler: %s: %zu block(s), %zu error(s), %.1f ms", path.c_str(), a.blocks.size(), l.errors,
         std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    return l;
}

std::filesystem::path buildRoot() { return std::filesystem::weakly_canonical(gFsRoot).parent_path(); }

bool within(const std::filesystem::path& p, const std::filesystem::path& root) {
    auto rel = std::filesystem::weakly_canonical(p).lexically_relative(root);
    return !rel.empty() && *rel.begin() != "..";
}

Loaded compileCInto(Machine& m, const std::string& path, bool boot, const std::string& root) {
    namespace fs = std::filesystem;
    Loaded l;
    auto text = [](const fs::path& p) {   // File.ReadAllText drops a UTF-8 BOM
        auto t = readText(p.string());
        if (t && t->rfind("\xEF\xBB\xBF", 0) == 0) t->erase(0, 3);
        return t;
    };
    auto src = text(path);
    if (!src) return l;
    l.found = true;
    auto t0 = std::chrono::steady_clock::now();
    fs::path dir = fs::path(path).parent_path();
    std::string name = fs::path(path).filename().string();
    auto slashes = [](std::string s) { std::replace(s.begin(), s.end(), char(92), '/'); return fs::path(s); };
    // Includes: Runner.cs (boot) tries <dir>/<dir of the including file>/<name>, then <dir>/<name>;
    // InterruptsMachine.BuildC only the first (the top file counts as being in <dir>).
    auto resolve = [&](const std::string& inc, const std::string& including) -> std::optional<std::string> {
        fs::path incDir = slashes(including).parent_path();
        fs::path c = (boot || including != name ? dir / incDir : dir) / slashes(inc);
        if (boot && !fs::is_regular_file(c)) c = dir / slashes(inc);
        if ((!root.empty() && !within(c, root)) || !fs::is_regular_file(c)) return std::nullopt;   // D-13
        return text(c);
    };
    CcResult cr = compileC(*src, name, resolve);
    if (!cr.fatal.empty()) { LOGE("C compiler: %s: the reference threw (%s); nothing loaded (D-20)", path.c_str(), cr.fatal.c_str()); l.fatal = true; return l; }
    for (size_t i = 0; i < cr.errors.size() && i < 20; ++i)
        LOGW("C compiler: %s:%d:%d: %s", cr.errors[i].file.c_str(), cr.errors[i].line, cr.errors[i].col, cr.errors[i].message.c_str());
    if (!cr.errors.empty()) { l.compileFailed = true; l.errors = cr.errors.size(); return l; }
    double compileMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    AsmResult a = assemble(cr.asmText, (dir / "debug" / (name + ".generated.asm")).string(), readText);
    for (auto& w : a.warnings) LOGW("Assembler: %s (generated) line %u: %s", name.c_str(), w.line, w.text.c_str());
    for (auto& e : a.errors) LOGW("Assembler: %s (generated) line %u: %s", name.c_str(), e.line, e.text.c_str());
    if (!a.fatal.empty()) { LOGE("Assembler: %s (generated): %s; nothing loaded", name.c_str(), a.fatal.c_str()); l.fatal = true; return l; }
    l.errors = a.errors.size();
    // Runner loads the blocks whatever the assembler said; BuildC only a clean build
    if (a.blocks.empty() || (!boot && !a.errors.empty())) return l;
    for (auto& b : a.blocks) for (size_t i = 0; i < b.data.size(); ++i) m.ram[(b.start + i) & kRamMask] = b.data[i];
    l.loaded = true;
    l.firstBlock = a.blocks[0].start;
    l.runAddress = a.runAddress;
    LOGI("C compiler: %s: compiled in %.1f ms, %zu block(s), %.1f ms in all", path.c_str(), compileMs, a.blocks.size(),
         std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    return l;
}

static void noInt(Machine&, uint8_t) {}

void initInterrupts() {
    for (auto& g : gInt) for (IntFn& f : g) f = noInt;
    registerMachineInts();
    registerVideoInts();
    registerInputInts();
    registerFileInts();
    registerStringInts();
    registerTextInts();
}

} // namespace c93
