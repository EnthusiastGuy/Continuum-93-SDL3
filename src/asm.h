// Continuum 93 assembler: byte-identical port of the reference (Emulator/Interpreter). See asm.cpp.
#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace c93 {

struct AsmBlock {
    uint32_t start;
    std::vector<uint8_t> data;
    std::vector<std::pair<uint32_t, uint32_t>> lines;   // (offset in data, source line) of each line that emitted bytes
};
struct AsmMsg { uint32_t line; std::string text; };   // line in the include-expanded source, from 1

struct AsmResult {
    std::vector<AsmBlock> blocks;   // one per #ORG, in source order (code before the first #ORG is dropped)
    uint32_t runAddress = 0;
    std::vector<AsmMsg> errors;     // what the reference counts as errors (one per line at most)
    std::vector<AsmMsg> warnings;   // what the reference only logs: unknown mnemonics/formats, undeclared labels...
    std::string fatal;              // non-empty: the reference threw here; blocks are empty, nothing to load
    std::unordered_map<std::string, uint32_t> labels;
    std::vector<std::string> source;   // the include-expanded lines (the reference's FullSource)
};

// Returns the text of the file at path, or nullopt when it does not exist (#include).
using AsmIncludeResolver = std::function<std::optional<std::string>(const std::string& path)>;

// path: the source's own file path (its directory is the base of relative #includes); may be empty.
AsmResult assemble(std::string_view source, std::string_view path, const AsmIncludeResolver& resolve);

} // namespace c93
