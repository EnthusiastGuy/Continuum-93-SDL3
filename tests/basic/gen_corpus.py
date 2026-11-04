# Writes the hand-made BASIC corpus: one file per feature / error path.  python -I gen_corpus.py tests/basic
import os, sys
out = sys.argv[1]
C = {}

C['empty'] = ''
C['blank_lines'] = '\n\n   \n'
C['only_comment'] = "' just a comment\nREM another\n"
C['only_label'] = 'Start:\n'
C['two_labels'] = 'A:\nB:\n'
C['colon_only'] = ':\n'
C['crlf_mix'] = 'x = 1\r\ny = 2\rz = 3\nPRINT x\r\n'
C['lower_case'] = 'let a = 5\nprint a\nif a > 1 then\n  print "big"\nend if\nend\n'

C['assign_int'] = 'x = 10\ny = x\nz = -5\nw = 0\nbig = 2147483647\nneg = -2147483648\n'
C['assign_hex_bin'] = 'a = 0xFF\nb = 0X10\nc = 0b1010\nd = 0B11\ne1 = 0xFFFFFFFF\nf = 0x7FFFFFFF\n'
C['assign_float'] = 'f# = 1.5\ng# = -2.25\nh# = 1e3\ni# = 3.14159\nj# = 0.1\nk# = 1E-5\nl# = 2.5e+2\nm# = 1.\n'
C['assign_float_mixed'] = 'f# = 1.5\nf# = f# + 2.5\nx = f#\nf# = x\n'
C['assign_string'] = 'a$ = "Hello"\nb$ = a$\na$ = "World"\nc$ = ""\nd$ = "say \\"hi\\""\ne$ = "tab\\there\\nnew\\rcr\\\\back\\q"\n'
C['string_reuse'] = 'PRINT "same"\nPRINT "same"\nx$ = "same"\ny$ = "other"\ny$ = "same"\n'
C['let_forms'] = 'LET a = 1\nLET b$ = "x"\nLET c# = 2.0\nLET d = a + 1\n'
C['case_sensitive_vars'] = 'x = 1\nX = 2\nPRINT x\nPRINT X\n'
C['names'] = 'my_var = 1\nv2 = my_var\na#b = 3\nq$$ = "z"\nr#$ = "y"\n'

