// Control flow, stacks, banks and compares: JP JR CALL CALLR RET RETIF DJNZ PUSH POP REGS FREGS
// LDREGS STREGS CP SCP (Execution/ExJP.cs ExJR.cs ExCALL.cs ExCALLR.cs ExMisc.cs ExRETIF.cs
// ExDJNZ.cs ExPUSH.cs ExPOP.cs ExREGS.cs ExFREGS.cs ExLDREGS.cs ExSTREGS.cs ExCP.cs ExSCP.cs).
// Quirks kept from the reference:
//  - JR/CALLR jump to (address after the instruction) + offset - 5; IP is a full 32-bit value.
//  - Condition codes are 5 bits for JP/JR/CALL/CALLR (16..31 alias the flags), 4 bits for RETIF.
//  - RET/RETIF with an empty call stack stop the machine and leave IP after the instruction.
//  - DJNZ reads the jump target (and the (rrr) address) before decrementing; no flags change.
//  - PUSH r/POP r take the whole operand byte as the register index (no 0x1F mask), and indices
//    past Z reach into the next bank, as the reference's flat register array.
//  - PUSH r,r / fr,fr check the whole range up front; POP checks per element and keeps going
//    after an underflow. PUSH fr,fr keeps the first index unshifted (mix & 0xF0) and sizes the
//    range with the 26-register distance. POP r,r with a first index past Z never ends.
//  - Stack overflow/underflow clears both stacks, zeroes SPR/SPC, jumps to the HMEM error
//    handler and stores the error id (0x10 overflow, 0x11 underflow).
//  - LDREGS/STREGS copy registers r1 toward r2 (either direction) without the A..Z wrap;
//    LDREGS wraps its RAM read past 0xFFFFFF by subtracting 0xFFFFFF (0x1000000 -> 1).
//  - CP: carry = unsigned less-than, OV cleared, PO from the reference's formula using the low
//    byte of the difference as signed at every width.
//  - SCP leaves C and PO alone. CP float: CompareTo ordering (NaN lowest), SN = first < 0;
//    CP fr,reg skips when the register is 0 and CP reg,fr when fr is 0 (flags untouched).

constexpr uint32_t kErrStackOverflow = 0x10, kErrStackUnderflow = 0x11;

// ResetAllStacks + jump to the error handler (HMEM's last cell; the id goes in the one before).
inline uint32_t stackFault(Machine& m, uint32_t err) {   // returns the new ip
    std::memset(m.rs, 0, kRsSize);
    std::memset(m.cs, 0, sizeof m.cs);
    m.spr = m.spc = 0;
    m.hmem[kHmemSize - 2] = err;
    return m.hmem[kHmemSize - 1];
}
// Register byte i past the current bank start, as the reference's flat array (modulo only keeps
// indices the reference rejects in bounds).
inline uint8_t& flatReg(Machine& m, uint32_t i) { return m.regs[(m.bank * kRegs + i) % sizeof m.regs]; }

// --- JP JR CALL CALLR RET RETIF -----------------------------------------------------------
// Sub-op in the top 3 bits; the low 5 hold the condition (ff forms) or the register (rrr).
template <bool Call> inline uint32_t jumpTo(Machine& m, uint32_t ip, uint32_t target) {
    if constexpr (Call) m.cs[m.spc++ & (kCsSize - 1)] = ip;
    return target;
}
template <bool Call, bool Cond, Kind K> uint32_t jumpOp(Machine& m, uint32_t ip) {
    uint8_t low = subByte(m, ip) & 0x1F;
    uint32_t target;
    if constexpr (K == Imm) target = m.fetch<24>(ip);
    else target = m.get<24>(Cond ? m.fetchReg(ip) : low);
    C93_NEXT(!Cond || m.cond(low) ? jumpTo<Call>(m, ip, target) : ip);
}
template <bool Call, bool Cond> uint32_t jumpRelOp(Machine& m, uint32_t ip) {
    uint8_t low = subByte(m, ip) & 0x1F;
    int32_t off = sx<24>(m.fetch<24>(ip));
    C93_NEXT(!Cond || m.cond(low) ? jumpTo<Call>(m, ip, ip + uint32_t(off) - 5) : ip);
}
template <bool Call> void fillJump(Op* t, bool rel) {
    for (int i = 0; i < 256; ++i) t[i] = nop;
    if (rel) {
        put(t, 5, 0, jumpRelOp<Call, false>);   // JR nnn
        put(t, 5, 1, jumpRelOp<Call, true>);    // JR ff,nnn
        return;
    }
    put(t, 5, 0, jumpOp<Call, false, Imm>);     // JP nnn
    put(t, 5, 1, jumpOp<Call, true, Imm>);      // JP ff,nnn
    put(t, 5, 2, jumpOp<Call, false, Reg>);     // JP rrr
    put(t, 5, 3, jumpOp<Call, true, Reg>);      // JP ff,rrr
}

