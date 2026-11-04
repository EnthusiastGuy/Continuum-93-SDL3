// Arithmetic and maths: MUL DIV SMUL SDIV POW SQR CBR ISQR ISGN SIN COS TAN ABS ROUND FLOOR CEIL
// and the colour conversions RGB2HSL HSL2RGB RGB2HSB HSB2RGB.
// Reference: Execution/ExMUL.cs ExDIV.cs ExSMUL.cs ExSDIV.cs ExPOW.cs ExSQR.cs ExCBR.cs ExISQR.cs
// ExISGN.cs ExSIN.cs ExCOS.cs ExTAN.cs ExABS.cs ExROUND.cs ExFLOOR.cs ExCEIL.cs ExC_*.cs,
// Tools/CMath.cs (InverseSqrt), Colors/ColorConverter.cs. Every family is a C# switch: unknown
// sub-ops do nothing. Quirks kept from the reference:
//  - MUL/DIV/SMUL/SDIV "r,n" take a 13-bit immediate (5 bits of the register byte + 1 byte);
//    SMUL/SDIV sign-extend it from bit 12. "r,n,r" takes 8 bits, wider "n,rr" forms 16 bits.
//  - MUL and DIV touch only carry. MUL sets it when the product exceeds the register, but at
//    24 bits the product wraps at 32 bits first; DIV sets it on division by zero and stores
//    all-ones in the quotient and 0 in the remainder.
//  - The remainder register has the divisor's width; it is written after the quotient (wins
//    when both name the same register).
//  - SMUL/SDIV touch no flags; a zero operand (SMUL included) leaves everything unchanged.
//  - Float MUL/DIV set only SN (result < 0). DIV by a zero float does nothing; "DIV (mem),fr"
//    tests the memory dividend for zero, not the divisor, so it can store an infinity.
//  - "MUL/DIV r..rrrr,fr" compute in float, round half-even, set no flags; the 24/32-bit forms
//    store |rounded| (Math.Abs), the 8/16-bit forms wrap negatives.
//  - "MUL/DIV fr,(rrr)" and "(rrr),fr": fr is the sub-op byte's low 2 bits + the next byte's top 2.
//  - POW/SQR/CBR/ISQR pair a register with fr in one byte "RRRRffff": only A..P are reachable.
//    POW "fr,nnn"/"fr,(nnn)"/"(nnn),fr" read fr from the high nibble, SQR & co. from the low one.
//  - SQR/CBR "rrrr" (in place) use double-precision Math.Sqrt/Cbrt; every other form uses MathF.
//    ISQR is 1f / MathF.Sqrt everywhere.
//  - SQR/CBR/ISQR "r..rrrr,fr" set OV and leave the register alone when the result does not fit
//    (NaN included); otherwise clear OV. No other math op touches flags.
//  - Float -> integer casts are .NET's unchecked ones (see netTrunc); no rounding except Round.

// .NET 8 x64 unchecked float->integer cast: truncate through int64 and keep the low bits;
// NaN and values outside int64 become 0x8000000000000000, i.e. 0 in the low 32 bits.
inline uint32_t netTrunc(double d) { return d >= -0x1p63 && d < 0x1p63 ? uint32_t(int64_t(d)) : 0; }

// --- Integer MUL / DIV / SMUL / SDIV --------------------------------------------------------
// "ooooooDD DDDSSSSS [nnnnnnnn]": destination D straddles the bytes; the source is register S
// of width SW, or (SW == 0) the 13-bit immediate S:n.
template <int SW> constexpr int32_t sxSrc(uint32_t b) { if constexpr (SW == 0) return sx<13>(b); else return sx<SW>(b); }

struct Mul {
    template <int W, int SW> static void apply(Machine& m, uint8_t d, uint32_t a, uint32_t b) {
        uint64_t p = uint64_t(a) * b;
        if constexpr (W == 24) p = uint32_t(p);   // the reference multiplied in 32 bits at this width
        m.set<W>(d, uint32_t(p));
        m.f[1] = p > maskOf<W>();
    }
};
struct Div {
    template <int W, int SW> static void apply(Machine& m, uint8_t d, uint32_t a, uint32_t b) {
        m.set<W>(d, b ? a / b : maskOf<W>());
        m.f[1] = !b;
    }
};
struct SMul {
    template <int W, int SW> static void apply(Machine& m, uint8_t d, uint32_t a, uint32_t b) {
        if (b) m.set<W>(d, uint32_t(sx<W>(a)) * uint32_t(sxSrc<SW>(b)));   // copy-pasted zero check: x0 is skipped
    }
};
struct SDiv {
    template <int W, int SW> static void apply(Machine& m, uint8_t d, uint32_t a, uint32_t b) {
        if (b) m.set<W>(d, uint32_t(int64_t(sx<W>(a)) / sxSrc<SW>(b)));   // int64: INT_MIN / -1 threw in C#
    }
};

