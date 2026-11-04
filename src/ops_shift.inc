// SL SR RL RR SET RES BIT, EX, MIN MAX, SETF RESF INVF, LDF.
// Reference: Execution/ExSL.cs ExSR.cs ExRL.cs ExRR.cs ExSET.cs ExRES.cs ExBIT.cs ExEX.cs
// ExMIN.cs ExMAX.cs ExSETF.cs ExRESF.cs ExINVF.cs ExLDF.cs, with CPU/Registers.cs
// (Shift*/Roll*/Set*Bit/Reset*Bit/Test*Bit/Ex*), Flags.cs and .NET 8 MathF.Min/Max.
// Quirks kept from the reference:
// - Shift/bit counts are C# int shifts: count & 31, so a register count of 8..31 is not clamped
//   (SL r,9 shifts by 9 and clears; SL r,33 shifts by 1).
// - Carry for SL/RL is r >> ((W - n) & 31) != 0 and for SR/RR (r << ((W - n) & 31)) & mask != 0:
//   any bit shifted out, not just the last one; a count of 0 gives C = (r != 0) at 32 bits
//   and C = 0 below; counts past W wrap the carry shift around.
// - Only carry changes for shifts/rotates; SET/RES/EX/MIN/MAX/LDF touch no flags.
// - BIT sets Z when the tested bit is 1 (not when it is 0).
// - SETF/RESF take a 5-bit condition code: 0..7 are the negated flags (SETF NZ clears Z), 8..31
//   act on flag (code % 8). INVF toggles flag (code % 8).
// - MIN/MAX r,fr and fr,r lose the register field's top bit (the reference computed
//   (ldOp << 5) & 31 = 0), so only registers A..P are reachable; |fr| is compared, not fr.
// - MIN/MAX fr,fr index the destination with (byte & 0xF0): only F0 works, other values threw.
// - MAX r,fr keeps the register (through float) when |fr| exceeds the register range; at 32 bits
//   float(rrrr) can round up to 2^32, which converts back to 0.
// - Float -> integer goes through int64 truncation then masks; NaN -> 0.

// Pair encoding "ooooooAA AAABBBBB": A from the sub-op byte's low 2 bits and the next byte's top 3.
inline uint8_t pairA(Machine& m, uint32_t& ip, uint8_t& lo) {
    uint8_t hi = subByte(m, ip);
    lo = m.fetch8(ip);
    return uint8_t((hi & 3) << 3 | lo >> 5);
}

// F::apply<W>(m, r, s) returns the new register value; s is the count already masked to 0..31.
struct ShL {
    template <int W> static uint32_t apply(Machine& m, uint32_t r, uint32_t s) {
        m.f[1] = (r >> ((W - s) & 31)) != 0;
        return r << s;
    }
};
struct ShR {
    template <int W> static uint32_t apply(Machine& m, uint32_t r, uint32_t s) {
        m.f[1] = ((r << ((W - s) & 31)) & maskOf<W>()) != 0;
        return r >> s;
    }
};
struct RoL {
    template <int W> static uint32_t apply(Machine& m, uint32_t r, uint32_t s) {
        m.f[1] = (r >> ((W - s) & 31)) != 0;
        return r << s | r >> ((W - s) & 31);
    }
};
struct RoR {
    template <int W> static uint32_t apply(Machine& m, uint32_t r, uint32_t s) {
        m.f[1] = ((r << ((W - s) & 31)) & maskOf<W>()) != 0;
        return r >> s | r << ((W - s) & 31);
    }
};
struct BSet { template <int W> static uint32_t apply(Machine&, uint32_t r, uint32_t s) { return r | 1u << s; } };
struct BRes { template <int W> static uint32_t apply(Machine&, uint32_t r, uint32_t s) { return r & ~(1u << s); } };
struct BTst {
    template <int W> static uint32_t apply(Machine& m, uint32_t r, uint32_t s) { m.f[0] = (r & 1u << s) != 0; return r; }
};