C['arith'] = 'a = 1 + 2\nb = 5 * 3\nc = 10 / 2\nd = 9 \\ 2\ne1 = 10 MOD 3\nf = 2 ^ 8\ng = a + b * c - d\nh = (a + b) * (c + d)\n'
C['arith_vars'] = 'a = 3\nb = 4\nc = a * a + b * b\nd = c / a\ne1 = c MOD b\nf = a ^ b ^ 2\n'
C['minus_quirk'] = 'a = 5\nb = a-1\nc = a - 1\nd = a -1\n'
C['unary'] = 'a = 5\nb = - a\nc = NOT a\nd = -(a + 1)\ne1 = NOT NOT a\nf = - - a\n'
C['compare'] = 'a = 1\nb = 2\nc = a = b\nd = a <> b\ne1 = a < b\nf = a > b\ng = a <= b\nh = a >= b\n'
C['logical'] = 'a = 1\nb = 0\nc = a AND b\nd = a OR b\ne1 = a XOR b\nf = a NAND b\ng = a NOR b\nh = a XNOR b\ni = a IMPLY b\n'
C['logical_bool'] = 'a = 1\nb = 2\nc = a < b AND b > a\nd = a < b OR b\ne1 = a XOR b > 1\nf = (a = 1) NAND (b = 2)\ng = a NOR b = 2\nh = a < 1 XNOR 5\ni = a > 0 IMPLY b > 0\n'
C['logical_chain'] = 'a = 1\nIF a > 0 AND a < 10 AND a <> 5 THEN\n  PRINT "ok"\nEND IF\nb = a = 1 OR a = 2 OR a = 3\n'
C['shifts'] = 'a = 1 SHL 4\nb = 256 SHR 2\nc = 1 ROL 31\nd = 2 ROR 1\ns = 3\ne1 = a SHL s\nf = a SHR (s + 1)\ng = a ROL s\nh = a ROR s\ni = a SHL 32\nj = a SHL -1\nk = a SHR (1 < 2)\n'
C['precedence'] = 'a = 1 + 2 * 3 SHL 1 < 20 AND 3 OR 4 XOR 5 IMPLY 6\nb = 2 ^ 3 * 4\nc = -2 ^ 2\nd = NOT 1 + 2\n'
C['register_pressure'] = 'a = 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9 + 10\nb = (1 + (2 + (3 + (4 + (5 + (6 + (7 + (8 + 9))))))))\nc = a * b * a * b * a * b * a * b\nd = (a < b) + (a > b) + (a = b)\n'
C['bool_pool'] = 'x = 1\na = x = 1\nb = x = 2\nc = x = 3\nd = x < 4\ne1 = x > 5\nf = x <= 6\ng = x >= 7\nh = x <> 8\ni = x = 9\nj = x = 10\nk = x = 11\nl = x = 12\nm = x = 13\n'
C['bool_fallback'] = 'x = 1\ny = ' + ' + '.join(f'(x = {i})' for i in range(1, 16)) + '\nz = y = 1\nw = (x < 1) - (x > 2) * (x = 3) - (x <> 4) - (x = 5) - (x = 6) - (x = 7) - (x = 8) - (x = 9) - (x = 10) - (x = 11) - (x = 12) - (x = 13)\n'
C['misc_nested'] = 'x = 1\nIF x THEN\n  DIM inner(3)\n  REPEAT\n  UNTIL 1\n  STOP\nEND IF\nFOR i = 1 TO 2\nInLoop:\n  GOTO InLoop\nNEXT\nGOTO InLoop\nGOSUB InLoop\nDIM s$(40)\nFOR f = 1.5 TO 2\nNEXT\nf = 3\nf# = f\nPRINT AT 1, 2; 1.5; 3; x\nINPUT "v"; v#\nCLS "page"\nWAIT "x"\nIF "s" THEN\nEND IF\nWHILE x = 1 AND 2\nWEND\n'
C['misc_types'] = 'a$ = "x"\na$ = a$ + a$\nb$ = 5\nc = "str"\nd# = "s"\ne$ = 1.5\nPRINT "x" + 1\nPRINT 1 + "x"\ng$ = INKEY $ + "k"\nh = LEN(INKEY $)\nIF a$ + "y" THEN\nEND IF\n'
C['float_expr'] = 'a# = 1.5\nb# = 2.5\nc# = a# + b#\nd# = a# * b# - 1.0\ne# = a# / b#\nf# = -a#\ng# = a# ^ 2.0\n'
C['string_concat'] = 'a$ = "Hello"\nb$ = " World"\nc$ = a$ + b$\nd$ = a$ + "!" + b$\nPRINT a$ + b$\ne$ = "x" + 1\n'

C['print_forms'] = 'PRINT "Hello"\nPRINT 42\nPRINT -7\nPRINT 1.5\nx = 3\nPRINT x\ns$ = "str"\nPRINT s$\nf# = 2.5\nPRINT f#\nPRINT x + 1\nPRINT "a"; "b", "c"\nPRINT "a";\nPRINT\nPRINT ;\nPRINT newvar\nPRINT newstr$\nPRINT newf#\nPRINT x * 2; s$\n'
C['print_at'] = 'PRINT AT 10, 20; "Hi"\nPRINT AT 1, 2; "a" USING 0x1000\nPRINT AT 1, 2; "b" USING 0x1000, 5\nPRINT AT 1, 2; "c" USING 0x1000, 5, 1, 100, 3, 7\nPRINT AT 1, 2; "d" USING 1, 2, 3, 4, 5\nx = 4\nPRINT AT x, x + 1; "e"; "f"\n'
C['print_tab'] = 'PRINT TAB(5); "x"\nPRINT TAB(5); "x"; "y"\nPRINT TAB(1 + 2); 5\n'
C['print_using_plain'] = 'PRINT "x" USING 1, 2\n'
C['print_errors'] = 'PRINT AT 1 2; "x"\nPRINT TAB 5; "x"\nPRINT AT 1, 2 "x"\n'