template <class F, int W, int SW> uint32_t pairOp(Machine& m, uint32_t ip) {
    uint8_t hi = subByte(m, ip), lo = m.fetch8(ip);
    uint8_t d = uint8_t((hi & 3) << 3 | lo >> 5);
    uint32_t b;
    if constexpr (SW == 0) b = uint32_t(lo & 31) << 8 | m.fetch8(ip);
    else b = m.get<SW>(lo & 31);
    F::template apply<W, SW>(m, d, m.get<W>(d), b);
    C93_NEXT(ip);
}

// DIV d,s,rem. Imm: "ooooooDD DDDRRRRR n.." with an SW-bit divisor; Reg: "ooooooDD DDDSSSSS uuuRRRRR".
template <bool Signed, int W, Kind S, int SW> uint32_t divRem(Machine& m, uint32_t ip) {
    uint8_t hi = subByte(m, ip), lo = m.fetch8(ip);
    uint8_t d = uint8_t((hi & 3) << 3 | lo >> 5), rem;
    uint32_t b;
    if constexpr (S == Imm) { rem = lo & 31; b = m.fetch<SW>(ip); }
    else { b = m.get<SW>(lo & 31); rem = m.fetchReg(ip); }
    uint32_t a = m.get<W>(d);
    if constexpr (Signed) {
        if (!b) C93_NEXT(ip);
        int64_t x = sx<W>(a), y = sx<SW>(b);
        m.set<W>(d, uint32_t(x / y));
        m.set<SW>(rem, uint32_t(x % y));
    } else {
        m.set<W>(d, b ? a / b : maskOf<W>());
        m.set<SW>(rem, b ? a % b : 0);
        m.f[1] = !b;
    }
    C93_NEXT(ip);
}

template <class F> void fillPairs(Op* t) {
    put(t, 2, 0, pairOp<F, 8, 0>);      // MUL r,n
    put(t, 2, 1, pairOp<F, 8, 8>);      // MUL r,r
    put(t, 2, 2, pairOp<F, 16, 0>);     // MUL rr,n
    put(t, 2, 3, pairOp<F, 16, 8>);     // MUL rr,r
    put(t, 2, 4, pairOp<F, 16, 16>);    // MUL rr,rr
    put(t, 2, 5, pairOp<F, 24, 0>);     // MUL rrr,n
    put(t, 2, 6, pairOp<F, 24, 8>);     // MUL rrr,r
    put(t, 2, 7, pairOp<F, 24, 16>);    // MUL rrr,rr
    put(t, 2, 8, pairOp<F, 24, 24>);    // MUL rrr,rrr
    put(t, 2, 9, pairOp<F, 32, 0>);     // MUL rrrr,n
    put(t, 2, 10, pairOp<F, 32, 8>);    // MUL rrrr,r
    put(t, 2, 11, pairOp<F, 32, 16>);   // MUL rrrr,rr
    put(t, 2, 12, pairOp<F, 32, 24>);   // MUL rrrr,rrr
    put(t, 2, 13, pairOp<F, 32, 32>);   // MUL rrrr,rrrr
}
template <bool S> void fillDivRem(Op* t) {
    put(t, 2, 14, divRem<S, 8, Imm, 8>);     // DIV r,n,r
    put(t, 2, 15, divRem<S, 8, Reg, 8>);     // DIV r,r,r
    put(t, 2, 16, divRem<S, 16, Imm, 16>);   // DIV rr,n,rr
    put(t, 2, 17, divRem<S, 16, Reg, 8>);    // DIV rr,r,r
    put(t, 2, 18, divRem<S, 16, Reg, 16>);   // DIV rr,rr,rr
    put(t, 2, 19, divRem<S, 24, Imm, 16>);   // DIV rrr,n,rr
    put(t, 2, 20, divRem<S, 24, Reg, 8>);    // DIV rrr,r,r
    put(t, 2, 21, divRem<S, 24, Reg, 16>);   // DIV rrr,rr,rr
    put(t, 2, 22, divRem<S, 24, Reg, 24>);   // DIV rrr,rrr,rrr
    put(t, 2, 23, divRem<S, 32, Imm, 16>);   // DIV rrrr,n,rr
    put(t, 2, 24, divRem<S, 32, Reg, 8>);    // DIV rrrr,r,r
    put(t, 2, 25, divRem<S, 32, Reg, 16>);   // DIV rrrr,rr,rr
    put(t, 2, 26, divRem<S, 32, Reg, 24>);   // DIV rrrr,rrr,rrr
    put(t, 2, 27, divRem<S, 32, Reg, 32>);   // DIV rrrr,rrrr,rrrr
}

