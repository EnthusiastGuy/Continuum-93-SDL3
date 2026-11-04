// Replays the oracle's vectors (tests/oracle) through the C++ CPU and reports every difference.
//   conformance <vectors.txt> [mnemonic ...]     e.g. conformance build/vectors.txt ADD SUB
//   conformance --input                          INT 2 (input) self-test with synthetic host input
//   conformance --ldfill                         hand-checked LD ...,n cases (the oracle skips them)
//   conformance --bench ["<condition>"|--tracked] instructions per second per mix [WATCHER armed | service mode's tracked loop]
//   conformance --programs <programs.txt>        whole programs (oracle --programs)
//   conformance --asm <asm.txt>                  assembler output per file (oracle --asm)
//   conformance --basic <basic.txt>              BASIC compiler output per source (oracle --basic)
//   conformance --cc <cc.txt>                    C compiler output per source (oracle --cc)
//   conformance --disasm <disasm.txt>            service-mode disassembler text (oracle --disasm)
//   conformance --xsound <xsound.bin>            XSound PCM per sound definition (oracle --xsound)
//   conformance --watch <watch.txt>              service-mode WATCHER conditions (oracle --watch)
//   conformance --immediate <immediate.txt>      service-mode IMMEDIATE snippets (oracle --immediate)
// Only variants whose mnemonic (first word, width suffix included) is listed are run; none = all.
// Exit code: 0 when every case run matched.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "asm.h"
#include "audio.h"
#include "basic.h"
#include "cc.h"
#include "input.h"
#include "ints.h"
#include "machine.h"
#include "netrandom.h"
#include "service/disasm.h"
#include "service/immediate.h"
#include "service/watch.h"

using namespace c93;

namespace {

// Identical to the oracle's fill patterns.
uint8_t ramAt(uint32_t a) { return uint8_t((a * 2654435761u) >> 24); }
uint8_t rsAt(uint32_t a) { return uint8_t((a * 2246822519u) >> 24); }
uint32_t csAt(uint32_t a) { return a * 2654435761u ^ 0x9E3779B9u; }

std::vector<uint8_t> hex(const std::string& s) {
    std::vector<uint8_t> v(s.size() / 2);
    for (size_t i = 0; i < v.size(); ++i) v[i] = uint8_t(std::stoul(s.substr(i * 2, 2), nullptr, 16));
    return v;
}
uint32_t h32(const std::string& s) { return uint32_t(std::stoul(s, nullptr, 16)); }

// Changed cells as "addr:val,..." (same format as the oracle), restoring the pattern as it goes.
// Past 256 cells: "H<count>:<FNV-1a 64 of addr,value pairs>", as the oracle writes it.
template <class T>
std::string diff(T* cur, const T* pat, size_t n, size_t maxListed = 256) {
    std::string out;
    constexpr size_t page = 4096 / sizeof(T);
    size_t count = 0;
    uint64_t h = 14695981039346656037ull;
    char buf[40];
    for (size_t p = 0; p < n; p += page) {
        size_t len = p + page < n ? page : n - p;
        if (!std::memcmp(cur + p, pat + p, len * sizeof(T))) continue;
        for (size_t i = p; i < p + len; ++i) {
            if (cur[i] == pat[i]) continue;
            uint32_t v = uint32_t(cur[i]);
            if (++count <= maxListed) { std::snprintf(buf, sizeof buf, "%zX:%X,", i, v); out += buf; }
            h = (h ^ uint32_t(i)) * 1099511628211ull;
            h = (h ^ v) * 1099511628211ull;
            cur[i] = pat[i];
        }
    }
    if (!count) return "-";
    if (count > maxListed) { std::snprintf(buf, sizeof buf, "H%zX:%llX", count, (unsigned long long)h); return buf; }
    out.pop_back();
    return out;
}

std::string hexs(const uint8_t* b, size_t n) {
    std::string s;
    char buf[3];
    for (size_t i = 0; i < n; ++i) { std::snprintf(buf, 3, "%02X", b[i]); s += buf; }
    return s;
}

} // namespace

// Instruction mixes, each timed through the app's own Machine::run and counted once with step().
// "ADD r,n" is straight-line code comparable to the reference's RunSingleInstructionRepeatedly
// (~104 M instr/s on the dev machine); the others are loops shaped like real guest code.
int bench(const char* arg) {
    bool tracked = arg && !std::strcmp(arg, "--tracked");
    const char* watchText = tracked ? nullptr : arg;
    auto m = std::make_unique<TrackedMachine>();   // as the app allocates it (service/cpu_tracked.cpp casts)
    m->reset();
    if (tracked) m->trackActivity(true);           // run() hands the CPU to the tracked loop (D-26)
    static std::vector<svc::watch::Condition> conds;   // the WATCHER's per-instruction check (win_watcher.cpp)
    static uint64_t hits = 0;                           // counted, not stopped on: a mix may meet the condition
    if (watchText) {
        if (!svc::watch::parseAll({watchText}, conds) || conds.empty()) { std::fprintf(stderr, "bench: not a condition\n"); return 2; }
        m->watchHook = [](Machine& mm) { hits += svc::watch::allTrue(mm, conds); };
        m->callbacksPending.fetch_or(Machine::kWatchBit);
    }
    auto none = [](const std::string&) { return std::optional<std::string>(); };
    auto asmMix = [&](const std::string& src) {
        AsmResult a = assemble(src, "<bench>", none);
        for (auto& b : a.blocks) std::memcpy(m->ram + b.start, b.data.data(), b.data.size());
        return a.blocks.empty() || !a.errors.empty() ? ~0u : a.blocks[0].start;
    };
    auto cMix = [&](const std::string& src) {
        CcResult r = compileC(src, "bench.c", [](const std::string&, const std::string&) { return std::optional<std::string>(); });
        return r.asmText.empty() ? ~0u : asmMix(r.asmText);
    };
    struct Mix { const char* name; uint32_t start; int passes; };
    std::vector<Mix> mixes;
    constexpr int kN = 1'000'000;
    constexpr uint32_t kAt = 0x800000;   // clear of the other mixes and of compiled C (0x080000)
    for (int i = 0; i < kN; ++i) {   // ADD r,n x 1M, then BREAK
        uint8_t* p = m->ram + kAt + i * 4;
        p[0] = 2; p[1] = 0; p[2] = uint8_t(i % 26); p[3] = uint8_t(i);
    }
    m->ram[kAt + kN * 4] = 253;
    mixes.push_back({"ADD r,n", kAt, 200});
    mixes.push_back({"loop (CPU-benchmark.asm)", asmMix(   // LD/DEC/CP/JR, as Data's CPU benchmark
        "#ORG 0x500000\n LD XYZ, 2000000\n.L\n LD XYZ, XYZ\n DEC XYZ\n CP XYZ, 0\n JR GT, .L\n BREAK\n"), 20});
    mixes.push_back({"memory LD (rrr)", asmMix(            // byte/word loads and stores through registers
        "#ORG 0x510000\n LD XYZ, 1000000\n LD BCD, 0x600000\n.L\n LD E, (BCD)\n ADD E, 3\n LD (BCD + 0x100000), E\n"
        " LD FGHI, (BCD + 4)\n LD (BCD + 0x100004), FGHI\n INC BCD\n DJNZ XYZ, .L\n BREAK\n"), 20});
    mixes.push_back({"CALL/RET", asmMix(
        "#ORG 0x520000\n LD XYZ, 1000000\n.L\n CALL .F\n CALLR .F\n DJNZ XYZ, .L\n BREAK\n.F\n ADD B, 1\n RET\n"), 20});
    mixes.push_back({"compiled C", cMix(                     // the C compiler's code: stack frames, (rrr) locals, CALL
        "int f(int x) { return x * 3 + 1; }\n"
        "int main() { int s = 0; for (int i = 0; i < 100000; i++) s = s + (f(i) ^ (s >> 3)); return s; }\n"), 20});
    int bad = 0;
    for (const Mix& x : mixes) {
        if (x.start == ~0u) { std::printf("bench: %-26s does not build\n", x.name); ++bad; continue; }
        auto go = [&] { m->ip = x.start; m->spr = m->spc = 0; std::memset(m->regs, 0, sizeof m->regs); m->running = true; };
        uint64_t n = 0;
        for (go(); m->running.load(); ++n) m->step();   // count once
        uint32_t ipEnd = m->ip;
        uint8_t regs[kRegs];
        std::memcpy(regs, m->regs, kRegs);
        auto t0 = std::chrono::steady_clock::now();
        for (int pass = 0; pass < x.passes; ++pass) { go(); m->run(); }
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        bool same = m->ip == ipEnd && !std::memcmp(regs, m->regs, kRegs);   // run() ends where step() did
        bad += !same;
        std::printf("bench: %-26s %7.1f M instr/s (%5.1f M x %d in %.2f s) A=%u%s\n", x.name, double(n) * x.passes / s / 1e6,
                    n / 1e6, x.passes, s, m->regs[0], same ? "" : "  MISMATCH with step()");
    }
    if (watchText) std::printf("bench: watching %s (%llu hits)\n", watchText, (unsigned long long)hits);
    if (tracked) std::printf("bench: tracked loop (service mode's MEMORY MAP)%s\n", m->activity[0x800000 >> Machine::kPageShift] ? "" : "; NOTHING MARKED");
    return bad ? 1 : 0;
}

