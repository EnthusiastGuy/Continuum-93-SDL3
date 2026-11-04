// Service mode's WATCHER and IMMEDIATE windows: the reference's own window code (WatcherWindow.ParseCondition,
// EvaluateCondition, ParseAndEvaluateConditions; ImmediateWindow.CompileAndRun), called by reflection on windows
// built without their graphics. tests/conformance.cpp --watch / --immediate replay the output.
//
//   python tests/service/cases.py <dir>                 (writes watch_cases.txt, immediate_cases.txt)
//   dotnet run -c Release -- --watch <dir>/watch_cases.txt <watch.txt>
//   dotnet run -c Release -- --immediate <dir>/immediate_cases.txt <immediate.txt>
//
// Watch cases: one per line, the lines of one WATCHER text separated by tabs. The machine is WatchMachine below
// (conformance.cpp builds the same). Output: "W <case>", then "R <per line: - blank, X invalid, T/F the
// condition's value, E the reference threw> <1 when the machine switched to step-by-step, E it threw>".
// Immediate cases: "C <stepping 0/1>", "L <line>"..., "E" on a fresh machine (ImmediateMachine). Output: the
// case, then "S <status>" ("S THREW <exception>" when the reference threw) and
// "R <regs> <flags> <fregs> <bank> <fbank> <ip> <spr> <spc> <ram> <cs> <rs>" (diffs against the start, hex).
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text;
using Continuum93.CodeAnalysis;
using Continuum93.Emulator;
using Continuum93.ServiceModule.UI;

static class Watch
{
    const BindingFlags Any = BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.Instance;
    public static readonly float[] FRegs = [0f, -0f, 1.5f, -2.25f, float.NaN, float.PositiveInfinity, float.NegativeInfinity,
        float.Epsilon, float.MaxValue, 0.1f, 100f, -100f, 1e30f, 123.456f, 7f, 15.5f];
    public static byte RegAt(int i) => (byte)((uint)(i + 1) * 2654435761u >> 24);
    public static byte RamAt(uint a) => (byte)(a * 2246822519u >> 24);

    static Computer WatchMachine()
    {
        var c = new Computer();
        for (byte i = 0; i < 26; i++) c.CPU.REGS.Set8BitRegister(i, RegAt(i));
        for (byte i = 0; i < 16; i++) c.CPU.FREGS.SetRegister(i, FRegs[i]);
        byte[] ram = c.MEMC.RAM.Data;
        for (uint a = 0; a < 0x1000000; a++) ram[a] = RamAt(a);
        return c;
    }

    public static int RunWatch(string casesPath, string outPath)
    {
        Machine.COMPUTER = WatchMachine();
        var win = (WatcherWindow)RuntimeHelpers.GetUninitializedObject(typeof(WatcherWindow));
        var t = typeof(WatcherWindow);
        var lines = new List<string>();
        t.GetField("_lines", Any).SetValue(win, lines);
        var conds = t.GetField("_conditions", Any);
        conds.SetValue(win, Activator.CreateInstance(conds.FieldType));
        MethodInfo parse = t.GetMethod("ParseCondition", Any), eval = t.GetMethod("EvaluateCondition", Any),
            all = t.GetMethod("ParseAndEvaluateConditions", Any);
        using var w = new StreamWriter(outPath) { NewLine = "\n" };
        int n = 0;
        foreach (string c in File.ReadAllLines(casesPath))
        {
            lines.Clear();
            lines.AddRange(c.Split('\t'));
            var r = new StringBuilder();
            foreach (string l in lines)
            {
                string tr = l.Trim();
                if (tr.Length == 0) { r.Append('-'); continue; }
                object cond = parse.Invoke(win, [tr]);
                try { r.Append(cond == null ? 'X' : (bool)eval.Invoke(win, [cond]) ? 'T' : 'F'); }
                catch (TargetInvocationException) { r.Append('E'); }   // RAM past 16 MB + 3: the reference threw
            }
            DebugState.StepByStep = false;
            string hit;
            try { all.Invoke(win, null); hit = DebugState.StepByStep ? "1" : "0"; }
            catch (TargetInvocationException) { hit = "E"; }
            w.WriteLine("W " + c);
            w.WriteLine($"R {r} {hit}");
            n++;
        }
        Console.WriteLine($"watch: {n} cases -> {outPath}");
        return 0;
    }