// --- Float MUL / DIV ------------------------------------------------------------------------
template <bool D> inline void frMulDiv(Machine& m, uint8_t f, float v) {
    if (D && v == 0) return;
    float r = x86Nan(D ? m.fr(f) / v : m.fr(f) * v, m.fr(f), v);
    m.f[2] = r < 0;
    m.fr(f) = r;
}
// fr op= source. FReg: "AAAABBBB"; MemR: "ffuRRRRR" with fr's top 2 bits in the sub-op byte.
template <bool D, Kind S, int SW> uint32_t frMD(Machine& m, uint32_t ip) {
    if constexpr (S == FReg) { uint8_t x = m.fetch8(ip); frMulDiv<D>(m, x >> 4, m.fr(x & 15)); }
    else if constexpr (S == MemR) {
        uint8_t hi = subByte(m, ip), x = m.fetch8(ip);
        frMulDiv<D>(m, uint8_t((hi & 3) << 2 | x >> 6), m.rdF(m.get<24>(x & 31)));
    } else {
        uint8_t f = fetchF(m, ip);
        if constexpr (S == Imm) frMulDiv<D>(m, f, fromBits(m.fetch<32>(ip)));
        else if constexpr (S == Reg) frMulDiv<D>(m, f, float(m.get<SW>(m.fetchReg(ip))));
        else frMulDiv<D>(m, f, m.rdF(m.fetch<24>(ip)));
    }
    C93_NEXT(ip);
}
template <bool D, Kind K> uint32_t memMD(Machine& m, uint32_t ip) {   // (mem) op= fr
    uint8_t f;
    uint32_t a;
    if constexpr (K == MemN) { f = fetchF(m, ip); a = m.fetch<24>(ip); }
    else { uint8_t hi = subByte(m, ip), x = m.fetch8(ip); f = uint8_t((hi & 3) << 2 | x >> 6); a = m.get<24>(x & 31); }
    float v = m.rdF(a);
    if (D && v == 0) C93_NEXT(ip);   // the reference tested the dividend, not the divisor
    float r = x86Nan(D ? v / m.fr(f) : v * m.fr(f), v, m.fr(f));
    m.f[2] = r < 0;
    m.wrF(a, r);
    C93_NEXT(ip);
}
template <bool D, int W> uint32_t regMD(Machine& m, uint32_t ip) {    // register op= fr, rounded half-even
    uint8_t r = m.fetchReg(ip);
    float x = m.fr(fetchF(m, ip));
    if (D && x == 0) C93_NEXT(ip);
    float v = float(m.get<W>(r));
    v = D ? v / x : v * x;
    double n = std::nearbyint(double(v));
    m.set<W>(r, netTrunc(W >= 24 ? std::fabs(n) : n));
    C93_NEXT(ip);
}

