// The IMMEDIATE window's Ctrl+Enter: UI/ImmediateWindow.cs CompileAndRun + RunImmediateSynchronously, apart from
// the window so tests/conformance.cpp --immediate checks it against tests/oracle --immediate.
// The caller keeps the CPU thread off the machine meanwhile (it is parked in debugTrap, or stopped).
#pragma once
#include <optional>
#include <string>
#include <vector>

#include "asm.h"
#include "log.h"
#include "machine.h"

namespace c93::svc::immediate {

constexpr uint32_t ImmediateAddress = 0xC00000;
constexpr int MaxSteps = 5000;   // the reference's safety net

struct Status { std::string message; bool isError; };

// CodeBlockManager.Validate: block i against the blocks before it (its j loop stops at j == i), ends inclusive.
inline bool hasCollisions(const std::vector<AsmBlock>& b) {
    for (size_t i = 0; i < b.size(); ++i)
        for (size_t j = 0; j < i; ++j) {
            uint32_t si = b[i].start, ei = si + uint32_t(b[i].data.size()), sj = b[j].start, ej = sj + uint32_t(b[j].data.size());
            if ((si >= sj && si <= ej) || (ei <= ej && ei >= sj)) return true;
        }
    return false;
}

// stepByStep: DebugState.StepByStep. On: the snippet runs here and now, until it returns (at most MaxSteps
// instructions). Off: the machine is left at the snippet with the return address pushed, to run it itself.
inline Status compileAndRun(Machine& m, const std::vector<std::string>& lines, bool stepByStep) {
    std::string src = "#ORG 0xC00000\r\n#RUN 0xC00000\r\n";   // StringBuilder.AppendLine on Windows
    for (const std::string& l : lines) src += l + "\r\n";
    src += "RET\r\n";
    // shortcut: #include finds nothing here (the reference looked relative to its working folder).
    AsmResult a = assemble(src, "<immediate>", [](const std::string&) { return std::optional<std::string>(); });
    if (!a.fatal.empty()) {   // the reference threw out of its Update and the app died; here it is one more error
        LOGW("Service: immediate code does not assemble (%s); nothing ran", a.fatal.c_str());
        return {"Compile errors: " + std::to_string(a.errors.size() + 1), true};
    }
    if (!a.errors.empty() || hasCollisions(a.blocks)) return {"Compile errors: " + std::to_string(a.errors.size()), true};
    if (a.blocks.empty()) return {"No code generated", true};
    for (const AsmBlock& b : a.blocks)
        for (size_t i = 0; i < b.data.size(); ++i) m.ram[(b.start + i) & kRamMask] = b.data[i];   // shortcut: wraps where Array.Copy threw

    uint32_t returnIpo = m.ip;   // push the return address, jump to the snippet
    m.cs[m.spc++ & (kCsSize - 1)] = returnIpo;
    m.ip = ImmediateAddress;
    if (stepByStep) {
        bool wasRunning = m.running.load();
        for (int steps = 0; steps < MaxSteps;) {
            m.step();
            ++steps;
            if (m.ip == returnIpo) break;
            if (wasRunning && !m.running.load()) break;   // halted (illegal instruction, BREAK): the reference threw here
        }
        if (m.ip != returnIpo) return {"Immediate did not return within limit", true};
    }
    return {"Ran immediate", false};
}

} // namespace c93::svc::immediate