inline uint32_t retOp(Machine& m, uint32_t ip) {   // RET
    if (m.spc > 0) C93_NEXT(m.cs[--m.spc & (kCsSize - 1)]);
    m.running.store(false, std::memory_order_relaxed);   // underflow: the reference stops
    C93_NEXT(ip);
}
inline uint32_t retifOp(Machine& m, uint32_t ip) {   // RETIF ff
    if (m.cond(m.fetch8(ip) & 15)) C93_MUSTTAIL return retOp(m, ip);
    C93_NEXT(ip);
}

// --- DJNZ ---------------------------------------------------------------------------------
template <Kind J> inline uint32_t jumpTarget(Machine& m, uint32_t& ip) {
    if constexpr (J == Imm) return m.fetch<24>(ip);
    else return m.get<24>(m.fetchReg(ip));
}
template <int W, Kind J> uint32_t djnzReg(Machine& m, uint32_t ip) {
    uint8_t i = m.fetchReg(ip);
    uint32_t target = jumpTarget<J>(m, ip);
    uint32_t v = (m.get<W>(i) - 1) & maskOf<W>();
    m.set<W>(i, v);
    C93_NEXT(v ? target : ip);
}
template <int W, Kind T, Kind J> uint32_t djnzMem(Machine& m, uint32_t ip) {
    uint32_t a = addr<T>(m, ip);
    uint32_t target = jumpTarget<J>(m, ip);
    uint32_t v = (m.rd<W>(a) - 1) & maskOf<W>();
    m.wr<W>(a, v);
    C93_NEXT(v ? target : ip);
}
inline void fillDjnz(Op* t) {
    for (int i = 0; i < 256; ++i) t[i] = nop;
    t[ 0] = djnzReg<8, Imm>;             // DJNZ r,nnn
    t[ 1] = djnzReg<8, Reg>;             // DJNZ r,rrr
    t[ 2] = djnzReg<16, Imm>;            // DJNZ rr,nnn
    t[ 3] = djnzReg<16, Reg>;            // DJNZ rr,rrr
    t[ 4] = djnzReg<24, Imm>;            // DJNZ rrr,nnn
    t[ 5] = djnzReg<24, Reg>;            // DJNZ rrr,rrr
    t[ 6] = djnzReg<32, Imm>;            // DJNZ rrrr,nnn
    t[ 7] = djnzReg<32, Reg>;            // DJNZ rrrr,rrr
    t[ 8] = djnzMem<8, MemN, Imm>;       // DJNZ (nnn),nnn
    t[ 9] = djnzMem<8, MemN, Reg>;       // DJNZ (nnn),rrr
    t[10] = djnzMem<16, MemN, Imm>;      // DJNZ16 (nnn),nnn
    t[11] = djnzMem<16, MemN, Reg>;      // DJNZ16 (nnn),rrr
    t[12] = djnzMem<24, MemN, Imm>;      // DJNZ24 (nnn),nnn
    t[13] = djnzMem<24, MemN, Reg>;      // DJNZ24 (nnn),rrr
    t[14] = djnzMem<32, MemN, Imm>;      // DJNZ32 (nnn),nnn
    t[15] = djnzMem<32, MemN, Reg>;      // DJNZ32 (nnn),rrr
    t[16] = djnzMem<8, MemR, Imm>;       // DJNZ (rrr),nnn
    t[17] = djnzMem<8, MemR, Reg>;       // DJNZ (rrr),rrr
    t[18] = djnzMem<16, MemR, Imm>;      // DJNZ16 (rrr),nnn
    t[19] = djnzMem<16, MemR, Reg>;      // DJNZ16 (rrr),rrr
    t[20] = djnzMem<24, MemR, Imm>;      // DJNZ24 (rrr),nnn
    t[21] = djnzMem<24, MemR, Reg>;      // DJNZ24 (rrr),rrr
    t[22] = djnzMem<32, MemR, Imm>;      // DJNZ32 (rrr),nnn
    t[23] = djnzMem<32, MemR, Reg>;      // DJNZ32 (rrr),rrr
}

