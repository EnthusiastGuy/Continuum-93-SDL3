// Audio: XSound synthesis (audio_xsound.cpp) and the APU that plays it (audio.cpp).
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace c93 {

struct Machine;
struct NetRandom;

// XSoundParams.cs, same defaults. waveType holds an XSoundParams.WaveTypes value.
struct XSoundParams {
    uint8_t waveType = 0;                                  // SINE
    std::vector<std::pair<uint8_t, int>> waveTypeValues;    // COMPLEX: (type, weight %), Dictionary insertion order
    float envelopeAttack = 0, envelopeSustain = 0, envelopePunch = 0, envelopeDecay = 0;
    float freqLimit = 0, freqRamp = 0, freqDramp = 0;
    float sampleRate = 44100;
    int bitDepth = 16;
    float frequency = 440, soundVolume = 0.5f;
    float dutyCycle = 0, dutyCycleRamp = 0;
    float lpfFreq = 1, lpfRamp = 0, lpfResonance = 0, hpfFreq = 0, hpfRamp = 0;
    float vibratoDepth = 0, vibratoSpeed = 0;
    float arpeggioMod = 0, arpeggioSpeed = 0;
    float repeatSpeed = 0;
    float flangerOffset = 0, flangerRamp = 0;
    float phaserOffset = 0, phaserSweep = 0;
};

// The sound definition PLAY points at (XSoundParamsProcessing.ReadSoundParams). Returns "" or the
// message of the exception the reference threw (invalid wave type, duplicate COMPLEX entry).
std::string readSoundParams(const Machine& m, uint32_t addr, XSoundParams& out);
// XSoundGenerator.GenerateSound + PCMConvertor: mono 16-bit PCM at (int)sampleRate. Returns "" or
// why the reference's APU thread died generating it (it threw, or would generate forever).
std::string xsoundGenerate(const XSoundParams& p, NetRandom& noise, std::vector<int16_t>& pcm);

// --- APU (APUThreadWrapper.cs). Callable from any thread; never blocks the caller. ----------------
// Until audioInit runs (the conformance runner never calls it) every command is accepted and dropped.
void audioInit();                       // app: SDL audio device + APU thread; no device -> silent
void audioShutdown();
void audioPlay(XSoundParams p);         // PLAY
void audioPlayInitializationSounds();   // the boot sound (Computer(initWithAudio: true))
// WAV / Ogg Opus streams on channels 0..255, one set per format, as the reference.
enum class Stream : uint8_t { Wav, Ogg };
void audioRegister(Stream s, const std::string& path, uint8_t ch);
void audioPlayStream(Stream s, uint8_t ch);
void audioPauseStream(Stream s, uint8_t ch);
void audioStopStream(Stream s, uint8_t ch);
void audioSetStreamVolume(Stream s, uint8_t ch, float v);
void audioSetStreamLoop(Stream s, uint8_t ch, bool loop);

} // namespace c93
