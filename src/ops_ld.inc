// LD and the memory-block families: Execution/ExLD.cs (table-style, CPU/Instructions.cs sub-ops),
// ExMEMF.cs, ExMEMC.cs, ExFIND.cs, ExGETBITS.cs, ExSETBITS.cs, ExSETVAR.cs, ExGETVAR.cs, ExVDL.cs,
// helpers from RAM/Memory.cs and RAM/MemoryController.cs. No flags change except where noted.
//
// Reference quirks kept:
//  - LD rrr,fr / rrrr,fr overflow at >= 16777215 / >= 4294967296 (0xFFFFFFFF as a float), not 2^24 / 2^32;
//    on overflow OV is set and the register is left alone; otherwise OV is cleared.
//  - fr -> integer is .NET 8's unchecked cast: truncation via int32 for r/rr, via int64 for rrr/rrrr.
//  - LD fr,(rrr...) reads through GetMemoryWrapped, which wraps past 0xFFFFFF by subtracting 0xFFFFFF
//    (0x1000000 -> 1); LD fr,(nnn...) reads plainly. Stores of fr never wrap.
//  - Unsuffixed register sources widen into wider registers (LD rrrr,r zero-extends a byte).
//  - Offsets: (nnn,nnn) is a signed 24-bit literal; (x,r)/(x,rr)/(x,rrr) are the register read signed.
//  - LD (mem),nnnn,n: n bytes, zero-padded big-endian value in the last min(n,4) of them.
//    LD (mem),src,n,rrr and (mem),nnnn,n,nnn repeat that block; with 0 repeats the value bytes are
//    still written but the padding is not; the 32-bit value of a memory source is read first.
//  - SETBITS clears the bits after the field inside its last byte (the reference shifts them out).
//  - GETBITS/SETBITS bit count 0 or wider than the register means the register width.
//  - FIND not found -> 0xFFFFFF; the search runs over 16 MB + 3 (the reference's RAM array).
//  - VCL is VDL (both opcodes run ExVDL; nothing is cleared). GETVAR has only the n,rrrr and
//    rrrr,rrrr forms; other sub-ops of the switch families do nothing.
// shortcut: where the reference threw (address past 16 MB + 3, HMEM index >= 64K, empty FIND
// pattern) these wrap at 24 bits or do nothing instead of halting.

// Index of a block access at a, as the reference's 16 MB + 3 array; past it, wrap.
inline uint32_t ramAt(uint32_t a) { return a < kRamSize + 3 ? a : a & kRamMask; }

// --- Effective addresses: (nnn) (nnn,nnn) (nnn,r) (nnn,rr) (nnn,rrr), then the same on (rrr) ---
enum Am : uint8_t { aN, aNN, aNr, aNrr, aNrrr, aR, aRN, aRr, aRrr, aRrrr };
template <int A> inline uint32_t ea(Machine& m, uint32_t& ip) {
    uint32_t base = A < aR ? m.fetch<24>(ip) : m.get<24>(m.fetchReg(ip));
    constexpr int k = A % 5;
    if constexpr (k == 0) return base;
    if constexpr (k == 1) return base + uint32_t(sx<24>(m.fetch<24>(ip)));
    if constexpr (k == 2) return base + uint32_t(sx<8>(m.get<8>(m.fetchReg(ip))));
    if constexpr (k == 3) return base + uint32_t(sx<16>(m.get<16>(m.fetchReg(ip))));
    if constexpr (k == 4) return base + uint32_t(sx<24>(m.get<24>(m.fetchReg(ip))));
}

// MemoryController.GetMemoryWrapped(a, 4) as float bits, wrap quirk included.
inline uint32_t rdWrapped32(const Machine& m, uint32_t a) {
    uint32_t b = 0;
    for (uint32_t k = 0; k < 4; ++k) {
        uint32_t c = a + k;
        if (c > 0xFFFFFF) c -= 0xFFFFFF;
        b = b << 8 | m.ram[ramAt(c)];
    }
    return b;
}
inline float asFloat(uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; }
inline uint32_t cvtI32(float x) { return x > -2147483904.f && x < 2147483648.f ? uint32_t(int32_t(x)) : 0x80000000u; }
inline uint32_t cvtI64(float x) {
    return x >= -9223372036854775808.f && x < 9223372036854775808.f ? uint32_t(int64_t(x)) : 0;
}

