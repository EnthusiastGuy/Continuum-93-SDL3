; #DB value spellings: float specials, hex/octal widths, the CSV split quirks, non-ASCII strings.
#ORG 0x100
        #DB ,1
        #DB 1,,2
        #DB "a,b" 7, 3
        #DB "a""b", 4
        #DB "a"b", 5
        #DB 1 , 2
        #DB
        #DB "é😀x"
        #DB [2] "é😀x"
        #DB +5
        #DB 00
        #DB -1
        #DB -256
        #DB 0x1_0
        #DB infinity
        #DB 1e-50
        #DB 1e
        #DB 5.
        #DB .5e1
        #DB -∞
        #DB -0.0
        #DB 1e+5
        #DB INFINITY
        #DB -Infinity
        #DB +Infinity
        #DB NaN
        #DB -NaN
        #DB +nan
        #DB ∞
        #DB 1e39
        #DB 0x000000001
        #DB 0x00000000001
        #DB 0x1FFFFFFFF
        #DB 0o37777777777
        #DB 0o0
