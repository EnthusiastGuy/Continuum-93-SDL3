// XSound synthesis and the PLAY sound definition, ported from Emulator/Audio/XSound:
// XSoundGenerator.cs, Utilities/XWaveTypeGenerator.cs, XEnvelopeController.cs, XFrequencyController.cs,
// XFilterController.cs, XFlangerController.cs, XPhaserController.cs, XRepeatController.cs,
// PCMConvertor.cs, ExecutionUtilities/XSoundParamsProcessing.cs + XSoundFlags.cs.
// Every expression keeps the C#'s float/double types and order (x64 RyuJIT: SSE, no FMA), and the
// same CRT sin/tan/pow/log, so the PCM is bit-identical (oracle --xsound / conformance --xsound).
// Reproduced quirks:
//  - A repeat restarts the duty cycle at 0.5 - DutyCycle/2 (XFrequencyController.Reset), not at DutyCycle;
//    it resets frequency and phaser only (not envelope, filters, flanger).
//  - RINGMODULATION and LFO take the duty cycle (0..1) as their second frequency in Hz.
//  - COMPLEX is bit-crushed twice; weights are byte % 100.
//  - The envelope counts samples in a float: past 2^24 it stops advancing, so a longer sound never ends.
//  - PCM is normalised to the peak (LINQ Max: NaN ranks lowest) before the volume; volume > 1 wraps the
//    int16 (unchecked cast). Float->int casts are .NET 8 x64's: out of range or NaN gives int.MinValue.
//  - Where the reference's APU thread threw (no samples; a delay index made negative by a huge
//    flanger/phaser offset) the generator reports it; audio.cpp then stops the APU like the reference.
#include <algorithm>
#include <cmath>
#include <memory>

#include "audio.h"
#include "machine.h"
#include "netrandom.h"

namespace c93 {
namespace {

constexpr double kPi = 3.14159265358979323846;   // Math.PI

int32_t netI32(double d) { return d >= -2147483648.0 && d < 2147483648.0 ? int32_t(d) : INT32_MIN; }
// Math.Max / Math.Min / Math.Clamp (float): NaN propagates, +0 > -0.
float netMax(float a, float b) { if (a != b) return std::isnan(a) ? a : (b < a ? a : b); return std::signbit(b) ? a : b; }
float netMin(float a, float b) { if (a != b) return std::isnan(a) ? a : (a < b ? a : b); return std::signbit(a) ? a : b; }
float netClamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }   // lo <= hi at every call site
double sin2pi(double x) { return std::sin(2.0 * kPi * x); }   // only where C# wrote 2.0 * Math.PI * <one double>

bool validWave(uint8_t t) { return t <= 21 || (t >= 50 && t <= 52) || t == 100; }

// --- XWaveTypeGenerator ------------------------------------------------------------------------
struct Wave {
    NetRandom& rnd;
    float noise() { return float(rnd.nextDouble() * 2.0 - 1.0); }
    float sine(float t, float f) { return float(std::sin(2.0 * kPi * f * t)); }

