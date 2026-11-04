// Continuum 93 assembler, a line-by-line port of the reference's
//   Emulator/Interpreter/Assembler.cs (Assembler, CLine, AsmArgument), Interpret.cs, InterpretArguments.cs,
//   DBArgument.cs, CodeBlockManager.cs, Emulator/DataConverter.cs (number parsing, #DB byte sizes),
//   Mnemonics/Mnemonic.cs (lookups); the encoding table asm_ops.inc is generated from Mnem.OPS.
// Output is byte-identical to the reference (tests/oracle --asm, conformance --asm), except D-9:
//  - An instruction is matched by its "general form" text ("LD r,(rrr,nnn)"). A line with an unknown form or
//    mnemonic is an error and emits nothing (D-9; the reference emitted only the opcode byte, 0x00 for an
//    unknown mnemonic, with no error). Reproduced quirks:
//  - Op is the text up to the first space, so "LD\tA,1" (tab, no space) is one unknown mnemonic; it is
//    uppercased only when a space follows, so a lone "nop" or "#org" is unknown. A lone '\r' stays in its line.
//  - Label names are case-sensitive keys, their validity check is not.
//  - "#include" (lowercase, first on the line) takes the rest of the line as the path, comment included.
//  - Operands are split at every '+', '-' and ','; "1E-5" becomes "1E" and "-5". A sign on a register is dropped.
//  - Integer operands win over floats: "LD F0, 5" encodes the integer 5, not 5.0f.
//  - Labels resolve only in the reference's list of "absolute" ops and in JR/CALLR (relative); any other op
//    (SL, INT, SETVAR, ...) encodes a label as 0. "~x" in an absolute op resolves only if "~x" itself is a key.
//  - Only '.'-labels work in #DB (24-bit); a missing one throws. #RUN takes numbers only (a label gives 0).
//  - Label collection stops at the first duplicate/invalid/double label on a line; later labels are
//    undeclared (encode 0, #DB throws). Only "Unrecognized parameter" lines (and D-9 lines) count as errors.
//  - #DB: .NET regex CSV split (",1" loses the 1; text after a closing quote up to the next comma is dropped),
//    no escapes, non-ASCII -> '?' per UTF-16 unit, number width from its spelling (0x0001 = 2 bytes, 00 = 2 bytes).
//  - Code before the first #ORG takes addresses from 0 but is not emitted. Includes are appended at the end
//    of the including file; the "already included" list is cleared on every nested include.
//  - Where the reference throws (octal > 32 bits, #DB decimal > 32 bits, "()" operand, unknown #DB label,
//    "~x" lookups...), result.fatal is set and no blocks are returned (D-16: the machine halts).
// Not reproduced: block collision messages (log only), culture-specific ToUpper beyond ASCII.
#include "asm.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <unordered_set>

#include "log.h"

