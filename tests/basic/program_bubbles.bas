' A small but complete program touching most of the dialect
DIM table(15)
score = 0
lives = 3
name$ = "PLAYER"
speed# = 1.5
Main:
  CLS
  VIDEO 3
  LAYER VISIBILITY 0x07
  FOR i = 0 TO 15
    x = i * 10
    y = i * 5
    PLOT x, y, i
    LINE x, 0, x, y, 15 - i
    IF i MOD 2 = 0 THEN
      CIRCLE FILLED x, y, 4, i
    ELSEIF i MOD 3 = 0 THEN
      RECTANGLE x, y, 5, 5, 2
    ELSE
      ELLIPSE x, y, 6, 3
    END IF
  NEXT i
  WHILE lives > 0
    k = INKEY
    IF k = 27 THEN
      GOTO Finish
    END IF
    mx = MOUSE X
    my = MOUSE Y
    IF MOUSE BUTTON 0 THEN
      score = score + 10
      GOSUB ShowScore
    END IF
    lives = lives - 1
    WAIT 1
  WEND
  GOTO Main
ShowScore:
  PRINT AT 0, 0; name$ USING 0, 15
  PRINT score
  PRINT "Score: "; score
  RETURN
Finish:
  s$ = STR(score)
  t$ = "Final: " + s$
  PRINT t$
  POKE 0x1000, score AND 0xFF
  v = PEEK(0x1000) SHL 2
  t = TIME
  FOR f = 0.0 TO 2.0 STEP 0.5
    PRINT f
  NEXT
  END