// Memory.SetBytesToRAM: n bytes, the value's low min(n,4) bytes big-endian at the end, zeros before.
inline void putBytes(Machine& m, uint32_t a, uint32_t v, uint32_t n) {
    for (uint32_t k = 0; k < n; ++k) {
        uint32_t s = n - 1 - k;
        m.ram[ramAt(a + k)] = s < 4 ? uint8_t(v >> 8 * s) : 0;
    }
}
// Memory.SetBytesToRAMRepeated: rep blocks of putBytes.
inline void putBytesRep(Machine& m, uint32_t a, uint32_t v, uint32_t n, uint32_t rep) {
    if (rep == 0) {   // the reference zeroes nothing but still writes the first block's value bytes
        for (uint32_t s = 0; s < n && s < 4; ++s) m.ram[ramAt(a + n - 1 - s)] = uint8_t(v >> 8 * s);
        return;
    }
    uint64_t total = uint64_t(n) * rep;
    if (a <= kRamSize + 3 && total <= kRamSize + 3 - a) {
        putBytes(m, a, v, n);
        for (uint64_t done = n; done < total;) {   // doubling, as the reference
            uint64_t c = done < total - done ? done : total - done;
            std::memcpy(m.ram + a + done, m.ram + a, size_t(c));
            done += c;
        }
        return;
    }
    if (total > kRamSize) total = kRamSize;   // the reference threw; wrap, at most once round
    for (uint64_t k = 0; k < total; ++k) {
        uint32_t s = n - 1 - uint32_t(k % n);
        m.ram[(a + uint32_t(k)) & kRamMask] = s < 4 ? uint8_t(v >> 8 * s) : 0;
    }
}

// --- LD into integer registers -----------------------------------------------------------------
template <int W> uint32_t ldImm(Machine& m, uint32_t ip) { uint8_t d = m.fetchReg(ip); m.set<W>(d, m.fetch<W>(ip)); C93_NEXT(ip); }
template <int DW, int SW> uint32_t ldReg(Machine& m, uint32_t ip) { uint8_t d = m.fetchReg(ip); m.set<DW>(d, m.get<SW>(m.fetchReg(ip))); C93_NEXT(ip); }
template <int W, int A> uint32_t ldMem(Machine& m, uint32_t ip) { uint8_t d = m.fetchReg(ip); m.set<W>(d, m.rd<W>(ea<A>(m, ip))); C93_NEXT(ip); }
template <int W> uint32_t ldFromFr(Machine& m, uint32_t ip) {
    uint8_t d = m.fetchReg(ip);
    float x = m.fr(fetchF(m, ip));
    constexpr float lim = W == 8 ? 256.f : W == 16 ? 65536.f : W == 24 ? 16777215.f : 4294967296.f;
    m.f[3] = x >= lim;
    if (!m.f[3]) m.set<W>(d, W <= 16 ? cvtI32(x) : cvtI64(x));
    C93_NEXT(ip);
}

// --- LD into memory ----------------------------------------------------------------------------
template <int D, int W> uint32_t stReg(Machine& m, uint32_t ip) { uint32_t a = ea<D>(m, ip); m.wr<W>(a, m.get<W>(m.fetchReg(ip))); C93_NEXT(ip); }
template <int D> uint32_t stFr(Machine& m, uint32_t ip) { uint32_t a = ea<D>(m, ip); m.wrF(a, m.fr(fetchF(m, ip))); C93_NEXT(ip); }
template <int D> uint32_t stFill(Machine& m, uint32_t ip) {   // (mem),nnnn,n
    uint32_t a = ea<D>(m, ip), v = m.fetch<32>(ip);
    putBytes(m, a, v, m.fetch8(ip));
    C93_NEXT(ip);
}
template <int D> uint32_t stFillRep(Machine& m, uint32_t ip) {   // (mem),nnnn,n,nnn
    uint32_t a = ea<D>(m, ip), v = m.fetch<32>(ip);
    uint8_t n = m.fetch8(ip);
    putBytesRep(m, a, v, n, m.fetch<24>(ip));
    C93_NEXT(ip);
}
template <int D, int S> uint32_t stCopyRep(Machine& m, uint32_t ip) {   // (mem),(mem),n,rrr
    uint32_t a = ea<D>(m, ip), v = m.rd<32>(ea<S>(m, ip));
    uint8_t n = m.fetch8(ip);
    putBytesRep(m, a, v, n, m.get<24>(m.fetchReg(ip)));
    C93_NEXT(ip);
}

