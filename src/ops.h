// Shared vocabulary for the instruction families (ops_*.inc; included by cpu.cpp and service/cpu_tracked.cpp only).
#pragma once
#include <cstdint>
#include <type_traits>

#include "machine.h"

// Threaded dispatch: a handler ends in C93_NEXT, which jumps straight into the next instruction's handler and
// returns to Machine::run only when the loop has something to do (callbacks, trap, WATCHER, stop). Same handlers,
// same effects in the same order; only the branch back differs. Clang and GCC 15+ guarantee the tail calls
// (musttail). MSVC makes them at /O2 without a guarantee, so its chain also returns to the loop once the stack
// has grown kChainStack below run()'s frame: a call it did not turn into a jump costs depth, never an overflow.
// Elsewhere (older GCC) and in the tracked copy, handlers return to the loop.
#if defined(__clang__) && defined(__has_cpp_attribute)
#if __has_cpp_attribute(clang::musttail)
#define C93_MUSTTAIL [[clang::musttail]]
#endif
#elif defined(__GNUC__) && defined(__has_cpp_attribute)
#if __has_cpp_attribute(gnu::musttail)
#define C93_MUSTTAIL [[gnu::musttail]]
#endif
#endif
#ifdef C93_MUSTTAIL
#define C93_CHAIN 1
#define C93_STACK_GUARD 0
#elif defined(_MSC_VER)
#include <intrin.h>
#define C93_CHAIN 1
#define C93_STACK_GUARD 1
#define C93_MUSTTAIL
#else
#define C93_CHAIN 0
#define C93_STACK_GUARD 0
#define C93_MUSTTAIL
#endif

// service/cpu_tracked.cpp builds the instructions a second time in c93::tracked, where Machine is TrackedMachine.
#ifndef C93_OPS_NS
#define C93_OPS_NS c93
#endif
namespace C93_OPS_NS {

// One instruction. ip is the address past the bytes dispatch already read (the opcode, and the sub-op byte of
// families that have one); the handler returns where the next instruction starts. Carrying ip in a register
// this way keeps the loop off a load-increment-store of Machine::ip per operand byte (D-29). Machine::ip is
// stored at every instruction boundary (run() or C93_NEXT), which is all code outside the handlers sees.
using Op = uint32_t (*)(Machine&, uint32_t ip);
inline constexpr bool kChain = C93_CHAIN && std::is_same_v<Machine, c93::Machine>;   // not in the tracked copy

// Chained: keep Machine::ip at the boundary (as the loop does) and go on unless the loop has something to do.
inline bool chainOn(Machine& m, uint32_t at) {
    m.ip = at;
    if (m.callbacksPending.load(std::memory_order_relaxed)) return false;   // stop included (Machine::running)
#if C93_STACK_GUARD
    if (uintptr_t(_AddressOfReturnAddress()) < m.chainFloor) return false;
#endif
    return true;
}
// A handler's last statement: the next instruction is at the argument. gSub is the loop's fused table (cpu_ops.inc).
#define C93_NEXT(...) do {                                                                                        \
        uint32_t at_ = (__VA_ARGS__);                                                                             \
        if constexpr (kChain)                                                                                     \
            if (chainOn(m, at_))                                                                                  \
                C93_MUSTTAIL return gSub[m.ram[at_ & kRamMask]][m.ram[(at_ + 1) & kRamMask]](m, at_ + 2);         \
        return at_;                                                                                               \
    } while (0)

// First instruction byte (Mnemonic.cs, "main op codes").
enum : uint8_t {
    NOOP = 0, LD = 1, ADD = 2, SUB = 3, DIV = 4, MUL = 5, SL = 6, SR = 7, RL = 8, RR = 9,
    SET = 10, RES = 11, BIT = 12, AND = 13, OR = 14, XOR = 15, INV = 16, EX = 17, CP = 18,
    INC = 19, DEC = 20, CALL = 21, CALLR = 22, JP = 23, JR = 24, POP = 26, PUSH = 28, INT = 29,
    RAND = 30, MIN = 31, MAX = 32, DJNZ = 33, SETF = 36, RESF = 37, INVF = 38, LDF = 39,
    REGS = 40, WAIT = 41, VDL = 42, VCL = 43, FIND = 44, IMPLY = 48, NAND = 49, NOR = 50,
    XNOR = 51, GETBITS = 56, SETBITS = 57, MEMF = 64, MEMC = 65, LDREGS = 70, STREGS = 71,
    SDIV = 92, SMUL = 93, SCP = 94, FREGS = 95, POW = 96, SQR = 97, CBR = 98, ISQR = 99,
    ISGN = 100, SIN = 102, COS = 103, TAN = 104, ABS = 128, ROUND = 129, FLOOR = 130,
    CEIL = 131, PLAY = 160, SETVAR = 190, GETVAR = 191, RGB2HSL = 192, HSL2RGB = 193,
    RGB2HSB = 194, HSB2RGB = 195, DEBUG = 252, BREAK = 253, RETIF = 254, RET = 255,
};

