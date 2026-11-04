// RAND and WAIT (Execution/ExRAND.cs, ExWAIT.cs).
// RAND draws from .NET's seeded System.Random algorithm (netrandom.h).
// The reference started unseeded (any sequence will do), but RAND fr,seed reseeds it and from then
// on its sequence is deterministic, so the port reproduces it. One generator for every machine, as
// the reference's static field. Ranges as Random.Next(min, max): max exclusive; 0 picks a default.

inline NetRandom gRand{int32_t(std::random_device{}() & 0x7FFFFFFF)};

// RAND r / r,n ... rrrr / rrrr,nnnn. 32-bit: Next(-v/2, v/2) + v/2, so the range is even.
template <int W, bool Imm> uint32_t randInt(Machine& m, uint32_t ip) {
    uint8_t i = m.fetchReg(ip);
    uint32_t v = Imm ? m.fetch<W>(ip) : m.get<W>(i);
    if constexpr (W == 32) {
        int32_t half = !Imm && v == 0 ? INT32_MAX / 2 : int32_t(v / 2);
        m.set<32>(i, uint32_t(gRand.next(-half, half) + half));
    } else {
        if (!Imm && v == 0) v = maskOf<W>();
        m.set<W>(i, uint32_t(gRand.next(0, int32_t(v))));
    }
    C93_NEXT(ip);
}
// RAND fr: [0, 1). RAND fr, nnnn / fr, rrrr: reseed with the signed value first.
template <int Seed> uint32_t randF(Machine& m, uint32_t ip) {   // 0 none, 1 immediate, 2 register
    uint8_t f = m.fetch8(ip);
    if constexpr (Seed == 1) gRand = NetRandom(int32_t(m.fetch<32>(ip)));
    if constexpr (Seed == 2) gRand = NetRandom(int32_t(m.get<32>(m.fetchReg(ip))));
    m.fr(f) = float(gRand.nextDouble());
    C93_NEXT(ip);
}
inline void fillRand(Op* t) {
    for (int i = 0; i < 256; ++i) t[i] = nop;   // switch in the reference: unknown sub-ops do nothing
    t[0] = randInt<8, false>;  t[1] = randInt<16, false>; t[2] = randInt<24, false>; t[3] = randInt<32, false>;
    t[4] = randInt<8, true>;   t[5] = randInt<16, true>;  t[6] = randInt<24, true>;  t[7] = randInt<32, true>;
    t[8] = randF<0>; t[9] = randF<1>; t[10] = randF<2>;
}

// WAIT nn: Thread.Sleep(nn ms) on the CPU thread. Sub-op "oouuuuuu": only top bits 00 exist.
inline uint32_t waitOp(Machine& m, uint32_t ip) { std::this_thread::sleep_for(std::chrono::milliseconds(m.fetch<16>(ip))); C93_NEXT(ip); }
inline void fillWait(Op* t) {
    for (int i = 0; i < 256; ++i) t[i] = i < 64 ? waitOp : nop;
}
