; Branches, flags, relative/absolute labels.
#ORG 0x80000
.top    NOP
        JP .top
        JP Z, .top
        JP nz, .end
        JP C, 0x80000
        JP 0x80000
        JP (.top)
        JP .nowhere
        JR .top
        JR NZ, .end
        JR ~top
        JR ~end
        JR 5
        JR -5
        CALL .top
        CALL EQ, .end
        CALL (ABC)
        CALL ABC
        CALLR .end
        CALLR GTE, .top
        CALLR ~top
        DJNZ A, .top
        DJNZ16 AB, .top
        SETF Z
        RESF C
        INVF NO
        SETF lte
        SETF 5
        RETIF EQ
        RETIF .top
        RET
        RETI
        NOP
        DEBUG
        BREAK
        HALT
        DR
.end    RET
