// Conformance oracle: for every instruction variant of the reference CPU, run N random
// single-instruction cases and write what the reference did. tests/conformance.cpp replays them.
//
//   dotnet run -c Release -- <out.txt> [cases per variant = 100] [seed = 93]
//   dotnet run -c Release -- --programs <dir> <out.txt>       (see Programs.cs)
//   dotnet run -c Release -- --optable <out.inc> | --asm <dir>... <out.txt>   (see Asm.cs)
//   dotnet run -c Release -- --basic <dir|file>... <out.txt>                   (see Basic.cs)
//   dotnet run -c Release -- --cc <dir|file>... <out.txt> | --ccruntime <out.inc>   (see Cc.cs)
//   dotnet run -c Release -- --disasm <out.txt> [cases] [seed]                     (see Disasm.cs)
//   dotnet run -c Release -- --xsound <out.bin>                                    (see Audio.cs)
//   dotnet run -c Release -- --asmmeta <out.inc>                                   (see AsmMeta.cs)
//   dotnet run -c Release -- --png2font <png> <out.font> [<png> <out.font>]...       (see Font.cs)
//   dotnet run -c Release -- --watch|--immediate <cases.txt> <out.txt>             (see Watch.cs)
//
// Encoding: each variant is assembled once by the reference assembler with placeholder
// operands; then its operand bit fields (letters in Mnem.OPS formats) are randomised.
// Register fields (5 bits) stay 0..25: the reference throws on 26..31.
//
// Output, one line per item (all numbers hex):
//   V <index> <name>
//   T <variant> <instr> <regs> <flags> <fregs> <spr> <spc> => <regs> <flags> <fregs> <ip> <spr> <spc> <bank> <fbank> <ram> <rs> <cs>
//   X <variant> <instr> ...   (the reference threw; replayer skips)
// Diff lists are "addr:value,..." against the fill patterns below, or "-" when empty.
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;
using Continuum93.Emulator;
using Continuum93.Emulator.Interpreter;
using Continuum93.Emulator.Mnemonics;
using Continuum93.Emulator.RAM;

static class Oracle
{
    public const uint IP = 0x080000;
    // Fill patterns; tests/conformance.cpp has identical copies.
    public static byte RamAt(uint a) => (byte)((a * 2654435761u) >> 24);
    public static byte RsAt(uint a) => (byte)((a * 2246822519u) >> 24);
    public static uint CsAt(uint a) => a * 2654435761u ^ 0x9E3779B9u;

    // Excluded: host-dependent or non-deterministic (interrupts, timing, random, sound, debugger).
    static readonly string[] Skip = ["INT", "WAIT", "RAND", "PLAY", "BREAK", "DEBUG"];

