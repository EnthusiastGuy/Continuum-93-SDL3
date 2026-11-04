; Directives: #ORG / #RUN / #DB in every spelling the reference accepts (and some it does not).
.pre    LD A, 1             ; before the first #ORG: takes addresses from 0, never emitted
        #DB 1, 2, 3
#ORG 0x2000
.start  LD BCD, .pre
        LD EFG, .post
#RUN 0x2010
#ORG
        NOP
#ORG 0b1000000000000000
        #DB 0x01, 0x0001, 0x000001, 0x00000001, 0x0000000001, 0x1, 0x123, 0x12345, 0x1234567
        #DB 0, 00, 000, 0000, 00000, 1, 255, 256, 65535, 65536, 16777215, 16777216, 4294967295, 01, 0255
        #DB -1, -255, -256, -65536, -16777216, -0x10, -0x1000, -0b1, -0o7, -007, +5, +05
        #DB 0o0, 0o00, 0o000, 0o0000, 0o00000, 0o7, 0o777, 0o777777, 0o37777777777
        #DB 0b1, 0b000000001, 0b11111111111111111, 0B101, 0b, 0x, 0o, 0b2, 0xG
        #DB 1.5, -0.25, 3e2, .5, 5., 1e39, -1e-50, 1_000, 0xFF_FF, 0b1111_0000, 1__2
        #DB "Hello", "a;b", "", "x", "  spaced  ", "tab	in"
        #DB [3] 0x1, [2] "ab", [0x2] 0x0102, [1_0] 7, [0] 1, [5] .start, [2]3, [0x1000000] 1, [3] 1.5
        #DB .start, .post, .pre
        #db 9
        #DB 1 ,2 , 3 ,
        #DB "quote "" inside", "a" junk, 4, ,5
        #DB     ; nothing
        #DB ""
        #DB ","
        #DB ";"
        #DB "unterminated
        #DB start, 1
#RUN 0
#ORG 0x3000
.post   RET
#run 5
#ORG .start
        NOP
#BOGUS 1
#Org 0x4000
#ORG 0x5000 ; trailing comment
        #DB "after"