C['if_simple'] = 'x = 5\nIF x > 3 THEN\n  PRINT "big"\nEND IF\nIF x THEN\n  PRINT "nz"\nENDIF\n'
C['if_else'] = 'x = 5\nIF x > 3 THEN\n  PRINT "big"\nELSE\n  PRINT "small"\nEND IF\n'
C['if_elseif'] = 'x = 5\nIF x = 1 THEN\n  PRINT "one"\nELSEIF x = 2 THEN\n  PRINT "two"\nELSEIF x THEN\n  PRINT "other"\nELSE\n  PRINT "else"\nEND IF\n'
C['if_nested'] = 'a = 1\nb = 2\nIF a = 1 THEN\n  IF b = 2 THEN\n    PRINT "both"\n  ELSE\n    PRINT "a"\n  END IF\nELSE\n  IF b THEN\n    PRINT "b"\n  ENDIF\nEND IF\n'
C['if_single_line'] = 'x = 1\nIF x THEN PRINT "yes"\nPRINT "after"\n'
C['if_single_line_endif'] = 'x = 1\nIF x THEN PRINT "yes" END IF\nPRINT "after"\n'
C['if_empty'] = 'x = 1\nIF x THEN\nEND IF\nIF x THEN ENDIF\nIF x THEN\n'
C['if_errors'] = 'x = 1\nIF x PRINT "a"\nIF THEN\nEND IF\nELSE\nENDIF\n'
C['if_label_inside'] = 'x = 1\nIF x THEN\nInner:\n  x = x + 1\n  GOTO Inner\nEND IF\nGOTO Inner\n'

C['for_basic'] = 'FOR i = 1 TO 10\n  PRINT i\nNEXT i\nFOR j = 0 TO 5 STEP 2\n  PRINT j\nNEXT\n'
C['for_expr'] = 'n = 10\nFOR i = n - 5 TO n * 2 STEP n / 5\n  PRINT i\nNEXT i\nFOR k = 10 TO 1 STEP -1\nNEXT k\n'
C['for_float'] = 'FOR f = 0.0 TO 1.0 STEP 0.25\n  PRINT f\nNEXT\nFOR g = 1 TO 2.5\nNEXT\nFOR h = 1.5 TO 3\nNEXT\nFOR q = 1 TO 3 STEP 0.5\nNEXT\nFOR r = 0.5 TO 2 STEP 1\nNEXT\n'
C['for_float_stepexpr'] = 's# = 0.5\nFOR f = 0.0 TO 10.0 STEP s#\nNEXT\nFOR g# = 1 TO 5\nNEXT\n'
C['for_nested'] = 'FOR i = 1 TO 3\n  FOR j = 1 TO 3\n    PLOT i, j, i + j\n  NEXT j\nNEXT i\n'
C['for_reuse_var'] = 'i = 100\nFOR i = 1 TO 2\nNEXT\nFOR i = 1.5 TO 2.5\nNEXT\n'
C['for_errors'] = 'FOR = 1 TO 2\nNEXT\nFOR i 1 TO 2\nNEXT\nFOR i = 1 2\nNEXT\nFOR i = 1 TO 2\n'
C['for_exit_continue'] = 'FOR i = 1 TO 10\n  IF i = 5 THEN\n    EXIT FOR\n  END IF\n  CONTINUE FOR\nNEXT\nWHILE 1\n  EXIT WHILE\n  CONTINUE WHILE\nWEND\nEXIT\nCONTINUE\nEXIT DO\n'

C['while'] = 'x = 0\nWHILE x < 10\n  x = x + 1\nWEND\nWHILE x\n  x = x - 1\nWEND\n'
C['while_nested'] = 'a = 0\nWHILE a < 3\n  b = 0\n  WHILE b < 3\n    b = b + 1\n  WEND\n  a = a + 1\nWEND\n'
C['while_unterminated'] = 'WHILE 1\n  PRINT "x"\n'
C['repeat'] = 'x = 0\nREPEAT\n  x = x + 1\nUNTIL x > 10\n'
C['repeat_unterminated'] = 'REPEAT\n  x = 1\n'

C['goto_gosub'] = 'Main:\n  GOSUB Sub1\n  GOTO Done\nSub1:\n  PRINT "in sub"\n  RETURN\nDone:\n  GOTO Main\n  GOSUB Sub1\n  END\n'
C['goto_estimates'] = 'a = 1\nb = 2\nDIM arr(3)\nc = 3\nL1:\nd = 4\nGOTO L2\nGOTO L1\nPRINT "x"\nL2:\nGOTO L1\nGOTO L2\nGOSUB Nowhere\nGOTO Nowhere\n'
C['goto_errors'] = 'GOTO\nGOSUB 10\nGOTO "x"\n'
C['return_end_stop'] = 'RETURN\nEND\nSTOP\n'

