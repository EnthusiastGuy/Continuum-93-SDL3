// PLAY (Execution/ExPLAY.cs). PLAY nnn / PLAY rrr: the sound definition at that address is read now,
// on the CPU thread (audio_xsound.cpp), and queued for the APU; registers, flags and RAM are untouched.
// Where the reading threw (invalid wave type, a COMPLEX type listed twice) the machine halts (D-16).
// Other sub-ops do nothing (switch without default in the reference).

template <bool Reg> uint32_t playOp(Machine& m, uint32_t ip) {
    uint32_t at = (ip - 2) & kRamMask;
    uint32_t a = Reg ? m.get<24>(m.fetchReg(ip)) : m.fetch<24>(ip);
    XSoundParams p;
    if (std::string err = readSoundParams(m, a, p); !err.empty()) {
        LOGE("Audio: PLAY at 0x%06X: %s; machine halted", at, err.c_str());
        m.running.store(false, std::memory_order_relaxed);
        return ip;
    }
    audioPlay(std::move(p));
    return ip;
}
inline void fillPlay(Op* t) {
    for (int i = 0; i < 256; ++i) t[i] = nop;
    t[0] = playOp<false>;
    t[1] = playOp<true>;
}
