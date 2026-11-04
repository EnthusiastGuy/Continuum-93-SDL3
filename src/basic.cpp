// Continuum 93 BASIC compiler, ported line for line from the reference
// (Compilers/C93Basic: BasicCompiler.cs, Lexer.cs, Token.cs, Parser.cs, AST.cs, SymbolTable.cs,
// VariableManager.cs, Emitter.cs). Error count and log are identical to the reference; so is the text with
// compileBasic(..., reference = true), verified by `conformance --basic` against tests/oracle --basic.
//
// D-18: by default the emitted code is fixed where the reference's could not run (the "!ref_" branches):
// lines with no opcode form (operand widths, floats in integer forms, MOD/INT/NEG/WAIT r, "LD .label, r",
// hex without 0x), temps clobbered by fixed registers, and the string runtime (basic_helpers_d18.inc).
// Checked by `conformance --basic` (assembles cleanly) and `conformance --basicrun tests/basicrun`.
//
// Reproduced reference quirks (do not "fix" without a recorded decision; the reference mode keeps all):
// - StringBuilder.AppendLine ends lines with "\r\n" (Windows); the hand-written "\n" pieces stay "\n".
// - "REM" anywhere a token may start begins a comment, also inside names ("remaining" -> comment).
// - "-" followed by a digit lexes as a negative number literal, so "a = b-1" is "a = b" then "-1".
// - Expect() returns the token *before* the matched one: EXIT/CONTINUE FOR always parse as the WHILE form.
// - Keyword functions keep their "$" (LEFT$, MID$...), so the emitter never matches them: "Unsupported
//   function"; the identifier spelling LEFT(...) does match.
// - DIM, REPEAT, STOP, EXIT, CONTINUE are "Unsupported statement" in the emitter (DIM still declares).
// - Float immediates are printed byte-swapped (BitConverter on a reversed array): 1.5 -> 0000C03F.
// - GOTO/GOSUB to a label already known emit a numeric address from a size estimate (10 per
//   statement until the label is emitted, then the running estimate) - so _currentAddress is tracked.
// - Shifts by a non-literal emit the right operand twice; booleans in "A" go back into the int pool.
// - Unhandled nulls (MOUSE without X/Y/BUTTON) surface as .NET's NullReferenceException message; argument
//   count errors of math functions throw ("Compiler error", no assembly), those of string functions do not.
// - Assignments ignore array indices; PRINT of a float literal emits nothing; the run address is 0 after a
//   lexer error (the emitter never got its start address).
// - DATA floats print as .NET float.ToString() (en-US, D-15), e.g. 1E+30, 123456790, "∞".
// shortcut: every non-ASCII character counts as a letter and never as whitespace (C#: char.IsLetterOrDigit /
// IsWhiteSpace per UTF-16 unit); columns and lengths are counted in UTF-16 units as in C#.
#include "basic.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace c93 {
namespace {

#define BASIC_TOKENS(X) \
    X(INTEGER) X(FLOAT) X(STRING) X(IDENTIFIER) X(PLUS) X(MINUS) X(MULTIPLY) X(DIVIDE) X(INT_DIVIDE) \
    X(MODULO) X(POWER) X(ASSIGN) X(EQUAL) X(NOT_EQUAL) X(LESS_THAN) X(GREATER_THAN) X(LESS_EQUAL) \
    X(GREATER_EQUAL) X(AND) X(OR) X(XOR) X(NOT) X(IMPLY) X(NAND) X(NOR) X(XNOR) X(SHL) X(SHR) X(ROL) \
    X(ROR) X(COMMA) X(SEMICOLON) X(COLON) X(LPAREN) X(RPAREN) X(LBRACKET) X(RBRACKET) X(LET) X(DIM) \
    X(IF) X(THEN) X(ELSE) X(ELSEIF) X(END) X(ENDIF) X(FOR) X(TO) X(STEP) X(NEXT) X(WHILE) X(WEND) \
    X(REPEAT) X(UNTIL) X(GOTO) X(GOSUB) X(RETURN) X(EXIT) X(CONTINUE) X(REM) X(DATA) X(PRINT) X(INPUT) \
    X(CLS) X(SCREEN) X(VIDEO) X(PLOT) X(LINE) X(RECTANGLE) X(CIRCLE) X(ELLIPSE) X(FILLED) X(INK) \
    X(PAPER) X(LOCATE) X(FONT) X(COLOR) X(FLAGS) X(MAXWIDTH) X(OUTLINE) X(LAYER) X(SHOW) X(HIDE) \
    X(VISIBILITY) X(SPRITE) X(BEEP) X(PLAY) X(INKEY) X(MOUSE) X(BUTTON) X(X) X(Y) X(LOAD) X(SAVE) \
    X(OPEN) X(CLOSE) X(READ) X(WRITE) X(PEEK) X(POKE) X(PEEK16) X(POKE16) X(PEEK24) X(POKE24) \
    X(PEEK32) X(POKE32) X(MEMCOPY) X(MEMFILL) X(VARPTR) X(WAIT) X(SLEEP) X(TIME) X(TICKS) X(ON) \
    X(ERROR) X(RESUME) X(ERR) X(ERL) X(STOP) X(AT) X(USING) X(TAB) X(ABS) X(SGN) X(INT) X(FIX) \
    X(FLOOR) X(CEIL) X(ROUND) X(SIN) X(COS) X(TAN) X(ASIN) X(ACOS) X(ATAN) X(ATAN2) X(EXP) X(LOG) \
    X(LOG10) X(SQR) X(POW) X(ISQR) X(MIN) X(MAX) X(RND) X(RANDOMIZE) X(PI) X(E) X(LEN) X(LEFT) \
    X(RIGHT) X(MID) X(CHR) X(ASC) X(VAL) X(STR) X(STRING_FUNC) X(INSTR) X(UCASE) X(LCASE) X(TRIM) \
    X(LTRIM) X(RTRIM) X(EOF) X(NEWLINE) X(COMMENT)

#define X(n) t##n,
enum Tk : uint8_t { BASIC_TOKENS(X) };
#undef X
#define X(n) #n,
const char* const kTkName[] = { BASIC_TOKENS(X) };
#undef X

const std::unordered_map<std::string, Tk> kKeywords = {
    {"LET", tLET}, {"DIM", tDIM}, {"IF", tIF}, {"THEN", tTHEN}, {"ELSE", tELSE}, {"ELSEIF", tELSEIF},
    {"END", tEND}, {"ENDIF", tENDIF}, {"FOR", tFOR}, {"TO", tTO}, {"STEP", tSTEP}, {"NEXT", tNEXT},
    {"WHILE", tWHILE}, {"WEND", tWEND}, {"REPEAT", tREPEAT}, {"UNTIL", tUNTIL}, {"GOTO", tGOTO},
    {"GOSUB", tGOSUB}, {"RETURN", tRETURN}, {"EXIT", tEXIT}, {"CONTINUE", tCONTINUE}, {"REM", tREM},
    {"DATA", tDATA}, {"STOP", tSTOP}, {"PRINT", tPRINT}, {"INPUT", tINPUT}, {"INKEY", tINKEY},
    {"MOUSE", tMOUSE}, {"BUTTON", tBUTTON}, {"LOAD", tLOAD}, {"SAVE", tSAVE}, {"OPEN", tOPEN},
    {"CLOSE", tCLOSE}, {"READ", tREAD}, {"WRITE", tWRITE}, {"AT", tAT}, {"USING", tUSING}, {"TAB", tTAB},
    {"CLS", tCLS}, {"SCREEN", tSCREEN}, {"VIDEO", tVIDEO}, {"PLOT", tPLOT}, {"LINE", tLINE},
    {"RECTANGLE", tRECTANGLE}, {"CIRCLE", tCIRCLE}, {"ELLIPSE", tELLIPSE}, {"FILLED", tFILLED},
    {"INK", tINK}, {"PAPER", tPAPER}, {"LOCATE", tLOCATE}, {"FONT", tFONT}, {"COLOR", tCOLOR},
    {"FLAGS", tFLAGS}, {"MAXWIDTH", tMAXWIDTH}, {"OUTLINE", tOUTLINE}, {"LAYER", tLAYER}, {"SHOW", tSHOW},
    {"HIDE", tHIDE}, {"VISIBILITY", tVISIBILITY}, {"SPRITE", tSPRITE}, {"BEEP", tBEEP}, {"PLAY", tPLAY},
    {"PEEK", tPEEK}, {"POKE", tPOKE}, {"PEEK16", tPEEK16}, {"POKE16", tPOKE16}, {"PEEK24", tPEEK24},
    {"POKE24", tPOKE24}, {"PEEK32", tPEEK32}, {"POKE32", tPOKE32}, {"MEMCOPY", tMEMCOPY},
    {"MEMFILL", tMEMFILL}, {"VARPTR", tVARPTR}, {"WAIT", tWAIT}, {"SLEEP", tSLEEP}, {"TIME", tTIME},
    {"TICKS", tTICKS}, {"ON", tON}, {"ERROR", tERROR}, {"RESUME", tRESUME}, {"ERR", tERR}, {"ERL", tERL},
    {"MOD", tMODULO}, {"AND", tAND}, {"OR", tOR}, {"XOR", tXOR}, {"NOT", tNOT}, {"IMPLY", tIMPLY},
    {"NAND", tNAND}, {"NOR", tNOR}, {"XNOR", tXNOR}, {"SHL", tSHL}, {"SHR", tSHR}, {"ROL", tROL},
    {"ROR", tROR}, {"ABS", tABS}, {"SGN", tSGN}, {"INT", tINT}, {"FIX", tFIX}, {"FLOOR", tFLOOR},
    {"CEIL", tCEIL}, {"ROUND", tROUND}, {"SIN", tSIN}, {"COS", tCOS}, {"TAN", tTAN}, {"ASIN", tASIN},
    {"ACOS", tACOS}, {"ATAN", tATAN}, {"ATAN2", tATAN2}, {"EXP", tEXP}, {"LOG", tLOG}, {"LOG10", tLOG10},
    {"SQR", tSQR}, {"POW", tPOW}, {"ISQR", tISQR}, {"MIN", tMIN}, {"MAX", tMAX}, {"RND", tRND},
    {"RANDOMIZE", tRANDOMIZE}, {"PI", tPI}, {"E", tE}, {"LEN", tLEN}, {"LEFT$", tLEFT}, {"RIGHT$", tRIGHT},
    {"MID$", tMID}, {"CHR$", tCHR}, {"ASC", tASC}, {"VAL", tVAL}, {"STR$", tSTR}, {"STRING$", tSTRING_FUNC},
    {"INSTR", tINSTR}, {"UCASE$", tUCASE}, {"LCASE$", tLCASE}, {"TRIM$", tTRIM}, {"LTRIM$", tLTRIM},
    {"RTRIM$", tRTRIM}};

constexpr const char* kNullRef = "Object reference not set to an instance of an object.";
constexpr const char* kOverflow = "Value was either too large or too small for an Int32.";
struct Thrown : std::runtime_error { using std::runtime_error::runtime_error; };
struct FormatError : std::runtime_error { using std::runtime_error::runtime_error; };   // .NET FormatException

std::string upper(std::string_view s) {
    std::string u(s);
    for (char& c : u) if (c >= 'a' && c <= 'z') c = char(c - 32);
    return u;
}
bool isWs(unsigned char c) { return c == ' ' || (c >= 9 && c <= 13); }
bool isDigit(unsigned char c) { return c >= '0' && c <= '9'; }
bool isLetterOrDigit(unsigned char c) { return isDigit(c) || ((c | 32) >= 'a' && (c | 32) <= 'z') || c >= 0x80; }
// UTF-16 length of UTF-8 text: the unit C# counts columns and lengths in.
int u16len(std::string_view s) {
    int n = 0;
    for (unsigned char c : s) n += ((c & 0xC0) != 0x80) + (c >= 0xF0);
    return n;
}
bool endsWith(std::string_view s, std::string_view e) { return s.size() >= e.size() && s.substr(s.size() - e.size()) == e; }
std::string_view trim(std::string_view s) {
    while (!s.empty() && isWs(s.front())) s.remove_prefix(1);
    while (!s.empty() && isWs(s.back())) s.remove_suffix(1);
    return s;
}
std::string hexN(uint32_t v, int digits) {
    char b[16];
    auto r = std::to_chars(b, b + 16, v, 16);
    std::string s(b, r.ptr);
    for (char& c : s) if (c >= 'a') c = char(c - 32);
    return std::string(s.size() < size_t(digits) ? digits - s.size() : 0, '0') + s;
}
std::string replaceAll(std::string s, std::string_view from, std::string_view to) {
    for (size_t p = 0; (p = s.find(from, p)) != std::string::npos; p += to.size()) s.replace(p, from.size(), to);
    return s;
}

} // namespace

// float.ToString() of .NET 8 (en-US): shortest round-trip digits, "G" layout, exponent past 9 digits.
std::string netFloat(float f) {
    if (std::isnan(f)) return "NaN";
    if (std::isinf(f)) return f > 0 ? "\xE2\x88\x9E" : "-\xE2\x88\x9E";
    if (f == 0) return std::signbit(f) ? "-0" : "0";
    char b[64];
    auto r = std::to_chars(b, b + 64, f, std::chars_format::scientific);
    std::string_view s(b, r.ptr - b);
    std::string sign = s[0] == '-' ? "-" : "";
    if (!sign.empty()) s.remove_prefix(1);
    size_t e = s.find('e');
    std::string digits;
    for (char c : s.substr(0, e)) if (c != '.') digits += c;
    int exp = 0;
    std::from_chars(s.data() + e + (s[e + 1] == '+' ? 2 : 1), s.data() + s.size(), exp);
    int scale = exp + 1, n = int(digits.size());
    if (scale > std::max(n, 9) || scale < -3) {
        std::string m = digits.substr(0, 1) + (n > 1 ? "." + digits.substr(1) : "");
        std::string ex = std::to_string(std::abs(exp));
        return sign + m + "E" + (exp < 0 ? "-" : "+") + (ex.size() < 2 ? "0" : "") + ex;
    }
    if (scale <= 0) return sign + "0." + std::string(-scale, '0') + digits;
    if (scale >= n) return sign + digits + std::string(scale - n, '0');
    return sign + digits.substr(0, scale) + "." + digits.substr(scale);
}

namespace {

// The reference reverses the big-endian bytes and reads them back little-endian: a byte swap.
uint32_t swappedBits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return (u >> 24) | ((u >> 8) & 0xFF00) | ((u << 8) & 0xFF0000) | (u << 24);
}

// --- Lexer ---------------------------------------------------------------------------------------
struct Token { Tk type; std::string value; int line, col; };
struct Msg { int line; std::string text; };