// --- LD into float registers -------------------------------------------------------------------
inline uint32_t frImm(Machine& m, uint32_t ip) { uint8_t d = fetchF(m, ip); m.fr(d) = asFloat(m.fetch<32>(ip)); C93_NEXT(ip); }
template <int W> uint32_t frReg(Machine& m, uint32_t ip) { uint8_t d = fetchF(m, ip); m.fr(d) = float(m.get<W>(m.fetchReg(ip))); C93_NEXT(ip); }
template <int A> uint32_t frMem(Machine& m, uint32_t ip) {
    uint8_t d = fetchF(m, ip);
    uint32_t a = ea<A>(m, ip);
    m.fr(d) = asFloat(A < aR ? m.rd<32>(a) : rdWrapped32(m, a));
    C93_NEXT(ip);
}
inline uint32_t frFr(Machine& m, uint32_t ip) { uint8_t d = fetchF(m, ip); m.fr(d) = m.fr(fetchF(m, ip)); C93_NEXT(ip); }

template <int I> struct Ic { static constexpr int v = I; };
template <int I, int N, class F> void forN(F&& f) {
    if constexpr (I < N) { f(Ic<I>{}); forN<I + 1, N>(f); }
}

// Sub-op layout (CPU/Instructions.cs): r 0..12, rr 13..26, rrr 27..41, rrrr 42..57,
// 10 memory destinations of 17 sub-ops each from 58, fr 228..243. 244..255 stay illegal.
inline void fillLd(Op* t) {
    t[0] = ldImm<8>;   t[1] = ldReg<8, 8>;                                           t[12] = ldFromFr<8>;
    t[13] = ldImm<16>; t[14] = ldReg<16, 8>; t[15] = ldReg<16, 16>;                  t[26] = ldFromFr<16>;
    t[27] = ldImm<24>; t[28] = ldReg<24, 8>; t[29] = ldReg<24, 16>; t[30] = ldReg<24, 24>; t[41] = ldFromFr<24>;
    t[42] = ldImm<32>; t[43] = ldReg<32, 8>; t[44] = ldReg<32, 16>; t[45] = ldReg<32, 24>; t[46] = ldReg<32, 32>;
    t[57] = ldFromFr<32>;
    t[228] = frImm; t[229] = frReg<8>; t[230] = frReg<16>; t[231] = frReg<24>; t[232] = frReg<32>; t[243] = frFr;
    forN<0, 10>([&](auto a) {
        constexpr int A = decltype(a)::v;
        t[2 + A] = ldMem<8, A>; t[16 + A] = ldMem<16, A>; t[31 + A] = ldMem<24, A>; t[47 + A] = ldMem<32, A>;
        t[233 + A] = frMem<A>;
        Op* d = t + 58 + 17 * A;   // LD (mem),...
        d[0] = stFill<A>; d[1] = stFillRep<A>;
        d[2] = stReg<A, 8>; d[3] = stReg<A, 16>; d[4] = stReg<A, 24>; d[5] = stReg<A, 32>;
        forN<0, 10>([&](auto s) { d[6 + decltype(s)::v] = stCopyRep<A, decltype(s)::v>; });
        d[16] = stFr<A>;
    });
}

// --- MEMF / MEMC: three operands; registers first (the first one in the sub-op byte's low
// 5 bits), then the immediates, each group in operand order -------------------------------------
template <bool AI, bool BI, bool CI, int CW> inline void ops3(Machine& m, uint32_t& ip, uint32_t& a, uint32_t& b, uint32_t& c) {
    uint8_t op = subByte(m, ip);
    bool first = true;
    auto reg = [&] { uint8_t i = first ? uint8_t(op & 31) : m.fetchReg(ip); first = false; return i; };
    uint8_t ra = AI ? 0 : reg(), rb = BI ? 0 : reg(), rc = CI ? 0 : reg();
    a = AI ? m.fetch<24>(ip) : m.get<24>(ra);
    b = BI ? m.fetch<24>(ip) : m.get<24>(rb);
    c = CI ? m.fetch<CW>(ip) : m.get<CW>(rc);
}
template <int K> uint32_t memf(Machine& m, uint32_t ip) {   // MEMF addr, length, value
    uint32_t a, n, v;
    ops3<(K & 1) != 0, (K & 2) != 0, (K & 4) != 0, 8>(m, ip, a, n, v);
    if (a + n <= kRamSize + 3) std::memset(m.ram + a, int(v), n);
    else for (uint32_t k = 0; k < n; ++k) m.ram[(a + k) & kRamMask] = uint8_t(v);   // the reference threw
    C93_NEXT(ip);
}
template <int K> uint32_t memc(Machine& m, uint32_t ip) {   // MEMC source, destination, length; overlap-safe like Array.Copy
    uint32_t s, d, n;
    ops3<(K & 1) != 0, (K & 2) != 0, (K & 4) != 0, 24>(m, ip, s, d, n);
    if (s + n <= kRamSize + 3 && d + n <= kRamSize + 3) std::memmove(m.ram + d, m.ram + s, n);
    else for (uint32_t k = 0; k < n; ++k) m.ram[(d + k) & kRamMask] = m.ram[(s + k) & kRamMask];   // threw
    C93_NEXT(ip);
}