namespace c93 {
namespace {

struct AsmOp { const char* key; int primary, code, sub; const char* format; };
struct AsmName { const char* name; int index; };
#include "asm_ops.inc"

struct Fatal { std::string what; };
[[noreturn]] void fatal(std::string what) { throw Fatal{std::move(what)}; }

// A format string ("oooooooo uuuAAAAA BBBBBBBB") parsed once: the reference re-parses it as text per line.
struct Field { int pos = 0, width = 0; bool split = false; };   // split: the letters are not one run
struct Layout {
    bool whole = true, stray = false;   // whole bytes; stray: a character the reference cannot fill
    std::vector<uint8_t> base;          // '1' bits of the format
    Field sub, arg[26];                 // 'o' and 'A'..'Z'
};
Layout layoutOf(const char* format) {
    std::string f;
    for (const char* c = format; *c; ++c) if (*c != ' ') f += *c;
    Layout l;
    l.whole = f.size() % 8 == 0;
    l.base.assign(f.size() / 8, 0);
    for (int i = 0; i < int(f.size()); ++i) {
        char c = f[size_t(i)];
        Field* fd = c == 'o' ? &l.sub : c >= 'A' && c <= 'Z' ? &l.arg[c - 'A'] : nullptr;
        if (fd) {
            if (fd->width && fd->pos + fd->width != i) fd->split = true;
            if (!fd->width) fd->pos = i;
            ++fd->width;
        } else if (c == '1' && size_t(i / 8) < l.base.size()) l.base[size_t(i / 8)] |= uint8_t(0x80 >> (i % 8));
        else if (c != '0' && c != 'u' && c != '1') l.stray = true;
    }
    return l;
}

struct Tables {
    struct Entry { const AsmOp* op; Layout layout; };
    std::unordered_map<std::string_view, Entry> ops;
    std::unordered_map<std::string_view, int> regs, flags;
    Tables() {
        for (auto& o : kOps) ops.emplace(o.key, Entry{&o, o.primary ? Layout{} : layoutOf(o.format)});
        for (auto& r : kRegs) regs.emplace(r.name, r.index);
        for (auto& f : kFlags) flags.emplace(f.name, f.index);
    }
};
const Tables& tables() { static const Tables t; return t; }

// Instructions whose label operands resolve to absolute addresses (CLine.globalAddressingOperations).
const std::unordered_set<std::string_view> kAbsoluteOps = {
    "CALL", "JP", "MEMC", "MEMF", "DJNZ", "DJNZ16", "DJNZ24", "DJNZ32", "CP", "LD", "LD16", "LD24", "LD32",
    "ADD", "ADD16", "ADD24", "ADD32", "SUB", "SUB16", "SUB24", "SUB32", "DIV", "MUL", "FIND", "ISQR", "POW", "SQR",
    "REGS", "FREGS", "PUSH", "PUSH16", "PUSH24", "PUSH32", "POP", "POP16", "POP24", "POP32", "VCL", "VDL",
    "LDF", "LDREGS", "STREGS", "AND", "AND16", "AND24", "AND32", "OR", "OR16", "OR24", "OR32",
    "XOR", "XOR16", "XOR24", "XOR32", "NAND", "NAND16", "NAND24", "NAND32", "NOR", "NOR16", "NOR24", "NOR32",
    "XNOR", "XNOR16", "XNOR24", "XNOR32", "IMPLY", "IMPLY16", "IMPLY24", "IMPLY32", "INC", "INC16", "INC24", "INC32",
    "DEC", "DEC16", "DEC24", "DEC32", "RGB2HSL", "HSL2RGB", "RGB2HSB", "HSB2RGB", "PLAY"};

// ---- .NET string semantics on UTF-8 ------------------------------------------------------------------

// Byte length of the char.IsWhiteSpace / regex \s character at s[i] (0 if none): ASCII + Unicode spaces.
size_t wsAt(std::string_view s, size_t i) {
    auto u = [&](size_t k) { return uint8_t(i + k < s.size() ? s[i + k] : 0); };
    uint8_t c = u(0);
    if (c == ' ' || (c >= 9 && c <= 13)) return 1;
    if (c == 0xC2 && (u(1) == 0x85 || u(1) == 0xA0)) return 2;
    if (c == 0xE1 && u(1) == 0x9A && u(2) == 0x80) return 3;
    if (c == 0xE2 && u(1) == 0x80 && (u(2) <= 0x8A || u(2) == 0xA8 || u(2) == 0xA9 || u(2) == 0xAF)) return 3;
    if (c == 0xE2 && u(1) == 0x81 && u(2) == 0x9F) return 3;
    if (c == 0xE3 && u(1) == 0x80 && u(2) == 0x80) return 3;
    return 0;
}
size_t wsBefore(std::string_view s, size_t end) {   // length of a whitespace char ending at end
    for (size_t n = 1; n <= 3 && n <= end; ++n)
        if (wsAt(s, end - n) == n) return n;
    return 0;
}
std::string_view trim(std::string_view s) {
    size_t b = 0, e = s.size(), n;
    while (b < e && (n = wsAt(s, b))) b += n;
    while (e > b && (n = wsBefore(s, e))) e -= n;
    return s.substr(b, e - b);
}
std::string upper(std::string_view s) {   // shortcut: ASCII only (the reference uses culture ToUpper)
    std::string r(s);
    for (char& c : r) if (c >= 'a' && c <= 'z') c = char(c - 32);
    return r;
}
std::string lower(std::string_view s) {
    std::string r(s);
    for (char& c : r) if (c >= 'A' && c <= 'Z') c = char(c + 32);
    return r;
}
std::string without(std::string_view s, std::string_view chars) {
    std::string r;
    for (char c : s) if (chars.find(c) == std::string_view::npos) r += c;
    return r;
}
bool startsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }
bool endsWith(std::string_view s, char c) { return !s.empty() && s.back() == c; }

// ---- Numbers (DataConverter) -----------------------------------------------------------------------

bool isWhite(char c) { return c == ' ' || (c >= 9 && c <= 13); }   // .NET number parsing whitespace
std::string_view trimWhite(std::string_view s) {
    while (!s.empty() && isWhite(s.front())) s.remove_prefix(1);
    while (!s.empty() && (isWhite(s.back()) || s.back() == '\0')) s.remove_suffix(1);
    return s;
}
int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
// uint.TryParse(s, NumberStyles.HexNumber)
std::optional<uint32_t> parseHex(std::string_view s) {
    s = trimWhite(s);
    if (s.empty()) return std::nullopt;
    uint64_t v = 0;
    for (char c : s) {
        int d = hexDigit(c);
        if (d < 0) return std::nullopt;
        v = v << 4 | uint64_t(d);
        if (v > 0xFFFFFFFFull) return std::nullopt;
    }
    return uint32_t(v);
}
// long.TryParse(s) (NumberStyles.Integer, en-US)
std::optional<int64_t> parseLong(std::string_view s) {
    s = trimWhite(s);
    bool neg = false;
    if (!s.empty() && (s[0] == '+' || s[0] == '-')) { neg = s[0] == '-'; s.remove_prefix(1); }
    if (s.empty()) return std::nullopt;
    uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return std::nullopt;
        if (v > (uint64_t(1) << 63) / 10) return std::nullopt;
        v = v * 10 + uint64_t(c - '0');
        if (v > (uint64_t(1) << 63)) return std::nullopt;
    }
    if (!neg && v > uint64_t(INT64_MAX)) return std::nullopt;
    return neg ? int64_t(0 - v) : int64_t(v);
}
// Convert.ToUInt32(s, base) for base 2/8 (throws on overflow)
uint64_t parseBase(std::string_view s, int base, uint64_t max) {
    uint64_t v = 0;
    for (char c : s) {
        v = v * uint64_t(base) + uint64_t(c - '0');
        if (v > max) fatal("OverflowException: value too large for a UInt32");
    }
    return v;
}
bool allIn(std::string_view s, std::string_view set) {
    for (char c : s) if (set.find(c) == std::string_view::npos) return false;
    return true;
}