    public const uint StartIp = 0x001234, StartSpr = 0x30, StartSpc = 3;
    static Computer ImmediateMachine()
    {
        var c = new Computer();
        for (byte i = 0; i < 26; i++) c.CPU.REGS.Set8BitRegister(i, RegAt(i));
        c.CPU.REGS.IPO = StartIp; c.CPU.REGS.SPR = StartSpr; c.CPU.REGS.SPC = StartSpc;
        return c;
    }

    public static int RunImmediate(string casesPath, string outPath)
    {
        var t = typeof(ImmediateWindow);
        MethodInfo run = t.GetMethod("CompileAndRun", Any);
        using var w = new StreamWriter(outPath) { NewLine = "\n" };
        int n = 0;
        bool stepping = false;
        var lines = new List<string>();
        foreach (string line in File.ReadAllLines(casesPath))
        {
            if (line.StartsWith("C ")) { stepping = line[2] == '1'; lines.Clear(); w.WriteLine(line); continue; }
            if (line.StartsWith("L ")) { lines.Add(line[2..]); w.WriteLine(line); continue; }
            if (line != "E") continue;
            var c = ImmediateMachine();
            Machine.COMPUTER = c;
            byte[] ram = c.MEMC.RAM.Data, rs = c.MEMC.RSRAM.Data;
            uint[] cs = (uint[])typeof(Continuum93.Emulator.RAM.StackMemory).GetField("_data", Any).GetValue(c.MEMC.CSRAM);
            byte[] ram0 = (byte[])ram.Clone(), rs0 = (byte[])rs.Clone();
            uint[] cs0 = (uint[])cs.Clone();
            var win = (ImmediateWindow)RuntimeHelpers.GetUninitializedObject(t);
            t.GetField("_lines", Any).SetValue(win, new List<string>(lines));
            DebugState.StepByStep = stepping;
            DebugState.MoveNext = false;
            try
            {
                run.Invoke(win, null);
                w.WriteLine("S " + (string)t.GetField("_statusMessage", Any).GetValue(win));
            }
            catch (TargetInvocationException e) { w.WriteLine("S THREW " + e.InnerException?.GetType().Name); }
            var regs = c.CPU.REGS; var fregs = c.CPU.FREGS;
            var post = new byte[26]; var pf = new float[16];
            for (byte i = 0; i < 26; i++) post[i] = regs.Get8BitRegister(i);
            for (byte i = 0; i < 16; i++) pf[i] = fregs.GetRegister(i);
            w.WriteLine($"R {Convert.ToHexString(post)} {c.CPU.FLAGS.GetFlagsByte():X2} " +
                        $"{string.Concat(pf.Select(x => BitConverter.SingleToUInt32Bits(x).ToString("X8")))} " +
                        $"{regs.GetRegisterBank():X} {fregs.GetRegisterBank():X} {regs.IPO:X} {regs.SPR:X} {regs.SPC:X} " +
                        $"{Diff(ram, ram0)} {Diff(cs, cs0)} {Diff(rs, rs0)}");
            w.WriteLine("E");
            n++;
        }
        DebugState.StepByStep = false;
        Console.WriteLine($"immediate: {n} cases -> {outPath}");
        return 0;
    }

    static string Diff<T>(T[] cur, T[] start) where T : unmanaged, IEquatable<T>
    {
        var sb = new StringBuilder();
        for (int i = 0; i < cur.Length; i++)
            if (!cur[i].Equals(start[i])) sb.Append($"{i:X}:{Convert.ToUInt32(cur[i]):X},");
        return sb.Length == 0 ? "-" : sb.ToString(0, sb.Length - 1);
    }
}
