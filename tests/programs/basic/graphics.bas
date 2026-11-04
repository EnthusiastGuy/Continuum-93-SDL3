' steps: 20000
' Drawing commands and PRINT of numbers (video interrupts).
CLS
FOR i = 0 TO 20 STEP 4
  PLOT 10 + i, 20, i
  LINE 0, i, 100, i, 7
NEXT
RECTANGLE 5, 5, 40, 30, 3
RECTANGLE FILLED 60, 5, 20, 20, 9
CIRCLE 120, 60, 15
CIRCLE FILLED 160, 60, 10, 12
ELLIPSE FILLED 200, 60, 20, 8, 5
c = 0
WHILE c < 3
  PRINT c * 11
  c = c + 1
WEND
WHILE 1
WEND
