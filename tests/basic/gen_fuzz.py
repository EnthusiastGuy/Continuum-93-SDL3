# Seeded random BASIC sources (grammar-shaped, then corrupted).  python -I gen_fuzz.py tests/basic/fuzz 150 93
import os, random, sys
out, count = sys.argv[1], int(sys.argv[2])
R = random.Random(int(sys.argv[3]) if len(sys.argv) > 3 else 93)
os.makedirs(out, exist_ok=True)
VARS = ['a', 'b', 'x', 'y', 'i', 'n', 's$', 't$', 'f#', 'g#', 'arr', 'X', 'Y']
FUNCS = ['ABS', 'SGN', 'INT', 'FIX', 'FLOOR', 'CEIL', 'ROUND', 'SIN', 'COS', 'TAN', 'SQR', 'POW', 'MIN', 'MAX', 'RND', 'LEN', 'ASC',
         'VAL', 'INSTR', 'LEFT$', 'RIGHT$', 'MID$', 'CHR$', 'STR$', 'LEFT', 'RIGHT', 'MID', 'CHR', 'STR', 'STRING', 'UCASE', 'LCASE',
         'TRIM', 'LTRIM', 'RTRIM', 'ATAN', 'EXP', 'foo']
BIN = ['+', '-', '*', '/', '\\', 'MOD', '^', '=', '<>', '<', '>', '<=', '>=', 'AND', 'OR', 'XOR', 'NAND', 'NOR', 'XNOR', 'IMPLY',
       'SHL', 'SHR', 'ROL', 'ROR']
LABELS = ['L1', 'L2', 'Sub', 'Main']

def atom(d):
    k = R.randrange(14)
    if k < 3: return str(R.choice([0, 1, 2, 5, 10, 31, 32, 255, -1, -7, 65535]))
    if k == 3: return R.choice(['1.5', '0.25', '-2.5', '1e3', '3.0'])
    if k == 4: return R.choice(['"hi"', '"a b"', '""', '"x\\ny"'])
    if k < 8: return R.choice(VARS) + (f'[{expr(d + 1)}]' if R.random() < 0.1 else '')
    if k == 8: return R.choice(['PI', 'E', 'TIME', 'TICKS', 'INKEY', 'INKEY $', 'MOUSE X', 'MOUSE Y', f'MOUSE BUTTON {atom(d + 1)}'])
    if k == 9: return f'PEEK{R.choice(["", "16", "24", "32"])}({expr(d + 1)})'
    if k == 10: return f'VARPTR({R.choice(VARS)})'
    f = R.choice(FUNCS)
    return f + '(' + ', '.join(expr(d + 1) for _ in range(R.randrange(4))) + ')'

def expr(d=0):
    if d > 3 or R.random() < 0.4: e = atom(d)
    else: e = f'{expr(d + 1)} {R.choice(BIN)} {expr(d + 1)}'
    if R.random() < 0.15: e = f'({e})'
    if R.random() < 0.08: e = R.choice(['-', 'NOT ']) + e
    return e