int ldFill() {
    struct Case { const char* name; std::vector<uint8_t> ins; uint32_t at; std::vector<int> want; };   // -1 = untouched
    const Case cases[] = {
        {"LD (0x1000),0x11223344,6", {1, 58, 0x00, 0x10, 0x00, 0x11, 0x22, 0x33, 0x44, 6}, 0x1000, {0, 0, 0x11, 0x22, 0x33, 0x44, -1}},
        {"LD (ABC=0x2000),0xAABBCCDD,2", {1, 143, 0, 0xAA, 0xBB, 0xCC, 0xDD, 2}, 0x2000, {0xCC, 0xDD, -1}},
        {"LD (0x3000),0x102,3,4", {1, 59, 0x00, 0x30, 0x00, 0, 0, 1, 2, 3, 0, 0, 4}, 0x3000,
         {0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, -1}},
        {"LD (0x4000),0x11223344,6,0 (0 repeats)", {1, 59, 0x00, 0x40, 0x00, 0x11, 0x22, 0x33, 0x44, 6, 0, 0, 0}, 0x4000,
         {-1, -1, 0x11, 0x22, 0x33, 0x44, -1}},
        {"LD (0x5000),(0x6000),5,DEF=2", {1, 64, 0x00, 0x50, 0x00, 0x00, 0x60, 0x00, 5, 3}, 0x5000,
         {0, 0xDE, 0xAD, 0xBE, 0xEF, 0, 0xDE, 0xAD, 0xBE, 0xEF, -1}},
        {"LD (ABC=0x7010,GH=-16),0x55,1", {1, 194, 0, 6, 0, 0, 0, 0x55, 1}, 0x7000, {0x55, -1}},
    };
    auto m = std::make_unique<Machine>();
    int bad = 0;
    for (const Case& c : cases) {
        m->reset();
        for (uint32_t a = 0; a < kRamSize; ++a) m->ram[a] = 0xA5;
        m->r[0] = 0x00; m->r[1] = 0x20; m->r[2] = 0x00;            // ABC = 0x002000
        if (c.ins[1] == 194) { m->r[1] = 0x70; m->r[2] = 0x10; }  // ABC = 0x007010
        m->r[3] = 0; m->r[4] = 0; m->r[5] = 2;                     // DEF = 2
        m->r[6] = 0xFF; m->r[7] = 0xF0;                            // GH = -16
        m->ram[0x6000] = 0xDE; m->ram[0x6001] = 0xAD; m->ram[0x6002] = 0xBE; m->ram[0x6003] = 0xEF;
        m->ip = 0x80000;
        std::memcpy(m->ram + m->ip, c.ins.data(), c.ins.size());
        m->step();
        bool ok = m->ip == 0x80000 + c.ins.size();
        for (size_t i = 0; i < c.want.size(); ++i) ok &= m->ram[c.at + i] == (c.want[i] < 0 ? 0xA5 : c.want[i]);
        ok &= m->ram[c.at - 1] == 0xA5;
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", c.name);
        bad += !ok;
    }
    return bad ? 1 : 0;
}

