// C lexer, preprocessor and integer literals: CLexer.cs, CPreprocessor.cs, CConstExpr.cs (see cc_internal.h).
#include <algorithm>
#include <cctype>
#include <climits>
#include <iterator>

#include "cc_internal.h"

namespace c93::cc {

// ---------------------------------------------------------------------------- UTF-8 as UTF-16
namespace {
// Decodes one code point at s[i] (advancing i); invalid bytes give U+FFFD, as .NET's decoder.
uint32_t decode(std::string_view s, size_t& i) {
    uint8_t c = uint8_t(s[i++]);
    if (c < 0x80) return c;
    int n = c >= 0xF0 && c < 0xF5 ? 3 : c >= 0xE0 ? (c < 0xF0 ? 2 : 0) : c >= 0xC2 ? 1 : 0;
    if (n == 0) return 0xFFFD;
    uint32_t cp = c & (0x3F >> n);
    for (int k = 0; k < n; ++k) {
        if (i >= s.size() || (uint8_t(s[i]) & 0xC0) != 0x80) return 0xFFFD;
        cp = cp << 6 | (uint8_t(s[i++]) & 0x3F);
    }
    return cp;
}
}

std::string utf8(uint32_t cp) {
    std::string r;
    if (cp < 0x80) r += char(cp);
    else if (cp < 0x800) { r += char(0xC0 | cp >> 6); r += char(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { r += char(0xE0 | cp >> 12); r += char(0x80 | (cp >> 6 & 0x3F)); r += char(0x80 | (cp & 0x3F)); }
    else { r += char(0xF0 | cp >> 18); r += char(0x80 | (cp >> 12 & 0x3F)); r += char(0x80 | (cp >> 6 & 0x3F)); r += char(0x80 | (cp & 0x3F)); }
    return r;
}

size_t utf16Length(std::string_view s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size();) n += decode(s, i) >= 0x10000 ? 2 : 1;
    return n;
}

std::vector<uint8_t> lowBytes(std::string_view s) {
    std::vector<uint8_t> r;
    for (size_t i = 0; i < s.size();) {
        uint32_t cp = decode(s, i);
        if (cp >= 0x10000) { cp -= 0x10000; r.push_back(uint8_t(0xD800 + (cp >> 10))); r.push_back(uint8_t(0xDC00 + (cp & 0x3FF))); }
        else r.push_back(uint8_t(cp));
    }
    return r;
}

std::string Token::str() const {
    switch (kind) {
        case TK::String: return "\"" + text + "\"";
        case TK::Char: return "'" + text + "'";
        case TK::Eof: return "end of file";
        case TK::Newline: return "end of line";
        default: return text;
    }
}

// ---------------------------------------------------------------------------- lexer
namespace {
const std::string_view kPuncts[] = {
    "...", "<<=", ">>=",
    "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
    "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "##",
    "[", "]", "(", ")", "{", "}", ".", "&", "*", "+", "-", "~", "!", "/", "%",
    "<", ">", "^", "|", "?", ":", ";", "=", ",", "#"};

// char.IsLetter. shortcut: exact for ASCII and Latin-1; other BMP characters count as letters unless they are
// in the punctuation/symbol/private-use blocks. Upgrade with a Unicode table if a program needs it.
bool isLetter(uint32_t c) {
    if (c < 0x80) return (c | 32) >= 'a' && (c | 32) <= 'z';
    if (c < 0x100) return c == 0xAA || c == 0xB5 || c == 0xBA || (c >= 0xC0 && c != 0xD7 && c != 0xF7);
    if (c >= 0x10000) return false;   // a surrogate half on its own
    return !((c >= 0x2000 && c <= 0x2BFF) || (c >= 0x3000 && c <= 0x303F) || (c >= 0xD800 && c <= 0xF8FF) ||
             (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFF20) || c == 0xFFFD);
}
bool isDigit(uint32_t c) { return c >= '0' && c <= '9'; }   // shortcut: ASCII digits only (char.IsDigit takes all Nd)
bool isHex(uint32_t c) { return isDigit(c) || ((c | 32) >= 'a' && (c | 32) <= 'f'); }

class Lexer {
public:
    Lexer(std::string_view source, const std::string* file, Errors& errors) : file_(file), errors_(errors) {
        // Normalize line endings (CRLF and lone CR -> LF)
        src_.reserve(source.size());
        for (size_t i = 0; i < source.size(); ++i) {
            if (source[i] == '\r') { src_ += '\n'; if (i + 1 < source.size() && source[i + 1] == '\n') ++i; }
            else src_ += source[i];
        }
    }

    std::vector<Token> run() {
        std::vector<Token> tokens;
        tokens.reserve(src_.size() / 3 + 16);
        bool leadingSpace = false;
        while (true) {
            skipContinuations();
            if (pos_ >= src_.size()) break;
            char c = src_[pos_];
            if (c == '\n') { tokens.push_back(make(TK::Newline, "\n", line_, col_)); advance(); leadingSpace = false; continue; }
            if (c == ' ' || c == '\t' || c == '\f' || c == '\v') { advance(); leadingSpace = true; continue; }
            if (c == '/' && peek(1) == '/') {
                while (pos_ < src_.size() && src_[pos_] != '\n') advance();
                leadingSpace = true;
                continue;
            }
            if (c == '/' && peek(1) == '*') {
                int sl = line_, sc = col_;
                advance(); advance();
                bool closed = false;
                while (pos_ < src_.size()) {
                    if (src_[pos_] == '*' && peek(1) == '/') { advance(); advance(); closed = true; break; }
                    advance();
                }
                if (!closed) error(sl, sc, "Unterminated block comment");
                leadingSpace = true;
                continue;
            }
            Token t;
            if (lexToken(t)) { t.leadingSpace = leadingSpace; tokens.push_back(std::move(t)); }
            leadingSpace = false;
        }
        tokens.push_back(make(TK::Newline, "\n", line_, col_));
        tokens.push_back(make(TK::Eof, "", line_, col_));
        return tokens;
    }

private:
    std::string src_;
    const std::string* file_;
    Errors& errors_;
    size_t pos_ = 0;
    int line_ = 1, col_ = 1;

    Token make(TK k, std::string text, int line, int col) const {
        Token t;
        t.kind = k; t.text = std::move(text); t.file = file_; t.line = line; t.col = col;
        return t;
    }
    uint32_t cur() const { size_t i = pos_; return decode(src_, i); }
    char peek(int off) const { return pos_ + off < src_.size() ? src_[pos_ + off] : '\0'; }
    void advance() {   // one character: a UTF-16 column, two for an astral code point
        if (src_[pos_] == '\n') { ++line_; col_ = 1; ++pos_; return; }
        uint32_t cp = decode(src_, pos_);
        col_ += cp >= 0x10000 ? 2 : 1;
    }
    void appendCur(std::string& sb) { size_t s = pos_; advance(); sb.append(src_, s, pos_ - s); }
    void skipContinuations() {
        while (pos_ + 1 < src_.size() && src_[pos_] == '\\' && src_[pos_ + 1] == '\n') { pos_ += 2; ++line_; col_ = 1; }
    }
    void error(int line, int col, std::string m) { errors_.push_back({*file_, line, col, std::move(m)}); }

    bool lexToken(Token& out) {
        int line = line_, col = col_;
        uint32_t c = cur();
        if (isLetter(c) || c == '_') {
            std::string sb;
            while (pos_ < src_.size()) {
                if (uint8_t(src_[pos_]) < 0x80) {   // fast path: a run of ASCII letters, digits and '_'
                    size_t s = pos_;
                    while (pos_ < src_.size() && (std::isalnum(uint8_t(src_[pos_])) || src_[pos_] == '_')) ++pos_;
                    if (pos_ == s) break;
                    col_ += int(pos_ - s);
                    sb.append(src_, s, pos_ - s);
                } else if (isLetter(cur())) appendCur(sb);
                else break;
                skipContinuations();
            }
            out = make(TK::Identifier, std::move(sb), line, col);
            return true;
        }
        if (isDigit(c) || (c == '.' && isDigit(uint8_t(peek(1))))) {
            // pp-number: digits, letters, underscores, dots, and a sign directly after an exponent letter
            std::string sb;
            while (pos_ < src_.size()) {
                uint32_t d = cur();
                char prev = sb.empty() ? '\0' : char(std::tolower(uint8_t(sb.back()) < 0x80 ? sb.back() : 0));
                bool isHexLit = sb.size() > 1 && sb[0] == '0' && (sb[1] == 'x' || sb[1] == 'X');
                bool expSign = (d == '+' || d == '-') && (prev == 'p' || (prev == 'e' && !isHexLit));
                if (!(isLetter(d) || isDigit(d) || d == '_' || d == '.' || expSign)) break;
                appendCur(sb);
                skipContinuations();
            }
            out = make(TK::Number, std::move(sb), line, col);
            return true;
        }
        if (c == '"') { out = make(TK::String, readQuoted('"', line, col), line, col); return true; }
        if (c == '\'') {
            std::string v = readQuoted('\'', line, col);
            size_t n = utf16Length(v);
            if (n == 0) error(line, col, "Empty character constant");
            else if (n > 1) error(line, col, "Multi-character constants are not supported");
            out = make(TK::Char, std::move(v), line, col);
            return true;
        }
        std::string_view rest(src_.data() + pos_, src_.size() - pos_);
        for (std::string_view p : kPuncts) {   // first match wins: the list is longest first
            if (p[0] != rest[0] || !rest.starts_with(p)) continue;
            {
                pos_ += p.size(); col_ += int(p.size());
                out = make(TK::Punct, std::string(p), line, col);
                return true;
            }
        }
        error(line, col, "Unexpected character '" + utf8(c) + "'");
        advance();
        return false;
    }

    std::string readQuoted(char quote, int line, int col) {
        advance();
        std::string sb;
        while (true) {
            skipContinuations();
            if (pos_ >= src_.size() || src_[pos_] == '\n') {
                error(line, col, quote == '"' ? "Unterminated string literal" : "Unterminated character constant");
                return sb;
            }
            char c = src_[pos_];
            if (c == quote) { advance(); return sb; }
            if (c == '\\') { advance(); sb += readEscape(line, col); continue; }
            appendCur(sb);
        }
    }

    std::string readEscape(int line, int col) {
        if (pos_ >= src_.size()) return "\\";
        uint32_t e = cur();
        advance();
        switch (e) {
            case 'n': return "\n";
            case 't': return "\t";
            case 'r': return "\r";
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
                int v = int(e - '0');
                for (int i = 0; i < 2 && pos_ < src_.size() && src_[pos_] >= '0' && src_[pos_] <= '7'; ++i) { v = v * 8 + (src_[pos_] - '0'); advance(); }
                return utf8(uint32_t(v & 0xFF));
            }
            case 'x': {
                unsigned v = 0;
                int digits = 0;
                while (pos_ < src_.size() && isHex(uint8_t(src_[pos_]))) {
                    char h = src_[pos_];
                    v = v * 16 + unsigned(isDigit(uint8_t(h)) ? h - '0' : (h | 32) - 'a' + 10);   // wraps, as C#'s int
                    advance();
                    ++digits;
                }
                if (digits == 0) error(line, col, "\\x used with no following hex digits");
                return utf8(v & 0xFF);
            }
            case 'a': return "\a";
            case 'b': return "\b";
            case 'f': return "\f";
            case 'v': return "\v";
            case 'e': return "\x1B";
            case '\\': return "\\";
            case '\'': return "'";
            case '"': return "\"";
            case '?': return "?";
            default:
                error(line, col, "Unknown escape sequence '\\" + utf8(e) + "'");
                return utf8(e);
        }
    }
};
} // namespace

std::vector<Token> tokenize(std::string_view src, const std::string* file, Errors& errors) { return Lexer(src, file, errors).run(); }

// ---------------------------------------------------------------------------- integer literals
bool tryParseInt(const std::string& text, uint32_t& value, bool& isUnsigned, std::string& error) {
    value = 0;
    isUnsigned = false;
    std::string s = text;
    bool hasU = false;
    int lCount = 0;
    while (!s.empty()) {
        char last = char(std::tolower(uint8_t(s.back()) < 0x80 ? s.back() : 0));
        if (last == 'u' && !hasU) { hasU = true; s.pop_back(); }
        else if (last == 'l') { ++lCount; s.pop_back(); }
        else break;
    }
    if (lCount > 1) { error = "'long long' is not supported (integers are 32-bit)"; return false; }
    if (s.empty()) { error = "Invalid integer literal '" + text + "'"; return false; }
    int radix = 10;
    std::string digits = s;
    if (s.size() > 1 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { radix = 16; digits = s.substr(2); }
    else if (s.size() > 1 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) { radix = 2; digits = s.substr(2); }
    else if (s.size() > 1 && s[0] == '0') { radix = 8; digits = s.substr(1); }
    if (digits.empty()) { error = "Invalid integer literal '" + text + "'"; return false; }
    uint64_t acc = 0;
    for (size_t i = 0; i < digits.size();) {
        size_t at = i;
        uint32_t ch = decode(digits, i);
        int d;
        if (ch >= '0' && ch <= '9') d = int(ch - '0');
        else if (ch >= 'a' && ch <= 'f') d = int(ch - 'a' + 10);
        else if (ch >= 'A' && ch <= 'F') d = int(ch - 'A' + 10);
        else {
            // (precedence as the reference: '.' anywhere, or 'e' in a decimal literal)
            error = text.find('.') != std::string::npos || (text.find('e') != std::string::npos && radix == 10)
                        ? "Floating point is not supported"
                        : "Invalid digit '" + digits.substr(at, i - at) + "' in integer literal '" + text + "'";
            return false;
        }
        if (d >= radix) { error = "Invalid digit '" + digits.substr(at, i - at) + "' in integer literal '" + text + "'"; return false; }
        acc = acc * uint64_t(radix) + uint64_t(d);
        if (acc > UINT32_MAX) { error = "Integer literal '" + text + "' is too large (integers are 32-bit)"; return false; }
    }
    value = uint32_t(acc);
    if (hasU) isUnsigned = true;
    else if (acc > INT32_MAX) {
        if (radix == 10) { error = "Integer literal '" + text + "' is too large for int; add a 'u' suffix"; return false; }
        isUnsigned = true;
    }
    return true;
}

// ---------------------------------------------------------------------------- #if evaluator
namespace {
class PPExpr {
public:
    PPExpr(const std::vector<Token>& t, Errors& e) : t_(t), errors_(e) {}
    size_t pos = 0;

    void error(const Token& t, std::string m) { if (errors_.empty()) errors_.push_back({*t.file, t.line, t.col, std::move(m)}); }

    int64_t ternary() {
        int64_t c = binary(0);
        if (accept("?")) {
            int64_t a = ternary();
            if (!accept(":")) error(pos < t_.size() ? t_[pos] : t_.back(), "Expected ':' in #if expression");
            int64_t b = ternary();
            return c != 0 ? a : b;
        }
        return c;
    }

private:
    const std::vector<Token>& t_;
    Errors& errors_;

    bool accept(std::string_view p) {
        if (pos < t_.size() && t_[pos].kind == TK::Punct && t_[pos].text == p) { ++pos; return true; }
        return false;
    }

    int64_t binary(int level) {
        static const std::vector<std::vector<std::string_view>> levels = {
            {"||"}, {"&&"}, {"|"}, {"^"}, {"&"}, {"==", "!="}, {"<", ">", "<=", ">="}, {"<<", ">>"}, {"+", "-"}, {"*", "/", "%"}};
        if (level == int(levels.size())) return unary();
        int64_t left = binary(level + 1);
        while (true) {
            std::string_view op;
            for (auto o : levels[size_t(level)]) if (accept(o)) { op = o; break; }
            if (op.empty()) return left;
            const Token& at = t_[pos - 1];
            int64_t right = binary(level + 1);
            uint64_t ul = uint64_t(left), ur = uint64_t(right);   // long arithmetic wraps in C#
            if (op == "||") left = (left != 0 || right != 0) ? 1 : 0;
            else if (op == "&&") left = (left != 0 && right != 0) ? 1 : 0;
            else if (op == "|") left = left | right;
            else if (op == "^") left = left ^ right;
            else if (op == "&") left = left & right;
            else if (op == "==") left = left == right;
            else if (op == "!=") left = left != right;
            else if (op == "<") left = left < right;
            else if (op == ">") left = left > right;
            else if (op == "<=") left = left <= right;
            else if (op == ">=") left = left >= right;
            else if (op == "<<") left = int64_t(ul << (right & 63));
            else if (op == ">>") left = left >> (right & 63);
            else if (op == "+") left = int64_t(ul + ur);
            else if (op == "-") left = int64_t(ul - ur);
            else if (op == "*") left = int64_t(ul * ur);
            else if (right == 0) { error(at, "Division by zero in #if expression"); left = 0; }
            else if (left == INT64_MIN && right == -1) fatal("OverflowException");
            else left = op == "/" ? left / right : left % right;
        }
    }

    int64_t unary() {
        if (accept("+")) return unary();
        if (accept("-")) return int64_t(0 - uint64_t(unary()));
        if (accept("!")) return unary() == 0 ? 1 : 0;
        if (accept("~")) return ~unary();
        if (accept("(")) {
            int64_t v = ternary();
            if (!accept(")")) error(pos < t_.size() ? t_[pos] : t_.back(), "Expected ')' in #if expression");
            return v;
        }
        if (pos >= t_.size()) { error(t_.back(), "Unexpected end of #if expression"); return 0; }
        const Token& t = t_[pos++];
        if (t.kind == TK::Number) {
            uint32_t v; bool uns; std::string err;
            if (tryParseInt(t.text, v, uns, err)) return uns ? int64_t(v) : int64_t(int32_t(v));
            error(t, err);
            return 0;
        }
        if (t.kind == TK::Char) return t.text.empty() ? 0 : int8_t(lowBytes(t.text)[0]);
        error(t, "Unexpected token '" + t.str() + "' in #if expression");
        return 0;
    }
};

// Hide sets (sorted vectors; null = empty)
bool contains(const HideSet& s, const std::string& n) { return s && std::binary_search(s->begin(), s->end(), n); }
HideSet with(const HideSet& s, const std::string& n) {
    auto r = std::make_shared<std::vector<std::string>>(s ? *s : std::vector<std::string>{});
    auto it = std::lower_bound(r->begin(), r->end(), n);
    if (it == r->end() || *it != n) r->insert(it, n);
    return r;
}
HideSet unite(const HideSet& a, const HideSet& b) {
    if (!a || a->empty()) return b;
    if (!b || b->empty()) return a;
    auto r = std::make_shared<std::vector<std::string>>();
    std::set_union(a->begin(), a->end(), b->begin(), b->end(), std::back_inserter(*r));
    return r;
}
HideSet intersect(const HideSet& a, const HideSet& b) {
    if (!a || !b) return nullptr;
    auto r = std::make_shared<std::vector<std::string>>();
    std::set_intersection(a->begin(), a->end(), b->begin(), b->end(), std::back_inserter(*r));
    return r;
}

std::string escape(const std::string& s) {
    std::string r;
    for (char c : s) {
        if (c == '\\') r += "\\\\";
        else if (c == '"') r += "\\\"";
        else if (c == '\n') r += "\\n";
        else r += c;
    }
    return r;
}

std::string stringify(const std::vector<Token>& toks) {
    std::string sb;
    for (size_t i = 0; i < toks.size(); ++i) {
        if (i > 0 && toks[i].leadingSpace) sb += ' ';
        const Token& t = toks[i];
        if (t.kind == TK::String) sb += "\"" + escape(t.text) + "\"";
        else if (t.kind == TK::Char) sb += "'" + escape(t.text) + "'";
        else sb += t.text;
    }
    return sb;
}

Token relocate(const Token& t, const HideSet& hs, const Token& site) {
    Token c = t;
    c.file = site.file; c.line = site.line; c.col = site.col;
    c.hide = unite(t.hide, hs);
    return c;
}

std::vector<Token> withoutLineEnds(std::vector<Token> v) {
    std::erase_if(v, [](const Token& t) { return t.kind == TK::Newline || t.kind == TK::Eof; });
    return v;
}
} // namespace

// ---------------------------------------------------------------------------- preprocessor
Preprocessor::Preprocessor(Files& files, Pool<Token>& store, Resolver resolve, Errors& errors) : files_(files), store_(store), resolve_(std::move(resolve)), errors_(errors) {
    define("__C93__", "1");
    define("__STDC__", "1");
}

void Preprocessor::error(const Token& at, std::string message) { errors_.push_back({*at.file, at.line, at.col, std::move(message)}); }

void Preprocessor::define(const std::string& name, const std::string& value) {
    Errors lexErrors;
    Macro m;
    m.name = name;
    m.body = withoutLineEnds(tokenize(value, files_.intern("<builtin>"), lexErrors));
    macros_[name] = std::move(m);
}

TokenList Preprocessor::process(std::string_view source, const std::string& file) {
    TokenList out;
    processFile(source, file, out);
    Token& eof = store_.emplace_back();
    eof.kind = TK::Eof; eof.file = files_.intern(file); eof.line = out.empty() ? 1 : out.back()->line; eof.col = 1;
    out.push_back(&eof);
    return out;
}

void Preprocessor::processFile(std::string_view source, const std::string& file, TokenList& out) {
    auto flush = [&](Token* b, Token* e) {
        run_.clear();
        expand(b, e, run_);
        for (Token& t : run_) out.push_back(&store_.emplace_back(std::move(t)));
    };
    std::vector<Token> tokens = tokenize(source, files_.intern(file), errors_);
    std::vector<Cond> conds;
    // Text lines are macro-expanded in runs between directives; a run is compacted in place at [run, w).
    size_t i = 0, run = 0, w = 0;
    while (i < tokens.size() && tokens[i].kind != TK::Eof) {
        size_t start = i;
        while (tokens[i].kind != TK::Newline && tokens[i].kind != TK::Eof) ++i;
        size_t end = i;
        if (tokens[i].kind == TK::Newline) ++i;
        bool active = conds.empty() || conds.back().active;
        if (end > start && tokens[start].kind == TK::Punct && tokens[start].text == "#") {
            if (w > run) flush(tokens.data() + run, tokens.data() + w);
            std::vector<Token> line(std::make_move_iterator(tokens.begin() + std::ptrdiff_t(start)), std::make_move_iterator(tokens.begin() + std::ptrdiff_t(end)));
            run = w = i;
            directive(line, file, conds, active, out);
            continue;
        }
        if (active) for (size_t k = start; k < end; ++k) { if (w != k) tokens[w] = std::move(tokens[k]); ++w; }
    }
    if (w > run) flush(tokens.data() + run, tokens.data() + w);
    if (!conds.empty()) error(tokens.back(), "Unterminated conditional directive (#if without #endif)");
}

void Preprocessor::directive(std::vector<Token>& line, const std::string& file, std::vector<Cond>& conds, bool active, TokenList& out) {
    if (line.size() == 1) return;   // null directive
    const Token dirTok = line[1];
    const std::string& dir = dirTok.text;
    std::vector<Token> args(line.begin() + 2, line.end());

    if (dir == "if") {
        bool v = active && evaluate(args, dirTok);
        conds.push_back({active, v, v});
        return;
    }
    if (dir == "ifdef" || dir == "ifndef") {
        bool v = false;
        if (active) {
            if (args.empty() || args[0].kind != TK::Identifier) error(dirTok, "#" + dir + " expects a macro name");
            else v = macros_.count(args[0].text) == (dir == "ifdef" ? 1u : 0u);
        }
        conds.push_back({active, v, v});
        return;
    }
    if (dir == "elif") {
        if (conds.empty()) { error(dirTok, "#elif without #if"); return; }
        Cond& s = conds.back();
        if (s.seenElse) error(dirTok, "#elif after #else");
        if (!s.parentActive || s.anyTaken) s.active = false;
        else { s.active = evaluate(args, dirTok); s.anyTaken = s.active; }
        return;
    }
    if (dir == "else") {
        if (conds.empty()) { error(dirTok, "#else without #if"); return; }
        Cond& s = conds.back();
        if (s.seenElse) error(dirTok, "Duplicate #else");
        s.seenElse = true;
        s.active = s.parentActive && !s.anyTaken;
        s.anyTaken = true;
        return;
    }
    if (dir == "endif") {
        if (conds.empty()) error(dirTok, "#endif without #if");
        else conds.pop_back();
        return;
    }
    if (!active) return;

    if (dir == "define") handleDefine(args, dirTok);
    else if (dir == "undef") {
        if (args.empty() || args[0].kind != TK::Identifier) error(dirTok, "#undef expects a macro name");
        else macros_.erase(args[0].text);
    }
    else if (dir == "include") handleInclude(args, dirTok, file, out);
    else if (dir == "error") {
        std::string j;
        for (size_t k = 0; k < args.size(); ++k) j += (k ? " " : "") + args[k].str();
        error(dirTok, "#error " + j);
    }
    else if (dir == "warning" || dir == "line") {}
    else if (dir == "pragma") {
        if (!args.empty() && args[0].text == "once") pragmaOnce_.insert(file);
        else if (args.size() >= 3 && args[0].text == "c93" && args[1].kind == TK::Identifier) {
            auto it = std::find_if(pragmas.begin(), pragmas.end(), [&](auto& p) { return p.first == args[1].text; });
            if (it != pragmas.end()) it->second = args[2];   // Dictionary overwrite keeps the first position
            else pragmas.emplace_back(args[1].text, args[2]);
        }
    }
    else error(dirTok, "Unknown preprocessor directive '#" + dir + "'");
}

void Preprocessor::handleDefine(const std::vector<Token>& args, const Token& dirTok) {
    if (args.empty() || args[0].kind != TK::Identifier) { error(dirTok, "#define expects a macro name"); return; }
    Macro m;
    m.name = args[0].text;
    size_t i = 1;
    // Function-like only if '(' immediately follows the name (no whitespace)
    if (i < args.size() && args[i].is("(") && !args[i].leadingSpace) {
        m.functionLike = true;
        ++i;
        bool expectParam = true;
        while (true) {
            if (i >= args.size()) { error(dirTok, "Missing ')' in macro parameter list"); return; }
            const Token& t = args[i++];
            if (t.is(")")) break;
            if (t.is(",") && !expectParam) { expectParam = true; continue; }
            if (t.is("...") && expectParam) { m.variadic = true; m.params.push_back("__VA_ARGS__"); expectParam = false; continue; }
            if (t.kind == TK::Identifier && expectParam) { m.params.push_back(t.text); expectParam = false; continue; }
            error(t, "Invalid token '" + t.str() + "' in macro parameter list");
            return;
        }
    }
    m.body.assign(args.begin() + std::ptrdiff_t(i), args.end());
    std::string name = m.name;
    macros_[name] = std::move(m);
}

void Preprocessor::handleInclude(const std::vector<Token>& args, const Token& dirTok, const std::string& file, TokenList& out) {
    std::optional<std::string> name;
    if (args.size() == 1 && args[0].kind == TK::String) name = args[0].text;
    else if (args.size() >= 2 && args[0].is("<")) {
        std::string sb;
        size_t i = 1;
        for (; i < args.size() && !args[i].is(">"); ++i) sb += args[i].text;
        if (i < args.size()) name = sb;
    }
    if (!name) { error(dirTok, "#include expects \"FILENAME\" or <FILENAME>"); return; }
    if (includeDepth_ >= 32) { error(dirTok, "#include nested too deeply"); return; }
    std::optional<std::string> src = resolve_ ? resolve_(*name, file) : std::nullopt;
    if (!src) { error(dirTok, "Cannot find include file '" + *name + "'"); return; }
    if (pragmaOnce_.count(*name)) return;
    ++includeDepth_;
    processFile(*src, *name, out);
    --includeDepth_;
}

bool Preprocessor::evaluate(const std::vector<Token>& args, const Token& dirTok) {
    // Replace defined X / defined(X) before macro expansion
    std::vector<Token> replaced;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i].kind == TK::Identifier && args[i].text == "defined") {
            std::string name;
            if (i + 1 < args.size() && args[i + 1].kind == TK::Identifier) { name = args[i + 1].text; i += 1; }
            else if (i + 3 < args.size() && args[i + 1].is("(") && args[i + 2].kind == TK::Identifier && args[i + 3].is(")")) { name = args[i + 2].text; i += 3; }
            else { error(args[i], "Malformed 'defined' expression"); return false; }
            Token t;
            t.kind = TK::Number; t.text = macros_.count(name) ? "1" : "0"; t.file = args[i].file; t.line = args[i].line; t.col = args[i].col;
            replaced.push_back(std::move(t));
            continue;
        }
        replaced.push_back(args[i]);
    }
    std::vector<Token> expanded = expand(std::move(replaced));
    for (Token& t : expanded)   // remaining identifiers evaluate to 0 ("true" to 1)
        if (t.kind == TK::Identifier) { t.kind = TK::Number; t.text = t.text == "true" ? "1" : "0"; }
    if (expanded.empty()) { error(dirTok, "#if with no expression"); return false; }
    Errors errs;
    PPExpr p(expanded, errs);
    int64_t v = p.ternary();
    if (p.pos < expanded.size() && errs.empty()) p.error(expanded[p.pos], "Unexpected token '" + expanded[p.pos].str() + "' in #if expression");
    if (!errs.empty()) { errors_.insert(errors_.end(), errs.begin(), errs.end()); return false; }
    return v != 0;
}