    float one(uint8_t type, float t, float f, float d) {
        float period, tt;
        switch (type) {
        case 0: return sine(t, f);
        case 3: period = 1.0f / f; tt = std::fmod(t, period); return tt < period / 2.0f ? 1.0f : -1.0f;
        case 7: period = 1.0f / f; tt = std::fmod(t, period); return 2.0f * (tt / period) - 1.0f;
        case 5: period = 1.0f / f; tt = std::fmod(t, period); return -(4.0f * std::fabs(tt / period - 0.5f) - 1.0f);
        case 6:
            period = 1.0f / f; tt = std::fmod(t, period);
            if (tt < d * period) return (2.0f * tt) / (d * period) - 1.0f;
            return 1.0f - (2.0f * (tt - d * period)) / ((1.0f - d) * period);
        case 50: return noise();
        case 51: { float w = noise(); return w / float(1.0 + std::pow(double(w), 2.0)); }
        case 9: return float(std::fabs(1.0 - std::fmod(double(t * f), 2.0)) * (1.0 - std::fmod(double(t * f), 1.0)));
        case 10: return netClamp(float(std::tan(2.0 * kPi * f * t)), -1.0f, 1.0f);
        case 13: {
            float mf = 5.0f, md = 0.05f;
            float m = f * (1.0f + md * float(std::sin(2.0 * kPi * mf * t)));
            return float(std::sin(2.0 * kPi * m * t));
        }
        case 14: return float(std::sin(2.0 * kPi * f * t) + 0.5 * std::sin(2.0 * kPi * 2 * f * t) +
                              0.25 * std::sin(2.0 * kPi * 4 * f * t));
        case 4: period = 1.0f / f; tt = std::fmod(t, period); return tt < d * period ? 1.0f : -1.0f;
        case 2: return netMax(0.0f, sine(t, f));
        case 8: period = 1.0f / f; tt = std::fmod(t, period); return 2.0f * (1.0f - tt / period) - 1.0f;
        case 1: { float s = sine(t, f), c = 1.0f - d; return netClamp(s, -c, c); }
        case 11: return float(std::pow(2.0, double(sine(t, f)))) - 1.0f;
        case 15: {
            float f1 = 730.0f, f2 = 1090.0f, f3 = 2440.0f;
            return float(0.5 * std::sin(2.0 * kPi * f * t) + 0.3 * std::sin(2.0 * kPi * f1 * t) +
                         0.2 * std::sin(2.0 * kPi * f2 * t) + 0.1 * std::sin(2.0 * kPi * f3 * t));
        }
        case 52: {   // Box-Muller
            double u1 = rnd.nextDouble(), u2 = rnd.nextDouble();
            return float(std::sqrt(-2.0 * std::log(u1)) * sin2pi(u2));
        }
        case 12: {
            float s = sine(t, f);
            if (std::fabs(s) > d) s = float(2.0 * d - std::fabs(s));
            return s;
        }
        case 16: return sine(t, f) * sine(t, d);
        case 17: { float n = noise(); float lfo = float(0.5 * (1.0 + std::sin(2.0 * kPi * d * t))); return lfo * n; }
        case 18: {
            float m = float(std::sin(2.0 * kPi * f * t * 0.25) * 0.5);
            return float(std::sin(2.0 * kPi * (f + m) * t));
        }
        case 19: {
            period = 1.0f / f;
            float saw = 2.0f * (t / period - float(std::floor(0.5 + t / period)));
            float mod = 0.5f * saw * float(std::sin(2.0 * kPi * f * t * 0.1));
            return saw + mod;
        }
        case 20: { float a = sine(t, f), b = float(std::sin(2.0 * kPi * (f * 0.995f) * t)); return 0.5f * (a + b); }
        case 21: { float ph = t + 0.5f * float(std::sin(2.0 * kPi * t * 0.1f)); return float(std::sin(2.0 * kPi * f * ph)); }
        default: return 0.0f;   // COMPLEX inside COMPLEX, or no case in the switch
        }
    }
    static float crush(float s, int bitDepth) {
        s = netMax(-1.0f, netMin(1.0f, s));
        int32_t half = netI32(std::pow(2.0, double(bitDepth))) / 2;
        return std::nearbyint(s * float(half)) / float(half);   // MathF.Round: half to even
    }
    float generate(const XSoundParams& p, float t, float f, float d) {
        float v;
        if (p.waveType == 100) {
            v = 0.0f;
            for (auto [type, weight] : p.waveTypeValues) v += one(type, t, f, d) * (float(weight) / 100.0f);
            v = crush(v, p.bitDepth);
        } else v = one(p.waveType, t, f, d);
        return crush(v, p.bitDepth);
    }
};

// --- Controllers --------------------------------------------------------------------------------
struct Envelope {
    float attack, sustain, decay, punch, now = 0;
    bool playing = true;
    explicit Envelope(const XSoundParams& p)
        : attack(p.envelopeAttack * p.sampleRate), sustain(p.envelopeSustain * p.sampleRate),
          decay(p.envelopeDecay * p.sampleRate), punch(p.envelopePunch) {}
    float get() {
        if (!playing) return 0.0f;
        now++;
        if (now < attack) return now / attack;
        if (now < attack + sustain) return 1.0f + (1.0f - ((now - attack) / sustain)) * 2.0f * punch;
        if (now < attack + sustain + decay) return 1.0f - ((now - attack - sustain) / decay);
        playing = false;
        return 0.0f;
    }
};

struct Frequency {
    const XSoundParams& p;
    float freq, minFreq, ramp, dramp, vibPhase = 0, vibSpeed, vibAmp;
    int32_t arpTime = 0, arpLimit;
    float arpMult, duty, dutySlide;
    static int32_t arpLimitOf(float speed) {
        if (speed >= 1.0f) return 0;
        float k = 1.0f - speed;
        return netI32(k * k * 20000.0f + 32.0f);
    }
    explicit Frequency(const XSoundParams& q)
        : p(q), freq(q.frequency), minFreq(q.freqLimit), ramp(q.freqRamp), dramp(q.freqDramp),
          vibSpeed(q.vibratoSpeed * q.vibratoSpeed * 0.01f), vibAmp(q.vibratoDepth * 0.5f),
          arpLimit(arpLimitOf(q.arpeggioSpeed)), duty(q.dutyCycle), dutySlide(-q.dutyCycleRamp * 0.00005f) {
        float m2 = q.arpeggioMod * q.arpeggioMod;
        arpMult = q.arpeggioMod >= 0.0f ? 1.0f - m2 * 0.9f : 1.0f + m2 * 10.0f;
    }
    float get() {
        freq += ramp;
        ramp += dramp;
        if (freq < minFreq) freq = minFreq;
        if (arpLimit != 0 && ++arpTime >= arpLimit) { arpTime = 0; freq *= arpMult; }
        vibPhase += vibSpeed;
        return freq * (1.0f + float(std::sin(double(vibPhase))) * vibAmp);
    }
    float dutyCycle() {
        duty += dutySlide;
        if (duty < 0.0f) duty = 0.0f;
        if (duty > 1.0f) duty = 1.0f;
        return duty;
    }
    void reset() {
        freq = p.frequency; ramp = p.freqRamp; dramp = p.freqDramp; vibPhase = 0.0f;
        arpTime = 0; arpLimit = arpLimitOf(p.arpeggioSpeed);
        duty = 0.5f - p.dutyCycle * 0.5f;   // not p.dutyCycle: reference quirk
        dutySlide = -p.dutyCycleRamp * 0.00005f;
    }
};

struct Filter {
    float fltp = 0, fltdp = 0, fltphp = 0, fltw, fltw_d, fltdmp, flthp, flthp_d;
    bool lowPass;
    explicit Filter(const XSoundParams& p) {
        fltw = float(std::pow(double(p.lpfFreq), 3.0)) * 0.1f;
        lowPass = p.lpfFreq != 1.0f;
        fltw_d = 1.0f + p.lpfRamp * 0.0001f;
        fltdmp = 5.0f / (1.0f + float(std::pow(double(p.lpfResonance), 2.0)) * 20.0f) * (0.01f + fltw);
        if (fltdmp > 0.8f) fltdmp = 0.8f;
        flthp = float(std::pow(double(p.hpfFreq), 2.0)) * 0.1f;
        flthp_d = 1.0f + p.hpfRamp * 0.0003f;
    }
    float process(float s) {
        fltw *= fltw_d;
        if (fltw < 0.0f) fltw = 0.0f;
        if (fltw > 0.1f) fltw = 0.1f;
        float prev = fltp;
        if (lowPass) { fltdp += (s - fltp) * fltw; fltdp -= fltdp * fltdmp; }
        else { fltp = s; fltdp = 0.0f; }
        fltp += fltdp;
        flthp *= flthp_d;
        if (flthp < 0.0f) flthp = 0.0f;
        if (flthp > 0.1f) flthp = 0.1f;
        fltphp += fltp - prev;
        fltphp -= fltphp * flthp;
        return fltphp;
    }
};

// C#: (pos - offset + 1024) % 1024 in wrapping int arithmetic; negative -> IndexOutOfRangeException.
int32_t delayIndex(int pos, int32_t offset) { return int32_t(uint32_t(pos) - uint32_t(offset) + 1024u) % 1024; }

struct Flanger {
    bool on;
    float offset = 0, slide = 0, buf[1024]{};
    int pos = 0;
    explicit Flanger(const XSoundParams& p) : on(p.flangerOffset != 0.0f || p.flangerRamp != 0.0f) {
        if (!on) return;
        offset = p.flangerOffset * p.flangerOffset * 1020.0f;
        if (p.flangerOffset < 0.0f) offset = -offset;
        slide = p.flangerRamp * p.flangerRamp;
        if (p.flangerRamp < 0.0f) slide = -slide;
    }
    bool process(float& s) {
        if (!on) return true;
        offset += slide;
        int32_t o = netI32(std::fabs(offset));
        if (o > 1023) o = 1023;
        int32_t i = delayIndex(pos, o);
        if (i < 0) return false;
        float out = s + buf[i];
        buf[pos] = out;
        pos = (pos + 1) % 1024;
        s = out;
        return true;
    }
};

struct Phaser {
    const XSoundParams& p;
    bool on;
    float offset = 0, slide = 0, buf[1024]{};
    int pos = 0;
    explicit Phaser(const XSoundParams& q) : p(q), on(q.phaserOffset != 0.0f || q.phaserSweep != 0.0f) { reset(); }
    void reset() {
        if (!on) return;
        offset = std::pow(p.phaserOffset, 2.0f) * 1024 * (p.phaserOffset < 0.0f ? -1 : 1);   // MathF.Pow
        slide = std::pow(p.phaserSweep, 2.0f) * (p.phaserSweep < 0.0f ? -1 : 1);
        pos = 0;
        std::fill(buf, buf + 1024, 0.0f);
    }
    bool process(float& s) {
        if (!on) return true;
        offset += slide;
        int32_t o = netI32(std::fabs(offset));
        if (o > 1023) o = 1023;   // Math.Min
        buf[pos] = s;
        int32_t i = delayIndex(pos, o);
        if (i < 0) return false;
        float delayed = buf[i];
        pos = (pos + 1) % 1024;
        s = s + delayed;
        return true;
    }
};

struct Repeat {
    int32_t time = 0, limit;
    explicit Repeat(const XSoundParams& p)
        : limit(p.repeatSpeed == 0.0f ? 0 : netI32(std::pow(double(1.0f - p.repeatSpeed), 2.0) * 20000 + 32)) {}
    bool check() {
        if (limit == 0) return false;
        if (++time >= limit) { time = 0; return true; }
        return false;
    }
};

} // namespace

