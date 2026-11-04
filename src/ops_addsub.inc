// ADD / SUB (Execution/ExADD.cs, ExSUB.cs). One template per operand shape replaces ~2300 lines.
// Quirks kept from the reference (see the table): an unsuffixed memory source is 8 bits wide
// even for wider registers ("ADD rr,(nnn)" adds one byte), and "ADD (rrr),rrrr" is 32-bit.

template <bool Sub, int W> inline uint32_t addsub(Machine& m, uint32_t a, uint32_t b) {
    if constexpr (W < 24) b &= maskOf<W>();   // the reference took byte/ushort parameters here
    if constexpr (Sub) { m.subFlags<W>(a, b); return a - b; }
    else { m.addFlags<W>(a, b); return a + b; }
}
template <bool Sub> struct AddSub {
    template <int W> static uint32_t apply(Machine& m, uint32_t a, uint32_t b) { return addsub<Sub, W>(m, a, b); }
};
template <bool Sub, Kind D, int DW, Kind S, int SW> uint32_t addsubOp(Machine& m, uint32_t ip) { C93_NEXT(binOp<AddSub<Sub>, D, DW, S, SW>(m, ip)); }

// fr op= float register, raw float bits, unsigned register value, or float in RAM.
template <bool Sub, Kind S, int SW> uint32_t frOp(Machine& m, uint32_t ip) {
    uint8_t d = fetchF(m, ip);
    float v;
    if constexpr (S == FReg) v = m.fr(fetchF(m, ip));
    else if constexpr (S == Imm) { uint32_t b = m.fetch<32>(ip); std::memcpy(&v, &b, 4); }
    else if constexpr (S == Reg) v = float(m.get<SW>(m.fetchReg(ip)));
    else v = m.rdF(addr<S>(m, ip));
    m.fr(d) = m.faddsub<Sub>(m.fr(d), v);
    C93_NEXT(ip);
}

// (mem) op= fr. Encoded fr first. ADD keeps the fr value on overflow, SUB the memory value:
// the reference passed its operands to the float helper in different orders.
template <bool Sub, Kind D> uint32_t memFrOp(Machine& m, uint32_t ip) {
    float r = m.fr(fetchF(m, ip));
    uint32_t a = addr<D>(m, ip);
    float v = m.rdF(a);
    m.wrF(a, Sub ? m.faddsub<true>(v, r) : m.faddsub<false>(r, v));
    C93_NEXT(ip);
}

// Integer register op= rounded |fr|; a negative fr flips ADD to SUB and back.
// Values above the register range wrap and set overflow. Math.Round is round-half-even.
template <bool Sub, int W> uint32_t regFrOp(Machine& m, uint32_t ip) {
    uint8_t d = m.fetchReg(ip);
    float x = m.fr(fetchF(m, ip));
    bool neg = x < 0;
    x = std::fabs(x);
    bool over = x > float(maskOf<W>());
    m.f[3] = over;
    double r = std::nearbyint(double(over ? std::fmod(x, float(uint64_t(maskOf<W>()) + 1)) : x));
    uint32_t v = std::isnan(r) ? 0 : uint32_t(int64_t(r));   // .NET's unchecked double->int: NaN -> 0
    if (Sub != neg) m.set<W>(d, addsub<true, W>(m, m.get<W>(d), v));
    else m.set<W>(d, addsub<false, W>(m, m.get<W>(d), v));
    C93_NEXT(ip);
}

