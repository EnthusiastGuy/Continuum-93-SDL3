' steps: 20000
' Loops, branches, arithmetic, logic and a subroutine; results stay in the variables.
total = 0
FOR i = 1 TO 10
  total = total + i * i
NEXT i
FOR j = 10 TO 0 STEP -3
  total = total - j
NEXT
n = 0
WHILE n < 5
  n = n + 1
  IF n = 3 THEN
    total = total + 100
  ELSEIF n = 4 THEN
    total = total / 2
  ELSE
    total = total - 1
  END IF
WEND
bits = 1 SHL 5
mask = bits OR 3
rot = mask ROR 1
flag = total > 100 AND n = 5
neg = - total
inv = NOT mask
q = total \ 7
GOSUB Double
' END would RET on an empty call stack: park in a loop instead
WHILE 1
WEND
' A label inside a block is not pre-collected, so GOSUB gets a label (a top-level one gets a bad numeric address)
IF 0 THEN
Double:
  total = total * 2
  RETURN
END IF
