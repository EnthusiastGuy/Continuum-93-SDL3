// Tiny JSON reader/writer for the service files (themes, layout.json). Enough of System.Text.Json:
// objects, arrays, strings (with escapes), numbers, true/false/null. No library (D-1: SDL + single headers).
#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace c93::svc::json {

// Not std::pair<std::string, Value>: a pair of an incomplete type is rejected by libstdc++ 12 (Debian bookworm).
struct Member;
struct Value {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Value> arr;
    std::vector<Member> obj;
    const Value* get(std::string_view key) const;   // case-sensitive, as System.Text.Json's default
};
struct Member { std::string first; Value second; };
inline const Value* Value::get(std::string_view key) const {
    for (auto& [k, v] : obj) if (k == key) return &v;
    return nullptr;
}

bool parse(std::string_view text, Value& out);   // false on malformed input
std::string quote(std::string_view s);          // a JSON string literal, escaped like JavaScriptEncoder.Default

} // namespace c93::svc::json
