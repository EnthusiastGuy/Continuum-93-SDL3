#include "service/json.h"

#include <cstdio>
#include <cstdlib>

#include "input.h"   // utf8Next

namespace c93::svc::json {
namespace {

struct Parser {
    std::string_view s;
    size_t i = 0;
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i; }
    bool lit(std::string_view w) { if (s.substr(i, w.size()) != w) return false; i += w.size(); return true; }
    static void utf8(std::string& o, uint32_t c) {
        if (c < 0x80) o += char(c);
        else if (c < 0x800) { o += char(0xC0 | c >> 6); o += char(0x80 | (c & 63)); }
        else if (c < 0x10000) { o += char(0xE0 | c >> 12); o += char(0x80 | (c >> 6 & 63)); o += char(0x80 | (c & 63)); }
        else { o += char(0xF0 | c >> 18); o += char(0x80 | (c >> 12 & 63)); o += char(0x80 | (c >> 6 & 63)); o += char(0x80 | (c & 63)); }
    }
    bool hex4(uint32_t& v) {
        if (i + 4 > s.size()) return false;
        v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s[i++];
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) return false;
            v = v << 4 | uint32_t(d);
        }
        return true;
    }
    bool string(std::string& o) {
        if (i >= s.size() || s[i] != '"') return false;
        for (++i; i < s.size();) {
            char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') { o += c; continue; }
            if (i >= s.size()) return false;
            switch (char e = s[i++]) {
            case '"': case '\\': case '/': o += e; break;
            case 'b': o += '\b'; break;
            case 'f': o += '\f'; break;
            case 'n': o += '\n'; break;
            case 'r': o += '\r'; break;
            case 't': o += '\t'; break;
            case 'u': {
                uint32_t u, lo;
                if (!hex4(u)) return false;
                if (u >= 0xD800 && u < 0xDC00 && lit("\\u") && hex4(lo) && lo >= 0xDC00 && lo < 0xE000)
                    u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                utf8(o, u);
                break;
            }
            default: return false;
            }
        }
        return false;
    }
    bool value(Value& v, int depth) {
        if (depth > 64) return false;
        ws();
        if (i >= s.size()) return false;
        char c = s[i];
        if (c == '{') {
            v.type = Value::Object;
            ++i; ws();
            if (i < s.size() && s[i] == '}') { ++i; return true; }
            for (;;) {
                std::string k;
                ws();
                if (!string(k)) return false;
                ws();
                if (i >= s.size() || s[i++] != ':') return false;
                v.obj.emplace_back(std::move(k), Value{});
                if (!value(v.obj.back().second, depth + 1)) return false;
                ws();
                if (i >= s.size()) return false;
                if (s[i] == ',') { ++i; continue; }
                if (s[i++] == '}') return true;
                return false;
            }
        }
        if (c == '[') {
            v.type = Value::Array;
            ++i; ws();
            if (i < s.size() && s[i] == ']') { ++i; return true; }
            for (;;) {
                v.arr.emplace_back();
                if (!value(v.arr.back(), depth + 1)) return false;
                ws();
                if (i >= s.size()) return false;
                if (s[i] == ',') { ++i; continue; }
                if (s[i++] == ']') return true;
                return false;
            }
        }
        if (c == '"') { v.type = Value::String; return string(v.str); }
        if (lit("true")) { v.type = Value::Bool; v.b = true; return true; }
        if (lit("false")) { v.type = Value::Bool; return true; }
        if (lit("null")) return true;
        std::string num(s.substr(i, s.find_first_not_of("+-0123456789.eE", i) - i));
        if (num.empty()) return false;
        char* end;
        v.type = Value::Number;
        v.num = std::strtod(num.c_str(), &end);
        i += num.size();
        return *end == 0;
    }
};

} // namespace

bool parse(std::string_view text, Value& out) {
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);   // File.ReadAllText drops the BOM
    Parser p{text};
    out = Value{};
    if (!p.value(out, 0)) return false;
    p.ws();
    return p.i == text.size();
}

std::string quote(std::string_view s) {
    std::string o = "\"";
    char u[8];
    auto esc = [&](uint32_t c) { std::snprintf(u, sizeof u, "\\u%04X", unsigned(c)); o += u; };
    for (const char* p = s.data(); p < s.data() + s.size();) {
        uint32_t c = utf8Next(p);
        switch (c) {
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        case '\b': o += "\\b"; break;
        case '\f': o += "\\f"; break;
        // JavaScriptEncoder.Default also escapes HTML-sensitive characters and everything past ASCII.
        case '"': case '&': case '<': case '>': case '\'': case '+': case '`': esc(c); break;
        default:
            if (c < 0x20 || c > 0x7E) {
                if (c >= 0x10000) { esc(0xD800 + ((c - 0x10000) >> 10)); esc(0xDC00 + ((c - 0x10000) & 0x3FF)); }
                else esc(c);
            } else o += char(c);
        }
    }
    return o + "\"";
}

} // namespace c93::svc::json