std::vector<Token> tokenize(std::string_view src, std::vector<Msg>& errors) {
    std::vector<std::string_view> lines;
    for (size_t i = 0, start = 0;; ++i) {
        if (i == src.size()) { lines.push_back(src.substr(start)); break; }
        if (src[i] == '\r' || src[i] == '\n') {
            lines.push_back(src.substr(start, i - start));
            if (src[i] == '\r' && i + 1 < src.size() && src[i + 1] == '\n') ++i;
            start = i + 1;
        }
    }
    std::vector<Token> t;
    for (size_t ln = 0; ln < lines.size(); ++ln) {
        std::string_view line = lines[ln];
        int L = int(ln + 1);
        size_t col = 0, len = line.size();
        bool ascii = u16len(line) == int(len);
        auto C = [&](size_t byteCol) { return ascii ? int(byteCol) : u16len(line.substr(0, byteCol)); };

        std::string_view tl = trim(line);
        if (!tl.empty() && tl.back() == ':' && tl.find(':') == tl.size() - 1) {   // a label-only line
            std::string_view label = trim(tl.substr(0, tl.size() - 1));
            if (!label.empty()) {
                t.push_back({tIDENTIFIER, std::string(label), L, 0});
                t.push_back({tCOLON, ":", L, u16len(label)});
                t.push_back({tNEWLINE, "\n", L, u16len(tl)});
            }
            continue;
        }

        while (col < len) {
            unsigned char ch = line[col];
            if (isWs(ch)) { ++col; continue; }

            if (ch == '\'' || (col + 2 < len && (ch | 32) == 'r' && (line[col + 1] | 32) == 'e' && (line[col + 2] | 32) == 'm')) {
                if (ch == '\'') t.push_back({tCOMMENT, std::string(line.substr(col)), L, C(col)});
                else {
                    t.push_back({tREM, "REM", L, C(col)});
                    t.push_back({tCOMMENT, std::string(line.substr(col + 3)), L, C(col + 3)});
                }
                break;
            }

            if (ch == '"') {
                size_t start = col++;
                std::string sb = "\"";
                while (col < len && line[col] != '"') {
                    if (line[col] == '\\' && col + 1 < len) {
                        switch (line[++col]) {
                            case 'n': sb += '\n'; break;
                            case 't': sb += '\t'; break;
                            case 'r': sb += '\r'; break;
                            case '\\': sb += '\\'; break;
                            case '"': sb += '"'; break;
                            default: sb += '\\'; sb += line[col]; break;
                        }
                    } else sb += line[col];
                    ++col;
                }
                if (col < len) { sb += '"'; t.push_back({tSTRING, sb, L, C(start)}); ++col; }
                else errors.push_back({L, "Unterminated string literal"});
                continue;
            }

            if (isDigit(ch) || (ch == '-' && col + 1 < len && isDigit(line[col + 1]))) {
                size_t start = col;
                std::string num;
                if (ch == '-') { num += '-'; ++col; }
                bool isFloat = false, isHex = false, isBin = false;
                if (col + 1 < len && line[col] == '0' && (line[col + 1] == 'x' || line[col + 1] == 'X')) {
                    isHex = true; num += line.substr(col, 2); col += 2;
                } else if (col + 1 < len && line[col] == '0' && (line[col + 1] == 'b' || line[col + 1] == 'B')) {
                    isBin = true; num += line.substr(col, 2); col += 2;
                }
                while (col < len) {
                    char c = line[col];
                    bool hex = isDigit(c) || ((c | 32) >= 'a' && (c | 32) <= 'f');
                    if ((isHex && hex) || (isBin && (c == '0' || c == '1')) || isDigit(c)) { num += c; ++col; }
                    else if (c == '.' && !isFloat && !isHex && !isBin) { isFloat = true; num += c; ++col; }
                    else if ((c == 'e' || c == 'E') && !isHex && !isBin) {
                        isFloat = true; num += c; ++col;
                        if (col < len && (line[col] == '+' || line[col] == '-')) num += line[col++];
                    } else break;
                }
                bool fl = isFloat || num.find_first_of(".eE") != std::string::npos;
                t.push_back({fl ? tFLOAT : tINTEGER, num, L, C(start)});
                continue;
            }

            Tk op = tEOF;
            switch (ch) {
                case '+': op = tPLUS; break;
                case '-': op = tMINUS; break;
                case '*': op = tMULTIPLY; break;
                case '/': op = tDIVIDE; break;
                case '\\': op = tINT_DIVIDE; break;
                case '^': op = tPOWER; break;
                case '=': op = tEQUAL; break;
                case ',': op = tCOMMA; break;
                case ';': op = tSEMICOLON; break;
                case ':': op = tCOLON; break;
                case '(': op = tLPAREN; break;
                case ')': op = tRPAREN; break;
                case '[': op = tLBRACKET; break;
                case ']': op = tRBRACKET; break;
                case '<':
                    if (col + 1 < len && line[col + 1] == '>') { t.push_back({tNOT_EQUAL, "<>", L, C(col)}); col += 2; }
                    else if (col + 1 < len && line[col + 1] == '=') { t.push_back({tLESS_EQUAL, "<=", L, C(col)}); col += 2; }
                    else { t.push_back({tLESS_THAN, "<", L, C(col)}); ++col; }
                    continue;
                case '>':
                    if (col + 1 < len && line[col + 1] == '=') { t.push_back({tGREATER_EQUAL, ">=", L, C(col)}); col += 2; }
                    else { t.push_back({tGREATER_THAN, ">", L, C(col)}); ++col; }
                    continue;
            }
            if (op != tEOF) { t.push_back({op, std::string(1, char(ch)), L, C(col)}); ++col; continue; }

            size_t identStart = col;
            while (col < len && (isLetterOrDigit(line[col]) || line[col] == '_' || line[col] == '$' || line[col] == '#')) ++col;
            std::string ident(line.substr(identStart, col - identStart));
            if (!ident.empty()) {
                auto k = kKeywords.find(upper(ident));
                t.push_back({k != kKeywords.end() ? k->second : tIDENTIFIER, ident, L, C(identStart)});
            } else {
                errors.push_back({L, std::string("Unexpected character: ") + char(ch)});
                ++col;
            }
        }
        t.push_back({tNEWLINE, "\n", L, C(len)});
    }
    t.push_back({tEOF, "", int(lines.size()), 0});
    return t;
}

// --- AST -----------------------------------------------------------------------------------------
enum class VT { Integer, Float, String };
enum class EK { Binary, Unary, Literal, Variable, Call, Peek, VarPtr, Time, Ticks, MouseX, MouseY, MouseButton, Inkey };

struct Expr {
    EK k;
    Tk op = tEOF;                     // Binary / Unary
    Expr* l = nullptr;                // left / operand / Peek address / VarPtr variable / MouseButton button
    Expr* r = nullptr;
    VT type = VT::Integer;            // Literal / Variable
    int ival = 0;
    float fval = 0;
    std::string s;                    // string literal, variable or function name
    std::vector<Expr*> args;          // function arguments / array indices
    int size = 0;                     // Peek: 1, 16, 24, 32
    bool isString = false;            // INKEY$
};

#define BASIC_STMTS(X) X(Label) X(Assignment) X(Dim) X(If) X(ElseIf) X(For) X(While) X(Repeat) X(Goto) \
    X(Gosub) X(Return) X(ExitFor) X(ExitWhile) X(ContinueFor) X(ContinueWhile) X(Print) X(Input) X(Cls) \
    X(End) X(Stop) X(Wait) X(Sleep) X(Plot) X(Line) X(Rectangle) X(Circle) X(Ellipse) X(Screen) X(Video) \
    X(Ink) X(Paper) X(Beep) X(Play) X(LoadFont) X(Font) X(FontColor) X(FontFlags) X(FontMaxWidth) \
    X(FontOutline) X(LayerShow) X(LayerHide) X(LayerVisibility) X(Sprite) X(Locate) X(Data) X(Poke) \
    X(MemCopy) X(MemFill)
#define X(n) n,
enum class SK { BASIC_STMTS(X) };
#undef X
#define X(n) #n "Node",
const char* const kStmtName[] = { BASIC_STMTS(X) };
#undef X

struct PrintItem { Expr* e; bool semicolon, comma; };
// PRINT's optional parts, in Stmt::e
enum { pAtX, pAtY, pFont, pColor, pFlags, pMaxWidth, pOutColor, pOutPattern, pTab, pCount };

struct Stmt {
    SK k;
    int line = 0;
    std::string name;                 // label, loop variable, DIM name, GOTO/GOSUB target
    VT type = VT::Integer;            // DIM
    std::vector<Expr*> e;             // the statement's expressions in source order (see each parser)
    std::vector<Stmt*> body, alt;     // THEN/loop body, ELSE
    std::vector<Stmt*> elseIfs;       // SK::ElseIf: e[0] condition, body
    bool filled = false, at = false;
    int size = 0;                     // POKE: 1, 16, 24, 32
    std::vector<PrintItem> items;
};

// --- Parser --------------------------------------------------------------------------------------
VT typeOfName(std::string_view n) { return endsWith(n, "$") ? VT::String : endsWith(n, "#") ? VT::Float : VT::Integer; }

// Convert.ToInt32(s, radix) for radix 2/16: IsTight parsing, wraps above 0x7FFFFFFF.
int convertToInt(std::string_view s, int radix) {
    if (s.empty()) throw Thrown("Specified argument was out of the range of valid values. (Parameter 'Index was out of range. Must be non-negative and less than the size of the collection.')");
    uint64_t v = 0;
    size_t i = 0;
    for (; i < s.size(); ++i) {
        char c = char(s[i] | 32);
        int d = isDigit(s[i]) ? s[i] - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : 99;
        if (d >= radix) break;
        v = v * radix + d;
        if (v > 0xFFFFFFFFu) throw Thrown("Value was either too large or too small for a UInt32.");
    }
    if (i == 0) throw FormatError("Could not find any recognizable digits.");
    if (i < s.size()) throw FormatError("Additional non-parsable characters are at the end of the string.");
    return int32_t(uint32_t(v));
}

class Parser {
public:
    Parser(std::vector<Token>& t, std::deque<Expr>& ex, std::deque<Stmt>& st, std::vector<Msg>& err)
        : t_(t), ex_(ex), st_(st), errors_(err) {}

    std::vector<Stmt*> parse() {
        std::vector<Stmt*> prog;
        while (!atEnd()) {
            if (cur().type == tNEWLINE) { advance(); continue; }
            if (cur().type == tEOF) break;
            try {
                if (Stmt* s = statement()) prog.push_back(s);
            } catch (const std::exception& e) {
                errors_.push_back({cur().line, std::string("Parse error: ") + e.what()});
                while (!atEnd() && cur().type != tNEWLINE && cur().type != tEOF) advance();
            }
        }
        return prog;
    }

private:
    std::vector<Token>& t_;
    size_t pos_ = 0;
    std::deque<Expr>& ex_;
    std::deque<Stmt>& st_;
    std::vector<Msg>& errors_;

    const Token& cur() const { return t_[pos_]; }
    const Token& advance() { return t_[pos_++]; }
    const Token& previous() const { return pos_ >= 2 ? t_[pos_ - 2] : t_[0]; }
    const Token& peek() const { return pos_ < t_.size() - 1 ? t_[pos_ + 1] : t_.back(); }
    bool atEnd() const { return cur().type == tEOF; }
    bool match(std::initializer_list<Tk> types) {
        for (Tk k : types) if (cur().type == k) { advance(); return true; }
        return false;
    }
    bool match(Tk k) { return match({k}); }
    const Token& expect(std::initializer_list<Tk> types) {
        if (match(types)) return previous();   // the token before the matched one (reference quirk)
        std::string names;
        for (Tk k : types) names += (names.empty() ? "" : ", ") + std::string(kTkName[k]);
        throw Thrown("Expected one of " + names + ", got " + kTkName[cur().type]);
    }
    const Token& expect(Tk k) { return expect({k}); }
    const Token& expectIdent() {
        if (cur().type == tIDENTIFIER) return advance();
        throw Thrown(std::string("Expected identifier, got ") + kTkName[cur().type]);
    }
    bool isIdent(const char* n) const { return cur().type == tIDENTIFIER && upper(cur().value) == n; }

    Expr* ne(EK k) { ex_.push_back(Expr{k}); return &ex_.back(); }
    Stmt* ns(SK k, int line) { st_.push_back(Stmt{k}); st_.back().line = line; return &st_.back(); }
    Stmt* ns(SK k, int line, std::initializer_list<Expr*> e) { Stmt* s = ns(k, line); s->e = e; return s; }
    Expr* binary(Expr* left, Tk op, Expr* right) {
        if (!left) throw Thrown(kNullRef);   // C#: Line = expr.Line on a null left operand
        Expr* b = ne(EK::Binary); b->l = left; b->op = op; b->r = right; return b;
    }

    bool endIfAhead() const { return cur().type == tEND && peek().type == tIF; }
    void block(std::vector<Stmt*>& out, std::initializer_list<Tk> stops, bool endIf) {
        for (;;) {
            Tk k = cur().type;
            bool stop = k == tEOF || (endIf && endIfAhead());
            for (Tk s : stops) stop |= k == s;
            if (stop) return;
            if (k == tNEWLINE) { advance(); continue; }
            if (Stmt* s = statement()) out.push_back(s);
        }
    }

    Stmt* statement() {
        if (atEnd()) return nullptr;
        while (cur().type == tNEWLINE && !atEnd()) advance();
        if (atEnd()) return nullptr;

        if (cur().type == tIDENTIFIER && peek().type == tCOLON) {
            Stmt* s = ns(SK::Label, cur().line);
            s->name = cur().value;
            advance(); advance();
            return s;
        }
        const Token& tok = cur();
        int line = tok.line;
        switch (tok.type) {
            case tIDENTIFIER: if (!isAssignment()) break; [[fallthrough]];
            case tLET: return assignment();
            case tDIM: return dim();
            case tIF: return ifStmt();
            case tFOR: return forStmt();
            case tWHILE: { advance(); Stmt* s = ns(SK::While, line, {expression()}); block(s->body, {tWEND}, false); expect(tWEND); return s; }
            case tREPEAT: { advance(); Stmt* s = ns(SK::Repeat, line); block(s->body, {tUNTIL}, false); expect(tUNTIL); s->e = {expression()}; return s; }
            case tGOTO: case tGOSUB: { advance(); Stmt* s = ns(tok.type == tGOTO ? SK::Goto : SK::Gosub, line); s->name = expectIdent().value; return s; }
            case tRETURN: advance(); return ns(SK::Return, line);
            case tEXIT: case tCONTINUE: {
                bool exit = tok.type == tEXIT;
                advance();
                bool isFor = expect({tFOR, tWHILE}).type == tFOR;
                return ns(exit ? (isFor ? SK::ExitFor : SK::ExitWhile) : (isFor ? SK::ContinueFor : SK::ContinueWhile), line);
            }
            case tPRINT: return print();
            case tINPUT: {
                advance();
                Expr* prompt = nullptr;
                if (cur().type == tSTRING) { prompt = expression(); match(tSEMICOLON); }
                return ns(SK::Input, line, {prompt, variable()});
            }
            case tCLS: advance(); return ns(SK::Cls, line, {cur().type != tNEWLINE && cur().type != tEOF ? expression() : nullptr});
            case tEND: advance(); return ns(SK::End, line);
            case tSTOP: advance(); return ns(SK::Stop, line);
            case tWAIT: advance(); return ns(SK::Wait, line, {expression()});
            case tSLEEP: advance(); return ns(SK::Sleep, line, {expression()});
            case tPLOT: return args(SK::Plot, 2, 1);
            case tLINE: return args(SK::Line, 4, 1);
            case tRECTANGLE: return args(SK::Rectangle, 4, 1, true);
            case tCIRCLE: return args(SK::Circle, 3, 1, true);
            case tELLIPSE: return args(SK::Ellipse, 4, 1, true);
            case tSCREEN: return args(SK::Screen, 1, 0);
            case tVIDEO: return args(SK::Video, 1, 0);
            case tINK: return args(SK::Ink, 1, 0);
            case tPAPER: return args(SK::Paper, 1, 0);
            case tBEEP: return args(SK::Beep, 2, 1);
            case tPLAY: return args(SK::Play, 1, 0);
            case tLOAD: {
                advance();
                if (match(tFONT)) { Expr* f = expression(); expect(tCOMMA); return ns(SK::LoadFont, line, {f, expression()}); }
                expression();
                if (match(tCOMMA)) expression();
                errors_.push_back({line, "LOAD command not yet implemented (use LOAD FONT)"});
                return nullptr;
            }
            case tFONT: {
                advance();
                if (match(tCOLOR)) return ns(SK::FontColor, line, {expression()});
                if (match(tFLAGS)) return ns(SK::FontFlags, line, {expression()});
                if (match(tMAXWIDTH)) return ns(SK::FontMaxWidth, line, {expression()});
                if (match(tOUTLINE)) { Expr* c = expression(); expect(tCOMMA); return ns(SK::FontOutline, line, {c, expression()}); }
                return ns(SK::Font, line, {expression()});
            }
            case tLAYER:
                advance();
                if (match(tSHOW)) return ns(SK::LayerShow, line, {expression()});
                if (match(tHIDE)) return ns(SK::LayerHide, line, {expression()});
                if (match(tVISIBILITY)) return ns(SK::LayerVisibility, line, {expression()});
                errors_.push_back({line, "LAYER command requires SHOW, HIDE, or VISIBILITY"});
                return nullptr;
            case tSPRITE: return args(SK::Sprite, 5, 1);
            case tLOCATE: return args(SK::Locate, 2, 0);
            case tDATA: {
                advance();
                Stmt* s = ns(SK::Data, line);
                do s->e.push_back(expression()); while (match(tCOMMA));
                return s;
            }
            case tPOKE: case tPOKE16: case tPOKE24: case tPOKE32: {
                Stmt* s = args(SK::Poke, 2, 0);
                s->size = tok.type == tPOKE ? 1 : tok.type == tPOKE16 ? 16 : tok.type == tPOKE24 ? 24 : 32;
                return s;
            }
            case tMEMCOPY: return args(SK::MemCopy, 3, 0);
            case tMEMFILL: return args(SK::MemFill, 3, 0);
            case tREM: case tCOMMENT:
                while (!atEnd() && cur().type != tNEWLINE && cur().type != tEOF) advance();
                return nullptr;
            default: break;
        }
        errors_.push_back({tok.line, std::string("Unexpected token: ") + kTkName[tok.type]});
        advance();
        return nullptr;
    }

