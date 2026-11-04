s$ = "abc"
t$ = LEFT(s$, 1) + RIGHT(s$, 1) + MID(s$, 2, 1)
u$ = UCASE(s$) + LCASE("XYZ")
PRINT LEN(s$ + t$)
v$ = CHR(65) + STR(LEN(s$))