std::string xsoundGenerate(const XSoundParams& p, NetRandom& noise, std::vector<int16_t>& pcm) {
    constexpr int OVERSAMPLING = 8;
    Wave wave{noise};
    Frequency freq(p);
    Envelope env(p);
    Filter filter(p);
    auto flanger = std::make_unique<Flanger>(p);
    Repeat repeat(p);
    auto phaser = std::make_unique<Phaser>(p);
    std::vector<float> data;

    for (int i = 0;; i++) {
        if (repeat.check()) { freq.reset(); phaser->reset(); }
        float e = env.get();
        if (e <= 0.0f) break;
        // The envelope clock is a float: once it stops advancing (2^24 samples) nothing ever changes.
        if (i > 1 << 24) return "the sound never ends (the reference generates it forever)";
        float f = freq.get(), duty = freq.dutyCycle(), sample = 0.0f;
        for (int si = 0; si < OVERSAMPLING; si++) {
            float t = float(i * OVERSAMPLING + si) / (p.sampleRate * OVERSAMPLING);
            float s = filter.process(wave.generate(p, t, f, duty));
            if (!flanger->process(s) || !phaser->process(s)) return "Index was outside the bounds of the array";
            sample += s * e;
        }
        data.push_back(sample / OVERSAMPLING);
    }

    if (data.empty()) return "Sequence contains no elements";
    size_t k = 0;
    float peak = std::fabs(data[0]);
    while (std::isnan(peak) && ++k < data.size()) peak = std::fabs(data[k]);
    for (; k < data.size(); k++) if (float a = std::fabs(data[k]); a > peak) peak = a;
    pcm.resize(data.size());
    for (size_t j = 0; j < data.size(); j++)
        pcm[j] = int16_t(netI32(data[j] / peak * p.soundVolume * 32767));   // (short) wraps
    return "";
}

