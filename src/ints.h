// Interrupts: INT n, r calls function <value of r> of interrupt group n with r's index.
// (Interrupts/InterruptHandler.cs.) Groups: 0 machine, 1 video, 2 input, 3 random,
// 4 file system, 5 strings. Each ints_*.cpp fills its group in a register*() function.
#pragma once
#include <filesystem>
#include <string>
#include <string_view>

#include "machine.h"

namespace c93 {

using IntFn = void (*)(Machine&, uint8_t reg);
inline constexpr int kIntGroups = 6;
extern IntFn gInt[kIntGroups][256];   // unassigned functions do nothing, as the reference's switches

void initInterrupts();                // fills gInt once; called by the CPU table setup
void registerMachineInts();           // ints.cpp: machine (0) and random (3)
void registerVideoInts();             // ints_video.cpp (1): pages, palettes, primitives, fill, scroll, copy
void registerTextInts();              // ints_text.cpp (1): text, fonts, sprites, tile maps, paths
void registerInputInts();             // ints_input.cpp (2)
void registerFileInts();              // ints_fs.cpp (4)
void registerStringInts();            // ints_fs.cpp (5)

// Root of the guest file system (Data/filesystem next to the exe); set by main or the tests.
extern std::string gFsRoot;

// Assembles a source file and loads its blocks into RAM (Run/Runner.cs, InterruptsMachine.Build).
struct Loaded {
    bool found = false;          // the file exists
    bool loaded = false;         // at least one block went into RAM
    uint32_t firstBlock = 0;     // where Runner starts the boot program
    uint32_t runAddress = 0;     // what INT 0 0xC0 returns (#RUN / first #ORG)
    size_t errors = 0;
    bool fatal = false;          // the reference assembler or C compiler threw: the machine halts (D-16, D-20)
    bool compileFailed = false;  // C: the compiler reported errors (counted in errors)
};
Loaded assembleInto(Machine& m, const std::string& path);
Loaded assembleText(Machine& m, std::string_view source, const std::string& path);   // path: base of #includes

// Compiles a C source, assembles the result and loads it (Run/Runner.cs when boot, else InterruptsMachine.BuildC).
// root: includes must stay inside it (empty: anywhere; the boot program is chosen by the host).
Loaded compileCInto(Machine& m, const std::string& path, bool boot, const std::string& root);

// Build interrupts read sources under Data/ (gFsRoot's parent); within() tests a path against such a root.
std::filesystem::path buildRoot();
bool within(const std::filesystem::path& p, const std::filesystem::path& root);

// Register r's neighbour in the A..Z ring, as Registers.GetNextRegister.
inline uint8_t nextReg(uint8_t r, int d) { return uint8_t((r + d) % 26); }

} // namespace c93