// --- PUSH POP -----------------------------------------------------------------------------

// PUSH r..rrrr, (nnn), (rrr) at width W. The index byte is unmasked (8-bit reads go flat;
// wider ones throw in the reference past Z, so & 31 only keeps them in bounds).
template <int W, Kind K> uint32_t pushOp(Machine& m, uint32_t ip) {
    uint32_t loc = K == MemN ? m.fetch<24>(ip) : m.fetch8(ip);
    if constexpr (K == MemR) loc = m.get<24>(uint8_t(loc & 31));
    if (m.spr >= kRsSize - W / 8) C93_NEXT(stackFault(m, kErrStackOverflow));
    uint32_t v;
    if constexpr (K != Reg) v = m.rd<W>(loc);
    else if constexpr (W == 8) v = flatReg(m, loc);
    else v = m.get<W>(uint8_t(loc & 31));
    m.rsSet<W>(m.spr, v);
    m.spr += W / 8;
    C93_NEXT(ip);
}
template <int W, Kind K> uint32_t popOp(Machine& m, uint32_t ip) {
    uint32_t loc = K == MemN ? m.fetch<24>(ip) : m.fetch8(ip);
    if constexpr (K == MemR) loc = m.get<24>(uint8_t(loc & 31));
    if (m.spr < W / 8) C93_NEXT(stackFault(m, kErrStackUnderflow));
    m.spr -= W / 8;
    uint32_t v = m.rsGet<W>(m.spr);
    if constexpr (K != Reg) m.wr<W>(loc, v);
    else if constexpr (W == 8) flatReg(m, loc) = uint8_t(v);
    else m.set<W>(uint8_t(loc & 31), v);
    C93_NEXT(ip);
}

inline void push8(Machine& m, uint8_t v) { m.rsSet<8>(m.spr++, v); }
inline void pushF(Machine& m, uint8_t f) { m.rsSet<32>(m.spr, fbits(m.fr(f))); m.spr += 4; }
// An underflow jumps (ip) and the caller goes on popping, as the reference.
inline void pop8(Machine& m, uint32_t& ip, uint8_t i) {
    if (m.spr < 1) { ip = stackFault(m, kErrStackUnderflow); return; }
    flatReg(m, i) = uint8_t(m.rsGet<8>(--m.spr));
}
inline void popF(Machine& m, uint32_t& ip, uint8_t f) {
    if (m.spr < 4) { ip = stackFault(m, kErrStackUnderflow); return; }
    m.spr -= 4;
    m.fr(f) = fromBits(m.rsGet<32>(m.spr));
}
inline uint8_t regDistance(int a, int b) { return uint8_t((b - a + 26) % 26 + 1); }   // C# % keeps the sign

inline uint32_t pushRange(Machine& m, uint32_t ip) {   // PUSH r,r: a up to b, wrapping Z -> A
    uint8_t a = m.fetchReg(ip), b = m.fetchReg(ip);
    if (m.spr >= kRsSize - regDistance(a, b)) C93_NEXT(stackFault(m, kErrStackOverflow));
    for (; a != b; a = a == 25 ? 0 : uint8_t(a + 1)) push8(m, flatReg(m, a));
    push8(m, flatReg(m, a));
    C93_NEXT(ip);
}
inline uint32_t popRange(Machine& m, uint32_t ip) {    // POP r,r: b down to a, wrapping A -> Z
    uint8_t a = m.fetchReg(ip), b = m.fetchReg(ip);
    for (; a != b; b = b == 0 ? 25 : uint8_t(b - 1)) pop8(m, ip, b);
    pop8(m, ip, b);
    C93_NEXT(ip);
}
inline uint32_t pushFr(Machine& m, uint32_t ip) {
    uint8_t f = m.fetch8(ip) & 15;
    if (m.spr >= kRsSize - 4) C93_NEXT(stackFault(m, kErrStackOverflow));
    pushF(m, f);
    C93_NEXT(ip);
}
inline uint32_t popFr(Machine& m, uint32_t ip) { uint8_t f = m.fetch8(ip); popF(m, ip, f); C93_NEXT(ip); }
inline uint32_t pushFrRange(Machine& m, uint32_t ip) {
    uint8_t mix = m.fetch8(ip), a = mix & 0xF0, b = mix & 15;   // the reference never shifts a
    if (m.spr >= kRsSize - regDistance(a, b) * 4u) C93_NEXT(stackFault(m, kErrStackOverflow));
    for (; a != b; a = a == 15 ? 0 : uint8_t(a + 1)) pushF(m, a);
    pushF(m, a);
    C93_NEXT(ip);
}
inline uint32_t popFrRange(Machine& m, uint32_t ip) {
    uint8_t mix = m.fetch8(ip), a = mix >> 4, b = mix & 15;
    for (; a != b; b = b == 0 ? 15 : uint8_t(b - 1)) popF(m, ip, b);
    popF(m, ip, b);
    C93_NEXT(ip);
}

