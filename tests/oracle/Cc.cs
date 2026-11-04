// C compiler vectors for src/cc*.cpp, and the runtime library text as a C++ include.
//
//   dotnet run -c Release -- --cc <dir|file>... <out.txt>   every .c under the paths, then every source the
//                                                           reference unit tests (CCompilerTests) compile
//   dotnet run -c Release -- --ccruntime <out.inc>          CRuntime headers + library as C++ raw strings
//
// The unit tests are built into the oracle (oracle.csproj) with CcHarness.cs in place of CTestHarness.cs;
// running them records each compiled source and its include map. Duplicate sources are written once.
// Output per source (b64 = base64 of the UTF-8 text):
//   N <name>
//   F <b64 file name>   S <b64 source>
//   I <b64 name> <b64 including file> <b64 content | ->   every include query, in the order the reference made it
//   A <b64 assembly | ->   E <line> <col> <b64 file> <b64 message>   G <b64 log>
//   O <origin> <stack top>   (hex, after the compile: #pragma c93 origin/stack)
//   X <exception type>       the reference threw something other than a compile error (no A/E/G/O lines)
using System.Diagnostics;
using System.Reflection;
using System.Text;
using Continuum93.Emulator.Compilers.C93C;

static class Cc
{
    sealed class Case { public string Name, File, Source; public Func<string, string, string> Resolve; }
    static readonly List<Case> Recorded = [];
    static string current = "";
    static int perTest;

    // Called by CcHarness for every source a unit test compiles.
    public static void Record(string source, Dictionary<string, string> includes)
    {
        var inc = includes == null ? null : new Dictionary<string, string>(includes);
        Recorded.Add(new Case
        {
            Name = $"test:{current}#{perTest++}", File = "main.c", Source = source,
            Resolve = inc == null ? null : (name, _) => inc.TryGetValue(name, out string s) ? s : null
        });
    }

    static string B(string s) => s == null ? "-" : Convert.ToBase64String(Encoding.UTF8.GetBytes(s));

    public static int Run(string[] paths, string outPath)
    {
        var cases = new List<Case>();
        foreach (string p in paths)
        {
            IEnumerable<string> files = File.Exists(p) ? [p] : Directory.GetFiles(p, "*.c", SearchOption.AllDirectories).Order(StringComparer.Ordinal);
            foreach (string f in files)
            {
                if (f.Contains(Path.DirectorySeparatorChar + "debug" + Path.DirectorySeparatorChar)) continue;
                string full = Path.GetFullPath(f), dir = Path.GetDirectoryName(full);
                cases.Add(new Case
                {
                    Name = full, File = Path.GetFileName(full), Source = File.ReadAllText(full),
                    Resolve = (name, including) =>   // as Run/Runner.cs (boot) and tests/oracle --asm
                    {
                        string c = Path.Combine(dir, Path.GetDirectoryName(including) ?? "", name);
                        if (!File.Exists(c)) c = Path.Combine(dir, name);
                        return File.Exists(c) ? File.ReadAllText(c) : null;
                    }
                });
            }
        }
        int fileCases = cases.Count;
        RunUnitTests();
        cases.AddRange(Recorded);

        using var w = new StreamWriter(outPath) { NewLine = "\n" };
        var seen = new HashSet<string>();
        int n = 0, threw = 0, failed = 0;
        foreach (var c in cases)
        {
            var queries = new List<(string, string, string)>();
            var cc = new CCompiler
            {
                IncludeResolver = (name, including) =>
                {
                    string s = c.Resolve?.Invoke(name, including);
                    queries.Add((name, including, s));
                    return s;
                }
            };
            string asm = null, ex = null;
            try { asm = cc.Compile(c.Source, c.File); }
            catch (Exception e) { ex = e.GetType().Name; }
            var key = new StringBuilder(c.File).Append('\0').Append(c.Source);
            foreach (var q in queries) key.Append('\0').Append(q.Item1).Append('\0').Append(q.Item2).Append('\0').Append(q.Item3 ?? "\u0001");
            if (!seen.Add(key.ToString())) continue;

            w.WriteLine($"N {c.Name}");
            w.WriteLine($"F {B(c.File)}");
            w.WriteLine($"S {B(c.Source)}");
            foreach (var (name, including, s) in queries) w.WriteLine($"I {B(name)} {B(including)} {B(s)}");
            n++;
            if (ex != null) { w.WriteLine($"X {ex}"); threw++; continue; }
            w.WriteLine($"A {(asm.Length == 0 ? "-" : B(asm))}");
            foreach (var e in cc.ErrorList) w.WriteLine($"E {e.Line} {e.Column} {B(e.File)} {B(e.Message)}");
            w.WriteLine($"G {B(cc.Log)}");
            w.WriteLine($"O {cc.CodeOrigin:X} {cc.StackTop:X}");
            if (cc.Errors > 0) failed++;
            if (c.Name.EndsWith("halcyon.c"))   // compiled at every Halcyon boot: report the reference's time
            {
                long best = long.MaxValue;
                for (int k = 0; k < 5; k++)
                {
                    var sw = Stopwatch.StartNew();
                    new CCompiler { IncludeResolver = c.Resolve }.Compile(c.Source, c.File);
                    best = Math.Min(best, sw.Elapsed.Ticks / 10);
                }
                w.WriteLine($"T {best:X}");
                Console.WriteLine($"cc: {c.File} best of 5: {best / 1000.0:F1} ms");
            }
        }
        Console.WriteLine($"cc: {fileCases} files + {Recorded.Count} unit-test compiles -> {n} unique sources ({failed} with errors, {threw} threw) -> {outPath}");
        return 0;
    }

