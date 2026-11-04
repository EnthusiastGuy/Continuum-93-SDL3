# Case files for the service-mode WATCHER / IMMEDIATE oracle (tests/oracle/Watch.cs) and conformance --watch / --immediate:
#   python tests/service/cases.py <dir>   -> <dir>/watch_cases.txt, <dir>/immediate_cases.txt
import os, random, sys
out = sys.argv[1] if len(sys.argv) > 1 else "."
random.seed(93)

lefts = ["A","a","B","Z","AB","ab","ZA","YZ","ABC","XYZ","ZAB","ABCD","ZABC","XYZA","QRST","AC","ABD","ABCDE","QQ","",
         "F","f","F0","F1","f15","F15","F16","F 1","F+3","F-0","F-1","F01","F00015","FG","FGH","F1.0","F0x1","Fz",
         "(0)","(0x0)","(0X10)","(0x00FF00)","(  0x10  )","(255)","(0xFFFFFF)","(0x1000000)","(0x1000002)","(-0)","(+5)","(0x)",
         "()","( )","(0x 5)","(0xG)","(abc)","(1)(2)","((1))","(4294967295)","(4294967296)","(0xFFFFFFFF)","(0x100000000)",
         "A =","A !","IP","PC","SPC","1","0x5","é"]
ops = ["==","!=","<","<=",">",">=","=","=<","=>","<>","!","<==","=="]
rights = ["0","1","5","255","256","0x0","0xff","0XFF","0x","0x 5","0x5 ","-0","+5","-1","18446744073709551615",
          "18446744073709551616","0xFFFFFFFFFFFFFFFF","0x10000000000000000","1.5","abc","","00012","1e3","1,000",
          " 7"]
frights = ["0","-0","1.5","-2.25","NaN","nan","-NaN","+NaN","Infinity","-Infinity","inf","∞","1e39","-1e39","1e-46",
           "1.401298E-45","1E-45","3.4028235E+38","3.4028236E+38","0.1","100","1,000","1,,0","1,","1.","1.5.","1e","1e+",
           "1e+3",".5","-.5","+-1","- 1","1 ","0x10","123.456","7","15.5","1E2","1e400","0.0000000000000000000000000000000000000000000014",
           "abc","","1_0","12,345.5","1.5e-3","1,5"]
cases = []
for l in lefts:
    for o in ops:
        rs = frights if l[:1] in "Ff" else rights
        for r in rs:
            cases.append(f"{l} {o} {r}")
            if random.random() < 0.15: cases.append(f"{l}{o}{r}")
# Values matching the machine: registers RegAt(i), fregs, RAM RamAt
def regat(i): return ((i+1)*2654435761 >> 24) & 0xFF
def ramat(a): return ((a*2246822519) & 0xFFFFFFFF) >> 24
for i in range(26):
    cases.append(f"{chr(65+i)} == {regat(i)}")
    cases.append(f"{chr(65+i)} == 0x{regat(i):X}")
    v = (regat(i) << 8) | regat((i+1)%26); cases.append(f"{chr(65+i)}{chr(65+(i+1)%26)} == {v}")
    v = (v << 8) | regat((i+2)%26); cases.append(f"{chr(65+i)}{chr(65+(i+1)%26)}{chr(65+(i+2)%26)} >= {v}")
    v = (v << 8) | regat((i+3)%26); cases.append(f"{chr(65+i)}{chr(65+(i+1)%26)}{chr(65+(i+2)%26)}{chr(65+(i+3)%26)} <= {v-1}")
for a in [0,1,2,0x1234,0xC00000,0xFFFFFF,0x1000000,0x1000001,0x1000002]:
    cases.append(f"(0x{a:X}) == {ramat(a) if a < 0x1000000 else 0}")
    cases.append(f"({a}) != {ramat(a) if a < 0x1000000 else 0}")
for f in ["0","-0","1.5","-2.25","NaN","Infinity","-Infinity","1E-45","3.4028235E+38","0.1","100","-100","1E+30","123.456","7","15.5"]:
    for k in range(16):
        for o in ["==","!=","<",">="]:
            if random.random() < 0.4: cases.append(f"F{k} {o} {f}")
# comments, blanks, multi-line
extra = ["; just a comment","A == 0 ; comment","A == 0;c","  A == 0  ","A == 0 ;","; ","A==0","A == 0 == 1","A ==",
         "== 5","!= 0","< 1","A  <  300","a <= 0xff",
         "A == 0\tB == 0","A == 0\t\tB != 0","A >= 0\tF0 == 0","A >= 0\tF0 == 0\t; x","   \t  ","",
         "F2 == 1.5\tF3 == -2.25\t(0x0) == 0","QQ == 0","QQ == 0\tZZ <= 0","A == 0\tbad","F4 != NaN","F4 == NaN","F5 == Infinity",
         "F7 > 0","F7 == 1E-45","F8 == 3.4028235E+38","F1 == 0","F1 == -0","F0 != -0"]
cases += extra
with open(os.path.join(out, "watch_cases.txt"),"w",encoding="utf-8",newline="\n") as f:
    for c in cases: f.write(c + "\n")
print(len(cases), "watch cases")

icases = [
    [],
    [""],
    ["LD A, 5"],
    ["LD A, 5", "LD BC, 0x1234", "ADD A, 3"],
    ["    LD ABCD, 0xDEADBEEF", "    INC A"],
    ["LD F0, 1.5", "LD F1, 2", "ADD F0, F1"],
    ["LD (0x100000), 0x42", "LD A, (0x100000)"],
    ["PUSH A", "POP B"],
    ["CALLR .sub", "LD A, 9", "RET", ".sub", "LD B, 7", "RET"],
    ["CALL .sub", "RET", ".sub", "LD Z, 3", "RET"],
    [".loop", "JR .loop"],
    [".loop", "JP .loop"],
    ["LD A, 0", ".again", "INC A", "CP A, 10", "JR NZ, .again"],
    ["LD A, 0", ".again", "INC A", "JR .again"],
    ["RET"],
    ["RET", "LD A, 1"],
    ["LD A,"],
    ["FOO BAR"],
    ["LD Q, (1,2,3)"],
    ["LD A, B, C"],
    ["LD A, 1", "LD"],
    ["nop"],
    ["#ORG 0xC00000", "LD A, 1"],
    ["#ORG 0xC00001", "LD A, 1"],
    ["#ORG 0xC00010", "LD A, 1"],
    ["#ORG 0x200000", "LD A, 1"],
    ["#ORG 0xBFFFFF", "LD A, 1"],
    ["#RUN 0x123"],
    ["#DB 1, 2, 3"],
    ["#DB .nolabel"],
    ["LD A, ()"],
    ["; comment only"],
    ["LD A, 5 ; trailing comment"],
    ["DEBUG"],
    ["LD A, 0xFF", "ADD A, 1"],
    ["SUB A, 0x21", "LD B, A"],
    ["JP 0x1234"],
    ["REGS 1", "LD A, 77", "REGS 0"],
    ["LD A, 1", ".x", ".x"],
    ["   ", "\tLD A, 3"],
    ["LD A, 0x100"],
    ["LD SPC, 0"],
]
with open(os.path.join(out, "immediate_cases.txt"), "w", encoding="utf-8", newline="\n") as f:
    for c in icases:
        for step in (1, 0):
            f.write(f"C {step}\n")
            for l in c: f.write(f"L {l}\n")
            f.write("E\n")
print(len(icases) * 2, "immediate cases")