    // keyword [FILLED] e1, e2 ... en [, optional...]: the common shape of most commands.
    Stmt* args(SK k, int required, int optional, bool canFill = false) {
        Stmt* s = ns(k, cur().line);
        advance();
        if (canFill) s->filled = match(tFILLED);
        for (int i = 0; i < required; ++i) { if (i) expect(tCOMMA); s->e.push_back(expression()); }
        for (int i = 0; i < optional; ++i) s->e.push_back(match(tCOMMA) ? expression() : nullptr);
        return s;
    }

    bool isAssignment() {
        size_t save = pos_;
        bool yes = false;
        if (cur().type == tIDENTIFIER) { advance(); yes = cur().type == tEQUAL || cur().type == tLBRACKET; }
        pos_ = save;
        return yes;
    }

    Stmt* assignment() {
        int line = cur().line;
        if (cur().type == tLET) advance();
        const Token& v = expectIdent();
        Expr* var = ne(EK::Variable);
        var->s = v.value; var->type = typeOfName(v.value);
        if (match(tLBRACKET)) {
            do var->args.push_back(expression()); while (match(tCOMMA));
            expect(tRBRACKET);
        }
        expect(tEQUAL);
        return ns(SK::Assignment, line, {var, expression()});
    }

    Stmt* dim() {
        Stmt* s = ns(SK::Dim, cur().line);
        advance();
        s->name = expectIdent().value;
        s->type = typeOfName(s->name);
        expect(tLPAREN);
        do s->e.push_back(expression()); while (match(tCOMMA));
        expect(tRPAREN);
        return s;
    }

    Stmt* ifStmt() {
        Stmt* s = ns(SK::If, cur().line);
        advance();
        s->e = {expression()};
        expect(tTHEN);
        while (cur().type == tNEWLINE) advance();
        if (cur().type == tEOF || cur().type == tENDIF || endIfAhead()) return s;
        block(s->body, {tENDIF, tELSE, tELSEIF}, true);
        if (match(tELSEIF)) {
            do {
                Stmt* ei = ns(SK::ElseIf, 0, {expression()});
                expect(tTHEN);
                block(ei->body, {tENDIF, tELSE, tELSEIF}, true);
                s->elseIfs.push_back(ei);
            } while (match(tELSEIF));
        }
        if (match(tELSE)) block(s->alt, {tENDIF}, true);
        if (endIfAhead()) { advance(); advance(); }
        else match(tENDIF);
        return s;
    }

    Stmt* forStmt() {
        Stmt* s = ns(SK::For, cur().line);
        advance();
        s->name = expectIdent().value;
        expect(tEQUAL);
        Expr* start = expression();
        expect(tTO);
        Expr* end = expression();
        Expr* step = match(tSTEP) ? expression() : nullptr;
        s->e = {start, end, step};
        block(s->body, {tNEXT}, false);
        if (match(tNEXT) && cur().type == tIDENTIFIER) advance();
        return s;
    }

    Stmt* print() {
        Stmt* s = ns(SK::Print, cur().line);
        advance();
        s->e.assign(pCount, nullptr);
        if (match(tAT)) {
            s->at = true;
            s->e[pAtX] = expression();
            expect(tCOMMA);
            s->e[pAtY] = expression();
            expect(tSEMICOLON);
        } else if (match(tTAB)) {
            expect(tLPAREN);
            s->e[pTab] = expression();
            expect(tRPAREN);
            expect(tSEMICOLON);
        }
        while (cur().type != tNEWLINE && cur().type != tEOF) {
            if (cur().type == tSEMICOLON || cur().type == tCOMMA) {
                bool semi = cur().type == tSEMICOLON;
                advance();
                if (cur().type == tNEWLINE || cur().type == tEOF) break;
                s->items.push_back({expression(), semi, !semi});
            } else {
                Expr* e = expression();
                bool semi = match(tSEMICOLON);
                bool comma = match(tCOMMA);
                s->items.push_back({e, semi, comma});
            }
            if (match(tUSING)) {   // USING font [, color [, flags [, maxWidth [, outlineColor [, outlinePattern]]]]]
                s->e[pFont] = expression();
                for (int i = pColor; i <= pOutPattern && match(tCOMMA); ++i) s->e[i] = expression();
            }
            if (cur().type == tNEWLINE || cur().type == tEOF) break;
        }
        return s;
    }

    Expr* variable() {
        const Token& id = expectIdent();
        Expr* v = ne(EK::Variable);
        v->s = id.value; v->type = typeOfName(id.value);
        if (match(tLPAREN)) {
            do v->args.push_back(expression()); while (match(tCOMMA));
            expect(tRPAREN);
        }
        return v;
    }

    // Precedence, loosest first: IMPLY, OR/NOR, XOR/XNOR, AND/NAND, comparisons, shifts, + -, * / \ MOD, unary, ^.
    Expr* expression() { return level(0); }
    Expr* level(int n) {
        static const std::initializer_list<Tk> ops[] = {
            {tIMPLY}, {tOR, tNOR}, {tXOR, tXNOR}, {tAND, tNAND},
            {tEQUAL, tNOT_EQUAL, tLESS_THAN, tGREATER_THAN, tLESS_EQUAL, tGREATER_EQUAL},
            {tSHL, tSHR, tROL, tROR}, {tPLUS, tMINUS}, {tMULTIPLY, tDIVIDE, tINT_DIVIDE, tMODULO}};
        if (n == 8) return unary();
        Expr* e = level(n + 1);
        while (match(ops[n])) {
            Tk op = t_[pos_ - 1].type;
            e = binary(e, op, level(n + 1));
        }
        return e;
    }
    Expr* unary() {
        if (match({tMINUS, tNOT})) {
            Expr* u = ne(EK::Unary);
            u->op = t_[pos_ - 1].type;
            u->l = unary();
            return u;
        }
        Expr* e = primary();
        if (match(tPOWER)) return binary(e, tPOWER, unary());   // right-associative
        return e;
    }

    Expr* call(std::string name) {
        Expr* f = ne(EK::Call);
        f->s = std::move(name);
        if (cur().type != tRPAREN) {
            do f->args.push_back(expression()); while (match(tCOMMA));
        }
        expect(tRPAREN);
        return f;
    }

    Expr* primary() {
        if (match(tLPAREN)) { Expr* e = expression(); expect(tRPAREN); return e; }
        if (match(tINTEGER)) {
            const Token& tk = t_[pos_ - 1];
            Expr* l = ne(EK::Literal);
            try {
                std::string_view v = tk.value;
                if (v.starts_with("0x") || v.starts_with("0X")) l->ival = convertToInt(v.substr(2), 16);
                else if (v.starts_with("0b") || v.starts_with("0B")) l->ival = convertToInt(v.substr(2), 2);
                else {   // int.Parse: [-]digits
                    size_t i = v[0] == '-' ? 1 : 0;
                    if (i == v.size() || v.find_first_not_of("0123456789", i) != std::string_view::npos)
                        throw FormatError("The input string '" + tk.value + "' was not in a correct format.");
                    int64_t x = 0;
                    for (size_t j = i; j < v.size(); ++j) {
                        x = x * 10 + (v[j] - '0');
                        if (x > 2147483648LL) throw Thrown(kOverflow);
                    }
                    if (i == 0 && x > 2147483647LL) throw Thrown(kOverflow);
                    l->ival = int32_t(i ? -x : x);
                }
            } catch (const FormatError& e) {
                throw Thrown("Failed to parse integer from token '" + tk.value + "' (Type: INTEGER) at line " + std::to_string(tk.line) + ": " + e.what());
            }
            l->type = VT::Integer;
            return l;
        }
        if (match(tFLOAT)) {
            const Token& tk = t_[pos_ - 1];
            Expr* l = ne(EK::Literal);
            l->type = VT::Float;
            l->fval = parseFloat(tk);
            return l;
        }
        if (match(tSTRING)) {
            Expr* l = ne(EK::Literal);
            const std::string& v = t_[pos_ - 1].value;
            l->type = VT::String;
            l->s = v.substr(1, v.size() - 2);
            return l;
        }
        if (match(tPI)) return named(EK::Call, "PI");
        if (match(tE)) return named(EK::Call, "E");
        if (match(tMOUSE)) {
            if (isIdent("X")) { advance(); return ne(EK::MouseX); }
            if (isIdent("Y")) { advance(); return ne(EK::MouseY); }
            if (match(tBUTTON)) { Expr* b = ne(EK::MouseButton); b->l = expression(); return b; }
            errors_.push_back({cur().line, "MOUSE requires X, Y, or BUTTON"});
            return nullptr;
        }
        if (match(tTIME)) return ne(EK::Time);
        if (match(tTICKS)) return ne(EK::Ticks);
        if (match(tINKEY)) {
            Expr* k = ne(EK::Inkey);
            if (cur().type == tIDENTIFIER && cur().value == "$") { advance(); k->isString = true; }
            return k;
        }
        if (Tk k = cur().type; k == tPEEK || k == tPEEK16 || k == tPEEK24 || k == tPEEK32) {
            advance();
            expect(tLPAREN);
            Expr* p = ne(EK::Peek);
            p->l = expression();
            expect(tRPAREN);
            p->size = k == tPEEK ? 1 : k == tPEEK16 ? 16 : k == tPEEK24 ? 24 : 32;
            return p;
        }
        if (match(tVARPTR)) {
            expect(tLPAREN);
            Expr* p = ne(EK::VarPtr);
            p->l = variable();
            expect(tRPAREN);
            return p;
        }
        if (Tk k = cur().type; (k >= tABS && k <= tRANDOMIZE) || (k >= tLEN && k <= tRTRIM)) {
            std::string name = upper(advance().value);
            expect(tLPAREN);
            return call(name);
        }
        if (cur().type == tIDENTIFIER) {
            const std::string& name = advance().value;
            if (cur().type == tLPAREN) { advance(); return call(name); }
            Expr* v = ne(EK::Variable);
            v->s = name; v->type = typeOfName(name);
            if (match(tLBRACKET)) {
                do v->args.push_back(expression()); while (match(tCOMMA));
                expect(tRBRACKET);
            }
            return v;
        }
        throw Thrown(std::string("Unexpected token in expression: ") + kTkName[cur().type] + " (value: '" + cur().value + "') at line " + std::to_string(cur().line));
    }
    Expr* named(EK k, const char* n) { Expr* e = ne(k); e->s = n; return e; }

    // float.Parse (en-US): [-]digits[.digits][e[+-]digits]; overflow gives infinity.
    float parseFloat(const Token& tk) {
        std::string_view v = tk.value;
        size_t i = v[0] == '-' ? 1 : 0, d = i;
        while (i < v.size() && isDigit(v[i])) ++i;
        bool ok = i > d;
        if (i < v.size() && v[i] == '.') { ++i; while (i < v.size() && isDigit(v[i])) ++i; }
        if (ok && i < v.size() && (v[i] | 32) == 'e') {
            size_t j = i + 1;
            if (j < v.size() && (v[j] == '+' || v[j] == '-')) ++j;
            size_t k = j;
            while (k < v.size() && isDigit(v[k])) ++k;
            i = k > j ? k : v.size() + 1;
        }
        if (!ok || i != v.size())
            throw Thrown("Failed to parse float from token '" + tk.value + "' (Type: FLOAT) at line " + std::to_string(tk.line) +
                         ": The input string '" + tk.value + "' was not in a correct format.");
        // strtof, not from_chars: Apple's libc++ has no float from_chars. Out of range rounds to inf/0, as .NET does.
        float f = std::strtof(std::string(v.substr(v[0] == '-')).c_str(), nullptr);
        return v[0] == '-' ? -f : f;
    }
};

// --- Emitter -------------------------------------------------------------------------------------
const struct { const char* name; const char* text; } kHelpers[] = {
#include "basic_helpers.inc"
};
const struct { const char* name; const char* needs; const char* text; } kHelpersD18[] = {
#include "basic_helpers_d18.inc"
};

struct Sym { VT type; std::string label; std::vector<int> dims; };

class Emitter {
public:
    // ref: emit exactly what the reference emits. Otherwise (D-18) the emitted code is fixed where the
    // reference's was wrong: lines the assembler cannot encode, and the string runtime.
    Emitter(uint32_t start, bool ref) : start_(start), cur_(start), ref_(ref) {}
    std::vector<std::string> errors;

