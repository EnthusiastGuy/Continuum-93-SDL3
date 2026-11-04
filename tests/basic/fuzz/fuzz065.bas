SPRITE arr, (0.25), 31, (PI), ((2) NOR NOT 1e3), TIME
i = s$
x = -TRIM(FLOOR(LEFT(VARPTR(y), CEIL(f#, SQR(), 1e3), INSTR(VARPTR(s$))), CEIL(n)), NOT (32)) AND t$[n] SHL NOT 3.0 <> "x\ny" = CHR$(INKEY $) ^ 32
IF NOT PEEK24(STR$(RTRIM(g#, (SGN("x\ny"))), (1)) ^ Y XOR -1 > 32 AND 2) THEN
  DATA (0), (32 / PEEK16(x \ i) \ x), SIN(VARPTR(i), -"a b")
ELSE
  SPRITE i, (2 ROL (INSTR()) <= MOUSE BUTTON ROUND(2, 3.0) >= "" ^ (MOUSE X) + (arr SHL g#)), 1, 255, INKEY, (65535 <> NOT (TAN() / 1e3) ROL 1e3 NOR (10) > VARPTR(x) < arr - 2 NOR b)
  Main:
  PRINT AT (NOT (2) NOR (31) XOR MOUSE BUTTON Y NAND (255) AND (TAN()) XOR VARPTR(t$)) = -1 + 255 \ (g#) XNOR (1e3) ROR NOT CHR((COS(Y, 2)), Y) >= -1 XNOR NOT (0), (PEEK24(-TAN()) > INKEY XNOR SQR(3.0) + NOT ("")) SHR TICKS ROR X; FIX(10 MOD PEEK24(VARPTR(Y)) XNOR STR$((65535)), VARPTR(i) \ PEEK32(b) IMPLY (-1) - 10) NOR (EXP((INT(arr)), SGN())) NAND 3.0 <= -2.5 ROL PEEK24((LCASE(-7, 3.0))) + (-b >= 2)
ENDIF
y = s$
