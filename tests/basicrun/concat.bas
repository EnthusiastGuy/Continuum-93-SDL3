' D-18: string + (literals, variables, empty, chains, a loop longer than a variable, a short variable grown)
a$ = "Hello" + " " + "World"
x$ = "ab"
y$ = "cd"
b$ = x$ + y$ + x$
c$ = "" + ""
s$ = ""
FOR i = 1 TO 20
    s$ = s$ + "ab"
NEXT i
t$ = "x"
u$ = "guard"
t$ = t$ + "yz"
t$ = t$ + "0123456789ABCDEFGHIJ"
' expect a$ = "Hello World"
' expect b$ = "abcdab"
' expect c$ = ""
' expect s$ = "abababababababa"
' expect t$ = "xyz0123456789AB"
' expect u$ = "guard"
