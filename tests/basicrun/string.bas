' D-18: STRING$(n, c$): n copies of the first character (0, negative, empty c$, longer than a variable)
a$ = STRING(5, "*")
b$ = STRING(0, "*")
c$ = STRING(-3, "x")
d$ = STRING(3, "")
e$ = STRING(40, "-")
n = 2
f$ = STRING(n + 1, "xyz")
' expect a$ = "*****"
' expect b$ = ""
' expect c$ = ""
' expect d$ = ""
' expect e$ = "---------------"
' expect f$ = "xxx"