    std::string emit(const std::vector<Stmt*>& prog) {
        if (prog.empty()) {
            errors.push_back("Program has no statements");
            return "#ORG " + hexN(start_, 6) + "\n    RET  ; Empty program\n";
        }
        collect(prog);
        cur_ = start_;
        L("#ORG ", hexN(start_, 6));
        for (Stmt* s : prog) statement(s);
        std::string out = a_;
        if (out.size() <= 120 && u16len(out) <= 30) { L("    RET  ; No code generated"); out = a_; }

        auto section = [](const char* title) {
            const char* rule = "; ---------------------------------------------------------\n";
            return std::string("\n") + rule + "; " + title + "\n" + rule;
        };
        if (printInt_) out += section("SHARED SUBROUTINES") + printIntSubroutine();
        if (!helpers_.empty()) {
            if (!printInt_) out += section("SHARED SUBROUTINES");
            for (auto& h : helpers_) {
                if (ref_) { for (auto& k : kHelpers) if (h == k.name) out += replaceAll(k.text, "\n", "\r\n"); }
                else for (auto& k : kHelpersD18) if (h == k.name) out += replaceAll(k.text, "\n", "\r\n");
            }
        }
        if (!d_.empty() || printInt_) {
            out += section("DATA SECTION") + d_;
            if (printInt_)
                out += ".string_print_int_buffer\n"
                       "    #DB 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0  ; Reserve 12 characters, enough to print the largest int number with minus in front.\n";
        }
        if (!v_.empty()) out += section("VARIABLES") + v_;
        return out;
    }

private:
    uint32_t start_, cur_;   // cur_: the reference's size estimate; only GOTO/GOSUB print it
    std::string a_, d_, v_;  // code, data section, variable section
    int labels_ = 0, strings_ = 0, temps_ = 0, helperBufs_ = 0;
    std::unordered_map<std::string, uint32_t> labelAddr_;
    std::vector<std::string> intPool_, floatPool_;   // stacks (LIFO), duplicates allowed as in the reference
    std::unordered_map<std::string, std::string> strLits_;
    bool printInt_ = false;
    std::vector<std::string> helpers_;               // first-use order
    std::unordered_map<std::string, Sym> syms_;

    static void put(std::string& o, std::string_view s) { o += s; }
    static void put(std::string& o, const char* s) { o += s; }
    static void put(std::string& o, const std::string& s) { o += s; }
    static void put(std::string& o, int v) { o += std::to_string(v); }
    template <class... A> void L(const A&... a) { (put(a_, a), ...); a_ += "\r\n"; }
    template <class... A> void D(const A&... a) { (put(d_, a), ...); d_ += "\r\n"; }
    template <class... A> void V(const A&... a) { (put(v_, a), ...); v_ += "\r\n"; }

    bool ref_;                                       // reference output (see the constructor)
    std::vector<std::string> settings_;              // D-18: INK/FONT/LOCATE... variables declared so far

    void needHelper(const char* h) {
        for (auto& x : helpers_) if (x == h) return;
        helpers_.push_back(h);
        if (!ref_)   // D-18 routines share code: emit what this one calls too
            for (auto& k : kHelpersD18)
                if (!std::strcmp(k.name, h))
                    for (std::string_view n = k.needs; !n.empty();) {
                        size_t e = std::min(n.find(' '), n.size());
                        needHelper(std::string(n.substr(0, e)).c_str());
                        n.remove_prefix(std::min(e + 1, n.size()));
                    }
    }

    // --- D-18: register classes. A temp is a 32-bit register (ABCD..YZAB) or a float one (F0..F15);
    // conditions leave an 8-bit A. Instructions take operands of one width, so narrower uses name the
    // temp's low registers (an address is the low 24 bits).
    static bool isF(const std::string& r) { return r.size() >= 2 && r[0] == 'F' && r[1] >= '0' && r[1] <= '9'; }
    static bool isR32(const std::string& r) { return r.size() == 4 && !isF(r); }
    static std::string lo(const std::string& r, size_t n) { return r.size() > n && !isF(r) ? r.substr(r.size() - n) : r; }
    std::string fit(const std::string& r, size_t n) { return ref_ ? r : lo(r, n); }
    static std::string floatBitsHex(float f) { uint32_t u; std::memcpy(&u, &f, 4); return "0x" + hexN(u, 8); }

    // r converted to the class t needs: 32-bit for integers and string addresses, float for floats.
    std::string as(const std::string& r, VT t) {
        bool wantF = t == VT::Float;
        if (ref_ || (wantF ? isF(r) : isR32(r))) return r;
        if (!isF(r) && !isR32(r)) {   // 8..24-bit: widen through the stack, the temp may contain r
            std::string w = temp(VT::Integer);
            L("    PUSH ", r);
            L("    LD ", w, ", 0");
            L("    POP ", lo(w, r.size()));
            return as(w, t);
        }
        std::string d = temp(wantF ? VT::Float : VT::Integer);
        L("    LD ", d, ", ", r);
        if (wantF) {   // LD fr, rrrr reads the integer as unsigned: convert a negative one's magnitude
            std::string ok = ".to_float_" + std::to_string(labels_++);
            L("    SCP ", r, ", 0");
            L("    JR GTE, ", ok);
            L("    INV ", r);
            L("    INC ", r);
            L("    LD ", d, ", ", r);
            L("    MUL ", d, ", 0xBF800000  ; -1.0");
            L(ok);
        }
        release(r, VT::Integer);
        return d;
    }

    // Loads each (source temp, input register) pair through the register stack: no input is overwritten
    // before it is read, whichever temps the sources are.
    void pass(const std::vector<std::pair<std::string, std::string>>& in) {
        for (auto& [src, dst] : in) L("    PUSH ", lo(src, dst.size()));
        for (size_t i = in.size(); i-- > 0;) L("    POP ", in[i].second);
    }

    std::string temp(VT t) {
        auto& pool = t == VT::Float ? floatPool_ : intPool_;
        if (!pool.empty()) { std::string r = pool.back(); pool.pop_back(); return r; }
        static const char* const ints[] = {"ABCD", "EFGH", "IJKL", "MNOP", "QRST", "UVWX", "YZAB"};
        static const char* const floats[] = {"F0", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "F13", "F14", "F15"};
        int i = temps_++;
        return t == VT::Float ? floats[i % 16] : ints[i % 7];
    }
    void release(const std::string& r, VT t) {
        if (ref_) { (t == VT::Float ? floatPool_ : intPool_).push_back(r); return; }
        // D-18: only temps go back, each once (the reference pooled A/B too, and twice)
        auto& pool = isF(r) ? floatPool_ : intPool_;
        if ((isF(r) || isR32(r)) && std::find(pool.begin(), pool.end(), r) == pool.end()) pool.push_back(r);
    }

    // D-18: #DB text for s and its 0: quotes and control characters as numbers (a raw line break split
    // the line, and the assembler has no escapes).
    static std::string dbText(const std::string& s) {
        std::string o;
        bool open = false;
        for (unsigned char c : s) {
            bool plain = c >= 0x20 && c != '"';
            if (plain != open || !plain) { if (open) o += '"'; if (!o.empty()) o += ", "; if (plain) o += '"'; open = plain; }
            if (plain) o += char(c); else o += std::to_string(c);
        }
        return o + (open ? "\", 0" : o.empty() ? "0" : ", 0");
    }

    static std::string varLabel(std::string_view name, VT t) {
        while (!name.empty() && (name.back() == '#' || name.back() == '$')) name.remove_suffix(1);
        std::string base;
        for (unsigned char c : name) {
            if (c < 0x80) base += isLetterOrDigit(c) || c == '_' ? char(c) : '_';
            else if (c >= 0xC0) base += c >= 0xF0 ? "__" : "_";   // one '_' per UTF-16 unit
        }
        return std::string(".var_") + (t == VT::Integer ? "int" : t == VT::Float ? "float" : "str") + "_" + base;
    }
    Sym& symbol(const std::string& name, VT t) {   // the reference's "allocate if missing" pattern
        auto it = syms_.find(name);
        if (it != syms_.end()) return it->second;
        Sym& s = syms_[name] = Sym{t, varLabel(name, t), {}};
        declare(s, nullptr);
        return s;
    }

    void declare(const Sym& s, const std::string* init) {
        V(s.label);
        bool isArray = !s.dims.empty();
        int32_t total = s.type == VT::String ? 256 : 4;
        if (isArray) {
            uint32_t n = 1;
            for (int d : s.dims) n *= uint32_t(d + 1);   // C# int arithmetic: wraps
            total = int32_t(n * uint32_t(total));
        }
        if (s.type == VT::String && init && !init->empty() && !ref_) {
            V("    #DB ", dbText(*init));   // D-18: an assignment copies 16 bytes, so reserve at least 16
            if (init->size() < 15) V("    #DB [", int(15 - init->size()), "] 0  ; Room for 16 bytes");
        } else if (s.type == VT::String && init && !init->empty()) {
            std::string e = replaceAll(replaceAll(replaceAll(replaceAll(*init, "\"", "\\\""), "\n", "\\n"), "\r", "\\r"), "\t", "\\t");
            V("    #DB \"", e, "\", 0");
        } else if (s.type == VT::String) {
            if (isArray)
                for (int i = 0; i < total / 16; ++i) V("    #DB 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0  ; String array element ", i);
            else V("    #DB 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0  ; Reserve 16 bytes for string");
        } else if (isArray) {
            int elements = total / 4;
            for (int i = 0; i < elements; i += 16) {
                std::string line = "    #DB";
                for (int j = 0; j < std::min(16, elements - i); ++j) line += " 0, 0, 0, 0";
                if (i == 0) line += "  ; Array: " + std::to_string(elements) + " elements (" + std::to_string(total) + " bytes total)";
                V(line);
            }
        } else V("    #DB 0, 0, 0, 0");
    }

    std::string allocString(const std::string& s) {
        auto it = strLits_.find(s);
        if (it != strLits_.end()) return it->second;
        std::string label = ".string" + std::to_string(strings_++);
        D(label);
        if (ref_) D("    #DB \"", replaceAll(s, "\"", "\"\""), "\", 0");
        else D("    #DB ", dbText(s));
        return strLits_[s] = label;
    }

    void collect(const std::vector<Stmt*>& prog) {
        uint32_t addr = start_;
        for (Stmt* s : prog) {
            if (s->k == SK::Label) labelAddr_[s->name] = addr;
            else if (s->k == SK::Dim) {
                Sym sym{s->type, varLabel(s->name, s->type), {}};
                for (Expr* d : s->e) if (d && d->k == EK::Literal && d->type == VT::Integer) sym.dims.push_back(d->ival);
                declare(syms_[s->name] = sym, nullptr);
            } else addr += 10;   // EstimateStatementSize: always 10
        }
    }

    // --- statements ---
    void statement(Stmt* s) {
        auto& e = s->e;
        switch (s->k) {
            case SK::Label: L(".", s->name); labelAddr_[s->name] = cur_; break;
            case SK::Assignment: assignment(s); break;
            case SK::Print: print(s); break;
            case SK::If: ifStmt(s); break;
            case SK::For: forStmt(s); break;
            case SK::While: {
                std::string startL = ".while_start_" + std::to_string(labels_), endL = ".while_end_" + std::to_string(labels_);
                ++labels_;
                L(startL);
                std::string c = expr(e[0]);
                L("    CP ", c, ", 0");
                L("    JR Z, ", endL);
                for (Stmt* b : s->body) statement(b);
                L("    JP ", startL);
                L(endL);
                break;
            }
            case SK::Goto: case SK::Gosub: {
                const char* op = s->k == SK::Goto ? "JP" : "CALL";
                auto it = labelAddr_.find(s->name);
                if (!ref_) L("    ", op, " .", s->name);   // D-18: the reference wrote its size estimate, without 0x
                else if (it != labelAddr_.end()) L("    ", op, " ", hexN(it->second, 6));
                else L("    ", op, " .", s->name, "  ; Forward reference");
                cur_ += 7;
                break;
            }
            case SK::Return: L("    RET"); cur_ += 1; break;
            case SK::End: L("    RET  ; END of program"); cur_ += 2; break;
            case SK::Cls:
                if (!ref_) {   // D-18: the page is computed before A is set (a temp may contain A)
                    std::string p = e[0] ? expr(e[0], VT::Integer) : "";
                    L("    LD A, 0x05  ; ClearVideoPage");
                    L("    LD B, ", p.empty() ? "0" : lo(p, 1));
                    L("    LD C, 0  ; Color 0");
                    L("    INT 0x01, A");
                    if (!p.empty()) release(p, VT::Integer);
                    break;
                }
                L("    LD A, 0x05  ; ClearVideoPage");
                if (e[0]) L("    LD B, ", expr(e[0], VT::Integer));
                else L("    LD B, 0");
                L("    LD C, 0  ; Color 0");
                L("    INT 0x01, A");
                cur_ += 15;
                break;
            case SK::Wait: case SK::Sleep:
                if (!ref_) { waitMs(e[0]); break; }   // D-18: WAIT takes only a constant
                if (s->k == SK::Wait) { L("    WAIT ", expr(e[0], VT::Integer)); cur_ += 5; break; }
                {
                std::string ms = expr(e[0], VT::Integer);
                L("    ; SLEEP ", ms, " ms");
                L("    LD A, ", ms);
                L("    DIV A, 17  ; Approximate frames (60 FPS)");
                L("    WAIT A");
                cur_ += 10;
                break;
            }
            case SK::Plot: plot(s); break;
            case SK::Line: case SK::Rectangle: case SK::Circle: case SK::Ellipse: shape(s); break;
            case SK::Screen: {
                std::string p = expr(e[0], VT::Integer);
                L("    ; SCREEN ", p, " - set active video page");
                L("    LD A, 0x03  ; ReadVideoAddress (placeholder)");
                L("    LD B, ", fit(p, 1));
                L("    INT 0x01, A");
                cur_ += 10;
                break;
            }
            case SK::Video:
                { std::string p = expr(e[0], VT::Integer);
                L("    LD A, 0x02  ; SetVideoPagesCount");
                L("    LD B, ", fit(p, 1)); }
                L("    INT 0x01, A");
                cur_ += 10;
                break;
            case SK::Ink: case SK::Paper: {
                bool ink = s->k == SK::Ink;
                std::string c = expr(e[0], VT::Integer);
                L("    ; ", ink ? "INK " : "PAPER ", c, ink ? " - set current color" : " - set background color");
                if (!ref_) store(ink ? ".currentColor" : ".currentPaper", c);
                else L("    LD ", ink ? ".currentColor, " : ".currentPaper, ", c);
                cur_ += 10;
                break;
            }
            case SK::Input: input(s); break;
            case SK::Beep: beep(s); break;
            case SK::Play: { std::string r = expr(e[0], VT::Integer); L("    PLAY ", fit(r, 3)); release(r, VT::Integer); cur_ += 5; break; }
            case SK::LoadFont: {
                std::string f = expr(e[0], VT::String), ad = expr(e[1], VT::Integer);
                if (!ref_) {
                    pass({{f, "BCD"}, {ad, "EFG"}});
                    L("    LD A, 0x40  ; LoadPNGFont");
                    L("    INT 0x04, A");
                    release(f, VT::String);
                    release(ad, VT::Integer);
                    break;
                }
                L("    LD A, 0x40  ; LoadPNGFont");
                L("    LD BCD, ", f);
                L("    LD EFG, ", ad);
                L("    INT 0x04, A");
                release(f, VT::String);
                release(ad, VT::Integer);
                cur_ += 15;
                break;
            }
            case SK::Font: setting(e[0], "    ; FONT ", " - set current font", ".currentFont"); break;
            case SK::FontColor: setting(e[0], "    ; FONT COLOR ", "", ".currentFontColor"); break;
            case SK::FontFlags: setting(e[0], "    ; FONT FLAGS ", "", ".currentFontFlags"); break;
            case SK::FontMaxWidth: setting(e[0], "    ; FONT MAXWIDTH ", "", ".currentFontMaxWidth"); break;
            case SK::FontOutline: {
                std::string c = expr(e[0], VT::Integer), p = expr(e[1], VT::Integer);
                L("    ; FONT OUTLINE ", c, ", ", p);
                if (!ref_) { store(".currentFontOutlineColor", c); store(".currentFontOutlinePattern", p); }
                else {
                    L("    LD .currentFontOutlineColor, ", c);
                    L("    LD .currentFontOutlinePattern, ", p);
                }
                release(c, VT::Integer);
                release(p, VT::Integer);
                cur_ += 15;
                break;
            }
            case SK::LayerShow: case SK::LayerHide: {
                bool show = s->k == SK::LayerShow;
                std::string r = expr(e[0], VT::Integer);
                L("    LD A, 0x30  ; ReadLayerVisibility");
                L("    INT 0x01, A  ; Get current mask in A");
                L("    LD B, 1");
                L("    SL B, ", fit(r, 1), "  ; Shift 1 by layer number");
                if (show) L("    OR A, B  ; Set bit");
                else { L("    INV B  ; Invert to create mask"); L("    AND A, B  ; Clear bit"); }
                L("    LD B, A");
                L("    LD A, 0x31  ; SetLayersVisibility");
                L("    INT 0x01, A");
                release(r, VT::Integer);
                cur_ += 20;
                break;
            }
            case SK::LayerVisibility: {
                std::string r = expr(e[0], VT::Integer);
                L("    LD A, 0x31  ; SetLayersVisibility");
                L("    LD B, ", fit(r, 1));
                L("    INT 0x01, A");
                release(r, VT::Integer);
                cur_ += 10;
                break;
            }
            case SK::Sprite: sprite(s); break;
            case SK::Locate: {
                std::string x = expr(e[0], VT::Integer), y = expr(e[1], VT::Integer);
                L("    ; LOCATE ", x, ", ", y);
                if (!ref_) { store(".currentTextX", x); store(".currentTextY", y); }
                else {
                    L("    LD .currentTextX, ", x);
                    L("    LD .currentTextY, ", y);
                }
                release(x, VT::Integer);
                release(y, VT::Integer);
                cur_ += 15;
                break;
            }
            case SK::Data:
                for (Expr* v : e) {
                    if (!v || v->k != EK::Literal) continue;
                    if (v->type == VT::Integer) D("    #DB ", v->ival);
                    else if (v->type == VT::Float) D("    #DB ", netFloat(v->fval));
                    else if (!ref_) D("    #DB ", dbText(v->s));
                    else D("    #DB \"", v->s, "\", 0");
                }
                break;
            case SK::Poke: {
                std::string ad = expr(e[0], VT::Integer), v = expr(e[1], VT::Integer);
                if (!ref_) L("    LD (", lo(ad, 3), "), ", lo(v, s->size == 1 ? 1 : s->size / 8));   // D-18: the value's low bytes
                else L("    LD (", ad, "), ", v, s->size == 1 ? "" : s->size == 16 ? ", 2" : s->size == 24 ? ", 3" : ", 4");
                release(ad, VT::Integer);
                release(v, VT::Integer);
                cur_ += 10;
                break;
            }
            case SK::MemCopy: case SK::MemFill: {
                std::string r0 = expr(e[0], VT::Integer), r1 = expr(e[1], VT::Integer), r2 = expr(e[2], VT::Integer);
                L(s->k == SK::MemCopy ? "    MEMC " : "    MEMF ", fit(r0, 3), ", ", fit(r1, 3), ", ", fit(r2, s->k == SK::MemCopy ? 3 : 1));
                release(r0, VT::Integer);
                release(r1, VT::Integer);
                release(r2, VT::Integer);
                cur_ += 10;
                break;
            }
            default: errors.push_back(std::string("Unsupported statement: ") + kStmtName[int(s->k)]); break;
        }
    }