    static int Main(string[] args)
    {
        if (args.Length == 3 && args[0] == "--programs") return Programs.Run(args[1], args[2]);
        if (args.Length == 2 && args[0] == "--optable") return Asm.OpTable(args[1]);
        if (args.Length == 2 && args[0] == "--asmmeta") return AsmMeta.Run(args[1]);
        if (args.Length >= 3 && args[0] == "--asm") return Asm.Run(args[1..^1], args[^1]);
        if (args.Length >= 3 && args[0] == "--basic") return Basic.Run(args[1..^1], args[^1]);
        if (args.Length >= 3 && args[0] == "--cc") return Cc.Run(args[1..^1], args[^1]);
        if (args.Length == 2 && args[0] == "--ccruntime") return Cc.Runtime(args[1]);
        if (args.Length >= 2 && args[0] == "--disasm") return Disasm.Run(args[1], args.Length > 2 ? int.Parse(args[2]) : 20, args.Length > 3 ? int.Parse(args[3]) : 93);
        if (args.Length == 2 && args[0] == "--xsound") return Audio.Run(args[1]);
        if (args.Length == 2 && args[0] == "--xsound") return Audio.Run(args[1]);
        if (args.Length >= 3 && args.Length % 2 == 1 && args[0] == "--png2font") return Font.Run(args[1..]);
        if (args.Length == 3 && args[0] == "--watch") return Watch.RunWatch(args[1], args[2]);
        if (args.Length == 3 && args[0] == "--immediate") return Watch.RunImmediate(args[1], args[2]);
        string outPath = args.Length > 0 ? args[0] : "vectors.txt";
        int cases = args.Length > 1 ? int.Parse(args[1]) : 100;
        var rnd = new Random(args.Length > 2 ? int.Parse(args[2]) : 93);

        Continuum93.Emulator.Colors.Palettes.Initialize();   // the emulator does this at startup
        var c = new Computer();
        byte[] ram = c.MEMC.RAM.Data, rs = c.MEMC.RSRAM.Data;
        uint[] cs = (uint[])typeof(StackMemory).GetField("_data", BindingFlags.NonPublic | BindingFlags.Instance)!.GetValue(c.MEMC.CSRAM)!;
        byte[] ramP = new byte[ram.Length], rsP = new byte[rs.Length];
        uint[] csP = new uint[cs.Length];
        for (uint a = 0; a < ramP.Length; a++) ramP[a] = RamAt(a);
        for (uint a = 0; a < rsP.Length; a++) rsP[a] = RsAt(a);
        for (uint a = 0; a < csP.Length; a++) csP[a] = CsAt(a);
        ramP.CopyTo(ram, 0); rsP.CopyTo(rs, 0); csP.CopyTo(cs, 0);

        using var w = new StreamWriter(outPath) { NewLine = "\n" };
        var parents = Mnem.OPS.Values.Where(o => !o.IsPrimary && o.ParentCode.HasValue).Select(o => o.ParentCode.Value).ToHashSet();
        int vi = 0, made = 0, threw = 0, unassembled = 0;
        foreach (var (name, op) in Mnem.OPS.OrderBy(kv => kv.Key, StringComparer.Ordinal))
        {
            if (Skip.Contains(name.Split(' ')[0].TrimEnd('1', '2', '3', '6', '4'))) continue;
            if (op.IsPrimary && parents.Contains(op.OpCode)) continue;   // bare "ADD": its sub-ops are tested instead
            // LD with a byte count (",n") is a memory fill/copy; the reference writes through an unchecked
            // pointer and kills the process when random operands run past RAM. Hand-written cases instead.
            if (name.StartsWith("LD ") && (name.Contains(",n,") || name.EndsWith(",n"))) continue;
            byte[] canon = Assemble(name);
            string fmt = (op.Format ?? "").Replace(" ", "");
            if (canon == null || canon.Length != 1 + fmt.Length / 8) { unassembled++; Console.Error.WriteLine($"oracle: cannot encode '{name}'"); continue; }
            w.WriteLine($"V {vi:X} {name}");

            for (int k = 0; k < cases; k++)
            {
                byte[] ins = Randomise(canon, fmt, rnd);
                var regs = new byte[26]; rnd.NextBytes(regs);
                byte flags = (byte)rnd.Next(256);
                var fr = new float[16];
                for (int i = 0; i < 16; i++) fr[i] = rnd.Next(4) == 0 ? 0f : rnd.Next(-200000, 200000) / 64f;
                uint spr = (uint)rnd.Next(64, rs.Length - 64), spc = (uint)rnd.Next(64, cs.Length - 64);

                var regsC = c.CPU.REGS; var fl = c.CPU.FLAGS; var fregs = c.CPU.FREGS;
                regsC.SetRegisterBank(0); fregs.SetRegisterBank(0);
                for (byte i = 0; i < 26; i++) regsC.Set8BitRegister(i, regs[i]);
                for (byte i = 0; i < 8; i++) fl.SetValueByIndexFast(i, (flags >> i & 1) != 0);
                for (byte i = 0; i < 16; i++) fregs.SetRegister(i, fr[i]);
                regsC.SPR = spr; regsC.SPC = spc; regsC.IPO = IP;
                Array.Copy(ins, 0, ram, IP, ins.Length);

                var pre = new StringBuilder();
                pre.Append($"{vi:X} {Hex(ins)} {Hex(regs)} {flags:X2} {FHex(fr)} {spr:X} {spc:X}");
                try
                {
                    c.ExecuteNextInstruction();
                    var post = new byte[26]; var pf = new float[16];
                    for (byte i = 0; i < 26; i++) post[i] = regsC.Get8BitRegister(i);
                    for (byte i = 0; i < 16; i++) pf[i] = fregs.GetRegister(i);
                    w.WriteLine($"T {pre} => {Hex(post)} {fl.GetFlagsByte():X2} {FHex(pf)} {regsC.IPO:X} {regsC.SPR:X} {regsC.SPC:X} " +
                                $"{regsC.GetRegisterBank():X} {fregs.GetRegisterBank():X} {Diff(ram, ramP)} {Diff(rs, rsP)} {Diff(cs, csP)}");
                    made++;
                }
                catch (Exception)
                {
                    w.WriteLine($"X {pre}");
                    threw++;
                    Diff(ram, ramP); Diff(rs, rsP); Diff(cs, csP);   // restore whatever it wrote
                }
                c.CPU.REGS.SetRegisterBank(0); c.CPU.FREGS.SetRegisterBank(0);
            }
            vi++;
        }
        Console.WriteLine($"oracle: {vi} variants, {made} cases, {threw} threw, {unassembled} could not be encoded -> {outPath}");
        return 0;
    }

