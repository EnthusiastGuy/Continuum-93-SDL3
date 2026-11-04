// Generated from the reference's Emitter.EmitStringHelper by scratch tools/helpers.py: the runtime string
// routines, verbatim (lines joined with \n; the emitter writes \r\n as AppendLine did). Do not edit.
    {"StrLen", R"(.StrLen
    ; Input: FGH = string address (24-bit)
    ; Output: A = string length (8-bit)
    PUSH B, U  ; Preserve registers
    LD A, 0  ; Initialize length counter
    LD BCD, FGH  ; Copy string address
.StrLen_loop
    LD B, (BCD)  ; Load byte from string
    CP B, 0  ; Check for null terminator
    JR Z, .StrLen_done
    INC A  ; Increment length
    INC BCD  ; Move to next byte
    JP .StrLen_loop
.StrLen_done
    POP B, U  ; Restore registers
    RET
)"},
    {"StrCat", R"(.StrCat
    ; Input: FGH = first string address, IJK = second string address, LMN = result buffer
    ; Concatenates two strings into result buffer
    PUSH A, U  ; Preserve registers
    ; Copy first string
    LD BCD, FGH  ; Source
    LD EFG, LMN  ; Destination
.StrCat_copy1
    LD A, (BCD)  ; Load byte
    LD (EFG), A  ; Store byte
    CP A, 0  ; Check for null
    JR Z, .StrCat_copy2
    INC BCD  ; Next source byte
    INC EFG  ; Next dest byte
    JP .StrCat_copy1
.StrCat_copy2
    ; Copy second string (EFG already points to end of first string)
    LD BCD, IJK  ; Source
.StrCat_copy2_loop
    LD A, (BCD)  ; Load byte
    LD (EFG), A  ; Store byte
    CP A, 0  ; Check for null
    JR Z, .StrCat_done
    INC BCD  ; Next source byte
    INC EFG  ; Next dest byte
    JP .StrCat_copy2_loop
.StrCat_done
    POP A, U  ; Restore registers
    RET
)"},
    {"StrLeft", R"(.StrLeft
    ; Input: FGH = string address, BCDE = number of characters, IJK = result buffer
    PUSH A, U  ; Preserve registers
    LD BCD, FGH  ; Source string
    LD EFG, IJK  ; Destination buffer
    LD HI, 0  ; Character counter
.StrLeft_loop
    CP HI, CD  ; Compare counter with count
    JR GTE, .StrLeft_done
    LD A, (BCD)  ; Load character
    CP A, 0  ; Check for null
    JR Z, .StrLeft_done
    LD (EFG), A  ; Store character
    INC BCD  ; Next source
    INC EFG  ; Next dest
    INC HI  ; Increment counter
    JP .StrLeft_loop
.StrLeft_done
    LD A, 0  ; Null terminator
    LD (EFG), A
    POP A, U  ; Restore registers
    RET
)"},
    {"StrRight", R"(.StrRight
    ; Input: FGH = string address, BCDE = number of characters, IJK = result buffer
    PUSH A, U  ; Preserve registers
    ; First, find string length
    LD BCD, FGH  ; String address
    LD HI, 0  ; Length counter
.StrRight_len
    LD A, (BCD)  ; Load byte
    CP A, 0  ; Check for null
    JR Z, .StrRight_start
    INC HI  ; Increment length
    INC BCD  ; Next byte
    JP .StrRight_len
.StrRight_start
    ; Calculate start position: length - count
    LD JK, HI  ; Length
    SUB JK, CD  ; Subtract count
    ; If result < 0, start at 0
    CP JK, 0
    JR LT, .StrRight_start0
    JP .StrRight_copy
.StrRight_start0
    LD JK, 0  ; Start at beginning
.StrRight_copy
    ; Copy from position JK to end
    LD BCD, FGH  ; Source string
    ADD BCD, JK  ; Add offset
    LD EFG, IJK  ; Destination
.StrRight_loop
    LD A, (BCD)  ; Load character
    LD (EFG), A  ; Store character
    CP A, 0  ; Check for null
    JR Z, .StrRight_done
    INC BCD  ; Next source
    INC EFG  ; Next dest
    JP .StrRight_loop
.StrRight_done
    POP A, U  ; Restore registers
    RET
)"},
    {"StrMid", R"(.StrMid
    ; Input: FGH = string address, BCDE = start position (1-based), EFGH = length (-1 for to end), IJK = result buffer
    PUSH A, U  ; Preserve registers
    ; Skip to start position (convert 1-based to 0-based)
    LD BCD, FGH  ; Source string
    SUB BCDE, 1  ; Convert to 0-based
    ADD BCD, BCDE  ; Add offset
    LD EFG, IJK  ; Destination
    LD HI, 0  ; Character counter
.StrMid_loop
    ; Check if we've reached the length limit
    CP EFGH, 0xFFFFFFFF  ; Check if length is -1
    JR NE, .StrMid_checklen
    JP .StrMid_copy
.StrMid_checklen
    CP HI, GH  ; Compare counter with length
    JR GTE, .StrMid_done
.StrMid_copy
    LD A, (BCD)  ; Load character
    CP A, 0  ; Check for null
    JR Z, .StrMid_done
    LD (EFG), A  ; Store character
    INC BCD  ; Next source
    INC EFG  ; Next dest
    INC HI  ; Increment counter
    JP .StrMid_loop
.StrMid_done
    LD A, 0  ; Null terminator
    LD (EFG), A
    POP A, U  ; Restore registers
    RET
)"},
    {"StrChr", R"(.StrChr
    ; Input: BCDE = ASCII code, FGH = result buffer
    PUSH A, U  ; Preserve registers
    LD A, CD  ; Get ASCII code (8-bit)
    LD (FGH), A  ; Store character
    LD A, 0  ; Null terminator
    INC FGH
    LD (FGH), A
    POP A, U  ; Restore registers
    RET
)"},
    {"StrAsc", R"(.StrAsc
    ; Input: FGH = string address
    ; Output: A = ASCII code of first character (0 if empty)
    PUSH B, U  ; Preserve registers
    LD BCD, FGH  ; String address
    LD A, (BCD)  ; Load first character
    POP B, U  ; Restore registers
    RET
)"},
    {"StrVal", R"(.StrVal
    ; Input: FGH = string address
    ; Output: BCDE = numeric value (32-bit)
    ; Note: This is a simplified implementation. Full VAL would parse floats too.
    PUSH A, U  ; Preserve registers
    LD BCD, FGH  ; String address
    LD BCDE, 0  ; Initialize result
    LD A, 1  ; Sign (1 = positive, -1 = negative)
    ; Check for negative sign
    LD HI, (BCD)  ; Load first character
    CP HI, 0x2D  ; Check for '-'
    JR NE, .StrVal_parse
    LD A, -1  ; Negative
    INC BCD  ; Skip '-'
.StrVal_parse
    LD JK, (BCD)  ; Load character
    CP JK, 0  ; Check for null
    JR Z, .StrVal_done
    ; Check if digit (0x30-0x39)
    CP JK, 0x30  ; '0'
    JR LT, .StrVal_done
    CP JK, 0x3A  ; '9' + 1
    JR GTE, .StrVal_done
    ; Convert digit to number
    SUB JK, 0x30  ; Subtract '0'
    MUL BCDE, 10  ; Multiply result by 10
    ADD BCDE, JK  ; Add digit
    INC BCD  ; Next character
    JP .StrVal_parse
.StrVal_done
    ; Apply sign
    CP A, -1
    JR NE, .StrVal_ret
    NEG BCDE  ; Negate result
.StrVal_ret
    POP A, U  ; Restore registers
    RET
)"},
    {"StrString", R"(.StrString
    ; Input: BCDE = number of repetitions, FGH = character string address, IJK = result buffer
    PUSH A, U  ; Preserve registers
    ; Get first character from string
    LD BCD, FGH  ; Character string
    LD A, (BCD)  ; Load first character
    LD EFG, IJK  ; Result buffer
    LD HI, 0  ; Counter
.StrString_loop
    CP HI, CD  ; Compare counter with count
    JR GTE, .StrString_done
    LD (EFG), A  ; Store character
    INC EFG  ; Next position
    INC HI  ; Increment counter
    JP .StrString_loop
.StrString_done
    LD A, 0  ; Null terminator
    LD (EFG), A
    POP A, U  ; Restore registers
    RET
)"},
    {"StrInstr", R"(.StrInstr
    ; Input: BCDE = start position (1-based), FGH = first string, IJK = second string
    ; Output: BCDE = position (1-based, 0 if not found)
    PUSH A, U  ; Preserve registers
    ; Convert start to 0-based and skip to position
    LD BCD, FGH  ; First string
    SUB BCDE, 1  ; Convert to 0-based
    ADD BCD, BCDE  ; Add offset
    LD EFG, IJK  ; Second string (search string)
    LD HI, (EFG)  ; Load first character of search string
    CP HI, 0  ; Check if search string is empty
    JR Z, .StrInstr_notfound
    LD JK, BCDE  ; Save start position for result
.StrInstr_outer
    LD A, (BCD)  ; Load character from first string
    CP A, 0  ; Check for end of string
    JR Z, .StrInstr_notfound
    CP A, HI  ; Compare with first char of search
    JR NE, .StrInstr_next
    ; Potential match - check rest of search string
    LD LMN, BCD  ; Save position in first string
    LD OPQ, EFG  ; Save position in search string
    INC LMN  ; Next char in first string
    INC OPQ  ; Next char in search string
.StrInstr_match
    LD A, (OPQ)  ; Load from search string
    CP A, 0  ; Check for end of search
    JR Z, .StrInstr_found
    LD R, (LMN)  ; Load from first string
    CP R, 0  ; Check for end of first string
    JR Z, .StrInstr_notfound
    CP A, R  ; Compare characters
    JR NE, .StrInstr_next
    INC LMN  ; Next in first
    INC OPQ  ; Next in search
    JP .StrInstr_match
.StrInstr_found
    ; Found! Calculate 1-based position
    SUB BCD, FGH  ; Calculate offset
    ADD BCDE, 1  ; Convert to 1-based
    LD BCDE, BCD  ; Result
    POP A, U  ; Restore registers
    RET
.StrInstr_next
    INC BCD  ; Next position in first string
    INC JK  ; Increment position counter
    JP .StrInstr_outer
.StrInstr_notfound
    LD BCDE, 0  ; Not found
    POP A, U  ; Restore registers
    RET
)"},
    {"StrUcase", R"(.StrUcase
    ; Input: FGH = string address, IJK = result buffer
    PUSH A, U  ; Preserve registers
    LD BCD, FGH  ; Source
    LD EFG, IJK  ; Destination
.StrUcase_loop
    LD A, (BCD)  ; Load character
    CP A, 0  ; Check for null
    JR Z, .StrUcase_done
    ; Check if lowercase letter (a-z: 0x61-0x7A)
    CP A, 0x61  ; 'a'
    JR LT, .StrUcase_store
    CP A, 0x7B  ; 'z' + 1
    JR GTE, .StrUcase_store
    SUB A, 0x20  ; Convert to uppercase
.StrUcase_store
    LD (EFG), A  ; Store character
    INC BCD  ; Next source
    INC EFG  ; Next dest
    JP .StrUcase_loop
.StrUcase_done
    LD A, 0  ; Null terminator
    LD (EFG), A
    POP A, U  ; Restore registers
    RET
)"},
    {"StrLcase", R"(.StrLcase
    ; Input: FGH = string address, IJK = result buffer
    PUSH A, U  ; Preserve registers
    LD BCD, FGH  ; Source
    LD EFG, IJK  ; Destination
.StrLcase_loop
    LD A, (BCD)  ; Load character
    CP A, 0  ; Check for null
    JR Z, .StrLcase_done
    ; Check if uppercase letter (A-Z: 0x41-0x5A)
    CP A, 0x41  ; 'A'
    JR LT, .StrLcase_store
    CP A, 0x5B  ; 'Z' + 1
    JR GTE, .StrLcase_store
    ADD A, 0x20  ; Convert to lowercase
.StrLcase_store
    LD (EFG), A  ; Store character
    INC BCD  ; Next source
    INC EFG  ; Next dest
    JP .StrLcase_loop
.StrLcase_done
    LD A, 0  ; Null terminator
    LD (EFG), A
    POP A, U  ; Restore registers
    RET
)"},
    {"StrTrim", R"(.StrTrim
    ; Input: FGH = string address, IJK = result buffer
    ; Removes leading and trailing whitespace
    PUSH A, U  ; Preserve registers
    ; First, find start (skip leading whitespace)
    LD BCD, FGH  ; String address
.StrTrim_skip_lead
    LD A, (BCD)  ; Load character
    CP A, 0  ; Check for null
    JR Z, .StrTrim_empty
    CP A, 0x20  ; Space
    JR Z, .StrTrim_skip_lead_inc
    CP A, 0x09  ; Tab
    JR Z, .StrTrim_skip_lead_inc
    CP A, 0x0A  ; Newline
    JR Z, .StrTrim_skip_lead_inc
    CP A, 0x0D  ; Carriage return
    JR Z, .StrTrim_skip_lead_inc
    JP .StrTrim_copy
.StrTrim_skip_lead_inc
    INC BCD  ; Skip whitespace
    JP .StrTrim_skip_lead
.StrTrim_copy
    ; Find end of string (for trailing whitespace)
    LD EFG, BCD  ; Start of non-whitespace
    LD HI, BCD  ; End pointer
    LD JK, BCD  ; Last non-whitespace
.StrTrim_find_end
    LD A, (HI)  ; Load character
    CP A, 0  ; Check for null
    JR Z, .StrTrim_found_end
    CP A, 0x20  ; Space
    JR Z, .StrTrim_find_end_inc
    CP A, 0x09  ; Tab
    JR Z, .StrTrim_find_end_inc
    CP A, 0x0A  ; Newline
    JR Z, .StrTrim_find_end_inc
    CP A, 0x0D  ; Carriage return
    JR Z, .StrTrim_find_end_inc
    LD JK, HI  ; Update last non-whitespace
.StrTrim_find_end_inc
    INC HI  ; Next character
    JP .StrTrim_find_end
.StrTrim_found_end
    ; Copy from EFG to JK
    LD LMN, IJK  ; Destination
.StrTrim_copy_loop
    CP EFG, JK  ; Compare with end
    JR GT, .StrTrim_done
    LD A, (EFG)  ; Load character
    LD (LMN), A  ; Store character
    INC EFG  ; Next source
    INC LMN  ; Next dest
    JP .StrTrim_copy_loop
.StrTrim_empty
    LD EFG, IJK  ; Destination
    LD A, 0  ; Null terminator
    LD (EFG), A
    POP A, U  ; Restore registers
    RET
.StrTrim_done
    LD A, 0  ; Null terminator
    LD (LMN), A
    POP A, U  ; Restore registers
    RET
)"},
    {"StrLtrim", R"(.StrLtrim
    ; Input: FGH = string address, IJK = result buffer
    ; Removes leading whitespace
    PUSH A, U  ; Preserve registers
    LD BCD, FGH  ; Source
    ; Skip leading whitespace
.StrLtrim_skip
    LD A, (BCD)  ; Load character
    CP A, 0  ; Check for null
    JR Z, .StrLtrim_done
    CP A, 0x20  ; Space
    JR Z, .StrLtrim_skip_inc
    CP A, 0x09  ; Tab
    JR Z, .StrLtrim_skip_inc
    CP A, 0x0A  ; Newline
    JR Z, .StrLtrim_skip_inc
    CP A, 0x0D  ; Carriage return
    JR Z, .StrLtrim_skip_inc
    JP .StrLtrim_copy
.StrLtrim_skip_inc
    INC BCD  ; Skip whitespace
    JP .StrLtrim_skip
.StrLtrim_copy
    LD EFG, IJK  ; Destination
.StrLtrim_loop
    LD A, (BCD)  ; Load character
    LD (EFG), A  ; Store character
    CP A, 0  ; Check for null
    JR Z, .StrLtrim_done
    INC BCD  ; Next source
    INC EFG  ; Next dest
    JP .StrLtrim_loop
.StrLtrim_done
    POP A, U  ; Restore registers
    RET
)"},
    {"StrRtrim", R"(.StrRtrim
    ; Input: FGH = string address, IJK = result buffer
    ; Removes trailing whitespace
    PUSH A, U  ; Preserve registers
    LD BCD, FGH  ; Source
    LD EFG, IJK  ; Destination
    LD HI, EFG  ; Last non-whitespace position
.StrRtrim_loop
    LD A, (BCD)  ; Load character
    CP A, 0  ; Check for null
    JR Z, .StrRtrim_done
    LD (EFG), A  ; Store character
    CP A, 0x20  ; Space
    JR Z, .StrRtrim_next
    CP A, 0x09  ; Tab
    JR Z, .StrRtrim_next
    CP A, 0x0A  ; Newline
    JR Z, .StrRtrim_next
    CP A, 0x0D  ; Carriage return
    JR Z, .StrRtrim_next
    LD HI, EFG  ; Update last non-whitespace
.StrRtrim_next
    INC BCD  ; Next source
    INC EFG  ; Next dest
    JP .StrRtrim_loop
.StrRtrim_done
    ; Null terminate at last non-whitespace
    INC HI  ; After last non-whitespace
    LD A, 0  ; Null terminator
    LD (HI), A
    POP A, U  ; Restore registers
    RET
)"},