template <bool D> void fillFloatMD(Op* t) {
    put(t, 2, 28, frMD<D, FReg, 32>);   // MUL fr,fr
    put(t, 2, 29, frMD<D, Imm, 32>);    // MUL fr,nnn
    put(t, 2, 30, frMD<D, Reg, 8>);     // MUL fr,r
    put(t, 2, 31, frMD<D, Reg, 16>);    // MUL fr,rr
    put(t, 2, 32, frMD<D, Reg, 24>);    // MUL fr,rrr
    put(t, 2, 33, frMD<D, Reg, 32>);    // MUL fr,rrrr
    put(t, 2, 34, regMD<D, 8>);         // MUL r,fr
    put(t, 2, 35, regMD<D, 16>);        // MUL rr,fr
    put(t, 2, 36, regMD<D, 24>);        // MUL rrr,fr
    put(t, 2, 37, regMD<D, 32>);        // MUL rrrr,fr
    put(t, 2, 38, frMD<D, MemN, 32>);   // MUL fr,(nnn)
    put(t, 2, 39, frMD<D, MemR, 32>);   // MUL fr,(rrr)
    put(t, 2, 40, memMD<D, MemN>);      // MUL (nnn),fr
    put(t, 2, 41, memMD<D, MemR>);      // MUL (rrr),fr
}
inline void fillMul(Op* t) { for (int i = 0; i < 256; ++i) t[i] = nop; fillPairs<Mul>(t); fillFloatMD<false>(t); }
inline void fillDiv(Op* t) { for (int i = 0; i < 256; ++i) t[i] = nop; fillPairs<Div>(t); fillDivRem<false>(t); fillFloatMD<true>(t); }
inline void fillSMul(Op* t) { for (int i = 0; i < 256; ++i) t[i] = nop; fillPairs<SMul>(t); }
inline void fillSDiv(Op* t) { for (int i = 0; i < 256; ++i) t[i] = nop; fillPairs<SDiv>(t); fillDivRem<true>(t); }

// --- POW / SQR / CBR / ISQR ---------------------------------------------------------------------
// fr with a memory operand: MemR = "RRRRffff" (address in register R), MemN = fr byte + nnn,
// fr in the nibble at FShift.
struct FA { uint8_t f; uint32_t a; };
template <Kind K, int FShift> FA frAddr(Machine& m, uint32_t& ip) {
    uint8_t x = m.fetch8(ip);
    if constexpr (K == MemR) return {uint8_t(x & 15), m.get<24>(x >> 4)};
    else return {uint8_t(x >> FShift & 15), m.fetch<24>(ip)};
}

inline uint32_t powFrFr(Machine& m, uint32_t ip) { uint8_t x = m.fetch8(ip); m.fr(x >> 4) = fPow(m.fr(x >> 4), m.fr(x & 15)); C93_NEXT(ip); }
template <int W> uint32_t powFrReg(Machine& m, uint32_t ip) {
    uint8_t x = m.fetch8(ip);
    m.fr(x & 15) = fPow(m.fr(x & 15), float(m.get<W>(x >> 4)));
    C93_NEXT(ip);
}
template <int W> uint32_t powRegFr(Machine& m, uint32_t ip) {
    uint8_t x = m.fetch8(ip), r = x >> 4;
    m.set<W>(r, netTrunc(fPow(float(m.get<W>(r)), m.fr(x & 15))));
    C93_NEXT(ip);
}
template <Kind K> uint32_t powFrMem(Machine& m, uint32_t ip) { FA o = frAddr<K, 4>(m, ip); m.fr(o.f) = fPow(m.fr(o.f), m.rdF(o.a)); C93_NEXT(ip); }
template <Kind K> uint32_t powMemFr(Machine& m, uint32_t ip) { FA o = frAddr<K, 4>(m, ip); m.wrF(o.a, fPow(m.rdF(o.a), m.fr(o.f))); C93_NEXT(ip); }
inline uint32_t powFrImm(Machine& m, uint32_t ip) { uint8_t f = m.fetch8(ip) >> 4; m.fr(f) = fPow(m.fr(f), fromBits(m.fetch<32>(ip))); C93_NEXT(ip); }

inline void fillPow(Op* t) {
    for (int i = 0; i < 256; ++i) t[i] = nop;
    put(t, 2, 0, powFrFr);              // POW fr,fr
    put(t, 2, 1, powFrReg<8>);          // POW fr,r
    put(t, 2, 2, powFrReg<16>);         // POW fr,rr
    put(t, 2, 3, powFrReg<24>);         // POW fr,rrr
    put(t, 2, 4, powFrReg<32>);         // POW fr,rrrr
    put(t, 2, 5, powRegFr<8>);          // POW r,fr
    put(t, 2, 6, powRegFr<16>);         // POW rr,fr
    put(t, 2, 7, powRegFr<24>);         // POW rrr,fr
    put(t, 2, 8, powRegFr<32>);         // POW rrrr,fr
    put(t, 2, 9, powFrMem<MemR>);       // POW fr,(rrr)
    put(t, 2, 10, powMemFr<MemR>);      // POW (rrr),fr
    put(t, 2, 11, powFrImm);            // POW fr,nnn
    put(t, 2, 12, powFrMem<MemN>);      // POW fr,(nnn)
    put(t, 2, 13, powMemFr<MemN>);      // POW (nnn),fr
}

