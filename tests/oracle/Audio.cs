// XSound vectors: sound definitions in the PLAY operand format, parsed by the reference
// (XSoundParamsProcessing) and rendered by its generator (XSoundGenerator: samples + PCMConvertor).
// No audio device is involved. tests/conformance.cpp --xsound renders the same and compares.
//
//   dotnet run -c Release -- --xsound <out.bin>
//
// Output per case: a text line "S <name> <definition hex> <noise seed> <samples>" followed by that
// many little-endian int16 samples, or "X <name> <definition hex> <seed> <parse|gen> <message>".
// Noise: the generator's static unseeded Random gets a seeded implementation per case (reflection),
// so NOISE/PINKNOISE/GAUSSIANNOISE/LFO are reproducible.
using System.Reflection;
using System.Text;
using Continuum93.Emulator;
using Continuum93.Emulator.Audio.XSound;
using Continuum93.Emulator.Audio.XSound.ExecutionUtilities;
using Continuum93.Emulator.Audio.XSound.Generators;

static class Audio
{
    // XSoundFlags bits
    const int WT = 1 << 15, ENV = 1 << 14, PUNCH = 1 << 13, FREQ = 1 << 12, VIB = 1 << 11, ARP = 1 << 10, DUTY = 1 << 9,
              REP = 1 << 8, FLA = 1 << 7, PHA = 1 << 6, LPF = 1 << 5, HPF = 1 << 4, BITS = 1 << 3, RATE = 1 << 2;

    // flags, frequency, sustain, volume, then the optional fields in reader order: float -> 4 bytes BE, int -> 1 byte.
    static byte[] D(int flags, params object[] v)
    {
        var b = new List<byte> { (byte)(flags >> 8), (byte)flags };
        foreach (var x in v)
            if (x is int i) b.Add((byte)i);
            else { var f = BitConverter.GetBytes(Convert.ToSingle(x)); Array.Reverse(f); b.AddRange(f); }
        return b.ToArray();
    }

