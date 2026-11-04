// Disassembler: port of CodeAnalysis/Debugger.cs (ContinuumDebugger), DInstruction.cs, DArg.cs, DUtils.cs,
// DFormat.cs. Same text as the reference, including its quirks. Uses the assembler's op table
// (asm_ops.inc, generated from the reference's Mnem.OPS in insertion order, which the lookups depend on).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace c93 { struct Machine; }

namespace c93::svc {

struct DInstruction {
    uint32_t Address = 0;
    std::vector<uint8_t> Bytes;
    std::string SpecificInstruction;   // e.g. "LD A, 0x05", "JR -16 => 0x080010"
    bool Illegal = false;
    uint8_t InstructionLength = 1;
};

// ContinuumDebugger.RunAt: up to `count` instructions from `address`. Stops early where the reference
// threw (an operand its tables cannot render, or reading past RAM's 16 MB + 3 bytes).
std::vector<DInstruction> disassemble(const Machine& m, uint32_t address, int count = 200, bool simpleRepresentation = false);

} // namespace c93::svc