// F::f is the float function; F::d the one "rrrr" (in place) used.
struct Sqr { static float f(float x) { return x86Nan(std::sqrt(x), x); } static double d(float x) { return std::sqrt(double(x)); } };
struct Cbr { static float f(float x) { return std::cbrt(x); } static double d(float x) { return std::cbrt(double(x)); } };
struct ISqr { static float f(float x) { return x86Nan(1.0f / std::sqrt(x), x); } static double d(float x) { return f(x); } };

template <class F> uint32_t rootFr(Machine& m, uint32_t ip) { uint8_t f = fetchF(m, ip); m.fr(f) = F::f(m.fr(f)); C93_NEXT(ip); }
template <class F, int W> uint32_t rootReg(Machine& m, uint32_t ip) {
    uint8_t r = m.fetchReg(ip);
    float v = float(m.get<W>(r));
    m.set<W>(r, netTrunc(W == 32 ? F::d(v) : F::f(v)));
    C93_NEXT(ip);
}
template <class F, Kind K> uint32_t rootMem(Machine& m, uint32_t ip) { uint32_t a = addr<K>(m, ip); m.wrF(a, F::f(m.rdF(a))); C93_NEXT(ip); }
template <class F> uint32_t rootFrFr(Machine& m, uint32_t ip) { uint8_t x = m.fetch8(ip); m.fr(x >> 4) = F::f(m.fr(x & 15)); C93_NEXT(ip); }
template <class F, int W> uint32_t rootFrReg(Machine& m, uint32_t ip) { uint8_t x = m.fetch8(ip); m.fr(x & 15) = F::f(float(m.get<W>(x >> 4))); C93_NEXT(ip); }
template <class F, Kind K> uint32_t rootFrMem(Machine& m, uint32_t ip) { FA o = frAddr<K, 0>(m, ip); m.fr(o.f) = F::f(m.rdF(o.a)); C93_NEXT(ip); }
template <class F> uint32_t rootFrImm(Machine& m, uint32_t ip) { uint8_t f = fetchF(m, ip); m.fr(f) = F::f(fromBits(m.fetch<32>(ip))); C93_NEXT(ip); }
template <class F, int W> uint32_t rootRegFr(Machine& m, uint32_t ip) {
    uint8_t x = m.fetch8(ip);
    float r = F::f(m.fr(x & 15));
    m.f[3] = !(r < float(uint64_t(1) << W));   // NaN overflows too
    if (!m.f[3]) m.set<W>(x >> 4, netTrunc(r));
    C93_NEXT(ip);
}
template <class F, Kind K> uint32_t rootMemFr(Machine& m, uint32_t ip) { FA o = frAddr<K, 0>(m, ip); m.wrF(o.a, F::f(m.fr(o.f))); C93_NEXT(ip); }

template <class F> void fillRoot(Op* t) {
    for (int i = 0; i < 256; ++i) t[i] = nop;
    put(t, 2, 0, rootFr<F>);            // SQR fr
    put(t, 2, 1, rootReg<F, 8>);        // SQR r
    put(t, 2, 2, rootReg<F, 16>);       // SQR rr
    put(t, 2, 3, rootReg<F, 24>);       // SQR rrr
    put(t, 2, 4, rootReg<F, 32>);       // SQR rrrr
    put(t, 2, 5, rootMem<F, MemR>);     // SQR (rrr)
    put(t, 2, 6, rootMem<F, MemN>);     // SQR (nnn)
    put(t, 2, 7, rootFrFr<F>);          // SQR fr,fr
    put(t, 2, 8, rootFrReg<F, 8>);      // SQR fr,r
    put(t, 2, 9, rootFrReg<F, 16>);     // SQR fr,rr
    put(t, 2, 10, rootFrReg<F, 24>);    // SQR fr,rrr
    put(t, 2, 11, rootFrReg<F, 32>);    // SQR fr,rrrr
    put(t, 2, 12, rootFrMem<F, MemR>);  // SQR fr,(rrr)
    put(t, 2, 13, rootFrMem<F, MemN>);  // SQR fr,(nnn)
    put(t, 2, 14, rootFrImm<F>);        // SQR fr,nnn
    put(t, 2, 15, rootRegFr<F, 8>);     // SQR r,fr
    put(t, 2, 16, rootRegFr<F, 16>);    // SQR rr,fr
    put(t, 2, 17, rootRegFr<F, 24>);    // SQR rrr,fr
    put(t, 2, 18, rootRegFr<F, 32>);    // SQR rrrr,fr
    put(t, 2, 19, rootMemFr<F, MemR>);  // SQR (rrr),fr
    put(t, 2, 20, rootMemFr<F, MemN>);  // SQR (nnn),fr
}

