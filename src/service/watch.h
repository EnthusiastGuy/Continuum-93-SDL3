// The WATCHER's condition language: UI/WatcherWindow.cs ParseCondition / EvaluateCondition and the .NET
// number parsing they call (en-US, the reference machine's culture). Apart from the window so the CPU
// thread and tests/conformance.cpp --watch (against tests/oracle --watch) run the same code.
//
// Quirks kept: "=" is not an operator ("A = 5" is invalid, red); the first operator found splits the line
// ("A =< 5" compares a register named "A =", which is unknown and reads as 0, so the line is valid);
// an unknown register name reads as 0; any name starting with F must be F0..F15 ("FG" is invalid); a line
// holding only a comment is invalid; "==" on floats is |a - b| < float.Epsilon (inf == inf is false,
// NaN is neither == nor !=); hex values may have blanks after "0x" ("0x 5").
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "machine.h"

namespace c93::svc::watch {

enum class Type : uint8_t { Register, FloatRegister, Memory };
enum class Op : uint8_t { EQ, NE, LT, LE, GT, GE };
struct Condition {
    Type type = Type::Register;
    Op op = Op::EQ;
    uint8_t width = 0;   // register bits (8..32); 0: not a Mnem.REG name, reads as 0
    uint8_t index = 0;   // register (its first letter) or float register
    uint32_t address = 0;
    uint64_t value = 0;
    float floatValue = 0;
};

inline bool isWhite(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
inline std::string_view trim(std::string_view s) {
    while (!s.empty() && isWhite(s.front())) s.remove_prefix(1);
    while (!s.empty() && isWhite(s.back())) s.remove_suffix(1);
    return s;
}
inline bool startsWithNoCase(std::string_view s, std::string_view p) {
    if (s.size() < p.size()) return false;
    for (size_t i = 0; i < p.size(); ++i) if ((s[i] | 0x20) != (p[i] | 0x20)) return false;
    return true;
}

// byte/uint/ulong.TryParse(s): blanks around, one sign right before the digits, "-0" passes.
inline bool parseDec(std::string_view s, uint64_t max, uint64_t& out) {
    s = trim(s);
    bool neg = !s.empty() && s[0] == '-';
    if (!s.empty() && (s[0] == '+' || s[0] == '-')) s.remove_prefix(1);
    if (s.empty()) return false;
    uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        unsigned d = unsigned(c - '0');
        if (v > (max - d) / 10) return false;
        v = v * 10 + d;
    }
    if (neg && v) return false;
    out = v;
    return true;
}
// TryParse(s, NumberStyles.HexNumber): blanks around, hex digits, no sign, no prefix.
inline bool parseHex(std::string_view s, uint64_t max, uint64_t& out) {
    s = trim(s);
    if (s.empty()) return false;
    uint64_t v = 0;
    for (char c : s) {
        unsigned d = c >= '0' && c <= '9' ? unsigned(c - '0') : (c | 0x20) >= 'a' && (c | 0x20) <= 'f' ? unsigned((c | 0x20) - 'a' + 10) : 99;
        if (d > 15 || v > (max - d) / 16) return false;
        v = v * 16 + d;
    }
    out = v;
    return true;
}
// float.TryParse(s) (NumberStyles.Float | AllowThousands): blanks, sign, digits with ',' groups after the
// first digit, one '.', an exponent only when digits follow it; overflow gives infinity; then "NaN".
inline bool parseFloat(std::string_view s, float& out) {
    s = trim(s);
    std::string n;
    size_t i = 0;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) n += s[i++];
    bool digits = false, dot = false;
    for (; i < s.size(); ++i) {
        char c = s[i];
        if (c >= '0' && c <= '9') { digits = true; n += c; }
        else if (c == '.' && !dot) { dot = true; n += c; }
        else if (c == ',' && digits && !dot) {}
        else break;
    }
    if (digits && i < s.size() && (s[i] | 0x20) == 'e') {
        size_t j = i + 1;
        std::string e = "e";
        if (j < s.size() && (s[j] == '+' || s[j] == '-')) e += s[j++];
        if (j < s.size() && s[j] >= '0' && s[j] <= '9') {
            while (j < s.size() && s[j] >= '0' && s[j] <= '9') e += s[j++];
            n += e;
            i = j;
        }
    }
    if (digits && i == s.size()) { out = std::strtof(n.c_str(), nullptr); return true; }
    std::string_view t = s;
    bool neg = !t.empty() && t[0] == '-';
    if (!t.empty() && (t[0] == '+' || t[0] == '-')) t.remove_prefix(1);
    if (t.size() == 3 && startsWithNoCase(t, "nan")) { out = std::numeric_limits<float>::quiet_NaN(); return true; }
    if (t == "\xE2\x88\x9E") { out = neg ? -std::numeric_limits<float>::infinity() : std::numeric_limits<float>::infinity(); return true; }   // en-US "∞"
    return false;
}