    // D-18: the reference wrote "LD .label, r" (no such form) to labels it never declared
    void store(const std::string& var, const std::string& r) {
        if (std::find(settings_.begin(), settings_.end(), var) == settings_.end()) {
            settings_.push_back(var);
            V(var);
            V("    #DB 0, 0, 0, 0");
        }
        L("    LD (", var, "), ", r);
    }
    // SLEEP ms / WAIT n (both milliseconds, as WAIT n in the reference): WAIT 1 per unit
    void waitMs(Expr* e) {
        if (e && e->k == EK::Literal && e->type == VT::Integer && e->ival >= 0 && e->ival <= 0xFFFF) { L("    WAIT ", e->ival); return; }
        std::string r = expr(e, VT::Integer), loop = ".wait_" + std::to_string(labels_++);
        L(loop);
        L("    SCP ", r, ", 0");
        L("    JR LTE, ", loop, "_end");
        L("    WAIT 1");
        L("    DEC ", r);
        L("    JR ", loop);
        L(loop, "_end");
        release(r, VT::Integer);
    }

    void setting(Expr* e, const char* comment, const char* tail, const char* var) {
        std::string r = expr(e, VT::Integer);
        L(comment, r, tail);
        if (!ref_) store(var, r);
        else L("    LD ", var, ", ", r);
        release(r, VT::Integer);
        cur_ += 10;
    }

    void assignment(Stmt* s) {
        Expr* var = s->e[0];
        Expr* value = s->e[1];
        const std::string* init = var->type == VT::String && value && value->k == EK::Literal && value->type == VT::String ? &value->s : nullptr;
        auto it = syms_.find(var->s);
        Sym* sym;
        if (it == syms_.end()) {
            sym = &(syms_[var->s] = Sym{var->type, varLabel(var->s, var->type), {}});
            declare(*sym, init);
            if (init) return;   // the literal is the declaration's initial value
        } else sym = &it->second;

        std::string r = expr(value, var->type);
        if (var->type == VT::Integer) {
            if (r == "A" || (r.size() == 1 && r != "F")) {   // 8-bit boolean: widen into a 32-bit temp that does not contain it
                std::string t32;
                int attempts = 0;
                do {
                    t32 = temp(VT::Integer);
                    if (++attempts > 10) {
                        L("    LD Z, ", r, "  ; Save 8-bit boolean to safe register");
                        r = "Z";
                        t32 = temp(VT::Integer);
                        break;
                    }
                } while (t32.find(r) != std::string::npos);
                L("    LD ", t32, ", 0  ; Clear register");
                L("    LD ", t32.substr(t32.size() - 1), ", ", r, "  ; Load 8-bit boolean result into lower byte");
                cur_ += 12;
                r = t32;
            }
            L("    LD (", sym->label, "), ", r);
            cur_ += 7;
        } else if (var->type == VT::Float) {
            L("    LD (", sym->label, "), ", r);
            cur_ += 7;
        } else if (!ref_) {   // D-18: the 24-bit address, and a 0 at the end of the 16 bytes
            L("    MEMC ", lo(r, 3), ", ", sym->label, ", 16  ; Copy string (16 bytes max)");
            L("    LD ", lo(r, 1), ", 0");
            L("    LD (", sym->label, " + 15), ", lo(r, 1), "  ; Keep it terminated");
        } else {
            L("    MEMC ", r, ", ", sym->label, ", 16  ; Copy string (16 bytes max)");
            cur_ += 10;
        }
        release(r, var->type);
    }

    void print(Stmt* s) {
        auto& e = s->e;
        if (s->at && !ref_) {   // D-18: every operand first, then the registers (a temp may overlap them)
            std::vector<std::pair<std::string, std::string>> in = {{expr(e[pAtX], VT::Integer), "CD"}, {expr(e[pAtY], VT::Integer), "EF"}};
            if (e[pFont]) in.push_back({expr(e[pFont], VT::Integer), "GHI"});
            if (e[pColor]) in.push_back({expr(e[pColor], VT::Integer), "J"});
            std::string text;   // as in the reference, only the last item reaches KLM
            for (auto& it : s->items) { if (!text.empty()) release(text, VT::String); text = expr(it.e, VT::String); }
            if (!text.empty()) in.push_back({text, "KLM"});
            if (e[pFlags]) in.push_back({expr(e[pFlags], VT::Integer), "N"});
            if (e[pMaxWidth]) in.push_back({expr(e[pMaxWidth], VT::Integer), "OP"});
            if (e[pOutColor] && e[pOutPattern]) {
                in.push_back({expr(e[pOutColor], VT::Integer), "Q"});
                in.push_back({expr(e[pOutPattern], VT::Integer), "R"});
            }
            pass(in);
            L("    LD A, 0x14  ; DrawText function");
            if (!e[pFont]) L("    LD GHI, 0  ; Use default font");
            if (!e[pColor]) L("    LD J, 15  ; Default color");
            L("    INT 0x01, A");
            for (auto& i : in) release(i.first, VT::Integer);
        } else if (s->at) {
            std::string x = expr(e[pAtX], VT::Integer), y = expr(e[pAtY], VT::Integer);
            L("    LD A, 0x14  ; DrawText function");
            L("    LD CD, ", x);
            L("    LD EF, ", y);
            if (e[pFont]) L("    LD GHI, ", expr(e[pFont], VT::Integer));
            else L("    LD GHI, 0  ; Use default font");
            if (e[pColor]) L("    LD J, ", expr(e[pColor], VT::Integer));
            else L("    LD J, 15  ; Default color");
            for (auto& it : s->items) L("    LD KLM, ", expr(it.e, VT::String), "  ; String address");
            if (e[pFlags]) L("    LD N, ", expr(e[pFlags], VT::Integer));
            if (e[pMaxWidth]) L("    LD OP, ", expr(e[pMaxWidth], VT::Integer));
            if (e[pOutColor] && e[pOutPattern]) {
                std::string c = expr(e[pOutColor], VT::Integer), p = expr(e[pOutPattern], VT::Integer);
                L("    LD Q, ", c);
                L("    LD R, ", p);
            }
            L("    INT 0x01, A");
            cur_ += 30;
        } else if (e[pTab]) {
            L("    ; TAB(", expr(e[pTab], VT::Integer), ") - cursor positioning");
            for (auto& it : s->items) {
                std::string r = expr(it.e, VT::String);
                if (!ref_) {
                    pass({{r, "BCD"}});
                    L("    LD A, 0x12  ; DrawString function");
                    release(r, VT::String);
                } else {
                    L("    LD A, 0x12  ; DrawString function");
                    L("    LD BCD, ", r);
                }
                L("    INT 0x01, A");
            }
            cur_ += 20;
        } else {
            for (auto& it : s->items) {
                Expr* x = it.e;
                if (x && x->k == EK::Literal) {
                    if (x->type == VT::String) drawString(allocString(x->s));
                    else if (x->type == VT::Integer) {
                        printInt_ = true;
                        L("    LD BCDE, ", x->ival, "  ; Load integer value");
                        L("    CALL .PrintInt");
                        cur_ += 15;
                    }
                } else if (x && x->k == EK::Variable) {
                    Sym& v = symbol(x->s, typeOfName(x->s));
                    if (v.type == VT::String) drawString(v.label);
                    else {
                        printInt_ = true;
                        L("    LD BCDE, (", v.label, ")  ; Load variable value (32-bit)");
                        L("    CALL .PrintInt");
                        cur_ += 15;
                    }
                } else {
                    printInt_ = true;
                    std::string r = expr(x, VT::Integer);
                    L("    LD BCDE, ", r, "  ; Load expression result");
                    L("    CALL .PrintInt");
                    release(r, VT::Integer);
                    cur_ += 15;
                }
            }
        }
    }
    void drawString(const std::string& addr) {
        L("    LD A, 0x12  ; DrawString function");
        L("    LD BCD, 0  ; Default font");
        L("    LD EFG, ", addr, "  ; String address");
        L("    LD HI, 0  ; X position (default)");
        L("    LD JK, 0  ; Y position (default)");
        L("    LD L, 1  ; Color (default)");
        L("    LD M, 2  ; Video page");
        L("    INT 0x01, A  ; Execute interrupt");
        cur_ += 30;
    }

    void ifStmt(Stmt* s) {
        std::string c = expr(s->e[0], VT::Integer);
        std::string elseL = ".else_" + std::to_string(labels_), endL = ".endif_" + std::to_string(labels_);
        ++labels_;
        L("    CP ", c, ", 0");
        L("    JR Z, ", elseL);
        if (c != "A") release(c, VT::Integer);
        for (Stmt* b : s->body) statement(b);
        if (!s->alt.empty() || !s->elseIfs.empty()) L("    JP ", endL);
        L(elseL);
        for (Stmt* ei : s->elseIfs) {
            std::string eiL = ".elseif_" + std::to_string(labels_++);
            std::string c2 = expr(ei->e[0]);
            L("    CP ", c2, ", 0");
            L("    JR Z, ", eiL);
            for (Stmt* b : ei->body) statement(b);
            L("    JP ", endL);
            L(eiL);
        }
        for (Stmt* b : s->alt) statement(b);
        L(endL);
    }

    void forStmt(Stmt* s) {
        Expr *start = s->e[0], *end = s->e[1], *step = s->e[2];
        auto floatLit = [](Expr* x) { return x && x->k == EK::Literal && x->type == VT::Float; };
        auto intLit = [](Expr* x) { return x && x->k == EK::Literal && x->type == VT::Integer; };
        bool isFloat = floatLit(step) || floatLit(start) || floatLit(end);
        Sym& v = symbol(s->name, isFloat ? VT::Float : VT::Integer);
        std::string n = std::to_string(labels_++);
        std::string startL = ".for_start_" + n, endL = ".for_end_" + n, stepL = ".for_step_" + n;
        if (!isFloat) {
            if (intLit(start)) L("    LD (", v.label, "), ", start->ival, ", 4  ; Initialize loop variable (32-bit)");
            else {
                std::string r = expr(start, VT::Integer);
                L("    LD (", v.label, "), ", r, "  ; Initialize loop variable (32-bit)");
                release(r, VT::Integer);
            }
            L(startL);
            std::string endR = expr(end, VT::Integer), val = temp(VT::Integer);
            L("    LD ", val, ", (", v.label, ")  ; Load loop variable");
            L("    CP ", val, ", ", endR, "  ; Compare 32-bit values");
            L("    JR GT, ", endL);
            release(endR, VT::Integer);
            for (Stmt* b : s->body) statement(b);
            L(stepL);
            L("    LD ", val, ", (", v.label, ")  ; Reload loop variable");
            if (!step) L("    ADD ", val, ", 1  ; Increment by 1 (32-bit)");
            else if (intLit(step)) L("    ADD ", val, ", ", step->ival, "  ; Add step (32-bit)");
            else {
                std::string r = expr(step, VT::Integer);
                L("    ADD ", val, ", ", r, "  ; Add step (32-bit)");
                release(r, VT::Integer);   // released after the store in C#; no output in between
            }
            L("    LD (", v.label, "), ", val, "  ; Store loop variable");
            release(val, VT::Integer);
        } else {
            std::string r = expr(start, VT::Float);
            L("    LD (", v.label, "), ", r, "  ; Initialize loop variable (float)");
            release(r, VT::Float);
            L(startL);
            std::string endR = expr(end, VT::Float), val = temp(VT::Float);
            L("    LD ", val, ", (", v.label, ")  ; Load loop variable (float)");
            L("    CP ", val, ", ", endR, "  ; Compare float values");
            L("    JR GT, ", endL);
            release(endR, VT::Float);
            for (Stmt* b : s->body) statement(b);
            L(stepL, ":");
            std::string sr;
            if (step && floatLit(step)) {
                sr = temp(VT::Float);
                L("    LD ", sr, ", ", ref_ ? hexN(swappedBits(step->fval), 8) : floatBitsHex(step->fval), "  ; Load step value (float)");
            } else if (step) sr = expr(step, VT::Float);
            else {
                sr = temp(VT::Float);
                L("    LD ", sr, ", 0x3F800000  ; Load 1.0 (float)");
            }
            L("    LD ", val, ", (", v.label, ")  ; Load loop variable");
            L("    ADD ", val, ", ", sr, "  ; Add step (float)");
            L("    LD (", v.label, "), ", val, "  ; Store loop variable");
            release(sr, VT::Float);
            release(val, VT::Float);
        }
        L("    JP ", startL);
        L(endL);
    }

