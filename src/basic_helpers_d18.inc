// D-18: the runtime string routines the fixed compiler emits (basic_helpers.inc keeps the reference's,
// which fault). {name, helpers it calls, text}; lines joined with \n, written as \r\n.
// Convention: BCD = string, EFG = second string, HIJ = result buffer (32 bytes), KLMN = number (also the
// numeric result), OPQR = second number. A routine may change A..U, never V..Z; the call site saves what
// it needs. A result holds 31 characters at most (its buffer); longer ones are cut.
    {"StrCopy", "", R"(.StrCopy
    ; BCD = source, HIJ = destination, S = most characters; leaves HIJ on the 0 it writes
    LD A, (BCD)
    CP A, 0
    JR Z, .StrCopy_end
    CP S, 0
    JR Z, .StrCopy_end
    LD (HIJ), A
    INC BCD
    INC HIJ
    DEC S
    JR .StrCopy
.StrCopy_end
    LD A, 0
    LD (HIJ), A
    RET
)"},
    {"StrClamp", "", R"(.StrClamp
    ; S = KLMN limited to 0..31 (signed)
    LD S, 31
    SCP KLMN, 31
    RETIF GTE
    LD S, N
    SCP KLMN, 0
    RETIF GTE
    LD S, 0
    RET
)"},
    {"StrSkip", "", R"(.StrSkip
    ; BCD moves on by KLMN characters, never past the end of the string
    SCP KLMN, 0
    RETIF LTE
    LD A, (BCD)
    CP A, 0
    RETIF Z
    INC BCD
    DEC KLMN
    JR .StrSkip
)"},
    {"StrSpaces", "", R"(.StrSpaces
    ; BCD moves past leading blanks (space and control characters)
    LD A, (BCD)
    CP A, 0
    RETIF Z
    CP A, 0x21
    RETIF GTE
    INC BCD
    JR .StrSpaces
)"},
    {"StrLen", "", R"(.StrLen
    ; Input: BCD = string; output: KLMN = its length
    LD KLMN, 0
.StrLen_loop
    LD A, (BCD)
    CP A, 0
    RETIF Z
    INC KLMN
    INC BCD
    JR .StrLen_loop
)"},
    {"StrCat", "StrCopy", R"(.StrCat
    ; Input: BCD = first string, EFG = second string, HIJ = result buffer
    LD S, 31
    CALL .StrCopy
    LD BCD, EFG
    JP .StrCopy
)"},
    {"StrLeft", "StrClamp StrCopy", R"(.StrLeft
    ; Input: BCD = string, KLMN = number of characters, HIJ = result buffer
    CALL .StrClamp
    JP .StrCopy
)"},
    {"StrRight", "StrLen StrSkip StrCopy", R"(.StrRight
    ; Input: BCD = string, KLMN = number of characters, HIJ = result buffer
    LD OPQR, KLMN
    LD EFG, BCD
    CALL .StrLen
    LD BCD, EFG
    SUB KLMN, OPQR  ; Characters to skip
    CALL .StrSkip
    LD S, 31
    JP .StrCopy
)"},
    {"StrMid", "StrSkip StrClamp StrCopy", R"(.StrMid
    ; Input: BCD = string, KLMN = start (1-based), OPQR = length (negative: to the end), HIJ = result buffer
    DEC KLMN
    CALL .StrSkip
    LD S, 31
    LD KLMN, OPQR
    SCP KLMN, 0
    JR LT, .StrCopy
    CALL .StrClamp
    JP .StrCopy
)"},
    {"StrChr", "", R"(.StrChr
    ; Input: KLMN = character code, HIJ = result buffer
    LD (HIJ), N
    INC HIJ
    LD A, 0
    LD (HIJ), A
    RET
)"},
    {"StrAsc", "", R"(.StrAsc
    ; Input: BCD = string; output: KLMN = code of its first character (0 if empty)
    LD KLMN, 0
    LD N, (BCD)
    RET
)"},
    {"StrVal", "StrSpaces", R"(.StrVal
    ; Input: BCD = string; output: KLMN = the integer it starts with (" -12x" -> -12, none -> 0)
    CALL .StrSpaces
    LD KLMN, 0
    LD T, (BCD)  ; Sign
    CP T, 0x2D
    JR NZ, .StrVal_digit
    INC BCD
.StrVal_digit
    LD A, (BCD)
    SUB A, 0x30
    CP A, 10
    JR GTE, .StrVal_sign
    MUL KLMN, 10
    ADD KLMN, A
    INC BCD
    JR .StrVal_digit
.StrVal_sign
    CP T, 0x2D
    RETIF NZ
    INV KLMN
    INC KLMN
    RET
)"},
    {"StrString", "StrClamp StrCopy", R"(.StrString
    ; Input: KLMN = count, EFG = string whose first character repeats, HIJ = result buffer
    CALL .StrClamp
    LD A, (EFG)
    CP A, 0
    JR Z, .StrCopy_end
.StrString_loop
    CP S, 0
    JR Z, .StrCopy_end
    LD (HIJ), A
    INC HIJ
    DEC S
    JR .StrString_loop
)"},
    {"StrInstr", "StrSkip", R"(.StrInstr
    ; Input: KLMN = start (1-based), BCD = string, EFG = string to find; output: KLMN = position (0: none)
    SCP KLMN, 1
    JR GTE, .StrInstr_from
    LD KLMN, 1
.StrInstr_from
    LD OPQR, KLMN  ; Position of BCD
    DEC KLMN
    CALL .StrSkip
    LD A, (EFG)
    CP A, 0
    JR Z, .StrInstr_none  ; Nothing to find
.StrInstr_try
    LD A, (BCD)
    CP A, 0
    JR Z, .StrInstr_none
    LD HIJ, BCD
    LD STU, EFG
.StrInstr_cmp
    LD A, (STU)
    CP A, 0
    JR Z, .StrInstr_found
    CP A, (HIJ)
    JR NZ, .StrInstr_next
    INC HIJ
    INC STU
    JR .StrInstr_cmp
.StrInstr_next
    INC BCD
    INC OPQR
    JR .StrInstr_try
.StrInstr_found
    LD KLMN, OPQR
    RET
.StrInstr_none
    LD KLMN, 0
    RET
)"},
    {"StrUcase", "StrCase", R"(.StrUcase
    ; Input: BCD = string, HIJ = result buffer
    LD T, 0x61  ; 'a'
    LD U, 0x7A  ; 'z'
    JP .StrCase
)"},
    {"StrLcase", "StrCase", R"(.StrLcase
    ; Input: BCD = string, HIJ = result buffer
    LD T, 0x41  ; 'A'
    LD U, 0x5A  ; 'Z'
    JP .StrCase
)"},
    {"StrCase", "", R"(.StrCase
    ; Copies BCD to HIJ (31 characters at most), flipping the case bit of letters in T..U
    LD S, 31
.StrCase_loop
    LD A, (BCD)
    CP A, 0
    JR Z, .StrCase_end
    CP S, 0
    JR Z, .StrCase_end
    CP A, T
    JR LT, .StrCase_put
    CP A, U
    JR GT, .StrCase_put
    XOR A, 0x20
.StrCase_put
    LD (HIJ), A
    INC BCD
    INC HIJ
    DEC S
    JR .StrCase_loop
.StrCase_end
    LD A, 0
    LD (HIJ), A
    RET
)"},
    {"StrLtrim", "StrSpaces StrCopy", R"(.StrLtrim
    ; Input: BCD = string, HIJ = result buffer
    CALL .StrSpaces
    LD S, 31
    JP .StrCopy
)"},
    {"StrRtrim", "StrCopy", R"(.StrRtrim
    ; Input: BCD = string, HIJ = result buffer
    LD EFG, HIJ  ; Start of the result
    LD S, 31
    CALL .StrCopy
.StrRtrim_back
    CP HIJ, EFG
    JR Z, .StrCopy_end
    DEC HIJ
    LD A, (HIJ)
    CP A, 0x21
    JR LT, .StrRtrim_back
    INC HIJ
    JP .StrCopy_end
)"},
    {"StrTrim", "StrSpaces StrRtrim", R"(.StrTrim
    ; Input: BCD = string, HIJ = result buffer
    CALL .StrSpaces
    JP .StrRtrim
)"},
