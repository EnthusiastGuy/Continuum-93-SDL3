' steps: 30000
' Runtime string helpers. Only sources the reference CPU survives: many helper/layout mixes make it
' throw (out-of-range memory), and those runs are skipped by the program oracle.
s$ = "Hello World"
a$ = LEFT(s$, 5)
b$ = RIGHT(s$, 5)
c$ = MID(s$, 7, 3)
m$ = MID(s$, 3)
g$ = CHR(65)
l = LEN(s$)
k = ASC(b$)
v = VAL("-1234")
w$ = STR(l * 3)
PRINT a$
' END would RET on an empty call stack: park in a loop instead
WHILE 1
WEND