std::optional<uint32_t> getHex(std::string_view challenge) {
    std::string c = without(challenge, " ");
    if (c.size() > 3 && startsWith(c, "-0x")) {
        if (auto v = parseHex(std::string_view(c).substr(3))) return ~*v + 1;   // two's complement
    } else if (c.size() > 2 && lower(c.substr(0, 2)) == "0x") {
        return parseHex(std::string_view(c).substr(2));
    }
    return std::nullopt;
}
std::optional<uint32_t> getBin(std::string_view challenge) {
    std::string c = without(challenge, " ");
    auto ok = [](std::string_view d) { return !d.empty() && d.size() <= 32 && allIn(d, "01"); };
    if (c.size() > 2 && lower(c.substr(0, 2)) == "0b" && ok(std::string_view(c).substr(2)))
        return uint32_t(parseBase(std::string_view(c).substr(2), 2, 0xFFFFFFFFull));
    if (c.size() > 3 && lower(c.substr(0, 3)) == "-0b" && ok(std::string_view(c).substr(3)))
        return uint32_t(0 - parseBase(std::string_view(c).substr(3), 2, 0xFFFFFFFFull));
    return std::nullopt;
}
std::optional<uint32_t> getDec(std::string_view challenge) {
    if (auto v = parseLong(without(challenge, " "))) return uint32_t(*v);
    return std::nullopt;
}
std::optional<uint32_t> getOct(std::string_view challenge) {
    std::string c = without(challenge, " ");
    if (c.size() > 2 && lower(c.substr(0, 2)) == "0o") {
        if (allIn(std::string_view(c).substr(2), "01234567")) return uint32_t(parseBase(std::string_view(c).substr(2), 8, 0xFFFFFFFFull));
    } else if (c.size() > 3 && lower(c.substr(0, 3)) == "-0o") {
        if (allIn(std::string_view(c).substr(3), "01234567")) return uint32_t(0 - parseBase(std::string_view(c).substr(3), 8, 0xFFFFFFFFull));
    }
    return std::nullopt;
}
// DataConverter.TryToGetValueOf: all four are evaluated (the octal one may throw), hex > bin > oct > dec.
bool hasDigit(std::string_view s) { for (char c : s) if (c >= '0' && c <= '9') return true; return false; }
std::optional<uint32_t> valueOf(std::string_view s) {
    if (!hasDigit(s)) return std::nullopt;   // every spelling needs one: skips the four parsers for names
    auto hex = getHex(s), bin = getBin(s), dec = getDec(s), oct = getOct(s);
    return hex ? hex : bin ? bin : oct ? oct : dec;
}

// float.TryParse(s, NumberStyles.Float, InvariantCulture), .NET 8 (case-insensitive Infinity/NaN, overflow -> inf)
std::optional<float> floatOf(std::string_view s) {
    s = trimWhite(s);
    bool neg = false;
    if (!s.empty() && (s[0] == '+' || s[0] == '-')) { neg = s[0] == '-'; s.remove_prefix(1); }
    auto is = [&](std::string_view w) {
        if (s.size() != w.size()) return false;
        for (size_t i = 0; i < w.size(); ++i) if ((s[i] | 0x20) != w[i]) return false;
        return true;
    };
    if (is("infinity")) return neg ? -INFINITY : INFINITY;
    if (is("nan")) { uint32_t b = 0xFFC00000u; float f; std::memcpy(&f, &b, 4); return f; }   // .NET's NaN
    size_t i = 0, digits = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i, ++digits;
    if (i < s.size() && s[i] == '.') ++i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i, ++digits;
    if (!digits) return std::nullopt;
    bool negExp = false;
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        size_t j = i + 1;
        if (j < s.size() && (s[j] == '+' || s[j] == '-')) negExp = s[j++] == '-';
        size_t k = j;
        while (k < s.size() && s[k] >= '0' && s[k] <= '9') ++k;
        if (k == j) return std::nullopt;
        i = k;
    }
    if (i != s.size()) return std::nullopt;
    float f = 0;
    auto r = std::from_chars(s.data(), s.data() + s.size(), f);
    if (r.ec == std::errc::result_out_of_range) {   // rounds to 0 or infinity
        double d = 0;
        auto rd = std::from_chars(s.data(), s.data() + s.size(), d);
        f = rd.ec == std::errc() ? float(d) : negExp ? 0.0f : INFINITY;
    }
    return neg ? -f : f;
}
uint32_t floatBits(float f) { uint32_t b; std::memcpy(&b, &f, 4); return b; }

void put(std::vector<uint8_t>& o, uint32_t v, int bytes) { for (int i = bytes - 1; i >= 0; --i) o.push_back(uint8_t(v >> (8 * i))); }
std::vector<uint8_t> sized(uint32_t v, int bytes) { std::vector<uint8_t> o; put(o, v, bytes); return o; }
size_t leadingZeros(std::string_view s) { size_t n = 0; while (n < s.size() && s[n] == '0') ++n; return n; }
std::vector<uint8_t> padBytes(uint32_t v, size_t zeroPads) {   // width from leading zeros: 0 -> byte, 00 -> 16 bit...
    return zeroPads >= 1 && zeroPads <= 4 ? sized(v, int(zeroPads)) : std::vector<uint8_t>{};
}
std::vector<uint8_t> magnitudeBytes(uint32_t v) { return sized(v, v <= 0xFF ? 1 : v <= 0xFFFF ? 2 : v <= 0xFFFFFF ? 3 : 4); }