// --- FIND (rrr),n / (rrr),(nnn) / (rrr),(rrr): the register gets the match address -----------
template <int K> uint32_t find(Machine& m, uint32_t ip) {
    constexpr uint32_t end = kRamSize + 3;
    uint8_t r = subByte(m, ip) & 31;
    uint32_t from = m.get<24>(r);
    if constexpr (K == 0) {
        const void* p = std::memchr(m.ram + from, m.fetch8(ip), end - from);
        m.set<24>(r, p ? uint32_t(static_cast<const uint8_t*>(p) - m.ram) : 0xFFFFFF);
    } else {
        uint32_t s = K == 1 ? m.fetch<24>(ip) : m.get<24>(m.fetchReg(ip));
        const void* z = std::memchr(m.ram + s, 0, end - s);
        uint32_t len = z ? uint32_t(static_cast<const uint8_t*>(z) - m.ram) - s : 0;
        if (len == 0) C93_NEXT(ip);   // the reference threw: empty or unterminated pattern
        const uint8_t* pat = m.ram + s;
        uint32_t found = 0xFFFFFF;
        for (uint32_t i = from; i + len <= end;) {
            const void* p = std::memchr(m.ram + i, pat[0], end - i);
            if (!p) break;
            i = uint32_t(static_cast<const uint8_t*>(p) - m.ram);
            if (i + len > end) break;
            if (!std::memcmp(m.ram + i, pat, len)) { found = i; break; }
            ++i;
        }
        m.set<24>(r, found);
    }
    C93_NEXT(ip);
}

// --- GETBITS / SETBITS: big-endian bit memory addressed by a 32-bit register -------------------
// "ooooooAA AAABBBBB": A is the value register (GET target / SET address), B the other one.
inline uint32_t bitBase(uint32_t byteAddr, int n) {   // the reference threw past 16 MB + 3
    return byteAddr + n <= kRamSize + 3 ? byteAddr : byteAddr & kRamMask;
}
template <int W, bool BitsReg> uint32_t getbits(Machine& m, uint32_t ip) {
    uint8_t hi = subByte(m, ip), lo = m.fetch8(ip);
    uint8_t d = uint8_t((hi & 3) << 3 | lo >> 5);
    uint32_t bitAddr = m.get<32>(lo & 31);
    uint32_t bits = BitsReg ? m.get<8>(m.fetchReg(ip)) : m.fetch8(ip);
    if (bits == 0 || bits > W) bits = W;
    int total = int(bitAddr & 7) + int(bits), n = (total + 7) >> 3;
    const uint8_t* p = m.ram + bitBase(bitAddr >> 3, n);
    uint64_t raw = 0;
    for (int k = 0; k < n; ++k) raw = raw << 8 | p[k];
    m.set<W>(d, uint32_t(raw >> (n * 8 - total) & ((uint64_t(1) << bits) - 1)));
    C93_NEXT(ip);
}
template <int W, bool BitsReg> uint32_t setbits(Machine& m, uint32_t ip) {
    uint8_t hi = subByte(m, ip), lo = m.fetch8(ip);
    uint32_t bitAddr = m.get<32>(uint8_t((hi & 3) << 3 | lo >> 5));
    uint64_t v = m.get<W>(lo & 31);
    uint32_t bits = BitsReg ? m.get<8>(m.fetchReg(ip)) : m.fetch8(ip);
    if (bits == 0 || bits > W) bits = W;
    int total = int(bitAddr & 7) + int(bits), n = (total + 7) >> 3, shift = n * 8 - total;
    uint8_t* p = m.ram + bitBase(bitAddr >> 3, n);
    uint64_t raw = 0, mask = (uint64_t(1) << bits) - 1;
    for (int k = 0; k < n; ++k) raw = raw << 8 | p[k];
    raw = ((raw >> shift & ~mask) | (v & mask)) << shift;   // the low `shift` bits come back as zeros
    for (int k = n - 1; k >= 0; --k, raw >>= 8) p[k] = uint8_t(raw);
    C93_NEXT(ip);
}