// Expands [in, end) (consumed) onto output. Expansions are pushed back on a stack whose top is the queue front.
void Preprocessor::expand(Token* in, Token* end, std::vector<Token>& output) {
    std::vector<Token> st;
    auto empty = [&] { return st.empty() && in == end; };
    auto front = [&]() -> Token& { return st.empty() ? *in : st.back(); };
    auto pop = [&] { Token t = std::move(front()); if (st.empty()) ++in; else st.pop_back(); return t; };
    auto pushFront = [&](std::vector<Token>& toks) { for (size_t k = toks.size(); k-- > 0;) st.push_back(std::move(toks[k])); };
    while (!empty()) {
        Token& f = front();
        auto it = f.kind == TK::Identifier ? macros_.find(f.text) : macros_.end();
        if (it == macros_.end() || contains(f.hide, f.text)) { output.push_back(pop()); continue; }
        Token tok = pop();
        const Macro& macro = it->second;
        if (!macro.functionLike) {
            auto body = substitute(macro, nullptr, with(tok.hide, macro.name), tok);
            pushFront(body);
            continue;
        }
        // Function-like macro: needs '(' next, otherwise it's a plain identifier
        if (empty() || !front().is("(")) { output.push_back(std::move(tok)); continue; }
        pop();
        std::vector<std::vector<Token>> actuals(1);
        int depth = 0;
        Token close;
        bool closed = false;
        while (!empty()) {
            Token t = pop();
            if (t.is("(")) ++depth;
            else if (t.is(")")) {
                if (depth == 0) { close = std::move(t); closed = true; break; }
                --depth;
            }
            else if (t.is(",") && depth == 0) { actuals.emplace_back(); continue; }
            actuals.back().push_back(std::move(t));
        }
        if (!closed) { error(tok, "Unterminated argument list invoking macro '" + tok.text + "'"); return; }

        if (macro.params.empty() && actuals.size() == 1 && actuals[0].empty()) actuals.clear();
        if (macro.variadic) {
            size_t fixed = macro.params.size() - 1;
            if (actuals.size() < fixed) { error(tok, "Macro '" + macro.name + "' requires at least " + std::to_string(fixed) + " argument(s)"); continue; }
            std::vector<Token> va;   // extra arguments merge into __VA_ARGS__
            for (size_t a = fixed; a < actuals.size(); ++a) {
                if (a > fixed) {
                    Token c;
                    c.kind = TK::Punct; c.text = ","; c.file = tok.file; c.line = tok.line; c.col = tok.col;
                    va.push_back(std::move(c));
                }
                va.insert(va.end(), actuals[a].begin(), actuals[a].end());
            }
            actuals.resize(fixed);
            actuals.push_back(std::move(va));
        }
        else if (actuals.size() != macro.params.size()) {
            error(tok, "Macro '" + macro.name + "' expects " + std::to_string(macro.params.size()) + " argument(s) but got " + std::to_string(actuals.size()));
            continue;
        }
        auto result = substitute(macro, &actuals, with(intersect(tok.hide, close.hide), macro.name), tok);
        pushFront(result);
    }
}