template <bool Pop> void fillStack(Op* t) {
    for (int i = 0; i < 256; ++i) t[i] = nop;
    t[ 0] = Pop ? popOp<8, Reg> : pushOp<8, Reg>;          // PUSH r
    t[ 1] = Pop ? popOp<16, Reg> : pushOp<16, Reg>;        // PUSH rr
    t[ 2] = Pop ? popOp<24, Reg> : pushOp<24, Reg>;        // PUSH rrr
    t[ 3] = Pop ? popOp<32, Reg> : pushOp<32, Reg>;        // PUSH rrrr
    t[ 4] = Pop ? popRange : pushRange;                    // PUSH r,r
    t[ 5] = Pop ? popFr : pushFr;                          // PUSH fr
    t[ 6] = Pop ? popFrRange : pushFrRange;                // PUSH fr,fr
    t[ 7] = Pop ? popOp<8, MemN> : pushOp<8, MemN>;        // PUSH (nnn)
    t[ 8] = Pop ? popOp<8, MemR> : pushOp<8, MemR>;        // PUSH (rrr)
    t[ 9] = Pop ? popOp<16, MemN> : pushOp<16, MemN>;      // PUSH16 (nnn)
    t[10] = Pop ? popOp<16, MemR> : pushOp<16, MemR>;      // PUSH16 (rrr)
    t[11] = Pop ? popOp<24, MemN> : pushOp<24, MemN>;      // PUSH24 (nnn)
    t[12] = Pop ? popOp<24, MemR> : pushOp<24, MemR>;      // PUSH24 (rrr)
    t[13] = Pop ? popOp<32, MemN> : pushOp<32, MemN>;      // PUSH32 (nnn)
    t[14] = Pop ? popOp<32, MemR> : pushOp<32, MemR>;      // PUSH32 (rrr)
}

// --- REGS FREGS LDREGS STREGS -------------------------------------------------------------
// Bank from n, (nnn), r or (rrr); sub-op in the top 3 bits, register in the low 5.
template <bool F, Kind K> uint32_t bankOp(Machine& m, uint32_t ip) {
    uint8_t low = subByte(m, ip) & 0x1F, b;
    if constexpr (K == Imm) b = m.fetch8(ip);
    if constexpr (K == MemN) b = uint8_t(m.rd<8>(m.fetch<24>(ip)));
    if constexpr (K == Reg) b = uint8_t(m.get<8>(low));
    if constexpr (K == MemR) b = uint8_t(m.rd<8>(m.get<24>(low)));
    if constexpr (F) m.fbank = b; else m.setBank(b);
    C93_NEXT(ip);
}
template <bool F> void fillBank(Op* t) {
    for (int i = 0; i < 256; ++i) t[i] = nop;
    put(t, 5, 0, bankOp<F, Imm>);    // REGS n
    put(t, 5, 1, bankOp<F, MemN>);   // REGS (nnn)
    put(t, 5, 2, bankOp<F, Reg>);    // REGS r
    put(t, 5, 3, bankOp<F, MemR>);   // REGS (rrr)
}

