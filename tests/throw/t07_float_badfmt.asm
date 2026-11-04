; steps: 100
#ORG 0x080000
    LD A, 0x01
    LD B, 0
    LD CDE, .P
    LD FGH, 0x200000
    INT 0x05, A
    BREAK
.P
    #DB 0x44, 0x00