// --- One-operand float ops ------------------------------------------------------------------------
inline float fNeg(float x) { return -x; }
inline float fSin(float x) { return x86Nan(std::sin(x), x); }
inline float fCos(float x) { return x86Nan(std::cos(x), x); }
inline float fTan(float x) { return x86Nan(std::tan(x), x); }
inline float fAbs(float x) { return std::fabs(x); }
inline float fRound(float x) { return std::nearbyint(x); }   // MathF.Round: half to even
inline float fFloor(float x) { return std::floor(x); }
inline float fCeil(float x) { return std::ceil(x); }

// ISGN SIN COS TAN: "ooooAAAA [uuuuBBBB]", fr in the sub-op byte.
template <float (*Fn)(float)> uint32_t unaryIn(Machine& m, uint32_t ip) { uint8_t f = subByte(m, ip) & 15; m.fr(f) = Fn(m.fr(f)); C93_NEXT(ip); }
template <float (*Fn)(float)> uint32_t unaryIn2(Machine& m, uint32_t ip) { uint8_t f = subByte(m, ip) & 15; m.fr(f) = Fn(m.fr(fetchF(m, ip))); C93_NEXT(ip); }
template <float (*Fn)(float)> void fillUnaryIn(Op* t) {
    for (int i = 0; i < 256; ++i) t[i] = nop;
    put(t, 4, 0, unaryIn<Fn>);          // SIN fr
    put(t, 4, 1, unaryIn2<Fn>);         // SIN fr,fr
}
// ABS ROUND FLOOR CEIL: "oooooooo uuuuAAAA [uuuuBBBB]".
template <float (*Fn)(float)> uint32_t unary(Machine& m, uint32_t ip) { uint8_t f = fetchF(m, ip); m.fr(f) = Fn(m.fr(f)); C93_NEXT(ip); }
template <float (*Fn)(float)> uint32_t unary2(Machine& m, uint32_t ip) { uint8_t f = fetchF(m, ip); m.fr(f) = Fn(m.fr(fetchF(m, ip))); C93_NEXT(ip); }
template <float (*Fn)(float)> void fillUnary(Op* t) {
    for (int i = 0; i < 256; ++i) t[i] = nop;
    t[0] = unary<Fn>;                   // ABS fr
    t[1] = unary2<Fn>;                  // ABS fr,fr
}

