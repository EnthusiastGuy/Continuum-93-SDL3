REPEAT
  SLEEP (n)
  PLOT (-(arr) < f# <> (LTRIM(Y[TAN()])) ROL -MOUSE Y IMPLY 65535 ROR arr) ^ 31 OR 5 SHL LEFT(Y, 1, 5) SHL x, EXP((2), (STR$(arr) NOR VARPTR(y) / 1.5 > t$) OR (0.25 NAND NOT (arr) AND -(MIN())), NOT 2 ROR -EXP(-7, 10) / b / 0.25), (t$[b]) \ STRING((CHR$((VARPTR(Y)), b, TIME) \ -32 + 1.5), f# ROL (NOT VARPTR(Y) - COS(ATAN(), "x\ny", ASC(LEFT(), 1))))
  END
UNTIL PEEK24(-b = 31 >= X SHL VARPTR(f#))
DATA MOUSE BUTTON 3.0 <> g# ROL (VARPTR(g#)) SHR PEEK(32 / t$) NAND (a NOR 65535 / a NAND (PEEK32(("hi")))), VARPTR(i) SHL b SHL ("a b") <= 3.0 MOD 65535 * VARPTR(b) < 0.25
y = arr[10]
DATA -Y
LET i = (t$) IMPLY NOT (0.25) \ (b) IMPLY i ^ i XNOR INKEY OR n + ("a b" <= i)
s$ = 5
f# = n NOR 1.5 \ b * 1 SHL (POW(-(CEIL(255, -1, UCASE())) ^ 1, TICKS <= RIGHT(), PEEK32(MOUSE Y))) < Y
