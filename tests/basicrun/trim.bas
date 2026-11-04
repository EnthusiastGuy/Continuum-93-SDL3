' D-18: TRIM$, LTRIM$, RTRIM$ (spaces and tabs, all blank, empty, nothing to trim)
a$ = TRIM("  Hello  ")
b$ = LTRIM("  Hello  ")
c$ = RTRIM("  Hello  ")
d$ = TRIM("     ")
e$ = TRIM("")
f$ = TRIM("a b")
g$ = RTRIM("x")
h$ = LTRIM("   ")
' expect a$ = "Hello"
' expect b$ = "Hello  "
' expect c$ = "  Hello"
' expect d$ = ""
' expect e$ = ""
' expect f$ = "a b"
' expect g$ = "x"
' expect h$ = ""