C['dim'] = 'DIM a(10)\nDIM b#(5)\nDIM s$(3)\nDIM m(2, 3)\nDIM z(0)\nDIM n(-1)\nDIM v(x)\nDIM big(40)\n'
C['dim_use'] = 'DIM a(10)\nx = a[3]\ny = a[x + 1]\na[2] = 7\nDIM s$(2)\nt$ = s$[1]\nDIM f#(4)\ng# = f#[2]\nh = a[1, 2]\n'
C['dim_redeclare'] = 'x = 1\nDIM x(3)\nDIM x(3)\ny = x[1]\n'
C['dim_errors'] = 'DIM\nDIM a\nDIM b(1\nDIM c[2]\nDIM 5(1)\n'
C['dim_overflow'] = 'DIM a(2147483647)\nDIM b(65535, 65535)\n'
C['array_no_dim'] = 'x = q[1]\ny = r(2)\n'

C['graphics'] = 'CLS\nCLS 1\nCLS x\nPLOT 10, 20\nPLOT 10, 20, 5\nPLOT x, y, c\nPLOT x + 1, y * 2\nLINE 0, 0, 100, 100\nLINE 1, 2, 3, 4, 7\nRECTANGLE 1, 2, 30, 40\nRECTANGLE FILLED 1, 2, 30, 40, 9\nCIRCLE 50, 50, 10\nCIRCLE FILLED 50, 50, 10, 3\nELLIPSE 50, 50, 20, 10\nELLIPSE FILLED 50, 50, 20, 10, 4\n'
C['graphics_errors'] = 'PLOT 1\nLINE 1, 2, 3\nRECTANGLE 1, 2\nCIRCLE FILLED\nELLIPSE 1 2\n'
C['screen_video_ink'] = 'SCREEN 1\nVIDEO 3\nINK 15\nPAPER 2\nx = 4\nINK x + 1\nPAPER x\nSCREEN x\nVIDEO x\n'
C['font'] = 'LOAD FONT "fonts/a.png", 0x100000\nf$ = "b.png"\nLOAD FONT f$, x\nFONT 0x100000\nFONT COLOR 5\nFONT FLAGS 3\nFONT MAXWIDTH 200\nFONT OUTLINE 1, 2\n'
C['font_errors'] = 'LOAD "file.bin"\nLOAD "file.bin", 0x2000\nLOAD FONT "a" 5\nFONT OUTLINE 1\nFONT\n'
C['layer'] = 'LAYER SHOW 1\nLAYER HIDE 2\nLAYER VISIBILITY 0xFF\nx = 3\nLAYER SHOW x\nLAYER HIDE x + 1\nLAYER\nLAYER FOO\n'
C['sprite_locate'] = 'SPRITE 10, 20, 0x10000, 8, 8\nSPRITE 10, 20, 0x10000, 8, 8, 1\nx = 1\nSPRITE x, x, x, x, x, x\nLOCATE 5, 6\nLOCATE x, x + 1\nSPRITE 1, 2, 3\n'
C['audio'] = 'BEEP 0.5, 440.0\nBEEP 1, 880, 0.8\nx# = 0.2\nBEEP x#, x# * 2.0, x#\nPLAY 0x20000\nPLAY x\nBEEP 1\n'
C['wait_sleep'] = 'WAIT 10\nSLEEP 1000\nx = 5\nWAIT x\nSLEEP x * 100\n'
C['input'] = 'INPUT a$\nINPUT "Name"; n$\nINPUT "Age" a\nINPUT x\nINPUT "p"\nINPUT arr(1)\nINPUT\n'
C['memory'] = 'POKE 0x1000, 5\nPOKE16 0x1000, 0x1234\nPOKE24 0x1000, 0x123456\nPOKE32 0x1000, 0x12345678\na = PEEK(0x1000)\nb = PEEK16(0x1000)\nc = PEEK24(0x1000)\nd = PEEK32(0x1000)\nMEMCOPY 0x1000, 0x2000, 16\nMEMFILL 0x3000, 64, 0\nx = 5\ne1 = PEEK(x + 1)\nf# = PEEK(1)\n'
C['memory_errors'] = 'POKE 1\nMEMCOPY 1, 2\nMEMFILL 1\nx = PEEK 1\ny = PEEK(1\n'
C['varptr'] = 'a = 1\np = VARPTR(a)\nq = VARPTR(nothere)\ns$ = "x"\nr = VARPTR(s$)\nt = VARPTR(a(1))\nu = VARPTR 5\n'
C['time_ticks'] = 't = TIME\nu = TICKS\nv = TIME + TICKS\nPRINT TIME\n'
C['mouse'] = 'mx = MOUSE X\nmy = MOUSE Y\nb = MOUSE BUTTON 1\nc = MOUSE BUTTON (b + 1)\nIF MOUSE BUTTON 0 THEN\n  PRINT "click"\nEND IF\nx = 5\ny = 6\nPRINT MOUSE X\n'
C['mouse_x_lower'] = 'a = mouse x\nb = MOUSE y\nc = Mouse Button 2\n'
C['mouse_errors'] = 'a = MOUSE\nb = MOUSE + 1\nc = MOUSE Z\nPRINT MOUSE\n'
C['mouse_null_emit'] = 'a = MOUSE\n'
C['mouse_null_plot'] = 'PLOT MOUSE, 1\n'
C['mouse_null_sprite'] = 'SPRITE 1, 2, 3, 4, 5, MOUSE\nCLS MOUSE\n'
C['inkey'] = 'k = INKEY\nk$ = INKEY $\nj$ = INKEY$\nPRINT INKEY\nIF INKEY = 65 THEN\n  PRINT "A"\nEND IF\n'
C['data'] = 'DATA 1, 2, 3\nDATA "a", "b\\"c", 1.5, -2.25\nDATA 1e7, 1e8, 123456789.0, 0.0001, 0.00001, 16777216.0, 1e30, 1e-30, -0.0, 0.0, 3.4e38, 1e39\nDATA x, 1 + 2\nDATA 100000000.0, 1234567.0, 12345678.0, 0.001, 0.1e-3, 2.5e-5\n'
C['data_float_fmt'] = 'DATA 1.0, 10.0, 100.0, 1000000.0, 10000000.0, 99999999.0, 0.5, 0.123456789, 1.17549435e-38, 1e-45\nDATA 1e9, 1e10, 1234567890.0, 999999999.0, 4294967296.0, 1.5e9, 123456789012.0, -1e9, 0.00012345, 0.000012345\n'