template <bool S> void fillAddSub(Op* t) {
    if (S) for (int i = 0; i < 256; ++i) t[i] = nop;   // ExSUB is a switch: unknown sub-ops do nothing
    t[ 0] = addsubOp<S, Reg, 8, Imm, 8>;           // ADD r,n
    t[ 1] = addsubOp<S, Reg, 8, Reg, 8>;           // ADD r,r
    t[ 2] = addsubOp<S, Reg, 8, MemN, 8>;          // ADD r,(nnn)
    t[ 3] = addsubOp<S, Reg, 8, MemR, 8>;          // ADD r,(rrr)
    t[ 4] = addsubOp<S, Reg, 16, Imm, 8>;          // ADD rr,n
    t[ 5] = addsubOp<S, Reg, 16, Imm, 16>;         // ADD16 rr,nn
    t[ 6] = addsubOp<S, Reg, 16, Reg, 8>;          // ADD rr,r
    t[ 7] = addsubOp<S, Reg, 16, Reg, 16>;         // ADD rr,rr
    t[ 8] = addsubOp<S, Reg, 16, MemN, 8>;         // ADD rr,(nnn)
    t[ 9] = addsubOp<S, Reg, 16, MemN, 16>;        // ADD16 rr,(nnn)
    t[10] = addsubOp<S, Reg, 16, MemR, 8>;         // ADD rr,(rrr)
    t[11] = addsubOp<S, Reg, 16, MemR, 16>;        // ADD16 rr,(rrr)
    t[12] = addsubOp<S, Reg, 24, Imm, 8>;          // ADD rrr,n
    t[13] = addsubOp<S, Reg, 24, Imm, 16>;         // ADD16 rrr,n
    t[14] = addsubOp<S, Reg, 24, Imm, 24>;         // ADD24 rrr,n
    t[15] = addsubOp<S, Reg, 24, Reg, 8>;          // ADD rrr,r
    t[16] = addsubOp<S, Reg, 24, Reg, 16>;         // ADD rrr,rr
    t[17] = addsubOp<S, Reg, 24, Reg, 24>;         // ADD rrr,rrr
    t[18] = addsubOp<S, Reg, 24, MemN, 8>;         // ADD rrr,(nnn)
    t[19] = addsubOp<S, Reg, 24, MemN, 16>;        // ADD16 rrr,(nnn)
    t[20] = addsubOp<S, Reg, 24, MemN, 24>;        // ADD24 rrr,(nnn)
    t[21] = addsubOp<S, Reg, 24, MemR, 8>;         // ADD rrr,(rrr)
    t[22] = addsubOp<S, Reg, 24, MemR, 16>;        // ADD16 rrr,(rrr)
    t[23] = addsubOp<S, Reg, 24, MemR, 24>;        // ADD24 rrr,(rrr)
    t[24] = addsubOp<S, Reg, 32, Imm, 8>;          // ADD rrrr,n
    t[25] = addsubOp<S, Reg, 32, Imm, 16>;         // ADD16 rrrr,n
    t[26] = addsubOp<S, Reg, 32, Imm, 24>;         // ADD24 rrrr,n
    t[27] = addsubOp<S, Reg, 32, Imm, 32>;         // ADD32 rrrr,n
    t[28] = addsubOp<S, Reg, 32, Reg, 8>;          // ADD rrrr,r
    t[29] = addsubOp<S, Reg, 32, Reg, 16>;         // ADD rrrr,rr
    t[30] = addsubOp<S, Reg, 32, Reg, 24>;         // ADD rrrr,rrr
    t[31] = addsubOp<S, Reg, 32, Reg, 32>;         // ADD rrrr,rrrr
    t[32] = addsubOp<S, Reg, 32, MemN, 8>;         // ADD rrrr,(nnn)
    t[33] = addsubOp<S, Reg, 32, MemN, 16>;        // ADD16 rrrr,(nnn)
    t[34] = addsubOp<S, Reg, 32, MemN, 24>;        // ADD24 rrrr,(nnn)
    t[35] = addsubOp<S, Reg, 32, MemN, 32>;        // ADD32 rrrr,(nnn)
    t[36] = addsubOp<S, Reg, 32, MemR, 8>;         // ADD rrrr,(rrr)
    t[37] = addsubOp<S, Reg, 32, MemR, 16>;        // ADD16 rrrr,(rrr)
    t[38] = addsubOp<S, Reg, 32, MemR, 24>;        // ADD24 rrrr,(rrr)
    t[39] = addsubOp<S, Reg, 32, MemR, 32>;        // ADD32 rrrr,(rrr)
    t[40] = addsubOp<S, MemN, 8, Imm, 8>;          // ADD (nnn),nnn
    t[41] = addsubOp<S, MemN, 16, Imm, 16>;        // ADD16 (nnn),nnn
    t[42] = addsubOp<S, MemN, 24, Imm, 24>;        // ADD24 (nnn),nnn
    t[43] = addsubOp<S, MemN, 32, Imm, 32>;        // ADD32 (nnn),nnn
    t[44] = addsubOp<S, MemN, 8, Reg, 8>;          // ADD (nnn),r
    t[45] = addsubOp<S, MemN, 16, Reg, 8>;         // ADD16 (nnn),r
    t[46] = addsubOp<S, MemN, 24, Reg, 8>;         // ADD24 (nnn),r
    t[47] = addsubOp<S, MemN, 32, Reg, 8>;         // ADD32 (nnn),r
    t[48] = addsubOp<S, MemN, 16, Reg, 16>;        // ADD16 (nnn),rr
    t[49] = addsubOp<S, MemN, 24, Reg, 16>;        // ADD24 (nnn),rr
    t[50] = addsubOp<S, MemN, 32, Reg, 16>;        // ADD32 (nnn),rr
    t[51] = addsubOp<S, MemN, 24, Reg, 24>;        // ADD24 (nnn),rrr
    t[52] = addsubOp<S, MemN, 32, Reg, 24>;        // ADD32 (nnn),rrr
    t[53] = addsubOp<S, MemN, 32, Reg, 32>;        // ADD32 (nnn),rrrr
    t[54] = addsubOp<S, MemR, 8, Imm, 8>;          // ADD (rrr),nnn
    t[55] = addsubOp<S, MemR, 16, Imm, 16>;        // ADD16 (rrr),nnn
    t[56] = addsubOp<S, MemR, 24, Imm, 24>;        // ADD24 (rrr),nnn
    t[57] = addsubOp<S, MemR, 32, Imm, 32>;        // ADD32 (rrr),nnn
    t[58] = addsubOp<S, MemR, 8, Reg, 8>;          // ADD (rrr),r
    t[59] = addsubOp<S, MemR, 16, Reg, 8>;         // ADD16 (rrr),r
    t[60] = addsubOp<S, MemR, 24, Reg, 8>;         // ADD24 (rrr),r
    t[61] = addsubOp<S, MemR, 32, Reg, 8>;         // ADD32 (rrr),r
    t[62] = addsubOp<S, MemR, 16, Reg, 16>;        // ADD16 (rrr),rr
    t[63] = addsubOp<S, MemR, 24, Reg, 16>;        // ADD24 (rrr),rr
    t[64] = addsubOp<S, MemR, 32, Reg, 16>;        // ADD32 (rrr),rr
    t[65] = addsubOp<S, MemR, 24, Reg, 24>;        // ADD24 (rrr),rrr
    t[66] = addsubOp<S, MemR, 32, Reg, 24>;        // ADD32 (rrr),rrr
    t[67] = addsubOp<S, MemR, 32, Reg, 32>;        // ADD (rrr),rrrr
    t[68] = frOp<S, FReg, 32>;                     // ADD fr,fr
    t[69] = frOp<S, Imm, 32>;                      // ADD fr,nnn
    t[70] = frOp<S, Reg, 8>;                       // ADD fr,r
    t[71] = frOp<S, Reg, 16>;                      // ADD fr,rr
    t[72] = frOp<S, Reg, 24>;                      // ADD fr,rrr
    t[73] = frOp<S, Reg, 32>;                      // ADD fr,rrrr
    t[74] = regFrOp<S, 8>;                         // ADD r,fr
    t[75] = regFrOp<S, 16>;                        // ADD rr,fr
    t[76] = regFrOp<S, 24>;                        // ADD rrr,fr
    t[77] = regFrOp<S, 32>;                        // ADD rrrr,fr
    t[78] = frOp<S, MemN, 32>;                     // ADD fr,(nnn)
    t[79] = frOp<S, MemR, 32>;                     // ADD fr,(rrr)
    t[80] = memFrOp<S, MemN>;                      // ADD (nnn),fr
    t[81] = memFrOp<S, MemR>;                      // ADD (rrr),fr
    t[82] = addsubOp<S, MemN, 8, MemN, 8>;         // ADD (nnn),(nnn)
    t[83] = addsubOp<S, MemN, 16, MemN, 16>;       // ADD16 (nnn),(nnn)
    t[84] = addsubOp<S, MemN, 24, MemN, 24>;       // ADD24 (nnn),(nnn)
    t[85] = addsubOp<S, MemN, 32, MemN, 32>;       // ADD32 (nnn),(nnn)
}

