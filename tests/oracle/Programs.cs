// Program vectors: whole programs (assembly, C or BASIC) run on the reference for N instructions.
// Covers what single instructions cannot: interrupts, loops, the OS. Programs are compiled by
// the reference's own assembler / C / BASIC compiler, so the C++ side needs none of them.
//
//   dotnet run -c Release -- --programs <dir with .asm/.c/.bas files> <out.txt>
//
// A program's first line may say how long to run: "; steps: 200000" (// or ' in C and BASIC; default 100000).
// Output per program (hex):
//   P <name> <steps>
//   B <address> <bytes>                       one line per code block, loaded into a fresh machine
//   E <regs> <flags> <ip> <spr> <spc> <bank> <executed> <running> <vramPages> <visible> <auto>
//   D <addr:value,...|->                      RAM bytes that differ from the freshly loaded image
using System.Text;
using System.Text.RegularExpressions;
using Continuum93.Emulator;
using Continuum93.Emulator.Compilers.C93Basic;
using Continuum93.Emulator.Compilers.C93C;
using Continuum93.Emulator.Interpreter;

static class Programs
{
    public static int Run(string dir, string outPath)
    {
        Continuum93.Emulator.Colors.Palettes.Initialize();   // real default palettes, as at emulator startup
        using var w = new StreamWriter(outPath) { NewLine = "\n" };
        int n = 0;
        foreach (string path in Directory.GetFiles(dir, "*", SearchOption.AllDirectories).Where(p => p.EndsWith(".asm") || p.EndsWith(".c") || p.EndsWith(".bas")).Order(StringComparer.Ordinal))
        {
            string src = File.ReadAllText(path), name = Path.GetRelativePath(dir, path).Replace(Path.DirectorySeparatorChar, '/');
            var steps = Regex.Match(src, @"^\s*(?:;|//|')\s*steps:\s*(\d+)", RegexOptions.Multiline);
            int maxSteps = steps.Success ? int.Parse(steps.Groups[1].Value) : 100000;
            if (path.EndsWith(".c"))
            {
                var cc = new CCompiler { IncludeResolver = (inc, _) => File.Exists(Path.Combine(Path.GetDirectoryName(path), inc)) ? File.ReadAllText(Path.Combine(Path.GetDirectoryName(path), inc)) : null };
                src = cc.Compile(src, name);
                if (cc.Errors > 0) { Console.Error.WriteLine($"programs: {name}: {cc.ErrorList[0]}"); continue; }
            }
            if (path.EndsWith(".bas"))
            {
                var bc = new BasicCompiler();
                src = bc.Compile(src);
                if (bc.Errors > 0) { Console.Error.WriteLine($"programs: {name}: {bc.Log}"); continue; }
            }
            var asm = new Assembler();
            asm.Build(src, path);
            var blocks = asm.BlockManager.GetBlocks();
            if (asm.Errors > 0 || blocks.Count == 0) { Console.Error.WriteLine($"programs: {name}: does not assemble"); continue; }

            var c = new Computer();
            c.Clear();
            foreach (var b in blocks) c.LoadMemAt(b.Start, b.Data);
            byte[] image = (byte[])c.MEMC.RAM.Data.Clone();
            c.CPU.REGS.IPO = blocks[0].Start;

            int done = 0;
            try { for (; done < maxSteps && c.IsRunning; done++) c.ExecuteNextInstruction(); }
            catch (Exception e) { Console.Error.WriteLine($"programs: {name}: reference threw after {done} steps ({e.Message})"); continue; }

            var regs = c.CPU.REGS; var g = c.GRAPHICS;
            w.WriteLine($"P {name} {maxSteps:X}");
            foreach (var b in blocks) w.WriteLine($"B {b.Start:X} {Convert.ToHexString(b.Data)}");
            var r = new byte[26];
            for (byte i = 0; i < 26; i++) r[i] = regs.Get8BitRegister(i);
            w.WriteLine($"E {Convert.ToHexString(r)} {c.CPU.FLAGS.GetFlagsByte():X2} {regs.IPO:X} {regs.SPR:X} {regs.SPC:X} {regs.GetRegisterBank():X} " +
                        $"{done:X} {(c.IsRunning ? 1 : 0)} {g.VRAM_PAGES:X} {g.LAYER_VISIBLE_BITS:X} {g.LAYER_BUFFER_MODE_BITS:X}");
            var d = new StringBuilder();
            byte[] ram = c.MEMC.RAM.Data;
            for (int i = 0; i < ram.Length; i++) if (ram[i] != image[i]) d.Append($"{i:X}:{ram[i]:X},");
            w.WriteLine("D " + (d.Length == 0 ? "-" : d.ToString(0, d.Length - 1)));
            n++;
        }
        Console.WriteLine($"programs: {n} programs -> {outPath}");
        return 0;
    }
}