def stmt(d=0):
    k = R.randrange(30)
    v = R.choice(VARS)
    if k < 6: return [f'{R.choice(["", "LET "])}{v} = {expr()}']
    if k == 6: return [f'PRINT ' + R.choice(['; ', ', ']).join(expr() for _ in range(R.randrange(1, 4)))]
    if k == 7: return [f'PRINT AT {expr()}, {expr()}; {expr()}' + (f' USING {", ".join(expr() for _ in range(R.randrange(1, 7)))}' if R.random() < .5 else '')]
    if k == 8 and d < 3:
        body = [f'IF {expr()} THEN'] + block(d)
        for _ in range(R.randrange(3)): body += [f'ELSEIF {expr()} THEN'] + block(d)
        if R.random() < .5: body += ['ELSE'] + block(d)
        return body + [R.choice(['END IF', 'ENDIF'])]
    if k == 9 and d < 3:
        return [f'FOR {R.choice(["i", "j", "k", "f"])} = {expr()} TO {expr()}' + (f' STEP {expr()}' if R.random() < .5 else '')] + block(d) + ['NEXT']
    if k == 10 and d < 3: return [f'WHILE {expr()}'] + block(d) + ['WEND']
    if k == 11: return [f'{R.choice(["GOTO", "GOSUB"])} {R.choice(LABELS)}']
    if k == 12: return [R.choice(LABELS) + ':']
    if k == 13: return [R.choice(['RETURN', 'END', 'CLS', f'CLS {expr()}', f'WAIT {expr()}', f'SLEEP {expr()}'])]
    if k == 14: return [f'PLOT {expr()}, {expr()}' + (f', {expr()}' if R.random() < .5 else '')]
    if k == 15: return [f'{R.choice(["LINE", "RECTANGLE", "RECTANGLE FILLED", "ELLIPSE", "ELLIPSE FILLED"])} {expr()}, {expr()}, {expr()}, {expr()}' + (f', {expr()}' if R.random() < .5 else '')]
    if k == 16: return [f'CIRCLE{R.choice(["", " FILLED"])} {expr()}, {expr()}, {expr()}' + (f', {expr()}' if R.random() < .5 else '')]
    if k == 17: return [R.choice([f'SCREEN {expr()}', f'VIDEO {expr()}', f'INK {expr()}', f'PAPER {expr()}', f'PLAY {expr()}', f'FONT {expr()}',
                                  f'FONT COLOR {expr()}', f'FONT FLAGS {expr()}', f'FONT MAXWIDTH {expr()}', f'FONT OUTLINE {expr()}, {expr()}',
                                  f'LAYER SHOW {expr()}', f'LAYER HIDE {expr()}', f'LAYER VISIBILITY {expr()}', f'LOCATE {expr()}, {expr()}'])]
    if k == 18: return [f'BEEP {expr()}, {expr()}' + (f', {expr()}' if R.random() < .5 else ''), f'LOAD FONT {expr()}, {expr()}']
    if k == 19: return [f'SPRITE {expr()}, {expr()}, {expr()}, {expr()}, {expr()}' + (f', {expr()}' if R.random() < .5 else '')]
    if k == 20: return [f'POKE{R.choice(["", "16", "24", "32"])} {expr()}, {expr()}', f'MEMCOPY {expr()}, {expr()}, {expr()}', f'MEMFILL {expr()}, {expr()}, {expr()}']
    if k == 21: return [f'DIM {v}({", ".join(expr() for _ in range(R.randrange(1, 3)))})']
    if k == 22: return ['DATA ' + ', '.join(expr() for _ in range(R.randrange(1, 4)))]
    if k == 23: return [f'INPUT {R.choice(["", chr(34) + "p" + chr(34) + "; "])}{v}']
    if k == 24 and d < 3: return ['REPEAT'] + block(d) + [f'UNTIL {expr()}']
    if k == 25: return [R.choice(["' note", 'REM x', 'EXIT FOR', 'CONTINUE WHILE', 'STOP'])]
    return [f'{v} = {expr()}']

def block(d):
    return ['  ' + l for _ in range(R.randrange(1, 4)) for l in stmt(d + 1)]

TOK = ['(', ')', ',', ';', ':', '=', 'THEN', 'TO', 'NEXT', 'END', 'IF', 'ELSE', '[', ']', '"', '.', '@', '0x', '0b2', '1e', 'MOUSE']
for n in range(count):
    lines = [l for _ in range(R.randrange(1, 12)) for l in stmt()]
    for _ in range(R.randrange(3)):   # corruption: drop, duplicate or inject a token somewhere
        i = R.randrange(len(lines))
        w = lines[i].split(' ')
        j = R.randrange(len(w))
        op = R.randrange(3)
        if op == 0 and len(w) > 1: del w[j]
        elif op == 1: w.insert(j, R.choice(TOK))
        else: w[j] = w[j] + R.choice(TOK)
        lines[i] = ' '.join(w)
    open(os.path.join(out, f'fuzz{n:03d}.bas'), 'w', encoding='utf-8', newline='').write('\n'.join(lines) + '\n')
print(count, 'fuzz sources')
