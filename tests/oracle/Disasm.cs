// Disassembler vectors: the reference's ContinuumDebugger text for every Mnem.OPS variant with random
// operands, random byte streams, and the end of RAM. tests/conformance.cpp --disasm replays them.
//
//   dotnet run -c Release -- --disasm <out.txt> [cases per variant = 20] [seed = 93]
//
// Output (numbers hex): "C <address> <count> <bytes>", then one "L <line>" per line of
// GetDissassembledFull (ADDR|BB BB|INSTR), then "E". RAM is zero apart from <bytes> at <address>.
using Continuum93.CodeAnalysis;
using Continuum93.Emulator;
using Continuum93.Emulator.Mnemonics;

static class Disasm
{
    public static int Run(string outPath, int cases, int seed)
    {
        var c = new Computer();
        var rnd = new Random(seed);
        byte[] ram = c.MEMC.RAM.Data;
        using var w = new StreamWriter(outPath) { NewLine = "\n" };
        int n = 0;
        void Case(uint addr, byte[] bytes, int count)
        {
            for (int i = 0; i < bytes.Length && addr + i < ram.Length; i++) ram[addr + i] = bytes[i];
            ContinuumDebugger.DebugInstructionsCount = count;
            ContinuumDebugger.RunAt(addr, c);
            string text = ContinuumDebugger.GetDissassembledFull();
            for (int i = 0; i < bytes.Length && addr + i < ram.Length; i++) ram[addr + i] = 0;
            w.WriteLine($"C {addr:X6} {count:X} {Convert.ToHexString(bytes)}");
            foreach (string l in text.Split(Environment.NewLine, StringSplitOptions.RemoveEmptyEntries)) w.WriteLine("L " + l);
            w.WriteLine("E");
            n++;
        }
        foreach (var op in Mnem.OPS.Values)
        {
            var subs = op.IsPrimary ? new List<int> { -1 } : Enumerable.Range(0, 256).Where(s => op.MatchSubop((byte)s)).ToList();
            if (subs.Count == 0) continue;
            for (int k = 0; k < cases; k++)
            {
                var b = new byte[24];
                rnd.NextBytes(b);
                b[0] = op.IsPrimary ? op.OpCode : op.ParentCode.Value;
                if (!op.IsPrimary) b[1] = (byte)subs[rnd.Next(subs.Count)];
                // Mostly a fixed address; sometimes anywhere, so relative targets wrap.
                uint addr = k % 4 == 3 ? (uint)rnd.Next(0x1000000 - 24) : 0x080000;
                Case(addr, b, 1);
            }
        }
        for (int k = 0; k < 500; k++)   // random streams: illegal bytes, chains of instructions
        {
            var b = new byte[96];
            rnd.NextBytes(b);
            Case(0x100000, b, 12);
        }
        for (uint a = 0xFFFFE8; a < 0x1000000; a += 3)   // the end of RAM (16 MB + 3 bytes)
        {
            var b = new byte[(int)(0x1000003 - a)];
            rnd.NextBytes(b);
            Case(a, b, 8);
        }
        Console.WriteLine($"disasm: {n} cases -> {outPath}");
        return 0;
    }
}