// INT 2 self-test: synthetic host input through the same entry points main.cpp uses, checked
// against bytes worked out by hand from the C# sources (InterruptsInput.cs, KeyboardStateExt.cs,
// GamepadStateExt.cs, MonoGame GamePad.SDL.cs); gamepad floats were evaluated in float32.
int inputTest() {
    auto m = std::make_unique<Machine>();
    m->reset();
    int bad = 0;
    auto check = [&](bool ok, const char* what) { std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); bad += !ok; };
    auto call = [&](uint8_t fn, uint32_t dest = 0x2000) {   // INT A,2 with A = fn, BCD = dest
        m->r[0] = fn; m->set<24>(1, dest);
        gInt[2][fn](*m, 0);
    };
    auto ramIs = [&](uint32_t a, std::vector<uint8_t> want) { return std::memcmp(m->ram + a, want.data(), want.size()) == 0; };

    // Keyboard state: LeftShift also sets 16 (any Shift); bits are MSB-first per byte.
    inputKey(160, true); inputKey(65, true);
    call(0x00);
    std::vector<uint8_t> bits(32, 0); bits[2] = 0x80; bits[8] = 0x40; bits[20] = 0x80;
    check(ramIs(0x2000, bits) && m->r[0] == 32, "0x00 keys as bits (16, 65, 160), A = 32");
    call(0x10);
    check(ramIs(0x2000, {16, 65, 160}) && m->r[0] == 3, "0x10 keys as codes, A = 3");
    inputKey(161, true); inputKey(160, false);
    check(gInput.keys[16], "any-Shift stays while RightShift is held");
    inputKey(161, false); inputKey(65, false); inputKey(163, true);
    check(!gInput.keys[16] && gInput.keys[17], "any-Shift clears; RightControl sets any-Ctrl (17)");
    inputKey(163, false);

    // Keyboard buffer: B reports the chars left after the read; at 255+ the count before it.
    inputChar(*m, 'a'); inputChar(*m, 'b');
    call(0x01); bool ok = m->r[0] == 'a' && m->r[1] == 1;
    call(0x01); ok &= m->r[0] == 'b' && m->r[1] == 0;
    call(0x01); ok &= m->r[0] == 0 && m->r[1] == 0;
    check(ok, "0x01 buffer 'a','b', then empty");
    for (int i = 0; i < 300; ++i) inputChar(*m, 'x');
    call(0x01); ok = m->r[1] == (300 & 0xFF);          // 300 >= 255: count before removing, as a byte
    for (int i = 0; i < 45; ++i) call(0x01);           // the last of these starts with 255
    ok &= m->r[1] == 255;
    call(0x01); ok &= m->r[1] == 253;                  // 254 -> remove -> 253
    check(ok && gInput.chars.size() == 253, "0x01 overflow counts (44, 255, 253)");
    gInput.chars.clear();

    // Keyboard callback: the char lands twice, IP is pushed (24-bit) and the CPU jumps there.
    call(0x02, 0x123456);
    m->ip = 0x1000; m->spr = 0; m->ram[0x1000] = 253; m->ram[0x123456] = 253;   // BREAK at both
    inputChar(*m, 'k');
    m->running = true; m->run();
    check(gInput.chars.size() == 2 && m->rs[0] == 0x00 && m->rs[1] == 0x10 && m->rs[2] == 0x00 &&
          m->spr == 3 && m->ip == 0x123457, "0x02 callback: char twice, IP 0x001000 pushed, jumped to 0x123456");
    call(0x02, 0xFFFFFF); gInput.chars.clear();

    // A char above 255 halts the machine (Convert.ToByte throws) and stays buffered.
    inputChar(*m, 0x20AC);
    m->running = true;
    call(0x01);
    check(!m->running && gInput.chars.size() == 1 && m->r[0] == 1, "0x01 U+20AC halts the machine, A untouched");
    gInput.chars.clear();

    // Mouse: BC = X, DE = Y, F = buttons.
    gInput.mouseX = 479; gInput.mouseY = 269; gInput.mouseButtons = 5;
    call(0x03);
    check(m->get<16>(1) == 479 && m->get<16>(3) == 269 && m->r[5] == 5, "0x03 mouse 479,269 buttons 5");

    // Gamepads. Pad 0: LX 16384 -> 0.342 -> 171; RX -32768 -> -1 -> 0; RY -20000 -> up 0.470 -> 187;
    // LT full -> 255; RT 100 -> 0.003 -> 0 but "pressed". Pad 1: connected, idle.
    const int16_t ax0[6] = {16384, 0, -32768, -20000, 32767, 100}, ax1[6] = {};
    gInput.pads[0] = padFromSdl(ax0, kPadA | kPadUp | kPadStart);
    gInput.pads[1] = padFromSdl(ax1, 0);
    call(0x14);
    std::vector<uint8_t> pads(41, 0);
    uint8_t p0[] = {0x11, 0x68, 0x1C, 0xEC, 255, 0, 171, 127, 0, 187}, p1[] = {0, 0, 0x80, 0, 0, 0, 127, 127, 127, 127};
    pads[0] = 0x13;   // pads 0 and 1 connected, pad 0 has something pressed
    std::memcpy(&pads[1], p0, 10); std::memcpy(&pads[11], p1, 10);
    check(ramIs(0x2000, pads), "0x14 gamepad state (41 bytes)");

    gInput.caps[0] = padCapsFromMapping("030000005e040000,Xbox 360 Controller,a:b0,b:b1,back:b6,dpdown:h0.4,dpleft:h0.8,"
        "dpright:h0.2,dpup:h0.1,guide:b10,leftshoulder:b4,leftstick:b8,lefttrigger:a2,leftx:a0,lefty:a1,rightshoulder:b5,"
        "rightstick:b9,righttrigger:a5,rightx:a3,righty:a4,start:b7,x:b2,y:b3,platform:Windows", true, "Xbox 360 Controller");
    gInput.caps[2] = padCapsFromMapping(nullptr, false, "Pad \xC3\xA9\xF0\x9F\x98\x80");   // "Pad é😀"
    call(0x15);
    check(ramIs(0x2000, {0x05, 0xFF, 0x7F, 0xFF, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0}), "0x15 capabilities (17 bytes)");
    call(0x16);
    check(m->r[0] == 29 && ramIs(0x2000, std::vector<uint8_t>((const uint8_t*)"Xbox 360 Controller\0\0Pad ??\0",
          (const uint8_t*)"Xbox 360 Controller\0\0Pad ??\0" + 29)), "0x16 names: ASCII, one '?' per non-ASCII char");
    gInput.caps[0].name.assign(200, 'n'); gInput.caps[2].name.assign(100, 'm');   // 300 > 252: cut to 168 + 84
    call(0x16);
    check(m->r[0] == 0 && m->ram[0x2000 + 167] == 'n' && m->ram[0x2000 + 168] == 0 && m->ram[0x2000 + 169] == 0 &&
          m->ram[0x2000 + 170 + 83] == 'm' && m->ram[0x2000 + 254] == 0 && m->ram[0x2000 + 255] == 0,
          "0x16 names cut in proportion; 256 bytes -> A = 0");

    std::printf("input: %s\n", bad ? "FAIL" : "all ok");
    return bad ? 1 : 0;
}

// Program vectors (oracle --programs): load each program into a fresh machine, run the same
// number of instructions, compare registers, machine state and every changed RAM byte.
int programs(const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { std::fprintf(stderr, "conformance: cannot open %s\n", path); return 2; }
    auto m = std::make_unique<Machine>();
    static std::vector<uint8_t> image(kRamSize + 4);
    std::string line, name;
    uint32_t steps = 0, start = 0;
    bool first = true;
    int pass = 0, fail = 0;
    char buf[160];
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;
        if (tag == "P") { std::string st; ls >> name >> st; steps = h32(st); m->reset(); first = true; continue; }
        if (tag == "B") {
            std::string a, bytes;
            ls >> a >> bytes;
            auto b = hex(bytes);
            uint32_t at = h32(a);
            std::memcpy(m->ram + at, b.data(), b.size());
            if (first) { start = at; first = false; }
            continue;
        }
        if (tag == "E") {
            std::string want;
            std::getline(ls, want);
            want.erase(0, want.find_first_not_of(' '));
            std::memcpy(image.data(), m->ram, image.size());
            m->ip = start;
            m->running = true;
            uint32_t done = 0;
            for (; done < steps && m->running.load(std::memory_order_relaxed); ++done) m->step();
            std::string got = hexs(m->r, kRegs);
            std::snprintf(buf, sizeof buf, " %02X %X %X %X %X %X %d %X %X %X", m->flagsByte(), m->ip, m->spr, m->spc,
                          m->bank, done, m->running ? 1 : 0, unsigned(m->vramPages), unsigned(m->layerVisible), unsigned(m->layerAuto));
            got += buf;
            std::string dl, wantD;
            std::getline(in, dl);
            wantD = dl.substr(2);
            std::string gotD = diff(m->ram, image.data(), kRamSize + 4, SIZE_MAX);   // program vectors list every cell
            bool ok = got == want && gotD == wantD;
            ok ? ++pass : ++fail;
            if (ok) { std::printf("ok   %s (%u steps)\n", name.c_str(), done); continue; }
            std::printf("FAIL %s\n  want %s\n  got  %s\n", name.c_str(), want.c_str(), got.c_str());
            if (gotD != wantD) {   // first differing RAM entry is usually enough to start
                size_t i = 0;
                while (i < gotD.size() && i < wantD.size() && gotD[i] == wantD[i]) ++i;
                size_t from = gotD.rfind(',', i) == std::string::npos ? 0 : gotD.rfind(',', i) + 1;
                std::printf("  RAM differs near: want ...%s\n                    got  ...%s\n",
                            wantD.substr(std::min(from, wantD.size()), 60).c_str(), gotD.substr(std::min(from, gotD.size()), 60).c_str());
            }
        }
    }
    std::printf("programs: %d pass, %d fail\n", pass, fail);
    return fail ? 1 : 0;
}