// reg op= count; the count is the 5-bit field B itself, or the 8-bit register it names.
template <class F, int W, bool RegCount> uint32_t shiftOp(Machine& m, uint32_t ip) {
    uint8_t lo, d = pairA(m, ip, lo);
    uint32_t n = RegCount ? m.get<8>(lo & 31) : lo & 31u;
    m.set<W>(d, F::template apply<W>(m, m.get<W>(d), n & 31));
    C93_NEXT(ip);
}

template <class F> void fillShift(Op* t) {
    for (int i = 0; i < 256; ++i) t[i] = nop;
    put(t, 2, 0, shiftOp<F, 8, false>);    // SL r,n
    put(t, 2, 1, shiftOp<F, 8, true>);     // SL r,r
    put(t, 2, 2, shiftOp<F, 16, false>);   // SL rr,n
    put(t, 2, 3, shiftOp<F, 16, true>);    // SL rr,r
    put(t, 2, 4, shiftOp<F, 24, false>);   // SL rrr,n
    put(t, 2, 5, shiftOp<F, 24, true>);    // SL rrr,r
    put(t, 2, 6, shiftOp<F, 32, false>);   // SL rrrr,n
    put(t, 2, 7, shiftOp<F, 32, true>);    // SL rrrr,r
}

// --- EX ----------------------------------------------------------------------------------------
template <int W> uint32_t exOp(Machine& m, uint32_t ip) {
    uint8_t b, a = pairA(m, ip, b);
    b &= 31;
    uint32_t v = m.get<W>(a);
    m.set<W>(a, m.get<W>(b));
    m.set<W>(b, v);
    C93_NEXT(ip);
}
inline uint32_t exFrOp(Machine& m, uint32_t ip) {
    uint8_t b = m.fetch8(ip);
    float v = m.fr(b >> 4);
    m.fr(b >> 4) = m.fr(b & 15);
    m.fr(b & 15) = v;
    C93_NEXT(ip);
}
inline void fillEx(Op* t) {
    for (int i = 0; i < 256; ++i) t[i] = nop;
    put(t, 2, 0, exOp<8>);                 // EX r,r
    put(t, 2, 1, exOp<16>);                // EX rr,rr
    put(t, 2, 2, exOp<24>);                // EX rrr,rrr
    put(t, 2, 3, exOp<32>);                // EX rrrr,rrrr
    put(t, 2, 4, exFrOp);                  // EX fr,fr
}

// --- MIN / MAX ---------------------------------------------------------------------------------
// .NET 8 MathF.Min/Max: NaN propagates (the first operand's if both), -0 orders below +0.
template <bool Max> inline float fminmax(float x, float y) {
    if (x != y) return std::isnan(x) ? x : Max ? (y < x ? x : y) : (x < y ? x : y);
    return Max ? (std::signbit(y) ? x : y) : (std::signbit(x) ? x : y);
}
inline uint32_t f2u(float x) { return std::isnan(x) ? 0 : uint32_t(int64_t(x)); }

template <bool Max, int W, bool Imm> uint32_t mmOp(Machine& m, uint32_t ip) {
    uint8_t b, a;
    uint32_t v;
    if constexpr (Imm) { a = m.fetchReg(ip); v = m.fetch<W>(ip); }
    else { a = pairA(m, ip, b); v = m.get<W>(b & 31); }
    uint32_t r = m.get<W>(a);
    m.set<W>(a, Max ? (r > v ? r : v) : (r < v ? r : v));
    C93_NEXT(ip);
}
template <bool Max> uint32_t mmFrNOp(Machine& m, uint32_t ip) {
    uint8_t i = fetchF(m, ip);
    uint32_t bits = m.fetch<32>(ip);
    float v; std::memcpy(&v, &bits, 4);
    m.fr(i) = fminmax<Max>(m.fr(i), v);
    C93_NEXT(ip);
}
template <bool Max> uint32_t mmFrFrOp(Machine& m, uint32_t ip) {
    uint8_t b = m.fetch8(ip);
    m.fr(b & 0xF0) = fminmax<Max>(m.fr(b & 0xF0), m.fr(b & 15));   // high nibble != 0 threw in the reference
    C93_NEXT(ip);
}
template <bool Max, int W> uint32_t mmRegFrOp(Machine& m, uint32_t ip) {
    uint8_t b = m.fetch8(ip), i = b >> 4;
    float x = float(m.get<W>(i)), y = std::fabs(m.fr(b & 15));
    if constexpr (Max) m.set<W>(i, f2u(y <= float(maskOf<W>()) ? fminmax<true>(x, y) : x));
    else m.set<W>(i, f2u(fminmax<false>(x, y)));
    C93_NEXT(ip);
}
template <bool Max, int W> uint32_t mmFrRegOp(Machine& m, uint32_t ip) {
    uint8_t b = m.fetch8(ip);
    float x = float(m.get<W>(b >> 4)), y = std::fabs(m.fr(b & 15));
    m.fr(b & 15) = fminmax<Max>(x, y);
    C93_NEXT(ip);
}