// Operand kinds. Every operand is fetched in encoding order: destination first, then source.
//   Reg:  a register index byte (low 5 bits) -> register view of width W
//   Imm:  W bits of immediate
//   MemN: a 24-bit address in the instruction -> W bits of RAM there
//   MemR: a register index -> its 24-bit value is the address -> W bits of RAM there
//   FReg: a float register index byte (low 4 bits)
enum Kind { Reg, Imm, MemN, MemR, FReg };

// Reads a source operand of kind K and width W (zero-extended).
template <Kind K, int W> inline uint32_t src(Machine& m, uint32_t& ip) {
    if constexpr (K == Reg)  return m.get<W>(m.fetchReg(ip));
    if constexpr (K == Imm)  return m.fetch<W>(ip);
    if constexpr (K == MemN) return m.rd<W>(m.fetch<24>(ip));
    if constexpr (K == MemR) return m.rd<W>(m.get<24>(m.fetchReg(ip)));
}
// Address of a memory destination (MemN or MemR).
template <Kind K> inline uint32_t addr(Machine& m, uint32_t& ip) {
    if constexpr (K == MemN) return m.fetch<24>(ip);
    else return m.get<24>(m.fetchReg(ip));
}
inline uint8_t fetchF(Machine& m, uint32_t& ip) { return m.fetch8(ip) & 15; }
// The sub-op byte just fetched (compact encodings keep operand bits in it).
inline uint8_t subByte(const Machine& m, uint32_t ip) { return m.ram[(ip - 1) & kRamMask]; }
// Float <-> raw IEEE bits (immediates and stacks carry floats as bit patterns).
inline uint32_t fbits(float v) { uint32_t b; std::memcpy(&b, &v, 4); return b; }
inline float fromBits(uint32_t b) { float v; std::memcpy(&v, &b, 4); return v; }

// dest op= src for integer operands. F::apply<W>(m, a, b) returns the result and sets flags.
// Operands are fetched in encoding order: destination first, then source.
// Pair: the compact encoding "ooooooAA AAABBBBB" of two register operands (Reg or MemR):
// field A straddles the sub-op byte's low 2 bits and the next byte's top 3.
template <class F, Kind D, int DW, Kind S, int SW, bool Pair = false> uint32_t binOp(Machine& m, uint32_t ip) {
    if constexpr (Pair) {
        uint8_t hi = subByte(m, ip), lo = m.fetch8(ip);
        uint8_t a = uint8_t((hi & 3) << 3 | lo >> 5), b = lo & 31;
        uint32_t v = S == Reg ? m.get<SW>(b) : m.rd<SW>(m.get<24>(b));
        if constexpr (D == Reg) m.set<DW>(a, F::template apply<DW>(m, m.get<DW>(a), v));
        else { uint32_t ad = m.get<24>(a); m.wr<DW>(ad, F::template apply<DW>(m, m.rd<DW>(ad), v)); }
    } else if constexpr (D == Reg) {
        uint8_t d = m.fetchReg(ip);
        uint32_t v = src<S, SW>(m, ip);
        m.set<DW>(d, F::template apply<DW>(m, m.get<DW>(d), v));
    } else {
        uint32_t a = addr<D>(m, ip);
        uint32_t v = src<S, SW>(m, ip);
        m.wr<DW>(a, F::template apply<DW>(m, m.rd<DW>(a), v));
    }
    return ip;
}

// Puts a handler at every byte value whose top bits hold `code`; the low `shift` bits belong
// to the operands (compact encodings).
inline void put(Op* t, int shift, int code, Op op) {
    for (int k = 0; k < 1 << shift; ++k) t[(code << shift) | k] = op;
}

} // namespace C93_OPS_NS