// Assembler vectors (oracle --asm): assemble each file with the C++ assembler, compare with the reference.
std::optional<std::string> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// D-9: a line the reference assembled to a lone opcode byte, an error here (see encode in src/asm.cpp)
bool isD9(const AsmMsg& e) {
    return e.text.find("': no ") != std::string::npos || e.text.find("': unknown mnemonic ") != std::string::npos;
}

int asmTest(const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { std::fprintf(stderr, "conformance: cannot open %s\n", path); return 2; }
    struct Want { std::string file, src, threw; bool fromDisk = true; uint32_t errors = 0, run = 0; std::vector<uint32_t> errLines; std::vector<AsmBlock> blocks; long long refUs = 0; };
    std::vector<Want> all;
    std::string line;
    while (std::getline(in, line)) {
        if (line.size() < 2) continue;
        char tag = line[0];
        std::string rest = line.substr(2);
        if (tag == 'F' || tag == 'C') { all.emplace_back(); all.back().file = rest; all.back().fromDisk = tag == 'F'; continue; }
        Want& w = all.back();
        std::istringstream ls(rest);
        std::string a, b;
        ls >> a >> b;
        if (tag == 'S') { auto v = hex(a); w.src.assign(v.begin(), v.end()); }
        else if (tag == 'X') w.threw = rest;
        else if (tag == 'R') { w.errors = h32(a); w.run = h32(b); }
        else if (tag == 'L') w.errLines.push_back(h32(a));
        else if (tag == 'B') w.blocks.push_back({h32(a), hex(b), {}});
        else if (tag == 'T') w.refUs = std::stoll(a, nullptr, 16);
    }
    auto resolve = [](const std::string& p) { return readFile(p); };
    int pass = 0, fail = 0, d9n = 0;
    for (auto& w : all) {
        std::string src = w.fromDisk ? readFile(w.file).value_or("") : w.src;
        AsmResult r = assemble(src, w.fromDisk ? w.file : "", resolve);
        // D-9: lines the reference assembled to a lone opcode byte are errors here. Set them apart: the
        // rest (other errors, run address, block count) must still match; the bytes cannot.
        auto d9 = std::stable_partition(r.errors.begin(), r.errors.end(), [](const AsmMsg& e) { return !isD9(e); });
        std::vector<AsmMsg> d9s(d9, r.errors.end());
        r.errors.erase(d9, r.errors.end());
        std::string why;
        char buf[200];
        if (!w.threw.empty() || !r.fatal.empty()) {
            if (w.threw.empty() != r.fatal.empty()) why = "reference " + (w.threw.empty() ? std::string("did not throw") : "threw: " + w.threw) + "; C++ " + (r.fatal.empty() ? "did not" : "fatal: " + r.fatal);
        } else if (r.errors.size() != w.errors) {
            std::snprintf(buf, sizeof buf, "errors: want %u, got %zu", w.errors, r.errors.size()); why = buf;
            if (!r.errors.empty()) why += " (first: line " + std::to_string(r.errors[0].line) + " " + r.errors[0].text + ")";
        } else if (r.runAddress != w.run) {
            std::snprintf(buf, sizeof buf, "run address: want %X, got %X", w.run, r.runAddress); why = buf;
        } else if (r.blocks.size() != w.blocks.size()) {
            std::snprintf(buf, sizeof buf, "blocks: want %zu, got %zu", w.blocks.size(), r.blocks.size()); why = buf;
        } else {
            for (size_t i = 0; i < r.errors.size() && why.empty(); ++i)
                if (r.errors[i].line != w.errLines[i]) { std::snprintf(buf, sizeof buf, "error line: want %u, got %u", w.errLines[i], r.errors[i].line); why = buf; }
            for (size_t i = 0; i < r.blocks.size() && why.empty() && d9s.empty(); ++i) {
                auto &g = r.blocks[i], &e = w.blocks[i];
                size_t k = 0;
                while (k < g.data.size() && k < e.data.size() && g.data[k] == e.data[k]) ++k;
                if (g.start == e.start && k == g.data.size() && k == e.data.size()) continue;
                uint32_t srcLine = 0;
                for (auto& [off, nr] : g.lines) if (off <= k) srcLine = nr;
                std::snprintf(buf, sizeof buf, "block %zu (start want %X got %X, %zu/%zu bytes) differs at offset %zu: want %s got %s",
                              i, e.start, g.start, e.data.size(), g.data.size(), k,
                              hexs(e.data.data() + k, std::min<size_t>(8, e.data.size() - std::min(k, e.data.size()))).c_str(),
                              hexs(g.data.data() + k, std::min<size_t>(8, g.data.size() - std::min(k, g.data.size()))).c_str());
                why = buf;
                if (srcLine) why += "\n  line " + std::to_string(srcLine) + ": " + r.source[srcLine - 1];
            }
        }
        if (why.empty() && !d9s.empty()) {
            ++d9n;
            std::printf("D-9  %s\n", w.file.c_str());
            for (auto& e : d9s) std::printf("  line %u: %s\n", e.line, e.text.c_str());
        } else {
            why.empty() ? ++pass : ++fail;
            std::printf("%s %s%s%s\n", why.empty() ? "ok  " : "FAIL", w.file.c_str(), why.empty() ? "" : "\n  ", why.c_str());
        }
        if (w.refUs) {   // timing: the OS is assembled at every boot
            double best = 1e30;
            for (int k = 0; k < 20; ++k) {
                auto t0 = std::chrono::steady_clock::now();
                assemble(src, w.file, resolve);
                best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
            }
            std::printf("     time: %.2f ms (reference %.1f ms)\n", best, double(w.refUs) / 1000.0);
        }
    }
    {   // D-9 self-check: such a line is an error naming the line, and emits nothing
        AsmResult a = assemble("#ORG 0x1000\nLD A, 1\nLD (BCD), 0\n//INT 0x01, A\nLD A, 2\n", "", resolve);
        AsmResult b = assemble("#ORG 0x1000\nLD A, 1\nLD A, 2\n", "", resolve);
        bool ok = a.errors.size() == 2 && a.errors[0].line == 3 && a.errors[0].text == "'LD (BCD), 0': no LD form takes operands (rrr),nnn" &&
                  a.errors[1].line == 4 && a.errors[1].text == "'//INT 0x01, A': unknown mnemonic //INT" &&
                  a.blocks.size() == 1 && b.blocks.size() == 1 && a.blocks[0].data == b.blocks[0].data;
        ok ? ++pass : ++fail;
        std::printf("%s D-9 self-check (error message, nothing emitted)\n", ok ? "ok  " : "FAIL");
    }
    std::printf("asm: %d ok, %d expected divergence (D-9), %d fail\n", pass, d9n, fail);
    return fail ? 1 : 0;
}