// DataConverter.TryToGetBytesValueOf: the width comes from how the number is written.
std::vector<uint8_t> bytesOf(std::string_view challenge) {
    auto hex = getHex(challenge), bin = getBin(challenge), oct = getOct(challenge), dec = getDec(challenge);
    std::string_view c = trim(challenge);
    size_t len = c.size() - (c.find('-') != std::string_view::npos ? 1 : 0);
    if (hex) {
        if (len == 3 || len == 4) return sized(*hex, 1);
        if (len == 5 || len == 6) return sized(*hex, 2);
        if (len == 7 || len == 8) return sized(*hex, 3);
        if (len == 9 || len == 10) return sized(*hex, 4);
        return {};
    }
    if (bin) {
        if (len <= 2) return {};
        return len <= 10 ? sized(*bin, 1) : len <= 18 ? sized(*bin, 2) : len <= 26 ? sized(*bin, 3) : len <= 34 ? sized(*bin, 4) : std::vector<uint8_t>{};
    }
    if (oct) {
        if (c.size() <= 2) return {};
        std::string input = without(c, "-").substr(2);
        size_t z = leadingZeros(input);
        return z == 0 ? magnitudeBytes(*oct) : padBytes(*oct, z);
    }
    if (dec) {
        if (c.empty()) return {};
        bool neg = c.find('-') != std::string_view::npos;
        std::string input = without(c, "-");
        auto pos = parseLong(input);   // uint.Parse(input): throws past 32 bits
        if (!pos || *pos < 0 || *pos > 0xFFFFFFFFll) fatal("OverflowException: #DB value too large for a UInt32");
        size_t z = leadingZeros(input);
        if (z == 0 && !neg) return magnitudeBytes(*dec);
        if (z == 0) {
            int hexLen = 1;
            for (uint32_t p = uint32_t(*pos); p > 0xF; p >>= 4) ++hexLen;
            return hexLen <= 2 ? sized(*dec, 1) : hexLen <= 4 ? sized(*dec, 2) : hexLen <= 6 ? sized(*dec, 3) : sized(*dec, 4);
        }
        return padBytes(*dec, z);
    }
    return {};
}

bool isLabelValid(std::string_view s) {
    if (s.size() < 2 || (s[0] != '.' && s[0] != '~')) return false;
    for (char c : upper(s.substr(1)))
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '?')) return false;
    return true;
}
bool hasStringContent(std::string_view s) { return s.size() > 2 && s[0] == '"' && s.back() == '"'; }
// Encoding.ASCII.GetBytes: one '?' per non-ASCII UTF-16 unit.
void ascii(std::string_view s, std::vector<uint8_t>& out) {
    for (size_t i = 0; i < s.size(); ++i) {
        uint8_t c = uint8_t(s[i]);
        if (c < 0x80) out.push_back(c);
        else if (c >= 0xF0) out.insert(out.end(), 2, '?');   // outside the BMP: a surrogate pair
        else if (c >= 0xC0) out.push_back('?');
    }
}

// DataConverter.GetCSArguments: the regex (?:^|,)\s*("(?:[^"]|"")*"|[^,]*) applied with Regex.Matches.
std::vector<std::string> csvSplit(std::string_view s) {
    std::vector<std::string> items;
    size_t n = s.size(), start = 0;
    while (start <= n) {
        size_t p = start;
        if (p != 0 && (p = s.find(',', start)) == std::string_view::npos) break;
        size_t q = p == 0 ? 0 : p + 1, w;
        while (q < n && (w = wsAt(s, q))) q += w;
        size_t end = std::string_view::npos;
        if (q < n && s[q] == '"') {   // greedy (?:[^"]|"")* then backtrack to a closing quote
            std::vector<size_t> steps{q + 1};
            for (size_t k = q + 1;;) {
                if (k < n && s[k] != '"') ++k;
                else if (k + 1 < n && s[k] == '"' && s[k + 1] == '"') k += 2;
                else break;
                steps.push_back(k);
            }
            for (size_t i = steps.size(); i-- > 0;)
                if (steps[i] < n && s[steps[i]] == '"') { end = steps[i] + 1; break; }
        }
        if (end == std::string_view::npos) { end = s.find(',', q); if (end == std::string_view::npos) end = n; }
        std::string_view m = s.substr(p, end - p);
        if (m.empty()) items.emplace_back();
        while (!m.empty() && m[0] == ',') m.remove_prefix(1);
        items.emplace_back(trim(m));
        start = end == p ? end + 1 : end;   // an empty match moves the scan one character on
    }
    return items;
}

// ---- Lines ----------------------------------------------------------------------------------------

struct Arg {
    std::optional<uint32_t> value;
    bool negative = false;
    std::optional<float> fvalue;
    int reg = -1, freg = -1, flag = -1;
    bool pending = false;
    std::string label;   // PendingLabel
    std::string form;    // GeneralForm ("" where the reference has null)
};

struct DbArg { std::vector<uint8_t> data; std::string label; };

struct Line {
    uint32_t nr = 0, address = 0;
    int block = 0;
    bool prevent = false, error = false;
    std::string text, op, generalForm;
    std::vector<Arg> args;
    std::vector<DbArg> db;
    std::vector<uint8_t> data;
};

struct Assembler {
    AsmResult& r;
    const AsmIncludeResolver& resolve;
    std::unordered_map<std::string, uint32_t>& labels;
    uint32_t gap = 0, org = 0, run = 0;   // GlobalAddressPointer, OrgAddress, RunAddress
    bool runFromOrg = false;
    uint32_t cur = 0;   // line being assembled, for fatal messages
    std::vector<uint32_t> blockStarts;
    std::vector<std::string> included;

