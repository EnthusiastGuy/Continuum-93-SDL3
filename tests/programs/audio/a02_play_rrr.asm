; steps: 500
; PLAY rrr (24-bit register view, operand byte masked to 5 bits) in a loop, and an unknown PLAY
; sub-op, which the reference skips (2 bytes consumed, nothing else).
#ORG 0x080000
    LD XYZ, .Saw
    LD B, 5
.Loop
    PLAY XYZ
    PLAY ZAB
    DJNZ B, .Loop
    #DB 160, 7
    LD C, 0x42
    LD DEF, .Saw
    PLAY DEF
    LD (0x100000), C
    BREAK
.Saw
    #DB 0b10000000, 0b00000000, 523.25, 0.05, 0.5, 7