// LDREGS r1,r2,(addr) / STREGS (addr),r1,r2: register r1+k*step <-> RAM addr+k, r1 toward r2.
// (rrr) form "oAAAAABB BBBCCCCC": LDREGS A=r1 B=r2 C=addr reg, STREGS A=addr reg B=r1 C=r2.
template <bool Store, Kind K> uint32_t regsMemOp(Machine& m, uint32_t ip) {
    uint8_t op = subByte(m, ip), mix = m.fetch8(ip), r1, r2;
    uint8_t mid = uint8_t((op & 3) << 3 | mix >> 5);
    uint32_t a;
    if constexpr (K == MemN) { r1 = op & 31; r2 = mix & 31; a = m.fetch<24>(ip); }
    else if constexpr (Store) { a = m.get<24>(op >> 2); r1 = mid; r2 = mix & 31; }
    else { r1 = op >> 2; r2 = mid; a = m.get<24>(mix & 31); }
    int step = r1 <= r2 ? 1 : -1, n = (r2 - r1) * step + 1;
    if (Store && a + n > kRamSize + 3) C93_NEXT(ip);   // Array.Copy past the end throws before copying
    for (int k = 0; k < n; ++k) {
        uint8_t& reg = m.r[r1 + k * step];
        uint32_t c = a + k;
        if constexpr (Store) m.ram[c] = reg;
        else reg = m.ram[c > 0xFFFFFF ? c - 0xFFFFFF : c];   // the reference's off-by-one wrap
    }
    C93_NEXT(ip);
}
template <bool Store> void fillRegsMem(Op* t) {
    for (int i = 0; i < 256; ++i) t[i] = nop;
    put(t, 7, 0, regsMemOp<Store, MemR>);   // LDREGS r,r,(rrr) / STREGS (rrr),r,r
    put(t, 7, 1, regsMemOp<Store, MemN>);   // LDREGS r,r,(nnn) / STREGS (nnn),r,r
}

// --- CP SCP -------------------------------------------------------------------------------
template <int W> inline void cmpU(Machine& m, uint32_t a, uint32_t b) {
    constexpr uint32_t h = 1u << (W - 1);
    uint32_t d = a - b;
    int8_t lo = int8_t(d);   // the reference casts the difference to sbyte at every width
    m.f[0] = m.f[5] = a == b;
    m.f[1] = m.f[7] = a < b;
    m.f[2] = (d & h) != 0;
    m.f[3] = false;
    m.f[4] = !(a > h && b > h && lo > 0) || (a < h && b < h && lo < 0);
    m.f[6] = a > b;
}
template <int W> inline void cmpS(Machine& m, int32_t a, int32_t b) {
    m.f[0] = m.f[5] = a == b;
    m.f[2] = int32_t(uint32_t(a) - uint32_t(b)) < 0;   // wraps at 32 bits like unchecked C#
    m.f[3] = false;
    m.f[6] = a > b;
    m.f[7] = a < b;
}
template <bool S, int W> inline void cmp(Machine& m, uint32_t a, uint32_t b) {
    if constexpr (S) cmpS<W>(m, sx<W>(a), sx<W>(b)); else cmpU<W>(m, a, b);
}
inline void cmpF(Machine& m, float a, float b) {
    int c = a < b ? -1 : a > b ? 1 : a == b ? 0 : std::isnan(a) ? (std::isnan(b) ? 0 : -1) : 1;   // float.CompareTo
    m.f[0] = m.f[5] = c == 0;
    m.f[1] = m.f[7] = c < 0;
    m.f[2] = a < 0;
    m.f[3] = m.f[4] = false;
    m.f[6] = c > 0;
}