std::vector<Token> Preprocessor::expand(std::vector<Token> input) {
    std::vector<Token> out;
    expand(input.data(), input.data() + input.size(), out);
    return out;
}

std::vector<Token> Preprocessor::substitute(const Macro& macro, const std::vector<std::vector<Token>>* actuals, const HideSet& hs, const Token& site) {
    std::vector<Token> result;
    const std::vector<Token>& body = macro.body;
    auto paramIndex = [&](const Token& t) -> int {
        if (!actuals || t.kind != TK::Identifier) return -1;
        auto f = std::find(macro.params.begin(), macro.params.end(), t.text);
        return f == macro.params.end() ? -1 : int(f - macro.params.begin());
    };
    for (size_t i = 0; i < body.size(); ++i) {
        const Token& t = body[i];
        // Stringification: # param
        if (macro.functionLike && t.is("#") && i + 1 < body.size() && paramIndex(body[i + 1]) >= 0) {
            Token s;
            s.kind = TK::String; s.text = stringify((*actuals)[size_t(paramIndex(body[i + 1]))]);
            s.file = site.file; s.line = site.line; s.col = site.col; s.leadingSpace = t.leadingSpace;
            result.push_back(std::move(s));
            ++i;
            continue;
        }
        // Token pasting: X ## Y
        if (i + 1 < body.size() && body[i + 1].is("##")) {
            auto operand = [&](const Token& x) { int p = paramIndex(x); return p >= 0 ? (*actuals)[size_t(p)] : std::vector<Token>{x}; };
            std::vector<Token> left = operand(t);
            i += 2;
            while (true) {
                if (i >= body.size()) { error(site, "'##' cannot appear at the end of a macro expansion"); break; }
                left = paste(std::move(left), operand(body[i]), site);
                if (i + 1 < body.size() && body[i + 1].is("##")) { i += 2; continue; }
                break;
            }
            for (auto& x : left) result.push_back(relocate(x, hs, site));
            continue;
        }
        int p = paramIndex(t);
        if (p >= 0) {
            // Arguments are fully macro-expanded before substitution
            std::vector<Token> e = expand((*actuals)[size_t(p)]);
            bool first = true;
            for (auto& x : e) {
                Token c = relocate(x, hs, site);
                if (first) c.leadingSpace = t.leadingSpace;
                first = false;
                result.push_back(std::move(c));
            }
            continue;
        }
        result.push_back(relocate(t, hs, site));
    }
    return result;
}

std::vector<Token> Preprocessor::paste(std::vector<Token> left, std::vector<Token> right, const Token& site) {
    if (left.empty()) return right;
    if (right.empty()) return left;
    std::string text = left.back().text + right[0].text;
    Errors lexErrors;
    std::vector<Token> toks = withoutLineEnds(tokenize(text, site.file, lexErrors));
    if (toks.size() != 1 || !lexErrors.empty())
        error(site, "Pasting \"" + left.back().text + "\" and \"" + right[0].text + "\" does not give a valid token");
    left.pop_back();
    left.insert(left.end(), toks.begin(), toks.end());
    left.insert(left.end(), right.begin() + 1, right.end());
    return left;
}

} // namespace c93::cc