template <bool X> void fillMinMax(Op* t) {
    for (int i = 0; i < 256; ++i) t[i] = nop;
    put(t, 2, 0, mmOp<X, 8, false>);       // MIN r,r
    put(t, 2, 1, mmOp<X, 16, false>);      // MIN rr,rr
    put(t, 2, 2, mmOp<X, 24, false>);      // MIN rrr,rrr
    put(t, 2, 3, mmOp<X, 32, false>);      // MIN rrrr,rrrr
    put(t, 2, 4, mmOp<X, 8, true>);        // MIN r,n
    put(t, 2, 5, mmOp<X, 16, true>);       // MIN rr,nn
    put(t, 2, 6, mmOp<X, 24, true>);       // MIN rrr,nnn
    put(t, 2, 7, mmOp<X, 32, true>);       // MIN rrrr,nnnn
    put(t, 2, 8, mmFrNOp<X>);              // MIN fr,n
    put(t, 2, 9, mmFrFrOp<X>);             // MIN fr,fr
    put(t, 2, 10, mmRegFrOp<X, 8>);        // MIN r,fr
    put(t, 2, 11, mmRegFrOp<X, 16>);       // MIN rr,fr
    put(t, 2, 12, mmRegFrOp<X, 24>);       // MIN rrr,fr
    put(t, 2, 13, mmRegFrOp<X, 32>);       // MIN rrrr,fr
    put(t, 2, 14, mmFrRegOp<X, 8>);        // MIN fr,r
    put(t, 2, 15, mmFrRegOp<X, 16>);       // MIN fr,rr
    put(t, 2, 16, mmFrRegOp<X, 24>);       // MIN fr,rrr
    put(t, 2, 17, mmFrRegOp<X, 32>);       // MIN fr,rrrr
}

// --- SETF / RESF / INVF (one operand byte, no sub-op) ------------------------------------------
template <bool V> uint32_t setfOp(Machine& m, uint32_t ip) { uint8_t c = m.fetch8(ip) & 31; m.f[c & 7] = c >= 8 ? V : !V; C93_NEXT(ip); }
inline uint32_t invfOp(Machine& m, uint32_t ip) { uint8_t c = m.fetch8(ip) & 7; m.f[c] = !m.f[c]; C93_NEXT(ip); }

// --- LDF: flags byte (optionally masked) to a register or RAM; sub-op in the top 3 bits --------
template <Kind D, bool Mask> uint32_t ldfOp(Machine& m, uint32_t ip) {
    uint8_t i = subByte(m, ip) & 31, v = m.flagsByte();
    if constexpr (D == Reg) { if (Mask) v &= m.fetch8(ip); m.set<8>(i, v); }
    else {
        uint32_t a = D == MemR ? m.get<24>(i) : m.fetch<24>(ip);
        if (Mask) v &= m.fetch8(ip);
        m.wr<8>(a, v);
    }
    C93_NEXT(ip);
}
inline void fillLdf(Op* t) {
    for (int i = 0; i < 256; ++i) t[i] = nop;
    put(t, 5, 0, ldfOp<Reg, false>);       // LDF r
    put(t, 5, 1, ldfOp<MemR, false>);      // LDF (rrr)
    put(t, 5, 2, ldfOp<MemN, false>);      // LDF (nnn)
    put(t, 5, 3, ldfOp<Reg, true>);        // LDF r,n
    put(t, 5, 4, ldfOp<MemR, true>);       // LDF (rrr),n
    put(t, 5, 5, ldfOp<MemN, true>);       // LDF (nnn),n
}