// BASIC vectors (oracle --basic): compile each source with the C++ compiler; assembly, error count and
// log must be identical. On a mismatch, print the first differing line with the line before it. Then the
// same source goes through INT 0 0xC1 (from a scratch guest file system next to the vectors): registers
// and loaded RAM must be what BuildBasic produced with the reference assembler's blocks.
std::string firstDiff(const std::string& want, const std::string& got) {
    size_t k = 0;
    while (k < want.size() && k < got.size() && want[k] == got[k]) ++k;
    if (k == want.size() && k == got.size()) return "";
    size_t start = want.rfind('\n', k ? k - 1 : 0);
    start = start == std::string::npos || k == 0 ? 0 : start + 1;
    size_t prev = start ? want.rfind('\n', start - 2) : std::string::npos;
    prev = prev == std::string::npos ? 0 : prev + 1;
    auto lineAt = [](const std::string& s, size_t from) {
        if (from >= s.size()) return std::string("<end>");
        std::string l = s.substr(from, s.find('\n', from) - from);
        if (!l.empty() && l.back() == '\r') l.back() = '$';   // make CRLF visible
        return l;
    };
    std::string n = std::to_string(std::count(want.begin(), want.begin() + start, '\n') + 1);
    return "line " + n + (start ? "\n    prev: " + lineAt(want, prev) : "") + "\n    want: " + lineAt(want, start) + "\n    got:  " + lineAt(got, start);
}

int basicTest(const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { std::fprintf(stderr, "conformance: cannot open %s\n", path); return 2; }
    struct Want {
        std::string file, src, asmText, log;
        int errors = 0;
        uint32_t run = 0;
        long long refUs = 0;
        bool assembled = false, threw = false;   // Q / X lines
        uint32_t asmErrors = 0, asmRun = 0;
        std::vector<AsmBlock> blocks;
    };
    std::vector<Want> all;
    std::string line;
    auto text = [](const std::string& h) { auto v = hex(h); return std::string(v.begin(), v.end()); };
    while (std::getline(in, line)) {
        if (line.size() < 2) continue;
        std::string rest = line.substr(2);
        if (line[0] == 'N') { all.emplace_back(); all.back().file = rest; continue; }
        Want& w = all.back();
        std::istringstream ls(rest);
        std::string a, b;
        ls >> a >> b;
        switch (line[0]) {
            case 'S': w.src = text(a); break;
            case 'R': w.errors = int(h32(a)); w.run = h32(b); break;
            case 'A': w.asmText = text(a); break;
            case 'G': w.log = text(a); break;
            case 'T': w.refUs = std::stoll(a, nullptr, 16); break;
            case 'Q': w.assembled = true; w.asmErrors = h32(a); w.asmRun = h32(b); break;
            case 'B': w.blocks.push_back({h32(a), hex(b), {}}); break;
            case 'X': w.threw = true; break;
        }
    }
    initInterrupts();
    auto fsRoot = std::filesystem::path(path).parent_path() / "basic-fs";
    std::filesystem::create_directories(fsRoot);
    gFsRoot = fsRoot.string();
    auto m = std::make_unique<Machine>();
    int pass = 0, fail = 0, d9n = 0;
    for (auto& w : all) {
        BasicResult r = compileBasic(w.src);
        std::string why;
        size_t d9 = 0;   // D-9 lines in the generated assembly: more errors in E, and the RAM cannot match
        if (!w.threw && !w.errors)
            for (auto& e : assemble(r.asmText, "", [](const std::string&) { return std::optional<std::string>(); }).errors) d9 += isD9(e);
        if (r.errors != w.errors) why = "errors: want " + std::to_string(w.errors) + ", got " + std::to_string(r.errors) + "\n  ";
        if (r.runAddress != w.run) why += "run address differs\n  ";
        if (std::string d = firstDiff(w.asmText, r.asmText); !d.empty()) why += "asm " + d + "\n  ";
        if (std::string d = firstDiff(w.log, r.log); !d.empty()) why += "log " + d + "\n  ";
        if (!w.threw) {   // the reference's BuildBasic threw with the assembler: nothing to compare
            std::ofstream(fsRoot / "t.bas", std::ios::binary) << w.src;
            m->reset();
            std::memcpy(m->ram + 0x100, "t.bas", 6);
            m->set<24>(1, 0x100);   // INT 0 0xC1 with A: path in BCD, errors in E
            m->set<8>(4, 0xEE);
            gInt[0][0xC1](*m, 0);
            bool none = w.errors == 0 && w.blocks.empty();
            uint32_t run = w.errors || none ? 0xFFFFFF : w.asmRun;
            uint32_t errs = w.errors ? std::min(w.errors, 255) : none ? 0xEE : ((w.asmErrors + d9) & 0xFF);
            char buf[120];
            if (m->get<24>(1) != run || m->get<8>(4) != errs) {
                std::snprintf(buf, sizeof buf, "INT 0xC1: BCD/E want %X/%X, got %X/%X\n  ", run, errs, m->get<24>(1), m->get<8>(4));
                why += buf;
            }
            for (auto& b : w.blocks)
                if (!d9 && std::memcmp(m->ram + b.start, b.data.data(), b.data.size())) {
                    std::snprintf(buf, sizeof buf, "INT 0xC1: block at %X differs in RAM\n  ", b.start);
                    why += buf;
                }
        }
        if (why.empty() && d9) {
            ++d9n;
            std::printf("D-9  %s (%zu line(s) of the generated assembly)\n", w.file.c_str(), d9);
        } else {
            why.empty() ? ++pass : ++fail;
            std::printf("%s %s%s%s\n", why.empty() ? "ok  " : "FAIL", w.file.c_str(), why.empty() ? "" : "\n  ", why.c_str());
        }
        if (w.refUs) {
            double best = 1e30;
            for (int k = 0; k < 20; ++k) {
                auto t0 = std::chrono::steady_clock::now();
                compileBasic(w.src);
                best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
            }
            std::printf("     time: %.2f ms (reference %.2f ms), %zu source bytes\n", best, double(w.refUs) / 1000.0, w.src.size());
        }
    }
    std::printf("basic: %d ok, %d expected divergence (D-9), %d fail\n", pass, d9n, fail);
    return fail ? 1 : 0;
}

// C compiler vectors (oracle --cc): compile each recorded source with the C++ compiler, replaying the
// reference's include answers; compare assembly text, errors, log and #pragma origin/stack exactly.
std::string unb64(const std::string& s) {
    if (s == "-") return "";
    std::string out;
    uint32_t acc = 0;
    int bits = 0;
    for (char c : s) {
        int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1;
        if (v < 0) continue;
        acc = acc << 6 | uint32_t(v);
        if ((bits += 6) >= 8) { bits -= 8; out += char(acc >> bits & 0xFF); }
    }
    return out;
}