// --- Colour conversions (ColorConverter.cs, float arithmetic in the same order) ----------------
// HSL/HSB pack as 0xHHHHSSLL (hue in degrees, S and L/B in percent), RGB as 0xRRGGBB.
inline float hueOf(float r, float g, float b, float mx, float mn) {
    if (mx == mn) return 0;
    float h = mx == r ? (g - b) / (mx - mn) : mx == g ? 2.f + (b - r) / (mx - mn) : 4.f + (r - g) / (mx - mn);
    h *= 60.f;
    return h < 0 ? h + 360.f : h;
}
inline uint32_t packHsx(float h, float s, float l) {
    return (netTrunc(h) & 0xFFFF) << 16 | (netTrunc(s * 100.f) & 255) << 8 | (netTrunc(l * 100.f) & 255);
}
inline uint32_t packRgb(float r, float g, float b) {
    return (netTrunc(r * 255.f) & 255) << 16 | (netTrunc(g * 255.f) & 255) << 8 | (netTrunc(b * 255.f) & 255);
}
template <bool Hsb> uint32_t rgbToHsx(uint32_t v) {
    float r = float(v >> 16 & 255) / 255.f, g = float(v >> 8 & 255) / 255.f, b = float(v & 255) / 255.f;
    float mx = std::fmax(r, std::fmax(g, b)), mn = std::fmin(r, std::fmin(g, b));   // no NaN or -0 here
    if constexpr (Hsb) return packHsx(hueOf(r, g, b, mx, mn), mx == 0.f ? 0.f : (mx - mn) / mx, mx);
    float l = (mx + mn) / 2.f;
    float s = mx == mn ? 0.f : l > 0.5f ? (mx - mn) / (2.f - mx - mn) : (mx - mn) / (mx + mn);
    return packHsx(hueOf(r, g, b, mx, mn), s, l);
}
inline uint32_t hslToRgb(uint32_t v) {
    float hue = float(v >> 16), s = float(v >> 8 & 255) / 100.f, l = float(v & 255) / 100.f;
    if (s == 0) return packRgb(l, l, l);
    float q = l < 0.5f ? l * (1.f + s) : l + s - l * s;
    float p = 2.f * l - q, hk = hue / 360.f;
    auto ch = [p, q](float t) {
        if (t < 0) t += 1;
        if (t > 1) t -= 1;
        if (t < 1.f / 6.f) return p + (q - p) * 6.f * t;
        if (t < 1.f / 2.f) return q;
        if (t < 2.f / 3.f) return p + (q - p) * (2.f / 3.f - t) * 6.f;
        return p;
    };
    return packRgb(ch(hk + 1.f / 3.f), ch(hk), ch(hk - 1.f / 3.f));
}
inline uint32_t hsbToRgb(uint32_t v) {
    float hue = float(v >> 16), s = float(v >> 8 & 255) / 100.f, br = float(v & 255) / 100.f;
    if (s == 0) return packRgb(br, br, br);
    float sector = hue / 60.f;
    int i = int(std::floor(double(sector)));
    float fr = sector - float(i);
    float p = br * (1.f - s), q = br * (1.f - s * fr), t = br * (1.f - s * (1.f - fr));
    switch (i) {
        case 0: return packRgb(br, t, p);
        case 1: return packRgb(q, br, p);
        case 2: return packRgb(p, br, t);
        case 3: return packRgb(p, q, br);
        case 4: return packRgb(t, p, br);
        default: return packRgb(br, p, q);
    }
}

template <uint32_t (*Fn)(uint32_t), Kind S, int SW, Kind D, int DW> uint32_t colorOp(Machine& m, uint32_t ip) {
    uint32_t v = Fn(src<S, SW>(m, ip));
    if constexpr (D == Reg) m.set<DW>(m.fetchReg(ip), v);
    else m.wr<DW>(addr<D>(m, ip), v);
    C93_NEXT(ip);
}
// SW/DW: 24 -> 32 for RGB2HSx, 32 -> 24 for HSx2RGB. Sources nnn|rrr|(nnn)|(rrr), destinations rrrr|(nnn)|(rrr).
template <uint32_t (*Fn)(uint32_t), int SW, int DW> void fillColor(Op* t) {
    for (int i = 0; i < 256; ++i) t[i] = nop;
    t[0] = colorOp<Fn, Imm, SW, Reg, DW>;     // RGB2HSL nnn,rrrr
    t[1] = colorOp<Fn, Reg, SW, Reg, DW>;     // RGB2HSL rrr,rrrr
    t[2] = colorOp<Fn, Imm, SW, MemN, DW>;    // RGB2HSL nnn,(nnn)
    t[3] = colorOp<Fn, Reg, SW, MemN, DW>;    // RGB2HSL rrr,(nnn)
    t[4] = colorOp<Fn, Imm, SW, MemR, DW>;    // RGB2HSL nnn,(rrr)
    t[5] = colorOp<Fn, Reg, SW, MemR, DW>;    // RGB2HSL rrr,(rrr)
    t[6] = colorOp<Fn, MemN, SW, Reg, DW>;    // RGB2HSL (nnn),rrrr
    t[7] = colorOp<Fn, MemR, SW, Reg, DW>;    // RGB2HSL (rrr),rrrr
    t[8] = colorOp<Fn, MemN, SW, MemN, DW>;   // RGB2HSL (nnn),(nnn)
    t[9] = colorOp<Fn, MemR, SW, MemN, DW>;   // RGB2HSL (rrr),(nnn)
    t[10] = colorOp<Fn, MemN, SW, MemR, DW>;  // RGB2HSL (nnn),(rrr)
    t[11] = colorOp<Fn, MemR, SW, MemR, DW>;  // RGB2HSL (rrr),(rrr)
}
