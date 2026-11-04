' D-18: UCASE$ and LCASE$ (letters only, other characters kept, empty, cut at 15 by the assignment)
a$ = UCASE("Hello, World 9z")
b$ = LCASE("Hello, World 9Z")
c$ = UCASE("")
d$ = LCASE("@[`{ AZaz")
s$ = "mixed Case"
e$ = UCASE(s$)
f$ = LCASE(UCASE("abcdefghijklmnopqrstuvwxyz"))
' expect a$ = "HELLO, WORLD 9Z"
' expect b$ = "hello, world 9z"
' expect c$ = ""
' expect d$ = "@[`{ azaz"
' expect e$ = "MIXED CASE"
' expect f$ = "abcdefghijklmno"