C['math_funcs'] = 'a = ABS(-5)\nb = SGN(-3)\nc = INT(2.7)\nd = FIX(2.7)\ne1 = FLOOR(2.5)\nf = CEIL(2.5)\ng = ROUND(2.5)\nh = ROUND(2.5, 1)\ni# = SIN(1.0)\nj# = COS(1.0)\nk# = TAN(1.0)\nl# = SQR(16.0)\nm# = POW(2.0, 3.0)\nn = MIN(3, 4)\no = MAX(3, 4)\np = RND(10)\nq = RND()\nr# = PI\ns# = E\nt# = PI * 2.0\n'
C['math_funcs_float'] = 'a# = ABS(-1.5)\nb# = SGN(-1.5)\nn# = MIN(1.5, 2.5)\nx# = MAX(a#, b#)\n'
C['math_unsupported'] = 'a = ASIN(1)\nb = ACOS(1)\nc = ATAN(1)\nd = ATAN2(1, 2)\ne1 = EXP(1)\nf = LOG(1)\ng = LOG10(1)\nh = ISQR(4)\ni = RANDOMIZE(1)\nj = FOO(1)\nk = foo()\n'
C['math_argcount_abs'] = 'a = ABS(1, 2)\n'
C['math_argcount_sgn'] = 'a = SGN()\n'
C['math_argcount_int'] = 'a = FIX(1, 2)\n'
C['math_argcount_floor'] = 'a = FLOOR()\n'
C['math_argcount_ceil'] = 'a = CEIL()\n'
C['math_argcount_round'] = 'a = ROUND()\n'
C['math_argcount_round3'] = 'a = ROUND(1, 2, 3)\n'
C['math_argcount_sin'] = 'a = SIN()\n'
C['math_argcount_cos'] = 'a = COS(1, 2)\n'
C['math_argcount_tan'] = 'a = TAN()\n'
C['math_argcount_sqr'] = 'a = SQR()\n'
C['math_argcount_pow'] = 'a = POW(1)\n'
C['math_argcount_min'] = 'a = MIN(1)\n'
C['math_argcount_max'] = 'a = MAX(1, 2, 3)\n'
C['math_after_error'] = 'x = 1\nPRINT "before"\na = ABS()\nPRINT "after"\n'
C['pi_e_names'] = 'p# = pi\nq# = e\nr = pi()\n'

