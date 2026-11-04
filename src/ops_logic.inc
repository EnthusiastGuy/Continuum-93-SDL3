// AND OR XOR NAND NOR XNOR IMPLY (Execution/ExAND.cs ...) and INV. All seven share one
// sub-op set (Mnem.AOX_*); the operation width is the register operand's, or the suffix.
// Flags: carry cleared, zero from the result. Quirk kept: at 24 bits the inverting ops test
// zero before masking to 24 bits, so Z is never set for NAND/NOR/XNOR/IMPLY there.

template <uint32_t (*Fn)(uint32_t, uint32_t)> struct Logic {
    template <int W> static uint32_t apply(Machine& m, uint32_t a, uint32_t b) {
        uint32_t r = Fn(a, b);
        m.f[1] = false;
        m.f[0] = (W == 24 ? r : r & maskOf<W>()) == 0;
        return r;
    }
};
inline uint32_t opAnd(uint32_t a, uint32_t b) { return a & b; }
inline uint32_t opOr(uint32_t a, uint32_t b) { return a | b; }
inline uint32_t opXor(uint32_t a, uint32_t b) { return a ^ b; }
inline uint32_t opNand(uint32_t a, uint32_t b) { return ~(a & b); }
inline uint32_t opNor(uint32_t a, uint32_t b) { return ~(a | b); }
inline uint32_t opXnor(uint32_t a, uint32_t b) { return ~(a ^ b); }
inline uint32_t opImply(uint32_t a, uint32_t b) { return ~(a ^ b) | b; }   // as the reference wrote it

// Packed: every op except AND uses the compact encoding (6-bit sub-op, register pairs in
// "ooooooAA AAABBBBB"); AND kept the plain one (8-bit sub-op, one byte per register).
template <class F, bool Packed, Kind D, int DW, Kind S, int SW> uint32_t logicOp(Machine& m, uint32_t ip) {
    constexpr bool pairable = (D == Reg || D == MemR) && (S == Reg || S == MemR);
    C93_NEXT(binOp<F, D, DW, S, SW, Packed && pairable>(m, ip));
}

template <class F, bool P> void fillLogic(Op* t) {
    for (int i = 0; i < 256; ++i) t[i] = nop;   // switch-style in the reference: unknown sub-ops do nothing
    constexpr int sh = P ? 2 : 0;
    put(t, sh, 0, logicOp<F, P, Reg, 8, Imm, 8>);       // AND r,n
    put(t, sh, 1, logicOp<F, P, Reg, 8, Reg, 8>);       // AND r,r
    put(t, sh, 2, logicOp<F, P, Reg, 16, Imm, 16>);     // AND rr,nn
    put(t, sh, 3, logicOp<F, P, Reg, 16, Reg, 16>);     // AND rr,rr
    put(t, sh, 4, logicOp<F, P, Reg, 24, Imm, 24>);     // AND rrr,nnn
    put(t, sh, 5, logicOp<F, P, Reg, 24, Reg, 24>);     // AND rrr,rrr
    put(t, sh, 6, logicOp<F, P, Reg, 32, Imm, 32>);     // AND rrrr,nnnn
    put(t, sh, 7, logicOp<F, P, Reg, 32, Reg, 32>);     // AND rrrr,rrrr
    put(t, sh, 8, logicOp<F, P, MemR, 8, Imm, 8>);      // AND (rrr),n
    put(t, sh, 9, logicOp<F, P, MemR, 16, Imm, 16>);    // AND16 (rrr),nn
    put(t, sh, 10, logicOp<F, P, MemR, 24, Imm, 24>);   // AND24 (rrr),nnn
    put(t, sh, 11, logicOp<F, P, MemR, 32, Imm, 32>);   // AND32 (rrr),nnnn
    put(t, sh, 12, logicOp<F, P, MemR, 8, Reg, 8>);     // AND (rrr),r
    put(t, sh, 13, logicOp<F, P, MemR, 16, Reg, 16>);   // AND (rrr),rr
    put(t, sh, 14, logicOp<F, P, MemR, 24, Reg, 24>);   // AND (rrr),rrr
    put(t, sh, 15, logicOp<F, P, MemR, 32, Reg, 32>);   // AND (rrr),rrrr
    put(t, sh, 16, logicOp<F, P, Reg, 8, MemR, 8>);     // AND r,(rrr)
    put(t, sh, 17, logicOp<F, P, Reg, 16, MemR, 16>);   // AND rr,(rrr)
    put(t, sh, 18, logicOp<F, P, Reg, 24, MemR, 24>);   // AND rrr,(rrr)
    put(t, sh, 19, logicOp<F, P, Reg, 32, MemR, 32>);   // AND rrrr,(rrr)
    put(t, sh, 20, logicOp<F, P, MemN, 8, Imm, 8>);     // AND (nnn),n
    put(t, sh, 21, logicOp<F, P, MemN, 16, Imm, 16>);   // AND16 (nnn),nn
    put(t, sh, 22, logicOp<F, P, MemN, 24, Imm, 24>);   // AND24 (nnn),nnn
    put(t, sh, 23, logicOp<F, P, MemN, 32, Imm, 32>);   // AND32 (nnn),nnnn
    put(t, sh, 24, logicOp<F, P, MemN, 8, Reg, 8>);     // AND (nnn),r
    put(t, sh, 25, logicOp<F, P, MemN, 16, Reg, 16>);   // AND (nnn),rr
    put(t, sh, 26, logicOp<F, P, MemN, 24, Reg, 24>);   // AND (nnn),rrr
    put(t, sh, 27, logicOp<F, P, MemN, 32, Reg, 32>);   // AND (nnn),rrrr
    put(t, sh, 28, logicOp<F, P, Reg, 8, MemN, 8>);     // AND r,(nnn)
    put(t, sh, 29, logicOp<F, P, Reg, 16, MemN, 16>);   // AND rr,(nnn)
    put(t, sh, 30, logicOp<F, P, Reg, 24, MemN, 24>);   // AND rrr,(nnn)
    put(t, sh, 31, logicOp<F, P, Reg, 32, MemN, 32>);   // AND rrrr,(nnn)
}

// INV r..rrrr: one byte, sub-op in the top 3 bits, register in the low 5.
template <int W> uint32_t invOp(Machine& m, uint32_t ip) {
    uint8_t i = subByte(m, ip) & 0x1F;
    uint32_t r = ~m.get<W>(i) & maskOf<W>();
    m.set<W>(i, r);
    m.f[1] = false;
    m.f[0] = r == 0;
    C93_NEXT(ip);
}
inline void fillInv(Op* t) {
    for (int i = 0; i < 256; ++i) t[i] = nop;
    for (int i = 0; i < 32; ++i) { t[i] = invOp<8>; t[32 + i] = invOp<16>; t[64 + i] = invOp<24>; t[96 + i] = invOp<32>; }
}
