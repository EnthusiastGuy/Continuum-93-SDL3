// The APU: Emulator/Audio/APUThreadWrapper.cs, APU.cs, XSound/XSoundPlayer.cs, Wav/WavStreamingPlayer.cs,
// Ogg/OggStreamingPlayer.cs. Callers queue commands; the APU thread drains the queue every 10 ms (as
// APUThreadLoop), generates XSound sounds and hands everything to one SDL playback device: each sound
// or stream is an SDL_AudioStream bound to it, and SDL mixes them. Without C93_AUDIO_OUT (the
// conformance runner) or without a device, commands are accepted and dropped.
// Reproduced quirks:
//  - PLAY queues "play the last PLAYed sound" (the C# lambda read the lastPlayedSound field when it ran):
//    PLAYs that pile up before the APU thread gets to them all play the newest definition.
//  - The APU thread dies where the reference's threw (the machine runs on; sounds already playing finish):
//    a generator failure, a sample rate outside 8000..48000 (MonoGame SoundEffect), a 257th sound playing
//    at once (MonoGame's OpenAL pool), the first sound with no audio device (here: nothing starts at all).
//  - Mono is panned centre at -3 dB per side, stereo plays as is (OpenAL Soft's pan law, which MonoGame used).
//  - WAV: the reference's minimal RIFF parser (16-bit assumed, >2 channels play as mono, one non-"data"
//    chunk skipped, everything after the data start up to the end of the file is audio). Streams refill
//    in 250 ms chunks while fewer than two are queued; at the end of an unlooped stream Stop() drops what
//    is still queued, so the last 250-500 ms are never heard. Volume is clamped to 0..1.
// Not reproduced (DECISIONS D-21): the reference's Ogg path decoded the header packets as audio, assumed
// mono and could not play a real file; the port decodes Ogg Opus at 48 kHz properly (channels, pre-skip, gain).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <random>
#include <thread>

#include "audio.h"
#include "log.h"
#include "netrandom.h"
#if C93_AUDIO_OUT
#include <SDL3/SDL.h>
#include <ogg/ogg.h>
#include <opus.h>
#endif

namespace c93 {
namespace {

struct Cmd {
    enum Op : uint8_t { PlayLast, PlaySound, Register, Play, Pause, Stop, Volume, Loop } op;
    Stream stream = Stream::Wav;
    uint8_t ch = 0;
    float value = 0;
    std::string path;
    std::shared_ptr<const XSoundParams> sound;
};

std::mutex gLock;   // guards gQueue and gLast, held only to push or pop
std::deque<Cmd> gQueue;
std::shared_ptr<const XSoundParams> gLast;
std::atomic<bool> gRunning{false};
std::thread gThread;

void push(Cmd c) {
    if (!gRunning.load(std::memory_order_relaxed)) return;
    std::lock_guard g(gLock);
    gQueue.push_back(std::move(c));
}

#if C93_AUDIO_OUT
SDL_AudioDeviceID gDev = 0;
constexpr float kCentre = 0.70710678f;
NetRandom gNoise{int32_t(std::random_device{}() & 0x7FFFFFFF)};   // XWaveTypeGenerator's unseeded Random
std::vector<SDL_AudioStream*> gVoices;                             // XSound sounds still playing

void reapVoices() {
    std::erase_if(gVoices, [](SDL_AudioStream* s) {
        if (SDL_GetAudioStreamQueued(s) > 0 || SDL_GetAudioStreamAvailable(s) > 0) return false;
        SDL_DestroyAudioStream(s);
        return true;
    });
}

// XSoundPlayer: generate, then play. Returns why the APU thread dies, or "".
std::string playSound(const XSoundParams& p) {
    float r = p.sampleRate;
    if (!(r >= 8000 && r < 48001)) return "sound sample rate " + std::to_string(r) + " Hz is outside 8000..48000 (MonoGame)";
    std::vector<int16_t> pcm;
    if (std::string why = xsoundGenerate(p, gNoise, pcm); !why.empty()) return "sound generator failed (" + why + ")";
    reapVoices();
    if (gVoices.size() >= 256) return "a 257th sound started while 256 play (MonoGame InstancePlayLimitException)";
    SDL_AudioSpec spec{SDL_AUDIO_S16, 1, int(r)};
    SDL_AudioStream* s = SDL_CreateAudioStream(&spec, nullptr);
    if (!s) { LOGW("Audio: cannot create a sound stream (%s); sound dropped", SDL_GetError()); return ""; }
    SDL_SetAudioStreamGain(s, kCentre);
    SDL_BindAudioStream(gDev, s);   // first: binding gives the stream its output format, needed to put data
    SDL_PutAudioStreamData(s, pcm.data(), int(pcm.size() * 2));
    SDL_FlushAudioStream(s);
    gVoices.push_back(s);
    return "";
}

// --- WAV / Ogg streams ----------------------------------------------------------------------------
struct Channel {
    bool ogg = false, valid = false, playing = false, paused = false, stopped = false, looped = false;
    SDL_AudioStream* s = nullptr;
    std::vector<uint8_t> file;
    size_t pos = 0, dataStart = 0, chunk = 0;   // chunk: 250 ms of output bytes
    int rate = 48000, channels = 1;
    float volume = 1;
    // Ogg Opus decoder state
    ogg_sync_state oy{};
    ogg_stream_state os{};
    bool oyInit = false, osInit = false;
    OpusDecoder* dec = nullptr;
    int skip = 0;                 // pre-skip samples per channel still to drop
    std::vector<int16_t> pcm;     // decoded, not yet queued
    size_t pcmPos = 0;