// First differing line of two texts, with two lines of context from the reference.
std::string ccFirstDiff(const std::string& want, const std::string& got) {
    auto lines = [](const std::string& t) { std::vector<std::string> v; std::istringstream s(t); for (std::string l; std::getline(s, l);) v.push_back(l); return v; };
    auto w = lines(want), g = lines(got);
    size_t i = 0;
    while (i < w.size() && i < g.size() && w[i] == g[i]) ++i;
    std::string r = "line " + std::to_string(i + 1) + " (want " + std::to_string(w.size()) + " lines, got " + std::to_string(g.size()) + ")";
    for (size_t k = i >= 2 ? i - 2 : 0; k < i; ++k) r += "\n      " + w[k];
    r += "\n  want: " + (i < w.size() ? w[i] : "<end>") + "\n  got:  " + (i < g.size() ? g[i] : "<end>");
    return r;
}

int ccTest(const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { std::fprintf(stderr, "conformance: cannot open %s\n", path); return 2; }
    struct Query { std::string name, including; std::optional<std::string> text; };
    struct Want { std::string name, file, src, asmText, log, threw; std::vector<Query> queries; std::vector<CcError> errors; uint32_t origin = 0, stack = 0; long long refUs = 0; };
    std::vector<Want> all;
    std::string line;
    while (std::getline(in, line)) {
        if (line.size() < 2) continue;
        char tag = line[0];
        std::string rest = line.substr(2);
        if (tag == 'N') { all.emplace_back(); all.back().name = rest; continue; }
        Want& w = all.back();
        std::istringstream ls(rest);
        std::string a, b, c, d;
        ls >> a >> b >> c >> d;
        if (tag == 'F') w.file = unb64(a);
        else if (tag == 'S') w.src = unb64(a);
        else if (tag == 'I') w.queries.push_back({unb64(a), unb64(b), c == "-" ? std::nullopt : std::optional<std::string>(unb64(c))});
        else if (tag == 'A') w.asmText = unb64(a);
        else if (tag == 'E') w.errors.push_back({unb64(c), std::stoi(a), std::stoi(b), unb64(d)});
        else if (tag == 'G') { for (char ch : unb64(a)) if (ch != '\r') w.log += ch; }
        else if (tag == 'O') { w.origin = h32(a); w.stack = h32(b); }
        else if (tag == 'X') w.threw = rest;
        else if (tag == 'T') w.refUs = std::stoll(a, nullptr, 16);
    }
    int pass = 0, fail = 0;
    for (auto& w : all) {
        std::vector<Query> asked;
        auto resolve = [&](const std::string& name, const std::string& including) -> std::optional<std::string> {
            for (auto& q : w.queries) if (q.name == name && q.including == including) { asked.push_back(q); return q.text; }
            asked.push_back({name, including, std::nullopt});
            return std::nullopt;
        };
        CcResult r = compileC(w.src, w.file, resolve);
        std::string why;
        auto errText = [](const CcError& e) { return e.file + ":" + std::to_string(e.line) + ":" + std::to_string(e.col) + ": " + e.message; };
        bool sameQueries = asked.size() == w.queries.size();
        for (size_t i = 0; sameQueries && i < asked.size(); ++i) sameQueries = asked[i].name == w.queries[i].name && asked[i].including == w.queries[i].including;
        if (!w.threw.empty() || !r.fatal.empty()) {
            if (w.threw.empty() != r.fatal.empty()) why = "reference " + (w.threw.empty() ? std::string("did not throw") : "threw " + w.threw) + "; C++ " + (r.fatal.empty() ? "did not" : "fatal " + r.fatal);
        } else if (!sameQueries) {
            why = "include queries differ (want " + std::to_string(w.queries.size()) + ", got " + std::to_string(asked.size()) + ")";
        } else if (r.errors.size() != w.errors.size()) {
            why = "errors: want " + std::to_string(w.errors.size()) + ", got " + std::to_string(r.errors.size());
            if (!w.errors.empty()) why += "\n  want: " + errText(w.errors[0]);
            if (!r.errors.empty()) why += "\n  got:  " + errText(r.errors[0]);
        } else {
            for (size_t i = 0; i < r.errors.size() && why.empty(); ++i)
                if (errText(r.errors[i]) != errText(w.errors[i])) why = "error " + std::to_string(i) + "\n  want: " + errText(w.errors[i]) + "\n  got:  " + errText(r.errors[i]);
            if (why.empty() && r.asmText != w.asmText) why = "assembly differs at " + ccFirstDiff(w.asmText, r.asmText);
            if (why.empty() && r.log != w.log) why = "log differs at " + ccFirstDiff(w.log, r.log);
            if (why.empty() && (r.origin != w.origin || r.stackTop != w.stack)) why = "origin/stack differ";
        }
        why.empty() ? ++pass : ++fail;
        if (!why.empty()) std::printf("FAIL %s\n  %s\n", w.name.c_str(), why.c_str());
        if (w.refUs) {   // timing: Halcyon is compiled at every boot
            double best = 1e30;
            for (int k = 0; k < 20; ++k) {
                auto t0 = std::chrono::steady_clock::now();
                compileC(w.src, w.file, resolve);
                best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
            }
            std::printf("     %s: %.2f ms (reference %.1f ms)\n", w.name.c_str(), best, double(w.refUs) / 1000.0);
        }
    }
    std::printf("cc: %d ok, %d fail\n", pass, fail);
    return fail ? 1 : 0;
}

// XSound vectors (oracle --xsound): each sound definition is parsed and rendered by the C++ generator
// with the same noise seed; the int16 PCM must match sample for sample, failures must match too.
int xsoundTest(const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { std::fprintf(stderr, "conformance: cannot open %s\n", path); return 2; }
    auto m = std::make_unique<Machine>();
    int pass = 0, fail = 0;
    long long samples = 0, diffs = 0;
    int maxErr = 0;
    double us = 0;
    for (std::string line; std::getline(in, line);) {
        std::istringstream ls(line);
        std::string kind, name, def, what;
        int32_t seed = 0;
        ls >> kind >> name >> def >> seed;
        std::vector<int16_t> want;
        if (kind == "S") {
            size_t n = 0;
            ls >> n;
            want.resize(n);
            in.read(reinterpret_cast<char*>(want.data()), std::streamsize(n * 2));   // little-endian, as this host
        } else std::getline(ls >> what >> std::ws, kind);   // X: kind := the reference's message
        std::vector<uint8_t> d = hex(def);
        std::memset(m->ram + 0x100000, 0, d.size() + 16);
        std::memcpy(m->ram + 0x100000, d.data(), d.size());
        XSoundParams p;
        std::string err = readSoundParams(*m, 0x100000, p), stage = "parse";
        std::vector<int16_t> got;
        if (err.empty()) {
            NetRandom noise(seed);
            auto t0 = std::chrono::steady_clock::now();
            err = xsoundGenerate(p, noise, got);
            us += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
            stage = "gen";
        }
        std::string why;
        if (!want.empty() || line[0] == 'S') {
            if (!err.empty()) why = "C++ failed (" + stage + ": " + err + ")";
            else if (got.size() != want.size()) why = "length " + std::to_string(got.size()) + ", reference " + std::to_string(want.size());
            else {
                long long d0 = 0;
                for (size_t i = 0; i < got.size(); ++i)
                    if (got[i] != want[i]) { d0++; maxErr = std::max(maxErr, std::abs(got[i] - want[i])); }
                diffs += d0;
                if (d0) why = std::to_string(d0) + " samples differ";
            }
            samples += (long long)want.size();
        } else if (err.empty()) why = "C++ rendered it; reference threw (" + what + ": " + kind + ")";
        else if (stage != what) why = "C++ failed in " + stage + " (" + err + "); reference in " + what + " (" + kind + ")";
        if (why.empty()) pass++;
        else { fail++; std::printf("FAIL %s: %s\n", name.c_str(), why.c_str()); }
    }
    std::printf("xsound: %d ok, %d fail; %lld samples compared, %lld differ (max |error| %d); C++ generator %.0f ms\n",
                pass, fail, samples, diffs, maxErr, us / 1000);
    return fail ? 1 : 0;
}