    static IEnumerable<(string, byte[])> Cases()
    {
        int[] waves = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 50, 51, 52];
        foreach (int w in waves)
        {
            yield return ($"wave{w}", D(WT | ENV | DUTY, 440f, 0.1f, 1f, w, 0.01f, 0.1f, 0.3f, 0.2f));
            // ramps, vibrato, arpeggio, duty sweep, 4-bit, 22050 Hz
            yield return ($"wave{w}-mod", D(WT | ENV | FREQ | VIB | ARP | DUTY | BITS | RATE, 1234.5f, 0.15f, 0.8f, w,
                0.02f, 0.05f, 100f, -0.02f, 0.000001f, 0.4f, 0.5f, 0.3f, 0.7f, 0.25f, -0.6f, 4, 22050f));
            yield return ($"wave{w}-fx", D(WT | PUNCH | REP | FLA | PHA | LPF | HPF, 220f, 0.2f, 0.6f, w,
                0.5f, 0.6f, 0.3f, 0.05f, -0.4f, 0.1f, 0.4f, 0.2f, 0.7f, 0.2f, 0.1f, -0.5f));
        }
        // envelopes
        yield return ("env-attack", D(ENV, 440f, 0f, 1f, 0.2f, 0f));
        yield return ("env-decay", D(ENV, 440f, 0f, 1f, 0f, 0.2f));
        yield return ("env-punch", D(ENV | PUNCH, 300f, 0.1f, 1f, 0.05f, 0.1f, 1.5f));
        yield return ("env-negpunch", D(ENV | PUNCH, 300f, 0.1f, 1f, 0.05f, 0.1f, -0.7f));
        // filters, flanger, phaser, repeat (repeat resets duty to 0.5 - duty/2)
        yield return ("lpf", D(WT | LPF, 880f, 0.2f, 1f, 7, 0.3f, 0.1f, 0.8f));
        yield return ("lpf-down", D(WT | LPF, 880f, 0.2f, 1f, 3, 0.9f, -0.5f, 0.2f));
        yield return ("hpf", D(WT | HPF, 880f, 0.2f, 1f, 7, 0.2f, 0.5f));
        yield return ("hpf-down", D(WT | HPF, 880f, 0.2f, 1f, 7, 0.6f, -0.3f));
        yield return ("flanger", D(WT | FLA, 500f, 0.2f, 1f, 7, 0.3f, 0.1f));
        yield return ("flanger-neg", D(WT | FLA, 500f, 0.2f, 1f, 3, -0.2f, -0.05f));
        yield return ("phaser", D(WT | PHA, 500f, 0.2f, 1f, 7, 0.5f, 0.9f));
        yield return ("phaser-neg", D(WT | PHA, 500f, 0.2f, 1f, 7, -0.3f, 0.2f));
        yield return ("repeat", D(WT | REP | DUTY, 400f, 0.3f, 1f, 4, 0.5f, 0.2f, 0.5f));
        yield return ("repeat-fast", D(WT | REP | FREQ, 400f, 0.3f, 1f, 7, 0.9f, 50f, 0.01f, 0f));
        yield return ("freq-limit", D(WT | FREQ, 1000f, 0.3f, 1f, 0, 300f, -0.05f, -0.000001f));
        yield return ("arp-neg", D(WT | ARP, 600f, 0.3f, 1f, 3, -0.5f, 0.8f));
        // COMPLEX: weights are % 100; nested COMPLEX and unknown-in-switch contribute 0; empty list
        yield return ("complex", D(WT, 440f, 0.2f, 1f, 100, 3, 1, 10, 2, 20, 3, 30));
        yield return ("complex-noise", D(WT | DUTY, 330f, 0.2f, 1f, 100, 4, 50, 150, 0, 40, 52, 30, 17, 25, 0.4f, 0.1f));
        yield return ("complex-nested", D(WT, 440f, 0.1f, 1f, 100, 2, 100, 50, 7, 99));
        yield return ("complex-empty", D(WT, 440f, 0.1f, 1f, 100, 0));
        // the boot sounds (APUThreadWrapper.PlayInitializationSound(s)), as definitions
        yield return ("boot-sound1", D(WT | ENV | REP | PHA, 700f, 0.05f, 0.1f, 3, 0f, 0.05f, 0.5f, 0.5f, 0.9f));
        yield return ("boot-sound2", D(WT | ENV | REP | PHA, 523.3f, 0.2f, 0.6f, 7, 0.1f, 0.5f, 0.4f, 0.4f, 0.1f));
        yield return ("boot-single", D(WT | ENV | REP | PHA, 440f, 0.3f, 0.5f, 3, 0f, 0.4f, 0.5f, 0.5f, 0.2f));
        // the definitions guest programs pass in the reference's tests (AudioTests/PlayTests.cs, TestEXEC_PLAY.cs)
        yield return ("exec-nnn", D(0, 440f, 200f, 1f));
        yield return ("exec-rrr", D(0, 420f, 150f, 0.8f));
        yield return ("play-revsaw", D(WT, 440f, 200f, 1f, 8));
        yield return ("play-complex", D(WT, 440f, 200f, 1f, 100, 3, 1, 10, 2, 20, 3, 30));
        yield return ("play-env", D(ENV, 440f, 200f, 1f, 0.5f, 0.8f));
        yield return ("play-gauss", D(WT | ENV, 440f, 200f, 1f, 52, 0.5f, 0.8f));
        yield return ("play-punch", D(PUNCH, 440f, 200f, 1f, 0.7f));
        yield return ("play-freq", D(FREQ, 440f, 200f, 1f, 0.1f, 0.2f, 0.3f));
        yield return ("play-vib", D(VIB, 440f, 200f, 1f, 0.35f, 0.45f));
        yield return ("play-arp", D(ARP, 440f, 200f, 1f, 0.11f, 0.22f));
        // edges: wrap-around volume, negative volume, bit depths, rates, odd frequencies, NaN, failures
        yield return ("vol-wrap", D(WT, 440f, 0.1f, 1.5f, 0));
        yield return ("vol-neg", D(WT, 440f, 0.1f, -0.5f, 7));
        foreach (int bd in new[] { 0, 1, 2, 8, 24, 30, 31, 32, 255 })
            yield return ($"bits{bd}", D(WT | BITS, 440f, 0.05f, 1f, 0, bd));
        foreach (float r in new[] { 8000f, 11025f, 48000f, 96000f, 7999.5f })
            yield return ($"rate{r}", D(WT | RATE, 440f, 0.05f, 1f, 7, r));
        yield return ("freq-zero", D(WT, 0f, 0.05f, 1f, 3));
        yield return ("freq-neg", D(WT, -440f, 0.05f, 1f, 7));
        yield return ("freq-nan", D(WT, float.NaN, 0.05f, 1f, 0));
        yield return ("freq-high", D(WT, 30000f, 0.05f, 1f, 9));
        yield return ("arp-nan", D(WT | ARP, 440f, 0.05f, 1f, 0, 0.5f, float.NaN));
        yield return ("repeat-big", D(WT | REP, 440f, 0.05f, 1f, 0, -1e10f));
        yield return ("sustain-zero", D(0, 440f, 0f, 1f));
        yield return ("sustain-neg", D(WT, 440f, -1f, 1f, 3));
        yield return ("flanger-huge", D(WT | FLA, 440f, 0.05f, 1f, 0, 0.1f, 1e5f));
        yield return ("phaser-huge", D(WT | PHA, 440f, 0.05f, 1f, 0, 0.1f, 1e5f));
        yield return ("bad-wave", D(WT, 440f, 0.1f, 1f, 22));
        yield return ("bad-complex", D(WT, 440f, 0.1f, 1f, 100, 2, 0, 10, 60, 10));
        yield return ("dup-complex", D(WT, 440f, 0.1f, 1f, 100, 2, 3, 10, 3, 20));
    }

    public static int Run(string outPath)
    {
        var c = new Computer();
        var rnd = (Random)typeof(XWaveTypeGenerator).GetField("random", BindingFlags.NonPublic | BindingFlags.Static)!.GetValue(null)!;
        var impl = typeof(Random).GetField("_impl", BindingFlags.NonPublic | BindingFlags.Instance)!;
        using var w = new BinaryWriter(File.Create(outPath));
        int n = 0, seed = 1000;
        long total = 0;
        foreach (var (name, def) in Cases())
        {
            seed++;
            const uint At = 0x100000;
            c.LoadMemAt(At, def);
            c.LoadMemAt(At + (uint)def.Length, new byte[16]);
            string head = $"{name} {Convert.ToHexString(def)} {seed}";
            XSoundParams p;
            try { p = XSoundParamsProcessing.ReadSoundParams(At, c); }
            catch (Exception e) { w.Write(Encoding.ASCII.GetBytes($"X {head} parse {e.Message}\n")); n++; continue; }
            impl.SetValue(rnd, impl.GetValue(new Random(seed)));
            byte[] pcm;
            try { pcm = new XSoundGenerator(p).GenerateSound(); }
            catch (Exception e) { w.Write(Encoding.ASCII.GetBytes($"X {head} gen {e.Message}\n")); n++; continue; }
            w.Write(Encoding.ASCII.GetBytes($"S {head} {pcm.Length / 2}\n"));
            w.Write(pcm);
            total += pcm.Length / 2;
            n++;
        }
        Console.WriteLine($"xsound: {n} sounds, {total} samples -> {outPath}");
        return 0;
    }
}