// INC / DEC: no flags (Registers.Increment*/Decrement*).
template <int Delta, Kind D, int W> uint32_t incOp(Machine& m, uint32_t ip) {
    if constexpr (D == Reg) { uint8_t i = m.fetchReg(ip); m.set<W>(i, m.get<W>(i) + Delta); }
    else { uint32_t a = addr<D>(m, ip); m.wr<W>(a, m.rd<W>(a) + Delta); }
    C93_NEXT(ip);
}
template <int Delta> void fillIncDec(Op* t) {
    for (int i = 0; i < 256; ++i) t[i] = nop;
    t[ 0] = incOp<Delta, Reg, 8>;                   // INC r
    t[ 1] = incOp<Delta, Reg, 16>;                  // INC rr
    t[ 2] = incOp<Delta, Reg, 24>;                  // INC rrr
    t[ 3] = incOp<Delta, Reg, 32>;                  // INC rrrr
    t[ 4] = incOp<Delta, MemR, 8>;                  // INC (rrr)
    t[ 5] = incOp<Delta, MemR, 16>;                 // INC16 (rrr)
    t[ 6] = incOp<Delta, MemR, 24>;                 // INC24 (rrr)
    t[ 7] = incOp<Delta, MemR, 32>;                 // INC32 (rrr)
    t[ 8] = incOp<Delta, MemN, 8>;                  // INC (nnn)
    t[ 9] = incOp<Delta, MemN, 16>;                 // INC16 (nnn)
    t[10] = incOp<Delta, MemN, 24>;                 // INC24 (nnn)
    t[11] = incOp<Delta, MemN, 32>;                 // INC32 (nnn)
}
