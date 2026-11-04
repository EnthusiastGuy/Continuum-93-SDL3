x = 1
IF x THEN
  DIM inner(3)
  REPEAT
  UNTIL 1
  STOP
END IF
FOR i = 1 TO 2
InLoop:
  GOTO InLoop
NEXT
GOTO InLoop
GOSUB InLoop
DIM s$(40)
FOR f = 1.5 TO 2
NEXT
f = 3
f# = f
PRINT AT 1, 2; 1.5; 3; x
INPUT "v"; v#
CLS "page"
WAIT "x"
IF "s" THEN
END IF
WHILE x = 1 AND 2
WEND