// Service-mode disassembler: the text of ContinuumDebugger.GetDissassembledFull, line for line.
int disasmTest(const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { std::fprintf(stderr, "conformance: cannot open %s\n", path); return 2; }
    auto m = std::make_unique<Machine>();
    int pass = 0, fail = 0;
    uint32_t addr = 0;
    int count = 0;
    std::vector<uint8_t> bytes;
    std::vector<std::string> want;
    for (std::string line; std::getline(in, line);) {
        if (line.rfind("C ", 0) == 0) {
            std::istringstream ls(line.substr(2));
            std::string a, n, hex;
            ls >> a >> n >> hex;
            addr = uint32_t(std::stoul(a, nullptr, 16));
            count = int(std::stoul(n, nullptr, 16));
            bytes.clear();
            for (size_t i = 0; i + 1 < hex.size(); i += 2) bytes.push_back(uint8_t(std::stoul(hex.substr(i, 2), nullptr, 16)));
            want.clear();
        } else if (line.rfind("L ", 0) == 0) want.push_back(line.substr(2));
        else if (line == "E") {
            for (size_t i = 0; i < bytes.size(); ++i) m->ram[addr + i] = bytes[i];
            std::vector<std::string> got;
            char b[16];
            for (auto& d : svc::disassemble(*m, addr, count)) {
                std::snprintf(b, sizeof b, "%06X|", d.Address);
                std::string l = b;
                for (size_t i = 0; i < d.Bytes.size(); ++i) { std::snprintf(b, sizeof b, i ? " %02X" : "%02X", d.Bytes[i]); l += b; }
                got.push_back(l + "|" + d.SpecificInstruction);
            }
            for (size_t i = 0; i < bytes.size(); ++i) m->ram[addr + i] = 0;
            if (got == want) { ++pass; continue; }
            if (++fail <= 20) {
                std::printf("FAIL at %06X:\n", addr);
                for (size_t i = 0; i < std::max(got.size(), want.size()); ++i)
                    if (i >= got.size() || i >= want.size() || got[i] != want[i])
                        std::printf("  want %s\n  got  %s\n", i < want.size() ? want[i].c_str() : "(none)", i < got.size() ? got[i].c_str() : "(none)");
            }
        }
    }
    std::printf("disasm: %d ok, %d fail\n", pass, fail);
    return fail ? 1 : 0;
}

// Service-mode WATCHER: per line "-" blank, "X" not a condition, "T"/"F" its value; then whether all hold.
// The machine is the oracle's WatchMachine (tests/oracle/Watch.cs). Cases where the reference threw are skipped.
int watchTest(const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { std::fprintf(stderr, "conformance: cannot open %s\n", path); return 2; }
    auto m = std::make_unique<Machine>();
    const float fr[16] = {0.f, -0.f, 1.5f, -2.25f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::max(),
                          0.1f, 100.f, -100.f, 1e30f, 123.456f, 7.f, 15.5f};
    for (int i = 0; i < 26; ++i) m->regs[i] = ramAt(uint32_t(i + 1));
    for (int i = 0; i < 16; ++i) m->fregs[0][i] = fr[i];
    for (uint32_t a = 0; a < kRamSize; ++a) m->ram[a] = rsAt(a);
    int pass = 0, fail = 0, skipped = 0;
    for (std::string c, r; std::getline(in, c) && std::getline(in, r);) {
        c = c.substr(2), r = r.substr(2);
        if (r.find('E') != std::string::npos) { ++skipped; continue; }
        std::vector<std::string> lines;
        for (size_t b = 0;;) {
            size_t e = c.find('\t', b);
            lines.push_back(c.substr(b, e == std::string::npos ? e : e - b));
            if (e == std::string::npos) break;
            b = e + 1;
        }
        std::string got;
        for (auto& l : lines) {
            std::string_view t = svc::watch::trim(l);
            auto cond = t.empty() ? std::nullopt : svc::watch::parse(t);
            got += t.empty() ? '-' : !cond ? 'X' : svc::watch::evaluate(*m, *cond) ? 'T' : 'F';
        }
        std::vector<svc::watch::Condition> cs;
        got += svc::watch::parseAll(lines, cs) && !cs.empty() && svc::watch::allTrue(*m, cs) ? " 1" : " 0";
        if (got == r) { ++pass; continue; }
        if (++fail <= 30) std::printf("FAIL '%s': want %s, got %s\n", c.c_str(), r.c_str(), got.c_str());
    }
    std::printf("watch: %d ok, %d fail, %d skipped (the reference threw)\n", pass, fail, skipped);
    return fail ? 1 : 0;
}

// Service-mode IMMEDIATE (Ctrl+Enter): status text and the machine afterwards, on the oracle's ImmediateMachine.
int immediateTest(const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { std::fprintf(stderr, "conformance: cannot open %s\n", path); return 2; }
    auto m = std::make_unique<Machine>();
    static std::vector<uint8_t> ram0(kRamSize + 3), rs0(kRsSize);
    static std::vector<uint32_t> cs0(kCsSize);
    auto diffs = [](auto* cur, auto& start) {
        std::string o;
        char b[32];
        for (size_t i = 0; i < start.size(); ++i)
            if (cur[i] != start[i]) { std::snprintf(b, sizeof b, "%zX:%X,", i, unsigned(cur[i])); o += b; }
        if (o.empty()) return std::string("-");
        o.pop_back();
        return o;
    };
    int pass = 0, fail = 0, threw = 0;
    bool stepping = false;
    std::vector<std::string> lines;
    std::string wantS, wantR;
    for (std::string line; std::getline(in, line);) {
        if (line.rfind("C ", 0) == 0) { stepping = line[2] == '1'; lines.clear(); }
        else if (line.rfind("L ", 0) == 0) lines.push_back(line.substr(2));
        else if (line.rfind("S ", 0) == 0) wantS = line.substr(2);
        else if (line.rfind("R ", 0) == 0) wantR = line.substr(2);
        else if (line == "E") {
            m->reset();
            for (int i = 0; i < 26; ++i) m->regs[i] = ramAt(uint32_t(i + 1));
            m->ip = 0x001234, m->spr = 0x30, m->spc = 3;
            m->running = true;   // as in the app: a halt (illegal instruction) ends the run where the reference threw
            std::memcpy(ram0.data(), m->ram, ram0.size());
            std::memcpy(rs0.data(), m->rs, rs0.size());
            std::memcpy(cs0.data(), m->cs, cs0.size() * 4);
            std::string gotS = svc::immediate::compileAndRun(*m, lines, stepping).message;
            std::string gotR;
            char b[64];
            for (int i = 0; i < 26; ++i) { std::snprintf(b, sizeof b, "%02X", m->r[i]); gotR += b; }
            std::snprintf(b, sizeof b, " %02X ", m->flagsByte());
            gotR += b;
            for (int i = 0; i < 16; ++i) {
                uint32_t u;
                std::memcpy(&u, &m->fregs[m->fbank][i], 4);
                std::snprintf(b, sizeof b, "%08X", u);
                gotR += b;
            }
            std::snprintf(b, sizeof b, " %X %X %X %X %X ", m->bank, m->fbank, m->ip, m->spr, m->spc);
            gotR += b + diffs(m->ram, ram0) + " " + diffs(m->cs, cs0) + " " + diffs(m->rs, rs0);
            if (wantS.rfind("THREW", 0) == 0 && gotR == wantR) { ++threw; continue; }   // the reference app died there
            if (gotS == wantS && gotR == wantR) { ++pass; continue; }
            if (++fail <= 20) {
                std::printf("FAIL (stepping %d):\n", int(stepping));
                for (auto& l : lines) std::printf("  | %s\n", l.c_str());
                if (gotS != wantS) std::printf("  status want '%s'\n         got  '%s'\n", wantS.c_str(), gotS.c_str());
                if (gotR != wantR) std::printf("  state want %s\n        got  %s\n", wantR.c_str(), gotR.c_str());
            }
        }
    }
    std::printf("immediate: %d ok, %d fail, %d where the reference threw (same machine state, other status)\n", pass, fail, threw);
    return fail ? 1 : 0;
}

