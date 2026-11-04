#include "service/state.h"

#include <chrono>
#include <cstdio>
#include <cstring>

#include "machine.h"
#include "service/disasm.h"
#include "service/ui.h"

namespace c93::svc {

CPUState cpuState;
Disassembled disassembled;
StepHistory stepHistory;

uint32_t ops::ip() { return machine().ip & kRamMask; }

void CPUState::Update() {
    Machine& m = machine();
    int ipNow = int(ops::ip());
    bool newInstruction = ipNow != IPAddress;   // change marks persist until the next instruction runs
    IPAddress = ipNow;
    uint8_t now[26];
    std::memcpy(now, m.regs + m.bank * kRegs, 26);
    if (newInstruction) {
        std::memset(regModified, 0, sizeof regModified);
        std::memcpy(RegPageOld, RegPage0, 26);
    }
    for (int i = 0; i < 26; ++i) if (now[i] != RegPage0[i]) regModified[i] = true;
    std::memcpy(RegPage0, now, 26);
    RegPageIndex = m.bank;
    std::memcpy(FRegsOld, FRegs, sizeof FRegs);
    std::memcpy(FRegs, m.fregs[m.fbank], sizeof FRegs);
    OldFlags = Flags;
    Flags = m.flagsByte();
    // MemoryController.GetMemoryPointedByAllAddressingRegisters / GetMemoryWrapped (wraps to 1, not 0).
    for (uint8_t r = 0; r < 26; ++r) {
        uint32_t a = m.get<24>(r);
        for (uint32_t i = 0; i < 16; ++i) {
            uint32_t c = a + i;
            if (c > 0xFFFFFF) c -= 0xFFFFFF;
            RegMemoryData[r * 16 + i] = m.ram[c];
        }
    }
}

std::string regName(int index, int length) { return std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZABC").substr(index, length); }

std::string regHex(int index, int length, const uint8_t regs[26]) {
    std::string s;
    char b[4];
    for (int i = 0; i < length; ++i) { std::snprintf(b, sizeof b, "%02X", regs[(index + i) % 26]); s += b; }
    return s;
}

const DissLine* Disassembled::GetCurentInstruction() const {
    int ip = int(ops::ip());
    for (auto& l : Lines) if (l.Address == ip) return &l;
    return nullptr;
}

void StepHistory::PushCurrent(const DissLine* current) {
    if (!current || (hasLast && lastAddress == current->Address)) return;
    hasLast = true;
    lastAddress = current->Address;
    history.push_back(*current);
    if (history.size() > 1000) history.erase(history.begin());
}

std::vector<DissLine> StepHistory::GetFittingWidth(int maxWidth, const Font& font, uint8_t fontFlags, int arrowWidth,
                                                   const int* excludeAddress) const {
    std::vector<DissLine> r;
    int used = 0;
    for (int i = int(history.size()) - 1; i >= 0; --i) {
        if (excludeAddress && history[i].Address == *excludeAddress) continue;
        int w = font.MeasureText(history[i].Instruction, 0, fontFlags).width + arrowWidth + 10;
        if (used + w > maxWidth) break;
        used += w;
        r.insert(r.begin(), history[i]);
    }
    return r;
}

// --- Operations ------------------------------------------------------------------------------------
namespace {
bool hasLastIp = false;
uint32_t lastIp = 0;
std::chrono::steady_clock::time_point lastTime;
} // namespace

void ops::Disassemble(bool force) {
    if (!serviceMode()) return;
    uint32_t start = ip();
    auto now = std::chrono::steady_clock::now();
    if (!force && hasLastIp && lastIp == start && now - lastTime < std::chrono::milliseconds(30)) return;
    // ContinuumDebugger.GetDissassembledFull + Disassembled.SetResponse, without the text round trip.
    disassembled.Lines.clear();
    char b[16];
    for (const DInstruction& d : disassemble(machine(), start)) {
        DissLine l;
        std::snprintf(b, sizeof b, "%06X", d.Address);
        l.TextAddress = b;
        l.Address = int(d.Address);
        for (uint8_t x : d.Bytes) { std::snprintf(b, sizeof b, "%02X ", x); l.OpCodes += b; }
        if (!l.OpCodes.empty()) l.OpCodes.pop_back();
        l.Instruction = d.SpecificInstruction;
        disassembled.Lines.push_back(std::move(l));
    }
    hasLastIp = true;
    lastIp = start;
    lastTime = now;
    stepHistory.PushCurrent(disassembled.GetCurentInstruction());
}

void ops::ToggleStepByStepMode() {
    if (!serviceMode()) return;
    Machine& m = machine();
    m.debugSet(!m.stepByStep.load(), false);
    hasLastIp = false;
    if (m.stepByStep.load()) Disassemble(true);
}

void ops::ContinueExecution() {
    if (!serviceMode()) return;
    machine().debugSet(false, true);
    hasLastIp = false;
}

void ops::AdvanceStepByStep() {
    if (!serviceMode() || !machine().stepByStep.load()) return;
    machine().debugSet(true, true);
}

void ops::UpdateAll() {
    cpuState.Update();
    // shortcut: Memory/Stacks/Video parsers (wave 2) update from their own windows' UpdateContent.
    if (serviceMode() && machine().stepByStep.load()) Disassemble();
}

} // namespace c93::svc
