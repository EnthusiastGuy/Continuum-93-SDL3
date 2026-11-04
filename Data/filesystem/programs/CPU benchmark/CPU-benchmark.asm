#include ..\..\lib\c93-keyboard.asm

	; Assemble this code starting at this address so it doesn't interfere with the OS
	#ORG 0x80000

	LD A, 0x02 	; SetVideoPagesCount interrupt
	LD B, 1 	; set a single video page
	INT 0x01, A ; Trigger interrupt Video with SetVideoPagesCount

	CALLR .ClearPage
	
	; Loading a font to be used
    LD A, 0x40
    LD BCD, .FontPath
    LD EFG, .FontData
    INT 0x04, A

	LDREGS A, R, (.DrawTextDrawParams)
	LD JK, 10
	LD EFG, .WaitMessage
	INT 0x01, A



	; Record the time at the start of the benchmark (100 ns ticks)
	LD A, 0x03
	LD B, 0x01
	LD CDE, .StartTime
	INT 0x00, A

	; Run the benchmark 16 million times for accuracy
	LD XYZ, 16000000

; We create a simple benchmark that runs some simple instructions
; for which we know what the CPU cycles are, resulting in a
; completely measurable execution cycles total. Cycles are those of
; real hardware: an in-order, single-issue pipelined CPU with an
; on-chip cache, the i486 class of 1993 (register ops 1 cycle, a taken
; branch 3). The MHz shown is the clock such a chip would need to
; match this machine. The loop below takes 6 cycles per run
.BenchLoop
	LD XYZ, XYZ			; 1 cycle
	DEC XYZ				; 1 cycle
	CP XYZ, 0			; 1 cycle
	JR GT, .BenchLoop	; 3 cycles (taken)
	
	; Record the time at the end of the benchmark
	LD A, 0x03
	LD B, 0x01
	LD CDE, .EndTime
	INT 0x00, A

	; Elapsed ticks: the low 32 bits are enough (they wrap after 7 minutes)
	LD ABCD, (.EndTimeLow)
	LD EFGH, (.StartTimeLow)

	; MHz = cycles / (ticks x 100 ns) / 1 000 000 = cycles x 10 / ticks
	LD F0, 960000000.0	; 6 cycles x 16 million runs x 10

	SUB ABCD, EFGH

	DIV F0, ABCD
	
	; Convert to string
	LD A, 0x01
	LD B, 0
	LD CDE, .NumberFormat
	LD FGH, .TextString
	INT 0x05, A

	CALLR .ClearPage

	; Draw a nice rectangle
	LD A, 0x07
	LD B, 0
	LD CD, 80
	LD EF, 64
	LD GH, 320
	LD IJ, 100
	LD K, 0x06
	INT 0x01, A

	; Get the CPU designation name into the .CPUDesignation buffer
	LD A, 0xF0
	LD BC, F0
	LD DEF, .CPUDesignation
	INT 0x00, A

	; DrawText returns the text's box in its parameter registers, so reload them for every line
	LDREGS A, R, (.DrawTextDrawParams)
	INT 0x01, A

	LDREGS A, R, (.DrawTextDrawParams)
	LD JK, 116
	LD EFG, .CPUDesignation
	INT 0x01, A

	LDREGS A, R, (.DrawTextDrawParams)
	LD JK, 10
	LD EFG, .DoneMessage
	INT 0x01, A

	LDREGS A, R, (.DrawTextDrawParams)
	LD JK, 250
	LD EFG, .ExitMessage
	INT 0x01, A

.HandleEscWait
    CALLR .InputUpdate

    LD A, 27	; Esc key
	CALLR .InputKeyPressed
	CP A, 1

    RETIF Z

    WAIT 16
    JP .HandleEscWait
	
.ClearPage
	LD Z, 0x05 	; ClearVideoPage
	LD AB, 0x00DA	; Clear video page 0 (background) with a solid color
	INT 0x01, Z ; Trigger interrupt Video
	RET
	
.WaitMessage
	#DB "Please wait a moment", 0

.DoneMessage
	#DB "Benchmark done", 0

.NumberFormat
	#DB "'CPU Frequency:' 0.00 'MHz'", 0

.TextString
	#DB [40] 0

.CPUDesignation
	#DB [40] 0

.ExitMessage
	#DB "Press ESC to exit", 0

.DrawTextDrawParams
    ; DrawText, font data, text, x, y, color, video page, max width, flags, outline color, outline pattern
    #DB 0x14, .FontData, .TextString, 0080, 00100, 0x00, 0x00, 00320, 0b00111000, 0xFF, 0b11111111
	
.StartTime
	#DB 0x00000000
.StartTimeLow
	#DB 0x00000000

.EndTime
	#DB 0x00000000
.EndTimeLow
	#DB 0x00000000
	
.OffsetTime
	#DB 0x00000000

.FontPath
	#DB "programs\CPU benchmark\font.png", 0

.FontData

	#DB [5000] 0x00