C['string_funcs_keyword'] = 's$ = "Hello World"\na$ = LEFT$(s$, 3)\nb$ = RIGHT$(s$, 3)\nc$ = MID$(s$, 2, 3)\nd$ = CHR$(65)\ne$ = STR$(42)\nf$ = STRING$(3, "x")\ng$ = UCASE$(s$)\nh$ = LCASE$(s$)\ni$ = TRIM$(s$)\nj$ = LTRIM$(s$)\nk$ = RTRIM$(s$)\n'
C['string_funcs_ident'] = 's$ = "Hello World"\na$ = LEFT(s$, 3)\nb$ = RIGHT(s$, 3)\nc$ = MID(s$, 2, 3)\nc2$ = MID(s$, 2)\nd$ = CHR(65)\ne$ = STR(42)\ne2$ = STR(1.5)\nf# = 2.5\ne3$ = STR(f#)\nf$ = STRING(3, "x")\ng$ = UCASE(s$)\nh$ = LCASE(s$)\ni$ = TRIM(s$)\nj$ = LTRIM(s$)\nk$ = RTRIM(s$)\nl = LEN(s$)\nm = ASC(s$)\nn = VAL("123")\no = INSTR(s$, "World")\np = INSTR(2, s$, "o")\nPRINT LEN("abc")\n'
C['string_funcs_errors'] = 'a = LEN()\nb = ASC(1, 2)\nc = VAL()\nd = INSTR("a")\ne1 = INSTR(1, 2, 3, 4)\nf$ = LEFT("a")\ng$ = RIGHT()\nh$ = MID("a")\ni$ = CHR()\nj$ = STR()\nk$ = STRING("a")\nl$ = UCASE()\nm$ = LCASE(1, 2)\nn$ = TRIM()\no$ = LTRIM()\np$ = RTRIM()\n'
C['string_funcs_in_concat'] = 's$ = "abc"\nt$ = LEFT(s$, 1) + RIGHT(s$, 1) + MID(s$, 2, 1)\nu$ = UCASE(s$) + LCASE("XYZ")\nPRINT LEN(s$ + t$)\nv$ = CHR(65) + STR(LEN(s$))\n'
C['string_compare'] = 'a$ = "x"\nb$ = "y"\nIF a$ = b$ THEN\n  PRINT "eq"\nEND IF\nc = a$ < b$\n'
C['string_unicode'] = 'a$ = "caf\u00e9 \u20ac"\nPRINT "\u00fcber"\nDATA "\u00f1"\n'
C['unicode_ident'] = 'caf\u00e9 = 1\nPRINT caf\u00e9\n\u00fcx$ = "a"\n'

C['lexer_errors'] = 'x = 1 & 2\ny = 3 ! 4\n'
C['lexer_unterminated'] = 'PRINT "abc\nx = 1\n'
C['lexer_dot'] = 'x = .5\n'
C['lexer_chars'] = 'a = 1 @ 2\nb = {3}\nc = 4 % 5\nd = 6 | 7\ne1 = 8 ~ 9\nf = ?\n'
C['rem_quirk'] = 'remaining = 5\nPRINT remaining\nx = 1 REM trailing\ny = 2 \' tick\nPREM = 3\nx = 1REM\nREM\nrem lower\n'
C['rem_short'] = 'x = 1\nRE\nR\n'
C['int_overflow'] = 'a = 2147483648\n'
C['int_overflow_neg'] = 'a = -2147483649\n'
C['hex_overflow'] = 'a = 0x100000000\n'
C['hex_empty'] = 'a = 0x\n'
C['hex_neg'] = 'a = -0x10\n'
C['bin_bad'] = 'a = 0b102\n'
C['bin_bad2'] = 'a = 0b2\n'
C['bin_overflow'] = 'a = 0b111111111111111111111111111111111\n'
C['bin_empty'] = 'a = 0b\n'
C['float_bad'] = 'a# = 1e\n'
C['float_bad2'] = 'a# = 1e+\n'
C['float_bad3'] = 'a# = 1e5e3\n'
C['float_dote'] = 'a# = 1.e5\nb# = 5.\nc# = 1E5\nd# = 1e-0\n'
C['float_hash_suffix'] = 'a# = 1.5# \nb = 2#\n'
C['float_huge'] = 'a# = 1e999\nb# = -1e999\nc# = 1e-999\n'
C['leading_zeros'] = 'a = 007\nb = 0x0000000000FF\nc = -0\n'
C['parse_errors'] = 'x =\ny = (1 + 2\nz = 1 +\n= 5\nfoo\n5\n"str"\n+\n'
C['parse_errors2'] = 'x = 1 2\nTHEN\nTO\nNEXT\nWEND\nUNTIL 1\nSTEP\n'
C['parse_unexpected_kw'] = 'SAVE\nOPEN\nCLOSE\nREAD\nWRITE\nON\nERROR\nRESUME\nERR\nERL\nAT\nUSING\nTAB\nFILLED\nCOLOR\nFLAGS\nSHOW\nHIDE\nBUTTON\nSIN\nMOD\n'
C['parse_expr_kw'] = 'a = SAVE\nb = (\nc = )\nd = ,\n'
C['label_forms'] = 'Lbl:\n  Lbl2:  \nx: y = 1\nfoo: \nPRINT 1 : PRINT 2\n_under:\n9lab:\nmy label:\n'
C['label_colon_inline'] = 'start: PRINT "x"\nGOTO start\n'
C['brackets'] = 'x = [1]\ny = (2)\nz = a[1\n'
C['comma_colon'] = 'a = 1, 2\nb = 3; 4\n'
C['no_code'] = 'DIM a(5)\n'
C['tiny_code'] = 'A:\nB:\nC:\nD:\nE1:\nF:\n'
C['tiny_code2'] = 'abcdefghijklmnop:\n'
C['dup_labels'] = 'X1:\nX1:\nGOTO X1\n'

