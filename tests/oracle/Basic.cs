// BASIC compiler vectors for src/basic.cpp: every .bas under the given paths, compiled by the reference.
//
//   dotnet run -c Release -- --basic <dir|file>... <out.txt>
//
// Output per source (numbers hex, text as UTF-8 hex):
//   N <path>
//   S <source>               the text the reference compiled (File.ReadAllText)
//   R <errors> <run address>
//   A <assembly>             GeneratedAssembly, byte for byte
//   G <log>                  BasicCompiler.Log
//   Q <errors> <run address> only when it compiled: the reference assembler on the assembly, then
//   B <start> <bytes>        one line per code block (what INT 0 0xC1 loads); X <exception> if it threw
//   T <microseconds>         best of 5 compiles, for the largest source only (timing)
using System.Diagnostics;
using System.Text;
using Continuum93.Emulator;
using Continuum93.Emulator.Compilers.C93Basic;
using Continuum93.Emulator.Interpreter;

static class Basic
{
    public static int Run(string[] paths, string outPath)
    {
        var files = paths.SelectMany(p => File.Exists(p) ? [p] : Directory.GetFiles(p, "*.bas", SearchOption.AllDirectories))
                         .Select(Path.GetFullPath).Distinct().Order(StringComparer.Ordinal).ToList();
        using var w = new StreamWriter(outPath) { NewLine = "\n" };
        string largest = files.OrderByDescending(f => new FileInfo(f).Length).FirstOrDefault();
        foreach (string path in files)
        {
            string src = File.ReadAllText(path);
            var bc = new BasicCompiler();   // fresh: the reference's symbol table survives between compiles
            string asm = bc.Compile(src);
            w.WriteLine($"N {path}");
            w.WriteLine($"S {Hex(src)}");
            w.WriteLine($"R {bc.Errors:X} {bc.GetRunAddress():X}");
            w.WriteLine($"A {Hex(asm)}");
            w.WriteLine($"G {Hex(bc.Log)}");
            if (bc.Errors == 0)   // what BuildBasic (INT 0 0xC1) loads next: the reference assembler's blocks
            {
                CompileLog.Reset();
                var a = new Assembler();
                try
                {
                    a.Build(asm, "");
                    w.WriteLine($"Q {a.Errors:X} {a.GetRunAddress():X}");
                    foreach (var b in a.BlockManager.GetBlocks()) w.WriteLine($"B {b.Start:X} {Convert.ToHexString(b.Data ?? [])}");
                }
                catch (Exception e) { w.WriteLine($"X {e.GetType().Name}"); }
            }
            if (path == largest)
            {
                long best = long.MaxValue;
                for (int k = 0; k < 5; k++)
                {
                    var sw = Stopwatch.StartNew();
                    new BasicCompiler().Compile(src);
                    best = Math.Min(best, sw.Elapsed.Ticks / 10);
                }
                w.WriteLine($"T {best:X}");
                Console.WriteLine($"basic: {Path.GetFileName(path)} ({src.Length} chars) best of 5: {best / 1000.0:F2} ms");
            }
        }
        Console.WriteLine($"basic: {files.Count} sources -> {outPath}");
        return 0;
    }

    static string Hex(string s) => Convert.ToHexString(Encoding.UTF8.GetBytes(s ?? ""));
}