    // D-18: an interrupt call whose operands are computed first and then passed (a temp may overlap the
    // interrupt's registers, A included); fixed lines (function, page, defaults) follow.
    void interrupt(std::vector<std::pair<std::string, std::string>> in, std::initializer_list<const char*> fixed, const char* intLine) {
        pass(in);
        for (const char* f : fixed) L(f);
        L(intLine);
        for (auto& i : in) release(i.first, VT::Integer);
    }

    void plot(Stmt* s) {
        if (!ref_) {
            std::vector<std::pair<std::string, std::string>> in = {{expr(s->e[0], VT::Integer), "CD"}, {expr(s->e[1], VT::Integer), "EF"}};
            if (s->e[2]) in.push_back({expr(s->e[2], VT::Integer), "G"});
            if (s->e[2]) interrupt(in, {"    LD A, 0x20  ; Plot function", "    LD B, 0  ; Page 0"}, "    INT 0x01, A");
            else interrupt(in, {"    LD A, 0x20  ; Plot function", "    LD B, 0  ; Page 0", "    LD G, 15  ; Default color"}, "    INT 0x01, A");
            return;
        }
        L("    LD A, 0x20  ; Plot function");
        L("    LD B, 0  ; Page 0");
        static const char* const regs[] = {"CD", "EF", "G"};
        static const char* const notes[] = {"  ; X coordinate", "  ; Y coordinate", "  ; Color"};
        for (int i = 0; i < 3; ++i) {
            Expr* x = s->e[i];
            if (!x && i == 2) { L("    LD G, 15  ; Default color"); break; }
            if (x && x->k == EK::Literal && x->type == VT::Integer) L("    LD ", regs[i], ", ", x->ival, notes[i]);
            else {
                std::string r = expr(x, VT::Integer);
                L("    LD ", regs[i], ", ", r, notes[i]);
                release(r, VT::Integer);
            }
        }
        L("    INT 0x01, A");
        cur_ += 25;
    }

    // LINE, RECTANGLE, CIRCLE, ELLIPSE: four (circle: three) operands, then the optional colour.
    void shape(Stmt* s) {
        bool circle = s->k == SK::Circle;
        int n = circle ? 3 : 4;
        std::string r[4];
        for (int i = 0; i < n; ++i) r[i] = expr(s->e[i], VT::Integer);
        if (!ref_) {
            const char* fn = s->k == SK::Line ? "    LD A, 0x21  ; Line function"
                           : s->k == SK::Rectangle ? (s->filled ? "    LD A, 0x06  ; DrawFilledRectangle" : "    LD A, 0x07  ; DrawRectangle")
                           : s->filled ? "    LD A, 0x23  ; DrawFilledEllipse" : "    LD A, 0x22  ; Ellipse";
            std::vector<std::pair<std::string, std::string>> in = {{r[0], "CD"}, {r[1], "EF"}, {r[2], "GH"}, {circle ? r[2] : r[3], "IJ"}};
            if (s->e[n]) in.push_back({expr(s->e[n], VT::Integer), "K"});
            if (s->e[n]) interrupt(in, {fn, "    LD B, 0  ; Page 0"}, "    INT 0x01, A");
            else interrupt(in, {fn, "    LD B, 0  ; Page 0", "    LD K, 15  ; Default color"}, "    INT 0x01, A");
            return;
        }
        switch (s->k) {
            case SK::Line: L("    LD A, 0x21  ; Line function"); break;
            case SK::Rectangle: L("    LD A, ", s->filled ? "0x06  ; DrawFilledRectangle" : "0x07  ; DrawRectangle"); break;
            default: L("    LD A, ", s->filled ? "0x23  ; DrawFilledEllipse" : "0x22  ; Ellipse"); break;
        }
        L("    LD B, 0  ; Page 0");
        L("    LD CD, ", r[0]);
        L("    LD EF, ", r[1]);
        bool ell = s->k == SK::Ellipse;
        L("    LD GH, ", r[2], circle || ell ? "  ; radiusX" : "");
        L("    LD IJ, ", circle ? r[2] : r[3], circle ? "  ; radiusY (same for circle)" : ell ? "  ; radiusY" : "");
        if (s->e[n]) L("    LD K, ", expr(s->e[n], VT::Integer));
        else L("    LD K, 15  ; Default color");
        L("    INT 0x01, A");
        cur_ += 25;
    }

    void input(Stmt* s) {
        if (Expr* p = s->e[0]; p && !ref_)
            interrupt({{expr(p, VT::String), "EFG"}}, {"    LD A, 0x12  ; DrawString", "    LD BCD, 0  ; Default font", "    LD HI, 0  ; X",
                       "    LD JK, 0  ; Y", "    LD L, 15  ; Color", "    LD M, 0  ; Page"}, "    INT 0x01, A");
        else if (p) {
            std::string r = expr(p, VT::String);
            L("    LD A, 0x12  ; DrawString");
            L("    LD BCD, 0  ; Default font");
            L("    LD EFG, ", r);
            L("    LD HI, 0  ; X");
            L("    LD JK, 0  ; Y");
            L("    LD L, 15  ; Color");
            L("    LD M, 0  ; Page");
            L("    INT 0x01, A");
            release(r, VT::String);
        }
        Expr* var = s->e[1];
        Sym& v = symbol(var->s, var->type);
        L("    LD A, 0x01  ; ReadKeyboardBuffer");
        L("    INT 0x02, A  ; Read character into A");
        L("    LD (", v.label, "), A");
        if (var->type == VT::String && !ref_) {   // D-18: the reference loaded the byte instead of storing 0
            L("    LD B, 0  ; Null terminator");
            L("    LD (", v.label, " + 1), B");
        } else if (var->type == VT::String) {
            L("    LD B, (", v.label, " + 1)");
            L("    LD B, 0  ; Null terminator");
        }
        cur_ += 15;
    }

    void beep(Stmt* s) {
        std::string dur = expr(s->e[0], VT::Float), pitch = expr(s->e[1], VT::Float);
        std::string vol = s->e[2] ? expr(s->e[2], VT::Float) : ref_ ? "F2" : temp(VT::Float);   // D-18: F2 may be dur or pitch
        if (!s->e[2]) L("    LD ", vol, ", 0x3F000000  ; 0.5 (default volume)");
        std::string snd = ".beep_sound_" + std::to_string(labels_++);
        D(snd);
        D("    #DB 0x00, 0x00  ; Flags: no optional parameters");
        D("    #DB 0.0  ; Frequency (will be set at runtime)");
        D("    #DB 0.0  ; Sustain (will be set at runtime)");
        D("    #DB 0.0  ; Volume (will be set at runtime)");
        L("    LD BCD, ", snd);
        L("    ADD BCD, 2  ; Skip flags");
        L("    LD (BCD), ", pitch, "  ; Store frequency");
        L("    ADD BCD, 4");
        L("    LD (BCD), ", dur, "  ; Store sustain");
        L("    ADD BCD, 4");
        L("    LD (BCD), ", vol, "  ; Store volume");
        L("    LD XYZ, ", snd);
        L("    PLAY XYZ");
        release(dur, VT::Float);
        release(pitch, VT::Float);
        if (s->e[2] || !ref_) release(vol, VT::Float);
        cur_ += 30;
    }

    void sprite(Stmt* s) {
        std::string r[5];
        for (int i = 0; i < 5; ++i) r[i] = expr(s->e[i], VT::Integer);
        if (!ref_) {
            std::vector<std::pair<std::string, std::string>> in = {{r[2], "BCD"}, {r[0], "EF"}, {r[1], "GH"}, {r[3], "IJ"}, {r[4], "KL"}};
            if (s->e[5]) in.push_back({expr(s->e[5], VT::Integer), "M"});
            if (s->e[5]) interrupt(in, {"    LD A, 0x10  ; DrawSpriteToVideoPage"}, "    INT 0x01, A");
            else interrupt(in, {"    LD A, 0x10  ; DrawSpriteToVideoPage", "    LD M, 0  ; Default page 0"}, "    INT 0x01, A");
            return;
        }
        std::string page = s->e[5] ? expr(s->e[5], VT::Integer) : "B";
        if (!s->e[5]) L("    LD B, 0  ; Default page 0");
        L("    LD A, 0x10  ; DrawSpriteToVideoPage");
        L("    LD BCD, ", r[2]);
        L("    LD EF, ", r[0]);
        L("    LD GH, ", r[1]);
        L("    LD IJ, ", r[3]);
        L("    LD KL, ", r[4]);
        L("    LD M, ", page);
        L("    INT 0x01, A");
        for (auto& x : r) release(x, VT::Integer);
        if (s->e[5]) release(page, VT::Integer);
        cur_ += 25;
    }

    // --- expressions: each returns the register holding the result ---
    std::string expr(Expr* x, VT t = VT::Integer) { return as(value(x, t), t); }   // D-18: in the class t needs
    std::string value(Expr* x, VT t) {
        if (!x) throw Thrown(kNullRef);
        switch (x->k) {
            case EK::Literal: {
                std::string r = temp(!ref_ && x->type == VT::String ? VT::Integer : t);
                if (!ref_ && x->type != VT::String) {   // D-18: a constant in the register's own format
                    float fv = x->type == VT::Float ? x->fval : float(x->ival);
                    if (t == VT::Float) L("    LD ", r, ", ", floatBitsHex(fv));
                    else L("    LD ", r, ", ", x->type == VT::Integer ? x->ival : fv > -2147483649.0f && fv < 2147483648.0f ? int32_t(fv) : 0);
                    return r;
                }
                if (x->type == VT::Integer) { L("    LD ", r, ", ", x->ival); cur_ += 9; }
                else if (x->type == VT::Float) { L("    LD ", r, ", ", hexN(swappedBits(x->fval), 8)); cur_ += 9; }
                else { L("    LD ", r, ", ", allocString(x->s)); cur_ += 7; }
                return r;
            }
            case EK::Variable: return variable(x, t);
            case EK::Binary: return binary(x, t);
            case EK::Unary: {
                if (!ref_ && x->op != tMINUS) t = VT::Integer;   // D-18: INV has integer forms only
                std::string o = expr(x->l, t), r = temp(t);
                if (x->op == tMINUS) { L("    LD ", r, ", 0"); L("    SUB ", r, ", ", o); }
                else { L("    LD ", r, ", ", o); L("    INV ", r); }
                cur_ += 10;
                return r;
            }
            case EK::Call: return call(x, t);
            case EK::Peek: {
                std::string ad = expr(x->l, VT::Integer), r = temp(ref_ ? t : VT::Integer);
                if (!ref_) {   // D-18: a load as wide as the PEEK, into the temp's low bytes
                    size_t n = x->size == 1 ? 1 : size_t(x->size / 8);
                    if (n < 4) L("    LD ", r, ", 0");
                    L("    LD ", lo(r, n), ", (", lo(ad, 3), ")");
                } else L("    LD ", r, ", (", ad, ")", x->size == 1 ? "" : x->size == 16 ? ", 2" : x->size == 24 ? ", 3" : ", 4");
                release(ad, VT::Integer);
                cur_ += 10;
                return r;
            }
            case EK::VarPtr: {
                auto it = syms_.find(x->l->s);
                if (it == syms_.end()) { errors.push_back("Variable " + x->l->s + " not found"); return "A"; }
                std::string r = temp(VT::Integer);
                L("    LD ", r, ", ", it->second.label);
                cur_ += 7;
                return r;
            }
            case EK::Time: case EK::Ticks: {
                bool time = x->k == EK::Time;
                std::string r = temp(VT::Integer);
                std::string dest = (time ? ".time_dest_" : ".ticks_dest_") + std::to_string(labels_++);
                D(dest);
                D(time ? "    #DB 0, 0, 0, 0  ; 32-bit milliseconds" : "    #DB 0, 0, 0, 0, 0, 0, 0, 0  ; 64-bit ticks");
                L("    LD A, 0x03  ; ReadClock");
                L(time ? "    LD B, 0x00  ; Mode: milliseconds" : "    LD B, 0x01  ; Mode: ticks");
                L("    LD CDE, ", dest);
                L("    INT 0x00, A");
                L("    LD ", r, ", (", dest, ")", time ? "" : "  ; Lower 32 bits");
                cur_ += 15;
                return r;
            }
            case EK::MouseX: case EK::MouseY: {
                std::string r = temp(VT::Integer);
                mouseRead("    INT 0x02, A  ; X in CD, Y in EF, buttons in I");
                if (!ref_) { release(r, VT::Integer); return x->k == EK::MouseX ? "CD" : "EF"; }   // D-18: widened by as()
                L("    LD ", r, x->k == EK::MouseX ? ", CD" : ", EF");
                cur_ += 15;
                return r;
            }
            case EK::MouseButton: {
                std::string b = expr(x->l, VT::Integer), r = temp(VT::Integer);
                if (!ref_) L("    PUSH ", lo(b, 1), "  ; Button number (the read changes A..J)");
                mouseRead("    INT 0x02, A  ; Buttons in I");
                L("    LD A, I  ; Button state");
                if (!ref_) L("    POP B  ; Button number");
                else L("    LD B, ", b, "  ; Button number");
                L("    BIT A, B  ; Test bit");
                L("    LD A, 1");
                L("    JR NZ, .mouse_btn_", labels_);
                L("    LD A, 0");
                L(".mouse_btn_", labels_++);
                release(b, VT::Integer);
                if (!ref_) { release(r, VT::Integer); return "A"; }   // D-18: widened by as()
                L("    LD ", r, ", A");
                cur_ += 20;
                return r;
            }
            case EK::Inkey: {
                L("    LD A, 0x01  ; ReadKeyboardBuffer");
                L("    INT 0x02, A  ; Character in A, remaining in B");
                if (x->isString) {
                    std::string r = temp(VT::String);
                    std::string label = ".inkey_str_" + std::to_string(labels_++);
                    D(label);
                    D(ref_ ? "    #DB 0  ; Null terminator" : "    #DB 0, 0  ; Character, null terminator");
                    if (!ref_) L("    LD (", label, "), A  ; Store character");   // D-18: before r, which may contain A
                    L("    LD ", r, ", ", label);
                    if (ref_) L("    LD (", label, "), A  ; Store character");
                    cur_ += 10;
                    return r;
                }
                if (!ref_) return "A";   // D-18: widened by as()
                std::string r = temp(VT::Integer);
                L("    LD ", r, ", A");
                cur_ += 5;
                return r;
            }
        }
        return "A";
    }
    void mouseRead(const char* intLine) {
        L("    LD A, 0x03  ; ReadMouseState");
        L("    LD BCD, 0  ; X register (destination)");
        L("    LD EFG, 0  ; Y register (destination)");
        L("    LD HIJ, 0  ; Button register (destination)");
        L(intLine);
    }

