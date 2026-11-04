; #include: relative to the including file, appended after this file, "already included" list quirk.
#include inc/one.asm
#include "inc\two.asm"
#include <inc/one.asm>
#include inc/missing.asm
#include inc/three.asm
#include inc/one.asm
  #include inc/four.asm
#INCLUDE inc/four.asm
#include inc/four.asm ; comment
#ORG 0x1000
.main   CALL .one
        CALL .two
        LD ABC, .three