    void warn(uint32_t line, std::string text) { r.warnings.push_back({line, std::move(text)}); }

    // Interpret.GetFullSourceLines: includes are appended after the including file's own lines.
    void expand(std::string_view source, const std::string& base, std::vector<std::string>& out, int depth) {
        if (depth > 64) fatal("include nesting too deep (the reference overflows its stack)");
        if (startsWith(source, "\xEF\xBB\xBF")) source.remove_prefix(3);   // File.ReadAllText drops the BOM
        included.clear();   // reference: a static list, cleared on every (nested) call
        std::vector<std::string> tail;
        for (size_t b = 0; b <= source.size();) {
            size_t e = source.find('\n', b);
            if (e == std::string_view::npos) e = source.size();
            std::string_view line = source.substr(b, e - b);
            if (e < source.size() && endsWith(line, '\r')) line.remove_suffix(1);
            b = e + 1;
            std::string_view t = trim(line);
            if (!startsWith(t, "#include")) { out.emplace_back(line); continue; }
            std::string inc(trim(t.substr(8)));
            size_t s0 = inc.find_first_not_of("\"<> "), s1 = inc.find_last_not_of("\"<> ");
            inc = s0 == std::string::npos ? "" : inc.substr(s0, s1 - s0 + 1);
            for (char& c : inc) if (c == '\\') c = '/';
            bool rooted = !inc.empty() && (inc[0] == '/' || (inc.size() >= 2 && inc[1] == ':'));
            if (!rooted && !base.empty()) inc = (endsWith(base, '/') ? base : base + "/") + inc;
            std::string key = lower(inc);
            bool already = false;
            for (auto& k : included) already |= k == key;
            if (already) { warn(0, "#include " + inc + " was already added, skipping"); continue; }
            auto text = resolve(inc);
            if (!text) { warn(0, "#include " + inc + " was not found"); continue; }
            expand(*text, dirName(inc), tail, depth + 1);
            included.push_back(key);
        }
        for (auto& l : tail) out.push_back(std::move(l));
    }
    static std::string dirName(std::string p) {
        for (char& c : p) if (c == '\\') c = '/';
        size_t s = p.rfind('/');
        return s == std::string::npos ? "" : s == 0 ? "/" : p.substr(0, s);
    }