    void oggClose() {
        if (osInit) ogg_stream_clear(&os);
        if (oyInit) ogg_sync_clear(&oy);
        if (dec) opus_decoder_destroy(dec);
        osInit = oyInit = false;
        dec = nullptr;
    }
    ~Channel() { if (s) SDL_DestroyAudioStream(s); oggClose(); }

    bool nextPacket(ogg_packet& op) {
        for (;;) {
            if (osInit) {
                int r = ogg_stream_packetout(&os, &op);
                if (r == 1) return true;
                if (r < 0) continue;   // a gap in the stream: take the next packet
            }
            ogg_page og;
            while (ogg_sync_pageout(&oy, &og) != 1) {
                if (pos >= file.size()) return false;
                size_t n = std::min<size_t>(4096, file.size() - pos);
                std::memcpy(ogg_sync_buffer(&oy, long(n)), &file[pos], n);
                ogg_sync_wrote(&oy, long(n));
                pos += n;
            }
            if (!osInit) { ogg_stream_init(&os, ogg_page_serialno(&og)); osInit = true; }
            ogg_stream_pagein(&os, &og);   // libogg refuses pages of other logical streams
        }
    }
    // (Re)starts decoding at the file start: OpusHead, OpusTags, then audio.
    bool oggOpen() {
        oggClose();
        ogg_sync_init(&oy);
        oyInit = true;
        pos = 0;
        pcm.clear();
        pcmPos = 0;
        ogg_packet op;
        if (!nextPacket(op) || op.bytes < 19 || std::memcmp(op.packet, "OpusHead", 8) != 0) return false;
        channels = op.packet[9];
        skip = op.packet[10] | op.packet[11] << 8;
        int gain = int16_t(op.packet[16] | op.packet[17] << 8);
        if (channels < 1 || channels > 2) return false;   // shortcut: surround needs opus_multistream, add it if a file needs it
        if (!nextPacket(op)) return false;                 // OpusTags
        int err = 0;
        dec = opus_decoder_create(48000, channels, &err);
        if (!dec) return false;
        opus_decoder_ctl(dec, OPUS_SET_GAIN(gain));
        return true;
    }
    // Ogg: decoded 16-bit PCM bytes; WAV: file bytes. 0 at the end.
    size_t read(uint8_t* dst, size_t want) {
        if (!ogg) {
            size_t n = std::min(want, file.size() - pos);
            std::memcpy(dst, file.data() + pos, n);
            pos += n;
            return n;
        }
        while (pcmPos == pcm.size()) {
            ogg_packet op;
            if (!dec || !nextPacket(op)) return 0;
            pcm.resize(5760 * size_t(channels));   // 120 ms, the longest Opus packet
            int n = opus_decode(dec, op.packet, opus_int32(op.bytes), pcm.data(), 5760, 0);
            pcm.resize(size_t(std::max(n, 0)) * channels);   // a broken packet decodes to nothing
            int drop = std::min(skip, std::max(n, 0));
            skip -= drop;
            pcmPos = size_t(drop) * channels;
        }
        size_t n = std::min(want / 2, pcm.size() - pcmPos);
        std::memcpy(dst, pcm.data() + pcmPos, n * 2);
        pcmPos += n;
        return n * 2;
    }
    void rewind() { if (ogg) oggOpen(); else pos = dataStart; }
    void gain() { SDL_SetAudioStreamGain(s, volume * (channels == 2 ? 1.0f : kCentre)); }

