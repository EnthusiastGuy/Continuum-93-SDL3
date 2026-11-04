// Continuum 93 C compiler: C source -> assembly text, identical to the reference (Emulator/Compilers/C93C).
// See cc_internal.h for the port's structure and the reference quirks it reproduces.
#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace c93 {

struct CcError {   // CCompileError: "<file>:<line>:<col>: error: <message>"
    std::string file;
    int line = 0, col = 0;
    std::string message;
};

struct CcResult {
    std::string asmText;              // empty when there are errors
    std::vector<CcError> errors;
    std::string log;                  // CCompiler.Log, '\n' line ends (the reference used the host's newline)
    uint32_t origin = 0x080000;       // #pragma c93 origin / stack, as after the reference's Compile
    uint32_t stackTop = 0xE00000;
    std::string fatal;                // non-empty: the reference threw a .NET exception here (not a compile error)
};

// #include "name" seen in includingFile (the top file's name, or the include name of the file it came from):
// the file's text, or nullopt when it does not exist. Built-in headers (stdlib.h, continuum.h...) never get here.
using CcIncludeResolver = std::function<std::optional<std::string>(const std::string& name, const std::string& includingFile)>;

CcResult compileC(std::string_view source, const std::string& fileName, const CcIncludeResolver& resolve);

} // namespace c93