    // Interpret.GetLabels: leading tokens of the cleaned line that start with '.' or '~'.
    static std::vector<std::string> labelsOf(std::string_view line) {
        std::string_view t = trim(line);
        if (t.empty() || (t[0] != '.' && t[0] != '~')) return {};   // fast path: no leading label token
        std::string c;
        for (char ch : trim(line)) {
            if (ch == '\t') ch = ' ';
            if (ch == ' ' && !c.empty() && (c.back() == ' ' || c.back() == ',')) continue;   // "  " -> " ", ", " -> ","
            c += ch;
        }
        std::vector<std::string> out;
        for (size_t b = 0; b <= c.size();) {
            size_t e = c.find(' ', b);
            if (e == std::string::npos) e = c.size();
            std::string_view tok = std::string_view(c).substr(b, e - b);
            if (tok.size() < 2 || (tok[0] != '.' && tok[0] != '~')) break;
            out.emplace_back(tok);
            b = e + 1;
        }
        return out;
    }
    static size_t commentStart(std::string_view line) {
        bool inString = false;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '"') inString = !inString;
            else if (!inString && line[i] == ';') return i;
        }
        return std::string_view::npos;
    }
    static std::string mnemonicOf(std::string_view line) {
        std::string c(line);
        size_t cs = commentStart(line);
        if (cs != std::string_view::npos) c = trim(line.substr(0, cs));
        auto ls = labelsOf(c);
        if (ls.size() == 1) {
            size_t i = c.find(ls[0]);
            if (i != std::string::npos) c.erase(i, ls[0].size());
        }
        return std::string(trim(c));
    }
    static std::string opOf(std::string_view m) {
        if (m.find(' ') == std::string_view::npos) return std::string(m);
        std::string_view t = trim(m);
        return upper(trim(t.substr(0, t.find(' '))));
    }
    static std::string argumentsOf(std::string_view m) {
        if (m.find(' ') == std::string_view::npos) return "";
        std::string_view t = trim(m);
        return std::string(trim(t.substr(t.find(' '))));
    }

    void defineLabels(const std::vector<std::string>& lines) {   // Interpret.GetAllDefinedLabels
        for (size_t i = 0; i < lines.size(); ++i) {
            auto ls = labelsOf(lines[i]);
            if (ls.empty()) continue;
            const char* problem = ls.size() > 1 ? "multiple labels on one line"
                                : !isLabelValid(ls[0]) ? "invalid label"
                                : labels.count(ls[0]) ? "label already defined" : nullptr;
            if (problem) {
                warn(uint32_t(i + 1), std::string(problem) + " '" + lines[i] + "'; labels after this line are undeclared (reference behaviour)");
                break;
            }
            labels.emplace(ls[0], 0);
        }
    }

    // CLine.Process
    void process(Line& L) {
        cur = L.nr;
        L.address = gap;
        auto ls = labelsOf(L.text);
        if (ls.size() == 1 && isLabelValid(ls[0])) {
            auto it = labels.find(ls[0]);
            if (it != labels.end()) it->second = gap;
        }
        std::string m = mnemonicOf(L.text);
        std::string args = argumentsOf(m);
        L.op = opOf(m);
        if (!m.empty() && m[0] == '#') {
            directive(L, args);
            L.block = int(blockStarts.size());
            return;
        }
        const auto& T = tables();
        if (!T.ops.count(L.op) && !trim(L.op).empty()) warn(L.nr, "unknown mnemonic " + L.op);
        resolveArguments(L, args);
        encode(L, true);
        L.block = int(blockStarts.size());
    }

    void directive(Line& L, const std::string& args) {
        if (L.op == "#ORG") {
            L.prevent = true;
            org = valueOf(upper(args)).value_or(gap);
            gap = org;
            if (run == 0) { run = org; runFromOrg = true; }
            blockStarts.push_back(org);
        } else if (L.op == "#RUN") {
            L.prevent = true;
            auto v = valueOf(upper(args));
            if ((run != 0 && runFromOrg) || run == 0) { run = v.value_or(0); runFromOrg = false; }
        } else if (L.op == "#DB") {
            for (auto& item : csvSplit(args)) L.db.push_back(dbArg(L, item));
            setDbData(L);
            gap += uint32_t(L.data.size());
        } else {
            warn(L.nr, "unknown directive " + L.op);
        }
    }

    DbArg dbArg(Line& L, const std::string& s) {   // DBArgument.ProcessArgument
        DbArg a;
        if (validRepetition(s)) { a.data = repetition(s); return a; }
        if (hasStringContent(s)) { ascii(std::string_view(s).substr(1, s.size() - 2), a.data); return a; }
        std::string t = without(s, "_");
        if (valueOf(t)) {
            a.data = bytesOf(t);
            if (a.data.empty()) warn(L.nr, "could not convert #DB value " + s + " into bytes");
            return a;
        }
        if (auto f = floatOf(t)) { put(a.data, floatBits(*f), 4); return a; }
        if (s.size() >= 2 && s[0] == '.') { a.label = s; a.data = labelBytes(s); return a; }
        if (s != "\"\"") warn(L.nr, "could not interpret #DB value " + s);
        return a;
    }
    std::vector<uint8_t> labelBytes(const std::string& label) {
        auto it = labels.find(label);
        if (it == labels.end()) fatal("KeyNotFoundException: #DB label " + label + " is not declared");
        return sized(it->second, 3);
    }
    static bool validRepetition(std::string_view s) {
        if (s.size() < 4 || s[0] != '[' || s.find(']') == std::string_view::npos) return false;
        size_t close = s.find(']');
        auto rep = valueOf(without(s.substr(1, close - 1), "_"));
        if (!rep || *rep < 1 || *rep > 0xFFFFFF) return false;
        std::string_view sub = trim(s.substr(close + 1));
        return hasStringContent(sub) || valueOf(without(sub, "_")).has_value();
    }
    static std::vector<uint8_t> repetition(std::string_view s) {
        size_t close = s.find(']');
        uint32_t rep = valueOf(without(s.substr(1, close - 1), "_")).value_or(0);
        std::string_view sub = trim(s.substr(close + 1));
        std::vector<uint8_t> one, out;
        if (hasStringContent(sub)) ascii(sub.substr(1, sub.size() - 2), one);
        else one = bytesOf(without(sub, "_"));
        out.reserve(size_t(rep) * one.size());
        for (uint32_t i = 0; i < rep; ++i) out.insert(out.end(), one.begin(), one.end());
        return out;
    }
    static void setDbData(Line& L) {
        std::vector<uint8_t> d;
        for (auto& a : L.db) d.insert(d.end(), a.data.begin(), a.data.end());
        if (!d.empty()) L.data = std::move(d);
    }

    // CLine.SplitOperands: whitespace removed, then tokens [+-]?[^+,-]+
    static std::vector<std::string> splitOperands(std::string_view in) {
        std::string s;
        for (size_t i = 0, w; i < in.size();) {
            if ((w = wsAt(in, i))) { i += w; continue; }
            s += in[i++];
        }
        std::vector<std::string> out;
        auto stop = [](char c) { return c == '+' || c == ',' || c == '-'; };
        for (size_t i = 0; i < s.size();) {
            size_t j = i + (s[i] == '+' || s[i] == '-'), k = j;
            while (k < s.size() && !stop(s[k])) ++k;
            if (k > j) { out.push_back(s.substr(i, k - i)); i = k; }
            else ++i;
        }
        return out;
    }

    void resolveArguments(Line& L, const std::string& text) {
        for (auto& a : splitOperands(text)) identify(L, std::string(trim(a)));
        if (L.op == "LD") ldForms(L.args);
        L.generalForm = L.op + " ";
        for (size_t i = 0; i < L.args.size(); ++i) L.generalForm += (i ? "," : "") + L.args[i].form;
    }

    static bool isAddressing(std::string_view a) { return startsWith(a, "(") && endsWith(a, ')'); }
    static bool isBeginAddressing(std::string_view a) { return startsWith(a, "(") && !endsWith(a, ')'); }
    static bool isEndAddressing(std::string_view a) { return (startsWith(a, "+") || startsWith(a, "-")) && endsWith(a, ')'); }
    static std::string extract(std::string_view a) { return std::string(trim(without(a, "()"))); }
    static bool hasLabel(std::string_view a) { return a.find_first_of(".~") != std::string_view::npos; }

    void identify(Line& L, const std::string& arg) {   // CLine.IdentifyArgument
        const std::string& op = L.op;
        bool flagOp = op == "CALL" || op == "CALLR" || op == "JP" || op == "JR" || op == "SETF" || op == "RESF" || op == "INVF" || op == "RETIF";
        std::string up = upper(arg);
        if (flagOp) {
            if (hasLabel(arg)) { L.args.push_back(labelArg("nnn", arg)); return; }
            auto f = tables().flags.find(up);
            if (f != tables().flags.end()) { Arg a; a.flag = f->second; a.form = "ff"; L.args.push_back(a); return; }
            L.args.push_back(valueOrReg(L, extract(up), isAddressing(up)));
            return;
        }
        if (hasLabel(arg) && !floatOf(arg)) {
            if (isAddressing(arg)) L.args.push_back(labelArg("(nnn)", extract(arg)));
            else if (isBeginAddressing(arg)) L.args.push_back(labelArg("(nnn", extract(arg)));
            else if (isEndAddressing(arg)) L.args.push_back(labelArg("nnn)", extract(arg)));
            else L.args.push_back(labelArg("nnn", arg));
            return;
        }
        L.args.push_back(valueOrReg(L, extract(up), isAddressing(up), isBeginAddressing(up), isEndAddressing(up)));
    }
    static Arg labelArg(const char* form, std::string label) {
        Arg a;
        a.form = form;
        a.pending = true;
        a.label = std::move(label);
        return a;
    }

    Arg valueOrReg(Line& L, const std::string& p, bool addr, bool begin = false, bool end = false) {   // IdentifyValueOrReg
        auto shape = [&](const std::string& x) { return begin ? "(" + x : end ? x + ")" : addr ? "(" + x + ")" : x; };
        std::string unsigned_ = without(p, "+-");
        Arg a;
        a.negative = p.find('-') != std::string::npos;
        a.value = valueOf(unsigned_);
        a.fvalue = floatOf(p);
        if (a.value || a.fvalue) { a.form = shape("nnn"); return a; }
        const auto& T = tables();
        if (auto g = T.regs.find(unsigned_); g != T.regs.end()) { a.reg = g->second; a.form = shape(std::string(unsigned_.size(), 'r')); return a; }
        if (p.empty()) fatal("ArgumentOutOfRangeException: empty operand");   // Mnem.TryGetFloatRegisterIndex("")
        if (p[0] == 'F' && p.size() > 1 && allIn(p.substr(1), "0123456789")) {   // byte.TryParse: leading zeros ok
            uint32_t i = 0;
            for (char c : p.substr(1)) i = std::min<uint32_t>(i * 10 + uint32_t(c - '0'), 256);
            if (i <= 15) { a.freg = int(i); a.form = "fr"; return a; }
        }
        if (p == "SPC" || p == "SPR" || p == "IPO") { a.form = shape(p); return a; }
        if (isLabelValid(p)) return a;   // reference: Label set, GeneralForm null
        if (!L.error) r.errors.push_back({L.nr, "unrecognized operand '" + p + "'"});   // counted once per line
        L.error = true;
        return a;
    }

    // CLine.ResolveArguments "temporary" LD rewrites of the generic nnn operand
    static void ldForms(std::vector<Arg>& a) {
        auto is = [&](size_t i, std::initializer_list<const char*> fs) {
            for (auto f : fs) if (a[i].form == f) return true;
            return false;
        };
        const auto addr = {"(nnn)", "(rrr)"}, open = {"(nnn", "(rrr"}, close = {"nnn)", "r)", "rr)", "rrr)"};
        switch (a.size()) {
        case 2:
            if (a[1].form != "nnn") break;
            if (a[0].form == "fr") a[1].form = "nnnn";
            else if (a[0].form == "r") a[1].form = "n";
            else if (a[0].form == "rr") a[1].form = "nn";
            else if (a[0].form == "rrrr") a[1].form = "nnnn";
            break;
        case 3:
            if (is(0, addr) && is(1, {"nnn"}) && is(2, {"nnn"})) a[1].form = "nnnn", a[2].form = "n";
            break;
        case 4:
            if (is(0, addr) && is(1, {"nnn"}) && is(2, {"nnn"}) && is(3, {"nnn", "rrr"})) a[1].form = "nnnn", a[2].form = "n";
            else if (is(0, addr) && is(1, addr) && is(2, {"nnn"}) && is(3, {"rrr"})) a[2].form = "n";
            else if (is(0, open) && is(1, close) && is(2, {"nnn"}) && is(3, {"nnn"})) a[2].form = "nnnn", a[3].form = "n";
            break;
        case 5:
            if (is(0, addr) && is(1, open) && is(2, {"r)", "rr)", "rrr)", "nnn)"}) && is(3, {"nnn"}) && is(4, {"rrr"})) a[3].form = "n";
            else if (is(0, open) && is(1, close) && is(2, {"nnn"}) && is(3, {"nnn"}) && is(4, {"nnn"})) a[2].form = "nnnn", a[3].form = "n";
            else if (is(0, open) && is(1, close) && is(2, addr) && is(3, {"nnn"}) && is(4, {"rrr"})) a[3].form = "n";
            break;
        case 6:
            if (is(0, open) && is(1, close) && is(2, open) && is(3, close) && is(4, {"nnn"}) && is(5, {"rrr"})) a[4].form = "n";
            break;
        }
    }

    // CLine.ResolveDataOpcodes: fills the format's o/A/B/... bit fields.
    void encode(Line& L, bool firstPass) {
        L.data.clear();
        const auto& T = tables();
        if (!L.op.empty()) {
            auto p = T.ops.find(L.op);
            L.data.push_back(uint8_t(p != T.ops.end() && p->second.op->primary ? p->second.op->code : 0));
        }
        auto it = T.ops.find(L.generalForm);
        if (it != T.ops.end()) {
            const AsmOp& o = *it->second.op;
            const Layout& lay = it->second.layout;
            if (o.primary) fatal("NullReferenceException: primary op used as an operand form");
            if (!lay.whole) fatal("NullReferenceException: format of '" + L.generalForm + "' is not whole bytes");
            auto unfilled = [&] { fatal("FormatException: unfilled field in '" + L.generalForm + "'"); };
            if (lay.stray || lay.sub.split) unfilled();
            size_t at = L.data.size();
            L.data.insert(L.data.end(), lay.base.begin(), lay.base.end());
            auto set = [&](const Field& fd, uint32_t v) {   // GetBinaryOfUint(v, width): the low bits, MSB first
                for (int k = 0; k < fd.width; ++k) {
                    int b = fd.width - 1 - k, p = fd.pos + k;
                    if (b < 32 && (v >> b & 1)) L.data[at + size_t(p / 8)] |= uint8_t(0x80 >> (p % 8));
                }
            };
            set(lay.sub, uint32_t(o.sub));
            for (size_t i = 0; i < 26; ++i) {
                const Field& fd = lay.arg[i];
                if (!fd.width) continue;
                if (i >= L.args.size() || fd.split) unfilled();   // letters left in the text
                set(fd, argValue(L.args[i]));
            }
        } else if (std::string_view g = trim(L.generalForm); !L.error && !(g == "NOP" || g == "DEBUG" || g == "BREAK" || g == "RET" || g.empty())) {
            // D-9: the reference silently emitted only the opcode byte here, which swallowed the next instruction
            L.data.clear();
            std::string shape(trim(std::string_view(L.generalForm).substr(L.op.size())));
            if (firstPass)
                r.errors.push_back({L.nr, "'" + std::string(trim(L.text)) + "': " +
                    (T.ops.count(L.op) ? "no " + L.op + " form takes " + (shape.empty() ? "zero operands" : "operands " + shape)
                                       : "unknown mnemonic " + L.op)});
        }
        gap += uint32_t(L.data.size());
    }
    static uint32_t argValue(const Arg& a) {
        if (a.value) return a.negative ? 0u - *a.value : *a.value;
        if (a.reg >= 0) return uint32_t(a.reg);
        if (a.freg >= 0) return uint32_t(a.freg);
        if (a.flag >= 0) return uint32_t(a.flag);
        if (a.fvalue) return floatBits(*a.fvalue);
        return 0;
    }

    // CLine.UpdateLabelAddress (second pass)
    void resolveLabels(Line& L) {
        cur = L.nr;
        if (L.op.empty()) return;
        bool any = false;
        for (auto& a : L.args) any |= a.pending;
        if (!any || L.op == "#DB") return;
        bool absolute = kAbsoluteOps.count(L.op) > 0, relative = L.op == "CALLR" || L.op == "JR";
        for (auto& a : L.args) {
            if (!a.pending || (!absolute && !relative)) continue;
            std::string dotted = a.label;
            for (char& c : dotted) if (c == '~') c = '.';
            if (!labels.count(dotted)) { warn(L.nr, "label " + a.label + " was not declared"); continue; }
            auto it = labels.find(absolute ? a.label : dotted);   // reference looks up the raw name for absolute ops
            if (it == labels.end()) fatal("KeyNotFoundException: label " + a.label);
            a.value = absolute ? it->second : it->second - L.address;
        }
        encode(L, false);
    }
};

} // namespace