// Mnem.REG: A..Z, then runs of 2..4 consecutive letters wrapping at Z (AB, ZA, XYZA...). Index = first letter.
inline uint8_t registerWidth(std::string_view name) {
    if (name.empty() || name.size() > 4) return 0;
    for (size_t i = 0; i < name.size(); ++i)
        if (name[i] < 'A' || name[i] > 'Z' || (i && name[i] - 'A' != (name[i - 1] - 'A' + 1) % 26)) return 0;
    return uint8_t(name.size() * 8);
}

// ParseCondition(line): line is already trimmed.
inline std::optional<Condition> parse(std::string_view line) {
    if (size_t k = line.find(';'); k != std::string_view::npos) line = trim(line.substr(0, k));
    if (line.empty()) return std::nullopt;
    size_t p = 0, len = 0;   // Regex \s*(==|!=|<=|>=|<|>)\s*: the leftmost operator
    for (; p < line.size() && !len; ++p) {
        char c = line[p], next = p + 1 < line.size() ? line[p + 1] : 0;
        if ((c == '=' || c == '!' || c == '<' || c == '>') && next == '=') len = 2;
        else if (c == '<' || c == '>') len = 1;
    }
    if (!len) return std::nullopt;
    --p;
    Condition c;
    std::string_view o = line.substr(p, len);
    c.op = o == "==" ? Op::EQ : o == "!=" ? Op::NE : o == "<=" ? Op::LE : o == ">=" ? Op::GE : o == "<" ? Op::LT : Op::GT;
    std::string_view left = trim(line.substr(0, p)), right = trim(line.substr(p + len));
    uint64_t v = 0;
    if (left.size() >= 2 && left.front() == '(' && left.back() == ')') {   // memory: (0x000000)
        std::string_view a = trim(left.substr(1, left.size() - 2));
        if (!(startsWithNoCase(a, "0x") ? parseHex(a.substr(2), 0xFFFFFFFFu, v) : parseDec(a, 0xFFFFFFFFu, v))) return std::nullopt;
        c.type = Type::Memory;
        c.address = uint32_t(v);
    } else if (left.size() > 1 && (left[0] | 0x20) == 'f') {   // float register F0..F15
        if (!parseDec(left.substr(1), 255, v) || v > 15) return std::nullopt;
        c.type = Type::FloatRegister;
        c.index = uint8_t(v);
    } else {
        std::string name(left);
        for (char& ch : name) if (ch >= 'a' && ch <= 'z') ch = char(ch - 32);
        c.type = Type::Register;
        c.width = registerWidth(name);
        c.index = c.width ? uint8_t(name[0] - 'A') : 0;
    }
    if (c.type == Type::FloatRegister) return parseFloat(right, c.floatValue) ? std::optional(c) : std::nullopt;
    const uint64_t max = ~uint64_t(0);
    if (!(startsWithNoCase(right, "0x") ? parseHex(right.substr(2), max, c.value) : parseDec(right, max, c.value))) return std::nullopt;
    return c;
}

template <class T> bool compare(T a, T b, Op op) {
    switch (op) {
    case Op::EQ: return a == b;
    case Op::NE: return a != b;
    case Op::LT: return a < b;
    case Op::LE: return a <= b;
    case Op::GT: return a > b;
    default: return a >= b;
    }
}

// EvaluateCondition: registers of the current bank, float registers of the current float bank.
inline bool evaluate(const Machine& m, const Condition& c) {
    switch (c.type) {
    case Type::Register: {
        uint64_t v = c.width == 8 ? m.get<8>(c.index) : c.width == 16 ? m.get<16>(c.index) : c.width == 24 ? m.get<24>(c.index)
                   : c.width == 32 ? m.get<32>(c.index) : 0;
        return compare<uint64_t>(v, c.value, c.op);
    }
    case Type::FloatRegister: {
        float a = m.fregs[m.fbank][c.index], e = c.floatValue;
        constexpr float eps = std::numeric_limits<float>::denorm_min();   // float.Epsilon
        if (c.op == Op::EQ) return std::fabs(a - e) < eps;
        if (c.op == Op::NE) return std::fabs(a - e) >= eps;
        return compare(a, e, c.op);
    }
    default:   // shortcut: past RAM's 3 slack bytes the reference threw (and crashed); this wraps at 16 MB
        return compare<uint64_t>(c.address < kRamSize + 3 ? m.ram[c.address] : m.ram[c.address & kRamMask], c.value, c.op);
    }
}

// ParseAndEvaluateConditions, parse half: the conditions of the non-blank lines; false if one is not a condition.
inline bool parseAll(const std::vector<std::string>& lines, std::vector<Condition>& out) {
    out.clear();
    bool valid = true;
    for (const std::string& l : lines) {
        std::string_view t = trim(l);
        if (t.empty()) continue;
        if (auto c = parse(t)) out.push_back(*c);
        else valid = false;
    }
    return valid;
}
inline bool allTrue(const Machine& m, const std::vector<Condition>& cs) {
    for (const Condition& c : cs) if (!evaluate(m, c)) return false;
    return true;
}

} // namespace c93::svc::watch