    std::string variable(Expr* x, VT t) {
        Sym& v = symbol(x->s, x->type);
        if (!ref_) {   // D-18: loaded in its own format (as() converts), an element through a 24-bit address
            std::string r = temp(v.type);
            if (!x->args.empty() && !v.dims.empty()) {
                std::string idx = expr(x->args[0], VT::Integer), ad = v.type == VT::Float ? temp(VT::Integer) : r;
                L("    MUL ", idx, ", ", v.type == VT::String ? 256 : 4);
                L("    LD ", ad, ", ", v.label);
                L("    ADD ", ad, ", ", idx);
                if (v.type != VT::String) L("    LD ", r, ", (", lo(ad, 3), ")");   // a string element is its address
                release(idx, VT::Integer);
                if (ad != r) release(ad, VT::Integer);
            } else if (v.type == VT::String) L("    LD ", r, ", ", v.label);
            else L("    LD ", r, ", (", v.label, ")");
            return r;
        }
        std::string r = temp(t);
        if (!x->args.empty() && !v.dims.empty()) {
            int size = v.type == VT::String ? 256 : 4;
            std::string idx = expr(x->args[0], VT::Integer), tmp = temp(VT::Integer);
            L("    LD ", tmp, ", ", idx);
            L("    MUL ", tmp, ", ", size);
            L("    LD ", r, ", ", v.label);
            L("    ADD ", r, ", ", tmp);
            L("    LD ", r, ", (", r, ")");
            cur_ += 25;
            return r;
        }
        if (v.type == VT::String) L("    LD ", r, ", ", v.label);
        else L("    LD ", r, ", (", v.label, ")");
        cur_ += 7;
        return r;
    }

    VT typeOf(Expr* x) {
        if (!x) return VT::Integer;
        switch (x->k) {
            case EK::Literal: return x->type;
            case EK::Variable: {
                auto it = syms_.find(x->s);
                return it != syms_.end() ? it->second.type : typeOfName(x->s);
            }
            case EK::Call: {
                std::string f = upper(x->s);
                for (const char* n : {"LEFT", "RIGHT", "MID", "CHR", "STR", "STRING", "UCASE", "LCASE", "TRIM", "LTRIM", "RTRIM"})
                    if (f == n) return VT::String;
                if (!ref_) return natural(f, x->args);
                return f == "VAL" ? VT::Float : VT::Integer;
            }
            case EK::Binary:
                if (x->op == tPLUS && (typeOf(x->l) == VT::String || typeOf(x->r) == VT::String)) return VT::String;
                if (!ref_) return operands(x->op, x->l, x->r);
                return VT::Integer;
            case EK::Unary:
                if (!ref_ && x->op == tMINUS) return typeOf(x->l);
                return VT::Integer;
            default: return VT::Integer;
        }
    }

    // D-18: the class a call computes in (the reference used the caller's, which some ops have no form for)
    VT natural(const std::string& f, const std::vector<Expr*>& a) {
        for (const char* n : {"FLOOR", "CEIL", "ROUND", "SIN", "COS", "TAN", "SQR", "POW", "PI", "E", "ABS", "SGN"})
            if (f == n) return VT::Float;
        if (f == "MIN" || f == "MAX")
            for (Expr* e : a) if (typeOf(e) == VT::Float) return VT::Float;
        return VT::Integer;
    }
    // D-18: the class a binary operator's operands are computed in (its result's, but compares give integers)
    VT operands(Tk op, Expr* l, Expr* r) {
        switch (op) {
            case tPLUS: case tMINUS: case tMULTIPLY: case tDIVIDE: case tEQUAL: case tNOT_EQUAL: case tLESS_THAN:
            case tGREATER_THAN: case tLESS_EQUAL: case tGREATER_EQUAL:
                return typeOf(l) == VT::Float || typeOf(r) == VT::Float ? VT::Float : VT::Integer;
            case tPOWER: return VT::Float;
            default: return VT::Integer;
        }
    }
    static bool comparison(Tk op) { return op == tEQUAL || op == tNOT_EQUAL || op == tLESS_THAN || op == tGREATER_THAN || op == tLESS_EQUAL || op == tGREATER_EQUAL; }

    std::string binary(Expr* b, VT t) {
        Tk op = b->op;
        bool logical = op == tAND || op == tOR || op == tXOR || op == tNAND || op == tNOR || op == tXNOR || op == tIMPLY;
        if (!ref_ && !(op == tPLUS && typeOf(b) == VT::String)) t = operands(op, b->l, b->r);   // D-18: one class for both
        std::string left = expr(b->l, logical ? VT::Integer : t);
        bool leftBool = left == "A";
        bool saved = logical && leftBool;
        if (saved) { L("    LD B, ", left, "  ; Save left boolean result"); cur_ += 3; }
        std::string right = expr(b->r, logical ? VT::Integer : t);
        bool rightBool = right == "A";
        if (saved) left = "B";
        std::string res = left;

        auto arith = [&](const char* m) { L("    ", m, " ", res, ", ", right); cur_ += 5; release(right, t); };
        auto compare = [&](const char* cond, const char* name, const char* colon) {
            if (!ref_) {   // D-18: the 0/1 goes into a 32-bit temp (A may be part of a live one)
                std::string r = isR32(res) ? res : temp(VT::Integer);
                L("    CP ", res, ", ", right);
                L("    LD ", r, ", 1");
                L("    JR ", cond, ", .", name, "_", labels_);
                L("    LD ", r, ", 0");
                L(".", name, "_", labels_++, colon);
                release(right, t);
                if (r != res) release(res, t);
                return r;
            }
            L("    CP ", res, ", ", right);
            L("    LD A, 1");
            L("    JR ", cond, ", .", name, "_", labels_);
            L("    LD A, 0");
            L(".", name, "_", labels_, colon);
            ++labels_;
            cur_ += 20;
            release(right, t);
            release(res, t);
            return std::string("A");
        };
        switch (op) {
            case tPLUS:
                if (typeOf(b->l) == VT::String || typeOf(b->r) == VT::String) return concat(left, right);
                arith("ADD");
                break;
            case tMINUS: arith("SUB"); break;
            case tMULTIPLY: arith("MUL"); break;
            case tDIVIDE: arith("DIV"); break;
            case tINT_DIVIDE:
                if (!ref_) { arith("DIV"); break; }   // D-18: integers already; INT is the interrupt instruction
                L("    DIV ", res, ", ", right);
                L("    INT ", res, ", ", res);
                cur_ += 10;
                release(right, t);
                break;
            case tMODULO:
                if (!ref_) {   // D-18: DIV's third operand takes the remainder (the reference divided)
                    std::string rem = temp(VT::Integer);
                    L("    DIV ", res, ", ", right, ", ", rem);
                    release(right, t);
                    release(res, t);
                    return rem;
                }
                arith("DIV");
                break;
            case tPOWER: arith("POW"); break;
            case tEQUAL: return compare("EQ", "eq", "");
            case tNOT_EQUAL: return compare("NE", "ne", "");
            case tLESS_THAN: return compare("LT", "lt", "");
            case tGREATER_THAN: return compare("GT", "gt", "");
            case tLESS_EQUAL: return compare("LTE", "le", ":");
            case tGREATER_EQUAL: return compare("GTE", "ge", ":");
            case tAND: case tOR: case tXOR: case tNAND: case tNOR: case tXNOR: case tIMPLY: {
                const char* m = kTkName[op];
                if (leftBool && !rightBool && right.size() > 1) {
                    L("    ", m, " ", res, ", ", right.substr(right.size() - 1), "  ; Logical ", m, " (boolean)");
                } else if (!leftBool && rightBool) {
                    res = res.substr(res.size() - 1);
                    L("    ", m, " ", res, ", ", right, "  ; Logical ", m, " (boolean)");
                } else L("    ", m, " ", res, ", ", right);
                release(right, t);
                if ((leftBool || rightBool) && res != "A") {
                    L("    LD A, ", res, "  ; Move boolean result to A");
                    cur_ += 3;
                    res = "A";
                }
                cur_ += 5;
                break;
            }
            case tSHL: case tSHR: case tROL: case tROR: {
                const char* m = op == tSHL ? "SL" : op == tSHR ? "SR" : op == tROL ? "RL" : "RR";
                Expr* r = b->r;
                if (!ref_) {   // D-18: the count by its low byte (evaluated twice as in the reference: same errors)
                    if (r && r->k == EK::Literal && r->type == VT::Integer && r->ival >= 0 && r->ival <= 31) L("    ", m, " ", res, ", ", r->ival);
                    else {
                        std::string sh = expr(r, VT::Integer);
                        L("    ", m, " ", res, ", ", lo(sh, 1));
                        release(sh, VT::Integer);
                    }
                    release(right, t);
                } else if (r && r->k == EK::Literal && r->type == VT::Integer && r->ival >= 0 && r->ival <= 31)
                    L("    ", m, " ", res, ", ", r->ival);
                else {
                    std::string sh = expr(r, VT::Integer);   // the right operand, emitted a second time
                    if (sh.size() == 1) L("    ", m, " ", res, ", ", sh);
                    else L("    ", m, " ", res, ", ", sh.substr(sh.size() - 1), "  ; Use lower 8 bits of ", sh);
                    release(sh, VT::Integer);
                }
                cur_ += 5;
                break;
            }
            default: break;
        }
        return res;
    }

    std::string concat(const std::string& left, const std::string& right) {
        std::string buf = ".strcat_buf_" + std::to_string(helperBufs_++);
        D(buf);
        D("    #DB 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0  ; 32 bytes for concatenated string");
        needHelper("StrCat");
        if (!ref_) {
            runtimeCall("StrCat", {{left, "BCD"}, {right, "EFG"}}, buf);
            std::string r = temp(VT::String);
            L("    LD ", r, ", ", buf);
            return r;
        }
        L("    PUSH A, U  ; Preserve registers");
        L("    LD FGH, ", left, "  ; First string address");
        L("    LD IJK, ", right, "  ; Second string address");
        L("    LD LMN, ", buf, "  ; Result buffer address");
        L("    CALL .StrCat");
        L("    POP A, U  ; Restore registers");
        release(left, VT::String);
        release(right, VT::String);
        std::string r = temp(VT::String);
        L("    LD ", r, ", ", buf);
        cur_ += 50;
        return r;
    }

    // A string function's 32-byte (or other) result buffer in the data section.
    std::string buffer(const char* kind, const char* db) {
        std::string label = std::string(".str") + kind + "_buf_" + std::to_string(helperBufs_++);
        D(label);
        D(db);
        return label;
    }
    static constexpr const char* k32 = "    #DB 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0  ; 32 bytes";
    std::string result(const std::string& buf, uint32_t size) {
        std::string r = temp(VT::String);
        L("    LD ", r, ", ", buf);
        cur_ += size;
        return r;
    }

    // D-18: a call into the string runtime (basic_helpers_d18.inc). Saves A..U, passes the inputs, then either
    // leaves the result in buf or (no buf) takes KLMN into a fresh temp, which the restore then skips.
    std::string runtimeCall(const char* helper, const std::vector<std::pair<std::string, std::string>>& in,
                            const std::string& buf, const char* preload = nullptr) {
        needHelper(helper);
        std::string res = buf.empty() ? temp(VT::Integer) : "";
        std::vector<std::string> saved;   // A..U runs that are not res
        for (int i = 0; i <= 20;) {
            if (res.find(char('A' + i)) != std::string::npos) { ++i; continue; }
            int j = i;
            while (j < 20 && res.find(char('A' + j + 1)) == std::string::npos) ++j;
            saved.push_back(i == j ? std::string(1, char('A' + i)) : std::string{char('A' + i), ',', ' ', char('A' + j)});
            i = j + 1;
        }
        for (auto& s : saved) L("    PUSH ", s, "  ; Preserve registers");
        pass(in);
        if (preload) L(preload);
        if (!buf.empty()) L("    LD HIJ, ", buf, "  ; Result buffer");
        L("    CALL .", helper);
        if (!res.empty()) L("    LD ", res, ", KLMN  ; Result");
        for (size_t i = saved.size(); i-- > 0;) L("    POP ", saved[i], "  ; Restore registers");
        for (auto& i : in) release(i.first, VT::Integer);
        return res;
    }
    std::optional<std::string> stringCall(const std::string& name, const std::vector<Expr*>& a) {
        size_t n = a.size();
        auto str = [&](size_t i) { return expr(a[i], VT::String); };
        auto num = [&](size_t i) { return expr(a[i], VT::Integer); };
        auto bad = [&](const std::string& msg) { errors.push_back(msg); return temp(VT::Integer); };
        auto toBuf = [&](const char* helper, const char* kind, const char* db, const std::vector<std::pair<std::string, std::string>>& in,
                         const char* preload = nullptr) {
            std::string buf = buffer(kind, db);
            runtimeCall(helper, in, buf, preload);
            return result(buf, 0);
        };
        if (name == "LEN" || name == "ASC") {
            if (n != 1) return bad(name == "LEN" ? "LEN requires 1 argument" : "ASC requires 1 argument");
            return runtimeCall(name == "LEN" ? "StrLen" : "StrAsc", {{str(0), "BCD"}}, "");
        }
        if (name == "VAL") {
            if (n != 1) return bad("VAL requires 1 argument");
            return runtimeCall("StrVal", {{str(0), "BCD"}}, "");
        }
        if (name == "INSTR") {
            if (n < 2 || n > 3) return bad("INSTR requires 2 or 3 arguments");
            std::vector<std::pair<std::string, std::string>> in;
            if (n == 3) in.push_back({num(0), "KLMN"});
            std::string s1 = str(n - 2), s2 = str(n - 1);
            in.push_back({s1, "BCD"});
            in.push_back({s2, "EFG"});
            return runtimeCall("StrInstr", in, "", n == 3 ? nullptr : "    LD KLMN, 1  ; Start position (default)");
        }
        if (name == "LEFT" || name == "RIGHT") {
            bool left = name == "LEFT";
            if (n != 2) return bad(left ? "LEFT$ requires 2 arguments" : "RIGHT$ requires 2 arguments");
            std::string s = str(0), c = num(1);
            return toBuf(left ? "StrLeft" : "StrRight", left ? "left" : "right", k32, {{s, "BCD"}, {c, "KLMN"}});
        }
        if (name == "MID") {
            if (n < 2 || n > 3) return bad("MID$ requires 2 or 3 arguments");
            std::string s = str(0), st = num(1);
            if (n == 2) return toBuf("StrMid", "mid", k32, {{s, "BCD"}, {st, "KLMN"}}, "    LD OPQR, -1  ; Length: to the end");
            std::string ln = num(2);
            return toBuf("StrMid", "mid", k32, {{s, "BCD"}, {st, "KLMN"}, {ln, "OPQR"}});
        }
        if (name == "CHR") {
            if (n != 1) return bad("CHR$ requires 1 argument");
            return toBuf("StrChr", "chr", "    #DB 0, 0  ; 2 bytes for single character + null", {{num(0), "KLMN"}});
        }
        if (name == "STRING") {
            if (n != 2) return bad("STRING$ requires 2 arguments");
            std::string c = num(0), s = str(1);
            return toBuf("StrString", "string", k32, {{c, "KLMN"}, {s, "EFG"}});
        }
        if (name == "UCASE" || name == "LCASE" || name == "TRIM" || name == "LTRIM" || name == "RTRIM") {
            std::string lower = name;
            for (char& c : lower) c = char(c | 32);
            if (n != 1) return bad(name + "$ requires 1 argument");
            std::string helper = "Str" + name.substr(0, 1) + lower.substr(1);
            return toBuf(helper.c_str(), lower.c_str(), k32, {{str(0), "BCD"}});
        }
        if (name == "STR") {   // INT 0x05 IntToString / FloatToString; "" = .NET's "G" ("{0}" printed its braces)
            if (n != 1) return bad("STR$ requires 1 argument");
            bool fl = typeOf(a[0]) == VT::Float;
            std::string v = expr(a[0], fl ? VT::Float : VT::Integer);
            std::string buf = buffer("str", k32), fmt = allocString("");
            L("    PUSH A, U  ; Preserve registers");
            if (fl) {   // B = float register, CDE = format, FGH = target
                L("    LD A, 0x01  ; FloatToString function");
                L("    LD B, ", v.substr(1), "  ; Float register index");
                L("    LD CDE, ", fmt, "  ; Format string");
                L("    LD FGH, ", buf, "  ; Target buffer");
            } else {    // BCDE = value, FGH = format, IJK = target
                L("    LD BCDE, ", v, "  ; Integer value (before A, which it may contain)");
                L("    LD A, 0x03  ; IntToString function");
                L("    LD FGH, ", fmt, "  ; Format string");
                L("    LD IJK, ", buf, "  ; Target buffer");
            }
            L("    INT 0x05, A  ; Convert");
            L("    POP A, U  ; Restore registers");
            release(v, fl ? VT::Float : VT::Integer);
            return result(buf, 0);
        }
        return std::nullopt;
    }