AsmResult assemble(std::string_view source, std::string_view path, const AsmIncludeResolver& resolve) {
    AsmResult r;
    Assembler a{r, resolve, r.labels};
    try {
        std::vector<std::string> text;
        a.expand(source, Assembler::dirName(std::string(path)), text, 0);
        a.defineLabels(text);
        std::vector<Line> lines(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            lines[i].nr = uint32_t(i + 1);
            lines[i].text = std::move(text[i]);
            if (!lines[i].text.empty()) a.process(lines[i]);
        }
        for (auto& L : lines) {
            a.resolveLabels(L);
            for (auto& d : L.db) if (!d.label.empty()) d.data = a.labelBytes(d.label);
            Assembler::setDbData(L);
        }
        r.runAddress = a.run;
        r.blocks.reserve(a.blockStarts.size());
        for (uint32_t s : a.blockStarts) r.blocks.push_back({s, {}});
        for (auto& L : lines) {
            if (L.block <= 0 || (L.prevent && !L.error) || L.data.empty()) continue;
            auto& b = r.blocks[size_t(L.block - 1)];
            b.lines.emplace_back(uint32_t(b.data.size()), L.nr);
            b.data.insert(b.data.end(), L.data.begin(), L.data.end());
        }
        for (auto& L : lines) r.source.push_back(std::move(L.text));
    } catch (const Fatal& f) {
        r.fatal = (a.cur ? "line " + std::to_string(a.cur) + ": " : "") + f.what;
        r.blocks.clear();
        LOGE("Assembler: %s; nothing assembled (the reference throws here)", r.fatal.c_str());
    }
    return r;
}

} // namespace c93