    // Play: start, restart after Stop, or resume. Returns whether to bind the stream to the device
    // (done by the caller outside the stream lock: binding takes the device lock).
    bool play() {
        if (!valid || (playing && !paused)) return false;
        if (!playing && stopped) { rewind(); stopped = false; }
        paused = false;
        playing = true;
        return true;
    }
    bool pause() {   // returns whether to unbind (which keeps what is queued)
        if (!valid || paused || !playing) return false;
        paused = true;
        return true;
    }
    void stop() {
        if (!valid || (stopped && !playing)) return;
        stopped = true;
        paused = playing = false;
        SDL_ClearAudioStream(s);
        rewind();
    }
    // DynamicSound_BufferNeeded, run by SDL whenever the device pulls from this stream.
    std::vector<uint8_t> buf;   // one chunk
    void refill() {
        while (playing && !paused && !stopped && (size_t(SDL_GetAudioStreamQueued(s)) + chunk - 1) / chunk < 2) {
            size_t got = 0;
            bool rewound = false;
            while (got < chunk) {
                size_t n = read(buf.data() + got, chunk - got);
                if (n == 0) {
                    if (looped && !rewound) { rewind(); rewound = true; continue; }   // an empty file stops (the reference hung)
                    stop();
                    return;
                }
                rewound = false;
                got += n;
            }
            SDL_PutAudioStreamData(s, buf.data(), int(got));
        }
    }
};
std::unique_ptr<Channel> gCh[2][256];

void SDLCALL refillCallback(void* c, SDL_AudioStream*, int, int) { static_cast<Channel*>(c)->refill(); }

// WavStreamingPlayer.ParseWaveFileHeader: little-endian fields, failures make the channel invalid.
bool parseWav(Channel& c) {
    const std::vector<uint8_t>& f = c.file;
    size_t p = 0;
    bool ok = true;
    auto take = [&](size_t n) { if (p + n > f.size()) { ok = false; return size_t(0); } size_t at = p; p += n; return at; };
    auto u32 = [&] { size_t a = take(4); return ok ? uint32_t(f[a] | f[a + 1] << 8 | f[a + 2] << 16 | uint32_t(f[a + 3]) << 24) : 0u; };
    auto u16 = [&] { size_t a = take(2); return ok ? uint16_t(f[a] | f[a + 1] << 8) : uint16_t(0); };
    auto id = [&](const char* s) { size_t a = take(4); return ok && std::memcmp(&f[a], s, 4) == 0; };
    auto skip = [&](int32_t n) { if (n < 0) ok = false; else p = std::min(f.size(), p + size_t(n)); };   // BinaryReader.ReadBytes
    if (!id("RIFF")) return false;
    u32();
    if (!id("WAVE") || !id("fmt ")) return false;
    int32_t fmtSize = int32_t(u32());
    if (u16() != 1) return false;   // PCM only
    c.channels = int16_t(u16());
    c.rate = int32_t(u32());
    u32(); u16();
    int bits = int16_t(u16());
    if (fmtSize > 16) skip(fmtSize - 16);
    if (!ok) return false;
    if (!id("data")) {
        if (!ok) return false;
        skip(int32_t(u32()));
        if (!id("data")) return false;
    }
    u32();   // the data size is ignored: the reference streams to the end of the file
    c.dataStart = c.pos = p;
    int frame = c.channels * (bits / 8);
    c.channels = c.channels == 2 ? 2 : 1;
    // shortcut: frames < 1 byte, and chunks that are not whole output frames (MonoGame threw), are refused
    c.chunk = size_t(c.rate) * size_t(std::max(frame, 0)) * 250 / 1000 / (2 * c.channels) * (2 * c.channels);
    return ok && frame > 0 && c.chunk > 0 && c.rate >= 8000 && c.rate <= 48000;   // DynamicSoundEffectInstance range
}

void registerStream(Stream kind, const std::string& path, uint8_t ch) {
    auto& slot = gCh[int(kind)][ch];
    slot.reset();
    auto c = std::make_unique<Channel>();
    c->ogg = kind == Stream::Ogg;
    size_t size = 0;
    if (void* data = SDL_LoadFile(path.c_str(), &size)) {   // shortcut: whole file in memory, stream from disk if files get big
        c->file.assign(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
        SDL_free(data);
        c->valid = c->ogg ? c->oggOpen() : parseWav(*c);
    }
    if (c->valid && c->ogg) c->chunk = size_t(48000) * c->channels * 2 * 250 / 1000;
    if (c->valid) {
        SDL_AudioSpec spec{SDL_AUDIO_S16, c->channels, c->ogg ? 48000 : c->rate};
        c->s = SDL_CreateAudioStream(&spec, nullptr);
        c->valid = c->s != nullptr;
    }
    if (c->valid) {
        c->buf.resize(c->chunk);
        SDL_SetAudioStreamGetCallback(c->s, refillCallback, c.get());
        c->gain();
    } else LOGW("Audio: %s is not a playable %s file; channel %d stays silent", path.c_str(), c->ogg ? "Ogg Opus" : "PCM WAV", ch);
    slot = std::move(c);
}

// Runs one command on the APU thread. Returns why the APU thread dies, or "".
std::string run(const Cmd& c, const std::shared_ptr<const XSoundParams>& last) {
    if (c.op == Cmd::PlayLast) return playSound(*last);
    if (c.op == Cmd::PlaySound) return playSound(*c.sound);
    if (c.op == Cmd::Register) { registerStream(c.stream, c.path, c.ch); return ""; }
    Channel* ch = gCh[int(c.stream)][c.ch].get();
    if (!ch) return "";
    if (c.op == Cmd::Loop) { ch->looped = c.value != 0; return ""; }
    if (!ch->valid) return "";
    SDL_LockAudioStream(ch->s);   // the device thread refills under this lock
    bool bind = false, unbind = false;
    switch (c.op) {
    case Cmd::Play: bind = ch->play(); break;
    case Cmd::Pause: unbind = ch->pause(); break;
    case Cmd::Stop: ch->stop(); break;
    case Cmd::Volume: ch->volume = c.value > 0 ? std::min(c.value, 1.0f) : 0.0f; ch->gain(); break;   // Math.Clamp; NaN -> 0
    default: break;
    }
    SDL_UnlockAudioStream(ch->s);
    if (bind) SDL_BindAudioStream(gDev, ch->s);
    if (unbind) SDL_UnbindAudioStream(ch->s);
    return "";
}

void apuLoop() {
    bool dead = false;
    while (gRunning.load()) {
        for (;;) {
            Cmd c;
            std::shared_ptr<const XSoundParams> last;
            {
                std::lock_guard g(gLock);
                if (gQueue.empty()) break;
                c = std::move(gQueue.front());
                gQueue.pop_front();
                last = gLast;
            }
            if (dead) continue;   // the reference's APU task had ended: nothing is heard any more
            if (std::string why = run(c, last); !why.empty()) {
                dead = true;
                LOGE("Audio: %s; the APU stopped, as in the reference (no new sounds until restart)", why.c_str());
            }
        }
        reapVoices();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
#endif

} // namespace

void audioInit() {
#if C93_AUDIO_OUT
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { LOGW("Audio: no output device (%s); running silent", SDL_GetError()); return; }
    SDL_AudioSpec want{SDL_AUDIO_F32, 2, 48000};
    gDev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want);
    if (!gDev) { LOGW("Audio: no output device (%s); running silent", SDL_GetError()); return; }
    SDL_AudioSpec have{};
    int frames = 0;
    SDL_GetAudioDeviceFormat(gDev, &have, &frames);
    const char* name = SDL_GetAudioDeviceName(gDev);
    LOGI("Audio: device '%s' opened, %d Hz, %d ch", name ? name : "default", have.freq, have.channels);
    gRunning = true;
    gThread = std::thread(apuLoop);
#endif
}

void audioShutdown() {
#if C93_AUDIO_OUT
    if (!gRunning.exchange(false)) return;
    gThread.join();
    for (auto& set : gCh) for (auto& c : set) c.reset();
    for (SDL_AudioStream* s : gVoices) SDL_DestroyAudioStream(s);
    gVoices.clear();
    SDL_CloseAudioDevice(gDev);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
#endif
}

void audioPlay(XSoundParams p) {
    if (!gRunning.load(std::memory_order_relaxed)) return;
    auto s = std::make_shared<const XSoundParams>(std::move(p));
    std::lock_guard g(gLock);
    gLast = std::move(s);
    gQueue.push_back({Cmd::PlayLast});
}

// PlayInitializationSounds: sound1 only (sound2 is commented out in the reference).
void audioPlayInitializationSounds() {
    XSoundParams p;
    p.waveType = 3;   // SQUARE
    p.frequency = 700.0f;
    p.envelopeAttack = 0.0f;
    p.envelopeSustain = 0.05f;
    p.envelopeDecay = 0.05f;
    p.soundVolume = 0.1f;
    p.repeatSpeed = 0.5f;
    p.phaserOffset = 0.5f;
    p.phaserSweep = 0.9f;
    push({Cmd::PlaySound, Stream::Wav, 0, 0, {}, std::make_shared<const XSoundParams>(p)});
}

void audioRegister(Stream s, const std::string& path, uint8_t ch) { push({Cmd::Register, s, ch, 0, path}); }
void audioPlayStream(Stream s, uint8_t ch) { push({Cmd::Play, s, ch}); }
void audioPauseStream(Stream s, uint8_t ch) { push({Cmd::Pause, s, ch}); }
void audioStopStream(Stream s, uint8_t ch) { push({Cmd::Stop, s, ch}); }
void audioSetStreamVolume(Stream s, uint8_t ch, float v) { push({Cmd::Volume, s, ch, v}); }
void audioSetStreamLoop(Stream s, uint8_t ch, bool loop) { push({Cmd::Loop, s, ch, loop ? 1.0f : 0.0f}); }

} // namespace c93