std::string readSoundParams(const Machine& m, uint32_t a, XSoundParams& p) {
    uint16_t flags = uint16_t(m.rd<16>(a)); a += 2;
    auto f = [&] { float v = m.rdF(a); a += 4; return v; };
    auto b = [&] { return uint8_t(m.rd<8>(a++)); };
    auto on = [&](int bit) { return (flags >> bit & 1) != 0; };
    auto bad = [](uint8_t t) { return "Invalid wave type value: " + std::to_string(t); };
    p.frequency = f(); p.envelopeSustain = f(); p.soundVolume = f();
    if (on(15)) {
        p.waveType = b();
        if (!validWave(p.waveType)) return bad(p.waveType);
        if (p.waveType == 100)
            for (int n = b(); n > 0; n--) {
                uint8_t t = b(), w = uint8_t(b() % 100);
                if (!validWave(t)) return bad(t);
                for (auto& e : p.waveTypeValues)
                    if (e.first == t) return "An item with the same key has already been added. Key: " + std::to_string(t);
                p.waveTypeValues.emplace_back(t, w);
            }
    }
    if (on(14)) { p.envelopeAttack = f(); p.envelopeDecay = f(); }
    if (on(13)) p.envelopePunch = f();
    if (on(12)) { p.freqLimit = f(); p.freqRamp = f(); p.freqDramp = f(); }
    if (on(11)) { p.vibratoDepth = f(); p.vibratoSpeed = f(); }
    if (on(10)) { p.arpeggioMod = f(); p.arpeggioSpeed = f(); }
    if (on(9)) { p.dutyCycle = f(); p.dutyCycleRamp = f(); }
    if (on(8)) p.repeatSpeed = f();
    if (on(7)) { p.flangerOffset = f(); p.flangerRamp = f(); }
    if (on(6)) { p.phaserOffset = f(); p.phaserSweep = f(); }
    if (on(5)) { p.lpfFreq = f(); p.lpfRamp = f(); p.lpfResonance = f(); }
    if (on(4)) { p.hpfFreq = f(); p.hpfRamp = f(); }
    if (on(3)) p.bitDepth = b();
    if (on(2)) p.sampleRate = f();
    return "";
}

} // namespace c93
