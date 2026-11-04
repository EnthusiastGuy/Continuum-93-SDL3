; steps: 600000
; Sieve of Eratosthenes over 0x2000 bytes at 0x100000: pure CPU, loops, memory, compares.
#ORG 0x080000
    LD A, 1
    LD DEF, 2
.Outer
    LD KLM, 0x100000
    ADD KLM, DEF
    LD G, (KLM)
    CP G, 0
    JP NE, .Next
    LD HIJ, DEF
    ADD HIJ, DEF
.Mark
    CP HIJ, 0x2000
    JP GTE, .Next
    LD KLM, 0x100000
    ADD KLM, HIJ
    LD (KLM), A
    ADD HIJ, DEF
    JP .Mark
.Next
    INC DEF
    CP DEF, 0x2000
    JP LT, .Outer
    BREAK
