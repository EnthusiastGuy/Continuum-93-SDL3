// Tiny JSON reader/writer for the service files (themes, layout.json). Enough of System.Text.Json:
// objects, arrays, strings (with escapes), numbers, true/false/null. No library (D-1: SDL + single headers).
#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace c93::svc::json {

struct Value {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;
    const Value* get(std::string_view key) const {   // case-sensitive, as System.Text.Json's default
        for (auto& [k, v] : obj) if (k == key) return &v;
        return nullptr;
    }
};

bool parse(std::string_view text, Value& out);   // false on malformed input
std::string quote(std::string_view s);          // a JSON string literal, escaped like JavaScriptEncoder.Default

} // namespace c93::svc::json