    // Runs every [Fact]/[Theory] of the reference's CCompilerTests (compiled into this assembly); failures are
    // expected (paths, the step cap) and ignored: only the recorded sources matter.
    static void RunUnitTests()
    {
        Continuum93.Emulator.Colors.Palettes.Initialize();
        var types = Assembly.GetExecutingAssembly().GetTypes()
            .Where(t => t.Namespace == "ContinuumUnitTests.CCompilerTests" && t.IsClass && !t.IsAbstract && t.IsPublic)
            .OrderBy(t => t.Name, StringComparer.Ordinal);
        int tests = 0;
        var log = Console.Out;
        foreach (var t in types)
            foreach (var m in t.GetMethods().Where(m => m.GetCustomAttribute<FactAttribute>() != null).OrderBy(m => m.Name, StringComparer.Ordinal))
            {
                var rows = m.GetCustomAttribute<TheoryAttribute>() != null
                    ? m.GetCustomAttributes<InlineDataAttribute>().SelectMany(a => a.GetData(m)).ToList()
                    : [Array.Empty<object>()];
                for (int r = 0; r < rows.Count; r++)
                {
                    current = $"{t.Name}.{m.Name}" + (rows.Count > 1 ? $"[{r}]" : "");
                    perTest = 0;
                    var ps = m.GetParameters();
                    object[] args = rows[r].Select((v, i) => v == null || ps[i].ParameterType.IsInstanceOfType(v) ? v : Convert.ChangeType(v, ps[i].ParameterType)).ToArray();
                    try
                    {
                        Console.SetOut(TextWriter.Null);   // the emulator logs to the console
                        m.Invoke(m.IsStatic ? null : Activator.CreateInstance(t), args);
                    }
                    catch (Exception) { }
                    finally { Console.SetOut(log); }
                    tests++;
                }
            }
        Console.WriteLine($"cc: ran {tests} unit-test cases, recorded {Recorded.Count} compiles");
    }

    public static int Runtime(string outPath)
    {
        var w = new StringBuilder();
        w.Append("// Generated by tests/oracle --ccruntime from the reference's CRuntime.cs. Do not edit: regenerate.\n");
        w.Append("// Line endings are LF (the C lexer normalises CRLF, so the result is the same).\n");
        string[] headers = ["stddef.h", "stdint.h", "stdbool.h", "limits.h", "string.h", "stdlib.h", "ctype.h", "continuum.h"];
        w.Append("struct CcHeader { const char* name; const char* text; };\nstatic const CcHeader kCcHeaders[] = {\n");
        foreach (string h in headers) w.Append($"  {{\"{h}\", {Raw(CRuntime.GetHeader(h))}}},\n");
        w.Append("};\nstatic const char* const kCcLibrary = ").Append(Raw(CRuntime.LibrarySource)).Append(";\n");
        File.WriteAllText(outPath, w.ToString());
        Console.WriteLine($"ccruntime: {headers.Length} headers + library -> {outPath}");
        return 0;
    }

    // Raw string literals in pieces below MSVC's 16 KB per-literal limit.
    static string Raw(string s)
    {
        s = s.Replace("\r\n", "\n");
        if (s.Contains(")C93\"")) throw new Exception("delimiter clash");
        var sb = new StringBuilder();
        for (int i = 0; i < s.Length; i += 8000)
            sb.Append("\n    R\"C93(").Append(s, i, Math.Min(8000, s.Length - i)).Append(")C93\"");
        return sb.ToString();
    }
}
