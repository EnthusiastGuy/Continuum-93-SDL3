; steps: 100
; PLAY with an invalid wave type: XSoundParamsProcessing throws ArgumentException; the machine halts (D-16).
#ORG 0x080000
    LD A, 1
    PLAY .Bad
    LD A, 2
    BREAK
.Bad
    #DB 0b10000000, 0b00000000, 440.0, 0.1, 1.0, 22