    std::string call(Expr* f, VT t) {
        std::string name = upper(f->s);
        if (!ref_) {
            if (auto r = stringCall(name, f->args)) return *r;
            t = natural(name, f->args);   // D-18: computed in its own class, as() converts
        }
        std::string res = temp(t);
        auto& a = f->args;
        auto n = a.size();
        // ABS(x) style: LD res, arg / OP res
        auto unaryOp = [&](const char* op, const char* err, VT argT) {
            if (n != 1) throw Thrown(err);
            std::string r = expr(a[0], argT);
            L("    LD ", res, ", ", r);
            if (!ref_ && !std::strcmp(op, "INT")) { release(r, VT::Float); return; }   // D-18: LD truncates (INT is the interrupt)
            L("    ", op, " ", res);
            cur_ += 8;
        };
        auto binaryOp = [&](const char* op, const char* err, VT argT) {
            if (n != 2) throw Thrown(err);
            std::string r1 = expr(a[0], argT), r2 = expr(a[1], argT);
            L("    LD ", res, ", ", r1);
            L("    ", op, " ", res, ", ", r2);
            cur_ += 8;
        };
        auto bad = [&](const char* msg) { errors.push_back(msg); return res; };

        if (name == "ABS") unaryOp("ABS", "ABS requires 1 argument", t);
        else if (name == "SGN") unaryOp("ISGN", "SGN requires 1 argument", t);
        else if (name == "INT" || name == "FIX") unaryOp("INT", "INT requires 1 argument", VT::Float);
        else if (name == "FLOOR") unaryOp("FLOOR", "FLOOR requires 1 argument", VT::Float);
        else if (name == "CEIL") unaryOp("CEIL", "CEIL requires 1 argument", VT::Float);
        else if (name == "ROUND") {
            if (n < 1 || n > 2) throw Thrown("ROUND requires 1-2 arguments");
            std::string r = expr(a[0], VT::Float);
            L("    LD ", res, ", ", r);
            L("    ROUND ", res);
            cur_ += 8;
        } else if (name == "SIN") unaryOp("SIN", "SIN requires 1 argument", VT::Float);
        else if (name == "COS") unaryOp("COS", "COS requires 1 argument", VT::Float);
        else if (name == "TAN") unaryOp("TAN", "TAN requires 1 argument", VT::Float);
        else if (name == "SQR") unaryOp("SQR", "SQR requires 1 argument", VT::Float);
        else if (name == "POW") binaryOp("POW", "POW requires 2 arguments", VT::Float);
        else if (name == "MIN") binaryOp("MIN", "MIN requires 2 arguments", t);
        else if (name == "MAX") binaryOp("MAX", "MAX requires 2 arguments", t);
        else if (name == "RND") {
            L("    RAND ", res);
            if (n > 0) {
                std::string r = expr(a[0], VT::Integer);
                if (!ref_) {   // D-18: no MOD instruction; DIV's third operand is the remainder
                    std::string rem = temp(VT::Integer);
                    L("    DIV ", res, ", ", r, ", ", rem);
                    L("    INC ", rem);
                    release(r, VT::Integer);
                    release(res, VT::Integer);
                    return rem;
                }
                L("    MOD ", res, ", ", r);
                L("    INC ", res);
            }
            cur_ += 5;
        } else if (name == "PI" || name == "E") {
            float v = name == "PI" ? 3.14159265358979323846f : 2.71828182845904523536f;
            L("    LD ", res, ", ", ref_ ? hexN(swappedBits(v), 8) : floatBitsHex(v));
            cur_ += 9;
        } else if (name == "LEN" || name == "ASC") {
            bool len = name == "LEN";
            if (n != 1) return bad(len ? "LEN requires 1 argument" : "ASC requires 1 argument");
            std::string s = expr(a[0], VT::String);
            needHelper(len ? "StrLen" : "StrAsc");
            L("    PUSH A, U  ; Preserve registers");
            L("    LD FGH, ", s, "  ; String address");
            L(len ? "    CALL .StrLen" : "    CALL .StrAsc");
            L("    LD ", res, len ? ", A  ; Length result" : ", A  ; ASCII code result");
            L("    POP A, U  ; Restore registers");
            release(s, VT::String);
            cur_ += 30;
        } else if (name == "LEFT" || name == "RIGHT") {
            bool left = name == "LEFT";
            if (n != 2) return bad(left ? "LEFT$ requires 2 arguments" : "RIGHT$ requires 2 arguments");
            std::string s = expr(a[0], VT::String), cnt = expr(a[1], VT::Integer);
            std::string buf = buffer(left ? "left" : "right", k32);
            needHelper(left ? "StrLeft" : "StrRight");
            L("    PUSH A, U  ; Preserve registers");
            L("    LD FGH, ", s, "  ; String address");
            L("    LD BCDE, ", cnt, "  ; Number of characters");
            L("    LD IJK, ", buf, "  ; Result buffer");
            L(left ? "    CALL .StrLeft" : "    CALL .StrRight");
            L("    POP A, U  ; Restore registers");
            release(s, VT::String);
            release(cnt, VT::Integer);
            return result(buf, 40);
        } else if (name == "MID") {
            if (n < 2 || n > 3) return bad("MID$ requires 2 or 3 arguments");
            std::string s = expr(a[0], VT::String), st = expr(a[1], VT::Integer);
            std::string ln = n == 3 ? expr(a[2], VT::Integer) : "";
            std::string buf = buffer("mid", k32);
            needHelper("StrMid");
            L("    PUSH A, U  ; Preserve registers");
            L("    LD FGH, ", s, "  ; String address");
            L("    LD BCDE, ", st, "  ; Start position (1-based)");
            if (n == 3) L("    LD EFGH, ", ln, "  ; Length");
            else L("    LD EFGH, 0xFFFFFFFF  ; Length = -1 (to end)");
            L("    LD IJK, ", buf, "  ; Result buffer");
            L("    CALL .StrMid");
            L("    POP A, U  ; Restore registers");
            release(s, VT::String);
            release(st, VT::Integer);
            if (n == 3) release(ln, VT::Integer);
            return result(buf, 45);
        } else if (name == "CHR") {
            if (n != 1) return bad("CHR$ requires 1 argument");
            std::string c = expr(a[0], VT::Integer);
            std::string buf = buffer("chr", "    #DB 0, 0  ; 2 bytes for single character + null");
            needHelper("StrChr");
            L("    PUSH A, U  ; Preserve registers");
            L("    LD BCDE, ", c, "  ; ASCII code");
            L("    LD FGH, ", buf, "  ; Result buffer");
            L("    CALL .StrChr");
            L("    POP A, U  ; Restore registers");
            release(c, VT::Integer);
            return result(buf, 35);
        } else if (name == "VAL") {
            if (n != 1) return bad("VAL requires 1 argument");
            std::string s = expr(a[0], VT::String);
            needHelper("StrVal");
            L("    PUSH A, U  ; Preserve registers");
            L("    LD FGH, ", s, "  ; String address");
            L("    CALL .StrVal");
            L("    LD ", res, ", BCDE  ; Numeric result (32-bit)");
            L("    POP A, U  ; Restore registers");
            release(s, VT::String);
            cur_ += 30;
        } else if (name == "STR") {
            if (n != 1) return bad("STR$ requires 1 argument");
            VT at = typeOf(a[0]);
            std::string num = expr(a[0], at);
            std::string buf = buffer("str", "    #DB 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0  ; 12 bytes");
            std::string fmt = allocString("{0}");
            L("    PUSH A, U  ; Preserve registers");
            if (at == VT::Float) {
                L("    LD A, 0x01  ; FloatToString function");
                L("    LD B, 0  ; Float register index (F0)");
                L("    LD F0, ", num, "  ; Load float value");
            } else {
                L("    LD A, 0x03  ; IntToString function");
                L("    LD BCDE, ", num, "  ; Integer value");
            }
            L("    LD FGH, ", fmt, "  ; Format string");
            L("    LD IJK, ", buf, "  ; Target buffer");
            L("    INT 0x05, A  ; Convert");
            L("    POP A, U  ; Restore registers");
            release(num, at);
            return result(buf, 40);
        } else if (name == "STRING") {
            if (n != 2) return bad("STRING$ requires 2 arguments");
            std::string cnt = expr(a[0], VT::Integer), ch = expr(a[1], VT::String);
            std::string buf = buffer("string", k32);
            needHelper("StrString");
            L("    PUSH A, U  ; Preserve registers");
            L("    LD BCDE, ", cnt, "  ; Number of repetitions");
            L("    LD FGH, ", ch, "  ; Character string address");
            L("    LD IJK, ", buf, "  ; Result buffer");
            L("    CALL .StrString");
            L("    POP A, U  ; Restore registers");
            release(cnt, VT::Integer);
            release(ch, VT::String);
            return result(buf, 40);
        } else if (name == "INSTR") {
            if (n < 2 || n > 3) return bad("INSTR requires 2 or 3 arguments");
            std::string st = n == 3 ? expr(a[0], VT::Integer) : "";
            std::string s1 = expr(a[n - 2], VT::String), s2 = expr(a[n - 1], VT::String);
            needHelper("StrInstr");
            L("    PUSH A, U  ; Preserve registers");
            if (n == 3) L("    LD BCDE, ", st, "  ; Start position");
            else L("    LD BCDE, 1  ; Start position (default)");
            L("    LD FGH, ", s1, "  ; First string address");
            L("    LD IJK, ", s2, "  ; Second string address");
            L("    CALL .StrInstr");
            L("    LD ", res, ", BCDE  ; Position result (0 if not found)");
            L("    POP A, U  ; Restore registers");
            if (n == 3) release(st, VT::Integer);
            release(s1, VT::String);
            release(s2, VT::String);
            cur_ += 40;
        } else if (name == "UCASE" || name == "LCASE" || name == "TRIM" || name == "LTRIM" || name == "RTRIM") {
            std::string lower = name;
            for (char& c : lower) c = char(c | 32);
            std::string helper = "Str" + name.substr(0, 1) + lower.substr(1);
            if (n != 1) { errors.push_back(name + "$ requires 1 argument"); return res; }
            std::string s = expr(a[0], VT::String);
            std::string buf = buffer(lower.c_str(), k32);
            needHelper(helper.c_str());
            L("    PUSH A, U  ; Preserve registers");
            L("    LD FGH, ", s, "  ; String address");
            L("    LD IJK, ", buf, "  ; Result buffer");
            L("    CALL .", helper);
            L("    POP A, U  ; Restore registers");
            release(s, VT::String);
            return result(buf, 35);
        } else {
            errors.push_back("Unsupported function: " + f->s);
            return "A";
        }
        return res;
    }

    std::string printIntSubroutine() {
        std::string fmt = allocString(ref_ ? "{0}" : "");   // D-18: "{0}" printed its braces
        std::string s;
        for (const char* l : {".PrintInt", "    ; Input: BCDE contains the 32-bit integer to print",
                              "    ; Use a fixed temporary buffer for string conversion", "    PUSH A, M  ; Save registers",
                              "    ; 1. Convert Integer to String", "    LD A, 0x03  ; IntToString function ID",
                              "    ; BCDE is already loaded with the value", "\x01",
                              "    LD IJK, .string_print_int_buffer  ; Static scratch buffer for string",
                              "    INT 0x05, A  ; Convert", "    ; 2. Call DrawString (Interrupt 0x01)",
                              "    LD A, 0x12  ; DrawString function ID", "    LD BCD, 0  ; Default font",
                              "    LD EFG, .string_print_int_buffer  ; Address of converted string", "    LD HI, 0  ; X",
                              "    LD JK, 0  ; Y", "    LD L, 1  ; Color", "    LD M, 2  ; Video Page",
                              "    INT 0x01, A  ; Execute Draw", "    POP A, M", "    RET"})
            s += (l[0] == 1 ? "    LD FGH, " + fmt + "  ; Format string \"{0}\"" : std::string(l)) + "\r\n";
        return s;
    }
};

} // namespace

BasicResult compileBasic(std::string_view source, uint32_t codeStart, bool reference) {
    BasicResult res;
    res.runAddress = codeStart;
    std::string& log = res.log;
    std::vector<Msg> lexErrors, parseErrors;
    std::vector<Token> tokens = tokenize(source, lexErrors);
    if (!lexErrors.empty()) {
        for (auto& e : lexErrors) log += "Lexer error at line " + std::to_string(e.line) + ": " + e.text + "\n";
        res.errors = int(lexErrors.size());
        res.runAddress = 0;   // the emitter never got its start address
        return res;
    }
    try {
        log += "Tokenized " + std::to_string(tokens.size()) + " tokens:\n";
        for (size_t i = 0; i < tokens.size() && i < 20; ++i)
            log += "  " + std::string(kTkName[tokens[i].type]) + " '" + tokens[i].value + "' at " + std::to_string(tokens[i].line) + ":" + std::to_string(tokens[i].col) + "\n";

        std::deque<Expr> exprs;
        std::deque<Stmt> stmts;
        std::vector<Stmt*> prog = Parser(tokens, exprs, stmts, parseErrors).parse();
        auto list = [&](const char* fmt) {
            for (Stmt* s : prog) log += std::string(fmt) + kStmtName[int(s->k)] + " at line " + std::to_string(s->line) + "\n";
        };
        log += "Parsed program with " + std::to_string(prog.size()) + " statements\n";
        list("  Statement: ");
        if (prog.empty()) { log += "Parser error: No statements found in program\n"; ++res.errors; }
        for (auto& e : parseErrors) { log += "Parser error at line " + std::to_string(e.line) + ": " + e.text + "\n"; ++res.errors; }

        Emitter em(codeStart, reference);
        std::string text = em.emit(prog);
        log += "Parsed " + std::to_string(prog.size()) + " statements\n";
        list("  - ");
        for (auto& e : em.errors) { log += "Emitter error: " + e + "\n"; ++res.errors; }
        res.asmText = std::move(text);
    } catch (const std::exception& e) {
        log += std::string("Compiler error: ") + e.what() + "\n";
        ++res.errors;
        res.asmText.clear();
    }
    return res;
}

} // namespace c93
