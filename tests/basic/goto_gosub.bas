Main:
  GOSUB Sub1
  GOTO Done
Sub1:
  PRINT "in sub"
  RETURN
Done:
  GOTO Main
  GOSUB Sub1
  END
