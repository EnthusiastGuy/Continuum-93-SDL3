; Two labels on one line: collection stops there, and the line's mnemonic is ".a .b NOP".
#ORG 0x1000
.first  NOP
.a .b   NOP
.later  NOP
        LD ABC, .first
        LD ABC, .later
