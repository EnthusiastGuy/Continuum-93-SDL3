; steps: 500
; PLAY nnn: definitions with every optional block; registers, flags and RAM must be untouched.
#ORG 0x080000
    LD ABCD, 0x11223344
    LD EF, 0x5566
    PLAY .Sine
    PLAY .Complex
    PLAY .Everything
    ADD A, 1
    PLAY .Boot
    LD (0x100000), ABCD
    LD (0x100004), EF
    BREAK
.Sine
    #DB 0b00000000, 0b00000000, 440.0, 0.1, 1.0
.Complex
    #DB 0b10000000, 0b00000000, 440.0, 0.1, 1.0, 100, 3, 1, 10, 2, 20, 3, 30
.Everything
    #DB 0b11111111, 0b11111100, 300.0, 0.05, 0.7, 7, 0.01, 0.05, 0.4, 100.0, 1.0, 0.0, 0.2, 0.3, 0.1, 0.5, 0.3, 0.2, 0.4, 0.1, 0.05, 0.5, 0.2, 0.6, 0.1, 0.3, 0.2, 0.1, 8, 22050.0
.Boot
    #DB 0b11000001, 0b01000000, 700.0, 0.05, 0.1, 3, 0.0, 0.05, 0.5, 0.5, 0.9