template <Kind K, int W> inline uint32_t cpVal(Machine& m, uint8_t i) {
    if constexpr (K == Reg) return m.get<W>(i); else return m.rd<W>(m.get<24>(i));
}
// "ooooooAA AAABBBBB": both operands from the packed register pair.
template <bool S, Kind A, Kind B, int W> uint32_t cpPair(Machine& m, uint32_t ip) {
    uint8_t hi = subByte(m, ip), lo = m.fetch8(ip), a = uint8_t((hi & 3) << 3 | lo >> 5);
    cmp<S, W>(m, cpVal<A, W>(m, a), cpVal<B, W>(m, lo & 31));
    C93_NEXT(ip);
}
// "oooooouu uuuAAAAA imm": register (or (rrr)) against an immediate.
template <bool S, Kind A, int W> uint32_t cpImm(Machine& m, uint32_t ip) {
    uint8_t i = m.fetchReg(ip);
    uint32_t b = m.fetch<W>(ip);
    cmp<S, W>(m, cpVal<A, W>(m, i), b);
    C93_NEXT(ip);
}
inline uint32_t cpFF(Machine& m, uint32_t ip) { uint8_t x = m.fetch8(ip); cmpF(m, m.fr(x >> 4), m.fr(x)); C93_NEXT(ip); }
inline uint32_t cpFImm(Machine& m, uint32_t ip) { uint8_t f = m.fetch8(ip); cmpF(m, m.fr(f), fromBits(m.fetch<32>(ip))); C93_NEXT(ip); }
template <int W> uint32_t cpFReg(Machine& m, uint32_t ip) {
    uint8_t f = fetchF(m, ip);
    float v = float(m.get<W>(m.fetchReg(ip)));
    if (v != 0) cmpF(m, m.fr(f), v);   // "division by zero": reported to nobody, flags untouched
    C93_NEXT(ip);
}
template <int W> uint32_t cpRegF(Machine& m, uint32_t ip) {
    uint8_t i = m.fetchReg(ip);
    float v = m.fr(fetchF(m, ip));
    if (v != 0) cmpF(m, float(m.get<W>(i)), v);
    C93_NEXT(ip);
}
// fr vs float in RAM. (nnn): "uuuuAAAA nnn"; (rrr): fr = sub-op low 2 bits + mix top 2.
template <Kind K, bool MemFirst> uint32_t cpFMem(Machine& m, uint32_t ip) {
    uint8_t hi = subByte(m, ip), mix = m.fetch8(ip), f;
    uint32_t a;
    if constexpr (K == MemN) { f = mix & 15; a = m.fetch<24>(ip); }
    else { f = uint8_t((hi & 3) << 2 | mix >> 6); a = m.get<24>(mix & 31); }
    float v = m.rdF(a);
    if (MemFirst) cmpF(m, v, m.fr(f)); else cmpF(m, m.fr(f), v);
    C93_NEXT(ip);
}

template <bool S> void fillCp(Op* t) {
    for (int i = 0; i < 256; ++i) t[i] = nop;
    put(t, 2, 0, cpImm<S, Reg, 8>);           // CP r,n
    put(t, 2, 1, cpPair<S, Reg, Reg, 8>);     // CP r,r
    put(t, 2, 2, cpImm<S, Reg, 16>);          // CP rr,nn
    put(t, 2, 3, cpPair<S, Reg, Reg, 16>);    // CP rr,rr
    put(t, 2, 4, cpImm<S, Reg, 24>);          // CP rrr,nnn
    put(t, 2, 5, cpPair<S, Reg, Reg, 24>);    // CP rrr,rrr
    put(t, 2, 6, cpImm<S, Reg, 32>);          // CP rrrr,nnnn
    put(t, 2, 7, cpPair<S, Reg, Reg, 32>);    // CP rrrr,rrrr
    put(t, 2, 8, cpPair<S, Reg, MemR, 8>);    // CP r,(rrr)
    put(t, 2, 9, cpPair<S, Reg, MemR, 16>);   // CP rr,(rrr)
    put(t, 2, 10, cpPair<S, Reg, MemR, 24>);  // CP rrr,(rrr)
    put(t, 2, 11, cpPair<S, Reg, MemR, 32>);  // CP rrrr,(rrr)
    put(t, 2, 12, cpPair<S, MemR, MemR, 8>);  // CP (rrr),(rrr)
    put(t, 2, 13, cpImm<S, MemR, 8>);         // CP (rrr),n
    if (S) return;                            // SCP has no float forms
    put(t, 2, 14, cpFF);                      // CP fr,fr
    put(t, 2, 15, cpFImm);                    // CP fr,nnn
    put(t, 2, 16, cpFReg<8>);                 // CP fr,r
    put(t, 2, 17, cpFReg<16>);                // CP fr,rr
    put(t, 2, 18, cpFReg<24>);                // CP fr,rrr
    put(t, 2, 19, cpFReg<32>);                // CP fr,rrrr
    put(t, 2, 20, cpRegF<8>);                 // CP r,fr
    put(t, 2, 21, cpRegF<16>);                // CP rr,fr
    put(t, 2, 22, cpRegF<24>);                // CP rrr,fr
    put(t, 2, 23, cpRegF<32>);                // CP rrrr,fr
    put(t, 2, 24, cpFMem<MemN, false>);       // CP fr,(nnn)
    put(t, 2, 25, cpFMem<MemR, false>);       // CP fr,(rrr)
    put(t, 2, 26, cpFMem<MemN, true>);        // CP (nnn),fr
    put(t, 2, 27, cpFMem<MemR, true>);        // CP (rrr),fr
}
