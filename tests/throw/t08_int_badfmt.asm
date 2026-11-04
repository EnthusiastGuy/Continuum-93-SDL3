; steps: 100
#ORG 0x080000
    LD A, 0x03
    LD BCDE, 5
    LD FGH, .P
    LD IJK, 0x200000
    INT 0x05, A
    BREAK
.P
    #DB 0x51, 0x00
