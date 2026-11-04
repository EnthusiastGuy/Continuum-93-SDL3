// Continuum 93 BASIC compiler: text-identical port of the reference (Compilers/C93Basic). See basic.cpp.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace c93 {

struct BasicResult {
    std::string asmText;      // the reference's GeneratedAssembly ("" when it threw or the lexer failed)
    int errors = 0;           // BasicCompiler.Errors
    std::string log;          // BasicCompiler.Log, line for line
    uint32_t runAddress = 0;  // GetRunAddress(): always the code start
};

// BasicCompiler.Compile(source, codeStartAddress) on a fresh compiler.
BasicResult compileBasic(std::string_view source, uint32_t codeStart = 0x080000);

// float.ToString() of .NET 8 (en-US); also used by the service-mode disassembler.
std::string netFloat(float f);

} // namespace c93