// --- SETVAR / GETVAR: 32-bit cells in hardware memory ------------------------------------------
inline uint32_t& hvar(Machine& m, uint32_t i) { return m.hmem[i & (kHmemSize - 1)]; }   // >= 64K threw
inline uint32_t setvarNN(Machine& m, uint32_t ip) { uint32_t i = m.fetch<32>(ip); hvar(m, i) = m.fetch<32>(ip); C93_NEXT(ip); }
inline uint32_t setvarNR(Machine& m, uint32_t ip) { uint32_t i = m.fetch<32>(ip); hvar(m, i) = m.get<32>(m.fetchReg(ip)); C93_NEXT(ip); }
inline uint32_t setvarRN(Machine& m, uint32_t ip) { uint32_t i = m.get<32>(m.fetchReg(ip)); hvar(m, i) = m.fetch<32>(ip); C93_NEXT(ip); }
inline uint32_t setvarRR(Machine& m, uint32_t ip) { uint32_t i = m.get<32>(m.fetchReg(ip)); hvar(m, i) = m.get<32>(m.fetchReg(ip)); C93_NEXT(ip); }
inline uint32_t getvarNR(Machine& m, uint32_t ip) { uint32_t v = hvar(m, m.fetch<32>(ip)); m.set<32>(m.fetchReg(ip), v); C93_NEXT(ip); }
inline uint32_t getvarRR(Machine& m, uint32_t ip) { uint32_t v = hvar(m, m.get<32>(m.fetchReg(ip))); m.set<32>(m.fetchReg(ip), v); C93_NEXT(ip); }

// --- VDL / VCL: present pages now; "ooAAAAAA" ---------------------------------------------------
template <int K> uint32_t vdl(Machine& m, uint32_t ip) {
    uint8_t r = subByte(m, ip) & 31;
    uint8_t v;
    if constexpr (K == 0) v = m.fetch8(ip);
    if constexpr (K == 1) v = uint8_t(m.get<8>(r));
    if constexpr (K == 2) v = uint8_t(m.rd<8>(m.fetch<24>(ip)));
    if constexpr (K == 3) v = uint8_t(m.rd<8>(m.get<24>(r)));
    m.manualUpdate(v);
    C93_NEXT(ip);
}

inline void nops(Op* t) { for (int i = 0; i < 256; ++i) t[i] = nop; }   // switch families
inline void fillMem(Op* f, Op* c) {
    forN<0, 8>([&](auto k) { put(f, 5, decltype(k)::v, memf<decltype(k)::v>); put(c, 5, decltype(k)::v, memc<decltype(k)::v>); });
}
inline void fillBits(Op* g, Op* s) {   // sub-ops 0..3: bit count immediate, 4..7: in a register
    nops(g); nops(s);
    forN<0, 8>([&](auto k) {
        constexpr int K = decltype(k)::v, W = 8 * ((K & 3) + 1);
        put(g, 2, K, getbits<W, (K >= 4)>);
        put(s, 2, K, setbits<W, (K >= 4)>);
    });
}
inline void fillFind(Op* t) { nops(t); put(t, 5, 0, find<0>); put(t, 5, 1, find<1>); put(t, 5, 2, find<2>); }
inline void fillVars(Op* s, Op* g) {
    nops(s); nops(g);
    s[0] = setvarNN; s[1] = setvarNR; s[2] = setvarRN; s[3] = setvarRR;
    g[1] = getvarNR; g[3] = getvarRR;
}
inline void fillVdl(Op* t) { put(t, 6, 0, vdl<0>); put(t, 6, 1, vdl<1>); put(t, 6, 2, vdl<2>); put(t, 6, 3, vdl<3>); }
