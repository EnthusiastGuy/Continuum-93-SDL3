#include "service/disasm.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>

#include "basic.h"     // netFloat: float.ToString() of .NET
#include "machine.h"

namespace c93::svc {
namespace {

struct AsmOp { const char* key; int primary, code, sub; const char* format; };
struct AsmName { const char* name; int index; };
#include "asm_ops.inc"

constexpr int kOpCount = int(sizeof kOps / sizeof kOps[0]);

// Mnem.GetPrimaryOperator / IsSingleByteInstruction / GetSecondaryOperator, precomputed. The C# scans
// OPS.Values in insertion order and takes the first hit; sub-ops have OpCode 0, so code 0 finds NOP first.
struct Tables {
    int16_t primary[256];
    bool single[256];
    int16_t secondary[256][256];
    Tables() {
        for (int c = 0; c < 256; ++c) {
            primary[c] = -1;
            single[c] = true;
            for (int s = 0; s < 256; ++s) secondary[c][s] = -1;
        }
        for (int i = kOpCount - 1; i >= 0; --i) {   // backwards: the first match wins
            const AsmOp& o = kOps[i];
            if (o.primary) { primary[o.code] = int16_t(i); continue; }
            primary[0] = int16_t(i);   // a sub-op's OpCode is 0
            single[o.code] = false;
            // Oper.SetAdvancedInfo + MatchSubop
            std::string f = o.format;
            int shift = 0;
            if (f.size() >= 8) { int n = 0; for (int k = 0; k < 8; ++k) n += f[k] == 'o' || f[k] == '0' || f[k] == '1'; shift = 8 - n; }
            int mask = 0, bits = 0;
            for (int k = 0; k < 8 && k < int(f.size()); ++k) {
                if (f[k] == '0') ++bits;
                else if (f[k] == '1') { mask |= 1 << (7 - k); ++bits; }
            }
            int corrected = o.sub;
            if (mask > 0) corrected = uint8_t((o.sub << bits) | mask);
            for (int s = 0; s < 256; ++s) if ((s >> shift) == corrected) secondary[o.code][s] = int16_t(i);
        }
    }
};
const Tables& tables() { static const Tables t; return t; }

struct Fail {};   // where the C# threw out of RunAt: disassembly stops before this instruction

uint8_t fetch(const Machine& m, uint32_t a) {   // MEMC.Get8bitFromRAM: the RAM array is 16 MB + 3
    if (a >= kRamSize + 3) throw Fail{};
    return m.ram[a];
}

std::string hex(uint32_t v, int pad) { char b[16]; std::snprintf(b, sizeof b, "%0*X", pad, v); return b; }

// DArg.cs
struct DArg {
    bool simple = false;
    std::optional<std::string> binary;   // null until the first bit (the C# then throws on GetBitCount)
    uint32_t value = 0;
    const std::string& bin() const { if (!binary) throw Fail{}; return *binary; }
    int bitCount() const { return int(bin().size()); }
    int byteCount() const { return int(std::ceil(float(bitCount()) / 8.0f)); }
    uint8_t v8() const { return uint8_t(value); }
    static std::string h(uint32_t v, int pad) { return "0x" + hex(v, pad); }   // DecimalToHex
    std::string sign(bool neg) const { return simple ? "" : neg ? "-" : "+"; }
    std::string hex8() const { return h(value & 0xFF, 2); }
    std::string hex16() const { return h(value & 0xFFFF, 4); }
    std::string hex24() const { return h(value & 0xFFFFFF, 6); }
    std::string hex32() const { return h(value, 8); }
    std::string s8() const { int8_t v = int8_t(value); return sign(v < 0) + "0x" + hex(uint32_t(std::abs(int(v))), 2); }
    std::string s13() const {
        int v = int(value & 0x1FFF);
        return v & 0x1000 ? sign(true) + "0x" + hex(uint32_t((v ^ 0x1FFF) + 1), 4) : sign(false) + "0x" + hex(uint32_t(v), 4);
    }
    std::string s16() const { int16_t v = int16_t(value); return sign(v < 0) + "0x" + hex(uint32_t(std::abs(int(v))), 4); }
    std::string s24() const {
        int v = int(value & 0xFFFFFF);
        return v & 0x800000 ? sign(true) + "0x" + hex(uint32_t((v ^ 0xFFFFFF) + 1), 6) : sign(false) + "0x" + hex(uint32_t(v), 6);
    }
    std::string s32() const {
        int64_t v = int32_t(value);
        char b[24];
        std::snprintf(b, sizeof b, "%08llX", static_cast<unsigned long long>(v < 0 ? -v : v));
        return sign(v < 0) + "0x" + b;
    }
    std::string relOffset() const {   // GetHexValue24BitRelativeOffset (decimal, despite the name)
        uint32_t pos = 0x7FFFFF & value, neg = 0x800000 - pos;
        return value >= 0x800000 ? sign(true) + std::to_string(neg) : sign(false) + std::to_string(pos);
    }
    std::string relTarget(uint32_t address) const {   // GetHexValue24BitRelative
        uint32_t pos = 0x7FFFFF & value, neg = 0x800000 - pos;
        uint32_t r = address - (value >= 0x800000 ? neg : uint32_t(0) - pos);
        return h(r & 0xFFFFFF, 6);
    }
    std::string relComposite(uint32_t address) const { return simple ? relTarget(address) : relOffset() + " => " + relTarget(address); }
    std::string floatValue() const { float f; std::memcpy(&f, &value, 4); return netFloat(f); }
};

const char* flagName(uint8_t i) {   // Flags.GetFlagNameByIndex
    static const char* const n[16] = {"NZ", "NC", "SP", "NO", "PE", "NE", "LTE", "GTE", "Z", "C", "SN", "OV", "PO", "EQ", "GT", "LT"};
    return i < 16 ? n[i] : "";
}

// DUtils.GetRegisterRepresentation
std::string regRepr(const std::string& form, uint8_t index) {
    static const std::string r = "ABCDEFGHIJKLMNOPQRSTUVWXYZABC";
    if (index > 25) return form == "(rrr)" ? "(" + std::to_string(index) + "?)" : std::string(form.size(), '?');
    auto sub = [&](size_t n) { if (index + n > r.size()) throw Fail{}; return r.substr(index, n); };
    if (form == "(rrr)") return "(" + sub(3) + ")";
    if (form == "(rrr") return "(" + sub(3);
    if (form == "rrr)") return sub(3) + ")";
    if (form == "rr)") return sub(2) + ")";
    if (form == "r)") return sub(1) + ")";
    return sub(form.size());
}

// DUtils.GetNumberRepresentation
std::string numRepr(const std::string& form, const DArg& a, bool rel, uint32_t address, bool sgn, bool flt) {
    if (form == "(nnn)") return "(" + a.hex24() + ")";
    if (form == "nnn)") return a.s24() + ")";
    if (form == "(nnn") return "(" + a.hex24();
    switch (a.byteCount()) {
    case 4: return flt ? a.floatValue() : sgn ? a.s32() : a.hex32();
    case 3: return rel ? a.relComposite(address) : sgn ? a.s24() : a.hex24();
    case 2: return sgn && a.bitCount() == 16 ? a.s16() : sgn && a.bitCount() == 13 ? a.s13() : a.hex16();
    case 1: return sgn ? a.s8() : a.hex8();
    default: return "?n?";
    }
}

std::string trim(std::string s) {
    size_t a = s.find_first_not_of(" \t\r\n\v\f"), b = s.find_last_not_of(" \t\r\n\v\f");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// DInstruction.ProcessParentheses: Regex.Replace(@"\(([^(),]+),\s*([^(),]+)\)") -> "(a + b)" / "(a-b)".
std::string processParentheses(const std::string& in) {
    std::string out;
    auto plain = [](char c) { return c != '(' && c != ')' && c != ','; };
    for (size_t i = 0; i < in.size();) {
        if (in[i] == '(') {
            size_t a = i + 1, e = a;
            while (e < in.size() && plain(in[e])) ++e;
            if (e > a && e < in.size() && in[e] == ',') {
                size_t b = e + 1, f = b;
                while (f < in.size() && plain(in[f])) ++f;
                if (f > b && f < in.size() && in[f] == ')') {
                    std::string left = trim(in.substr(a, e - a)), right = trim(in.substr(b, f - b));
                    out += "(" + left + (right.rfind('-', 0) == 0 ? "" : " + ") + right + ")";
                    i = f + 1;
                    continue;
                }
            }
        }
        out += in[i++];
    }
    return out;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> r;
    for (size_t a = 0;;) {
        size_t b = s.find(sep, a);
        r.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) return r;
        a = b + 1;
    }
}

DInstruction next(const Machine& m, uint32_t address, bool simple) {
    const Tables& t = tables();
    uint8_t first = fetch(m, address), second = fetch(m, address + 1);
    int prim = t.primary[first], sec = t.secondary[first][second];
    int op = t.single[first] ? prim : sec;
    bool rel = prim >= 0 && (std::strcmp(kOps[prim].key, "CALLR") == 0 || std::strcmp(kOps[prim].key, "JR") == 0);
    std::string format = op >= 0 ? kOps[op].format : "", mnemonic = op >= 0 ? kOps[op].key : "ILLEGAL";

    DInstruction d;
    d.Address = address;
    d.Illegal = op < 0;
    std::string compact;
    for (char c : format) if (c != ' ') compact += c;
    d.InstructionLength = uint8_t(compact.size() / 8 + 1);
    for (uint32_t i = 0; i < d.InstructionLength; ++i) d.Bytes.push_back(fetch(m, address + i));

    // CalculateInstructionVariables: the operand letters of the format pick their bits (its own try/catch
    // keeps whatever was assigned before a failure).
    std::vector<DArg> args;
    if (d.InstructionLength >= 2) {
        std::string bits;
        for (size_t i = 1; i < d.Bytes.size(); ++i)
            for (int k = 7; k >= 0; --k) bits += char('0' + (d.Bytes[i] >> k & 1));
        uint8_t count = 0;
        for (char c : compact)
            if (c != 'o' && c != 'u' && c != '0' && c != '1') count = std::max(count, uint8_t(uint8_t(c) - uint8_t('A') + 1));
        args.resize(count);
        for (DArg& a : args) a.simple = simple;
        for (size_t i = 0; i < compact.size(); ++i) {
            char c = compact[i];
            if (c == 'o' || c == 'u' || c == '0' || c == '1') continue;
            uint8_t index = uint8_t(uint8_t(c) - uint8_t('A'));
            if (index >= args.size() || i >= bits.size()) break;
            DArg& a = args[index];
            if (!a.binary) a.binary.emplace();
            if (a.binary->size() >= 32) break;   // shortcut: Convert.ToUInt32 overflow; no format has > 32 bits per operand
            *a.binary += bits[i];
            a.value = a.value << 1 | uint32_t(bits[i] - '0');
        }
    }

    // ComposeInstruction
    d.SpecificInstruction = mnemonic;
    size_t sp = mnemonic.find(' ');
    if (sp == std::string::npos) return d;
    std::string operand = mnemonic.substr(0, sp);
    std::string rest = mnemonic.substr(sp + 1);
    std::string argsString = rest.substr(0, rest.find(' '));
    bool sgn = mnemonic.rfind("SDIV", 0) == 0 || mnemonic.rfind("SMUL", 0) == 0 || mnemonic.rfind("SCP", 0) == 0;
    bool flt = mnemonic.find("fr") != std::string::npos;
    std::vector<std::string> forms = split(argsString, ','), out;
    for (size_t i = 0; i < forms.size(); ++i) {
        if (i >= args.size()) throw Fail{};
        const std::string& f = forms[i];
        const DArg& a = args[i];
        if (f == "fr") out.push_back("F" + std::to_string(a.v8()));
        else if (f.find('r') != std::string::npos) out.push_back(regRepr(f, a.v8()));
        else if (f.find('n') != std::string::npos) out.push_back(numRepr(f, a, rel, address, sgn, flt));
        else if (f.find('f') != std::string::npos) out.push_back(flagName(a.v8()));
        else out.push_back(f);
    }
    std::string joined;
    for (size_t i = 0; i < out.size(); ++i) joined += (i ? ", " : "") + out[i];
    d.SpecificInstruction = processParentheses(operand + " " + joined);
    return d;
}

} // namespace

std::vector<DInstruction> disassemble(const Machine& m, uint32_t address, int count, bool simple) {
    std::vector<DInstruction> r;
    uint32_t ip = address;
    try {
        for (; count > 0; --count) {
            DInstruction d = next(m, ip, simple);
            ip += d.Illegal ? 1 : d.InstructionLength;
            r.push_back(std::move(d));
        }
    } catch (const Fail&) {
        // The reference logged "Debugger RunAt failed" and returned what it had.
    }
    return r;
}

} // namespace c93::svc
