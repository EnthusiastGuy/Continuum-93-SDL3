; Lines the reference counts as errors ("Unrecognized parameter nature"), one count per line.
#include inc/err-inc.asm
#ORG 0x1000
        LD A, ZZ
        LD A, ZZ, YY
        ADD QQQQQ, 1
        LD A, #5
        NOP
        LD A, 1_0
        CP A, @
        LD A, 5
