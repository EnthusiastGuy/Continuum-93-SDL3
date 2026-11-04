' D-18: lines the reference could not encode: float/integer mixes, MOD, \, conditions as values, INT, ABS,
' negative integer to float, PEEK/POKE widths, GOSUB/GOTO by label, SLEEP with a variable
a# = 2.5
b = a# * 2
c = 7 MOD 3
d = 7 \ 2
f = (3 > 2) + (2 > 3) + 10
g = INT(3.7)
h = ABS(-4)
i = -3
k# = i
k = k# * 2
l = a# > 2
POKE16 0x9000, 0x1234
POKE 0x9002, 0x56
m = PEEK16(0x9000)
n = PEEK(0x9002)
o = PEEK32(0x9000)
p = 1
SLEEP p
GOSUB SetQ
r = q + 1
GOTO Skip
r = 100
Skip:
END
SetQ:
q = 41
RETURN
' expect b = 5
' expect c = 1
' expect d = 3
' expect f = 11
' expect g = 3
' expect h = 4
' expect k = -6
' expect l = 1
' expect m = 4660
' expect n = 86
' expect o = 305419776
' expect r = 42