C['program_bubbles'] = """' A small but complete program touching most of the dialect
DIM table(15)
score = 0
lives = 3
name$ = "PLAYER"
speed# = 1.5
Main:
  CLS
  VIDEO 3
  LAYER VISIBILITY 0x07
  FOR i = 0 TO 15
    x = i * 10
    y = i * 5
    PLOT x, y, i
    LINE x, 0, x, y, 15 - i
    IF i MOD 2 = 0 THEN
      CIRCLE FILLED x, y, 4, i
    ELSEIF i MOD 3 = 0 THEN
      RECTANGLE x, y, 5, 5, 2
    ELSE
      ELLIPSE x, y, 6, 3
    END IF
  NEXT i
  WHILE lives > 0
    k = INKEY
    IF k = 27 THEN
      GOTO Finish
    END IF
    mx = MOUSE X
    my = MOUSE Y
    IF MOUSE BUTTON 0 THEN
      score = score + 10
      GOSUB ShowScore
    END IF
    lives = lives - 1
    WAIT 1
  WEND
  GOTO Main
ShowScore:
  PRINT AT 0, 0; name$ USING 0, 15
  PRINT score
  PRINT "Score: "; score
  RETURN
Finish:
  s$ = STR(score)
  t$ = "Final: " + s$
  PRINT t$
  POKE 0x1000, score AND 0xFF
  v = PEEK(0x1000) SHL 2
  t = TIME
  FOR f = 0.0 TO 2.0 STEP 0.5
    PRINT f
  NEXT
  END
"""
big = ['\' Large generated program for timing']
big.append('DIM grid(63)')
for i in range(400):
    big.append(f'v{i} = {i} * 3 + (v{max(i-1,0)} SHL 1) - {i % 7}')
    if i % 10 == 0:
        big.append(f'L{i}:')
        big.append(f'IF v{i} > {i} AND v{i} < {i*100} THEN')
        big.append(f'  PRINT "line {i}"; v{i}')
        big.append(f'  PLOT v{i}, {i}, {i % 16}')
        big.append('ELSE')
        big.append(f'  GOSUB L{max(i-10,0)}')
        big.append('END IF')
        big.append(f'FOR k{i} = 1 TO {i % 13 + 2}')
        big.append(f'  s{i}$ = "item" + STR(k{i})')
        big.append('NEXT')
        big.append(f'WHILE v{i} > 0')
        big.append(f'  v{i} = v{i} - {i + 1}')
        big.append('WEND')
big.append('END')
C['program_large'] = '\n'.join(big) + '\n'

os.makedirs(out, exist_ok=True)
for name, text in C.items():
    open(os.path.join(out, name + '.bas'), 'w', encoding='utf-8', newline='').write(text)
print(len(C), 'files')
