// Snapshots of the machine for the tool windows, and the debugger operations.
// Ports ServiceModule/Parsers/CPUState.cs, Disassembled.cs, StepHistory.cs, RegistryUtils.cs and Operations.cs.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace c93::svc {

class Font;

// Parsers/CPUState.cs: registers/flags of the current bank, with the previous instruction's values.
struct CPUState {
    uint8_t RegPage0[26]{}, RegPageOld[26]{};
    float FRegs[16]{}, FRegsOld[16]{};
    uint8_t RegPageIndex = 0, Flags = 0, OldFlags = 0;
    uint8_t RegMemoryData[26 * 16]{};   // 16 bytes at each register's 24-bit value
    int IPAddress = 0;
    bool regModified[26]{};
    void Update();   // called as often as the reference does (once per window that needs it, per frame)
    bool IsRegisterChanged(int i) const { return regModified[i]; }
    static bool GetBitValue(uint8_t b, int bit) { return b >> bit & 1; }
};
extern CPUState cpuState;

// RegistryUtils.cs
std::string regName(int index, int length);                                     // "ABCD"
std::string regHex(int index, int length, const uint8_t regs[26]);              // "0012AB"

// Parsers/Disassembled.cs: one line per disassembled instruction ("ADDR|BB BB|INSTR").
struct DissLine {
    int Address = 0;
    std::string TextAddress, OpCodes, Instruction;
};
struct Disassembled {
    std::vector<DissLine> Lines;
    const DissLine* GetCurentInstruction() const;   // the line at the live IP, or null
};
extern Disassembled disassembled;

// Parsers/StepHistory.cs: instructions the debugger stopped on, oldest first (at most 1000).
struct StepHistory {
    std::vector<DissLine> history;
    bool hasLast = false;
    int lastAddress = 0;
    void PushCurrent(const DissLine* current);
    std::vector<DissLine> GetFittingWidth(int maxWidth, const Font& font, uint8_t fontFlags, int arrowWidth, const int* excludeAddress) const;
};
extern StepHistory stepHistory;

// Operations.cs (+ DebugState.cs, which lives in Machine as atomics the CPU thread waits on).
namespace ops {
void Disassemble(bool force = false);
void ToggleStepByStepMode();
void ContinueExecution();
void AdvanceStepByStep();
void UpdateAll();
uint32_t ip();   // CPU.REGS.IPO, as the service code reads it
} // namespace ops

} // namespace c93::svc