    // Placeholder operands the reference assembler accepts for every variant shape.
    static byte[] Assemble(string name)
    {
        int k = 0;
        string src = Regex.Replace(name, @"\(([^(),]+),([^(),]+)\)", "($1 + $2)");   // offset syntax: (X + Y)
        src = Regex.Replace(src, @"\b(rrrr|rrr|rr|r|fr|nnnn|nnn|nn|n|ff)\b", m =>
        {
            int reg = (k++ * 5) % 22;   // distinct, non-wrapping register runs
            string run = string.Concat(Enumerable.Range(0, m.Value.Length).Select(i => (char)('A' + reg + i)));
            return m.Value switch { "fr" => $"F{k}", "ff" => "Z", var s when s[0] == 'n' => "1", _ => run };
        });
        var asm = new Assembler();
        asm.Build(src);
        return asm.Errors == 0 ? asm.GetCompiledCode() : null;
    }

    // Random values into the letter fields of the format (bytes after the primary opcode).
    static byte[] Randomise(byte[] canon, string fmt, Random rnd)
    {
        var ins = (byte[])canon.Clone();
        foreach (char field in fmt.Where(char.IsUpper).Distinct())
        {
            int bits = fmt.Count(ch => ch == field);
            ulong v = bits == 5 ? (ulong)rnd.Next(26) : bits == 4 ? (ulong)rnd.Next(16) : (ulong)rnd.NextInt64() & ((1UL << bits) - 1);
            for (int i = fmt.Length - 1; i >= 0; i--)   // fill the field LSB-last-position first
            {
                if (fmt[i] != field) continue;
                int bi = 1 + i / 8, bit = 7 - i % 8;
                ins[bi] = (byte)(ins[bi] & ~(1 << bit) | (int)(v & 1) << bit);
                v >>= 1;
            }
        }
        return ins;
    }

    static string Hex(byte[] b) => Convert.ToHexString(b);
    static string FHex(float[] f) => string.Concat(f.Select(x => BitConverter.SingleToUInt32Bits(x).ToString("X8")));

    // Lists changed cells and restores them, so the next case starts from the pattern again.
    // Past 256 cells (block fills/copies) the list becomes "H<count>:<FNV-1a 64 of addr,value pairs>"
    // so the vector file stays small; tests/conformance.cpp computes the same.
    const int MaxListed = 256;
    static string Diff<T>(T[] cur, T[] pat) where T : unmanaged, IEquatable<T>
    {
        var sb = new StringBuilder();
        int count = 0, page = 4096 / System.Runtime.InteropServices.Marshal.SizeOf<T>();
        ulong h = 14695981039346656037UL;
        for (int p = 0; p < cur.Length; p += page)
        {
            int n = Math.Min(page, cur.Length - p);
            if (cur.AsSpan(p, n).SequenceEqual(pat.AsSpan(p, n))) continue;
            for (int i = p; i < p + n; i++)
            {
                if (cur[i].Equals(pat[i])) continue;
                uint v = Convert.ToUInt32(cur[i]);
                if (++count <= MaxListed) sb.Append($"{i:X}:{v:X},");
                h = (h ^ (uint)i) * 1099511628211UL;
                h = (h ^ v) * 1099511628211UL;
                cur[i] = pat[i];
            }
        }
        if (count == 0) return "-";
        return count > MaxListed ? $"H{count:X}:{h:X}" : sb.ToString(0, sb.Length - 1);
    }
}