int main(int argc, char** argv) {
    if (const char* fs = std::getenv("C93_FS")) gFsRoot = fs;   // guest file system for program tests
    if ((argc == 2 || argc == 3) && !std::strcmp(argv[1], "--bench")) return bench(argc == 3 ? argv[2] : nullptr);
    if (argc == 2 && !std::strcmp(argv[1], "--ldfill")) return ldFill();
    if (argc == 2 && !std::strcmp(argv[1], "--input")) return inputTest();
    if (argc == 3 && !std::strcmp(argv[1], "--programs")) return programs(argv[2]);
    if (argc == 3 && !std::strcmp(argv[1], "--asm")) return asmTest(argv[2]);
    if (argc == 3 && !std::strcmp(argv[1], "--basic")) return basicTest(argv[2]);
    if (argc == 3 && !std::strcmp(argv[1], "--cc")) return ccTest(argv[2]);
    if (argc == 3 && !std::strcmp(argv[1], "--disasm")) return disasmTest(argv[2]);
    if (argc == 3 && !std::strcmp(argv[1], "--xsound")) return xsoundTest(argv[2]);
    if (argc == 3 && !std::strcmp(argv[1], "--watch")) return watchTest(argv[2]);
    if (argc == 3 && !std::strcmp(argv[1], "--immediate")) return immediateTest(argv[2]);
    if (argc < 2) { std::fprintf(stderr, "usage: conformance <vectors.txt> [mnemonic ...]\n"); return 2; }
    std::set<std::string> only(argv + 2, argv + argc);
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) { std::fprintf(stderr, "conformance: cannot open %s\n", argv[1]); return 2; }

    auto m = std::make_unique<Machine>();
    m->reset();
    static std::vector<uint8_t> ramP(kRamSize + 4), rsP(kRsSize + 4);
    static std::vector<uint32_t> csP(kCsSize);
    for (uint32_t a = 0; a < ramP.size(); ++a) m->ram[a] = ramP[a] = ramAt(a);
    for (uint32_t a = 0; a < rsP.size(); ++a) m->rs[a] = rsP[a] = rsAt(a);
    for (uint32_t a = 0; a < csP.size(); ++a) m->cs[a] = csP[a] = csAt(a);

    std::map<int, std::string> names;
    std::map<int, std::pair<int, int>> score;   // variant -> (pass, fail)
    std::map<int, std::string> firstFail;
    int skipped = 0;
    std::string line;   // memory-fill cases list megabytes of changed cells on one line
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string tag, vs;
        ls >> tag >> vs;
        int v = int(h32(vs));
        if (tag == "V") { std::string rest; std::getline(ls, rest); names[v] = rest.substr(1); continue; }
        std::string mnem = names[v].substr(0, names[v].find(' '));
        if (!only.empty() && !only.count(mnem)) continue;
        if (tag == "X") { ++skipped; continue; }

        std::string ins, regs, flags, fr, spr, spc, arrow;
        ls >> ins >> regs >> flags >> fr >> spr >> spc >> arrow;
        auto bytes = hex(ins), r = hex(regs), fb = hex(fr);
        m->setBank(0);
        m->fbank = 0;
        std::memcpy(m->r, r.data(), kRegs);
        uint8_t fl = uint8_t(h32(flags));
        for (int i = 0; i < 8; ++i) m->f[i] = fl >> i & 1;
        for (int i = 0; i < 16; ++i) {   // floats arrive as big-endian hex of their bit pattern
            uint32_t bits = uint32_t(fb[i * 4]) << 24 | uint32_t(fb[i * 4 + 1]) << 16 | uint32_t(fb[i * 4 + 2]) << 8 | fb[i * 4 + 3];
            std::memcpy(&m->fregs[0][i], &bits, 4);
        }
        m->spr = h32(spr);
        m->spc = h32(spc);
        m->ip = 0x080000;
        std::memcpy(m->ram + m->ip, bytes.data(), bytes.size());

        m->step();

        std::string got = hexs(m->r, kRegs);
        char buf[96];
        std::snprintf(buf, sizeof buf, " %02X ", m->flagsByte());
        got += buf;
        for (int i = 0; i < 16; ++i) {
            uint32_t bits;
            std::memcpy(&bits, &m->fregs[m->fbank][i], 4);   // the oracle reads the bank in effect after
            std::snprintf(buf, sizeof buf, "%08X", bits);
            got += buf;
        }
        // the oracle reads registers through the current bank too
        std::snprintf(buf, sizeof buf, " %X %X %X %X %X ", m->ip, m->spr, m->spc, m->bank, m->fbank);
        got += buf;
        got += diff(m->ram, ramP.data(), kRamSize + 4) + " ";
        got += diff(m->rs, rsP.data(), kRsSize + 4) + " ";
        got += diff(m->cs, csP.data(), kCsSize);

        std::string want;
        std::getline(ls, want);
        while (!want.empty() && (want.back() == '\n' || want.back() == '\r')) want.pop_back();
        want.erase(0, want.find_first_not_of(' '));
        bool ok = got == want;
        auto& s = score[v];
        (ok ? s.first : s.second)++;
        if (!ok && !firstFail.count(v))
            firstFail[v] = "  in   " + ins + " regs " + regs + " flags " + flags + "\n  want " + want + "\n  got  " + got;
    }

    int pass = 0, fail = 0, cases = 0, bad = 0;
    for (auto& [v, s] : score) {
        cases += s.first + s.second;
        bad += s.second;
        if (s.second) {
            ++fail;
            std::printf("FAIL %-28s %d/%d\n%s\n", names[v].c_str(), s.second, s.first + s.second, firstFail[v].c_str());
        } else ++pass;
    }
    std::printf("conformance: %d variants pass, %d fail; %d of %d cases wrong; %d skipped (reference threw)\n",
                pass, fail, bad, cases, skipped);
    return fail ? 1 : 0;
}
