// Command-line tools (Utils/UtilsManager.cs, Utils/FontConvertor.cs, Tools/FontTools/BitFont.cs).
//
// As the reference: flags are case-insensitive (-x, /x, --x; png2font only -x and /x), the first
// tool flag ends processing, unknown arguments are warned about, and any argument at all means the
// machine never starts (the reference processed them in its first Update and then exited).
// The help text goes to stdout and the log: a Windows release build has no console, so the log
// (or a redirect, `continuum -help > help.txt`) is where it shows there.
#include "tools.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "third_party/stb_image.h"   // implementation in ints_fs.cpp

#include "log.h"

namespace c93 {
namespace {

namespace fs = std::filesystem;

const char* const kHelp[] = {
    "\n", "--- Continuum93 Emulator Command-Line Usage ---", "\n",
    "No arguments: Starts the emulator normally.", "\n",
    "-help: Shows this help page.",
    "-docs: Prepares the documentation suite.",
    "-png2font [path]: Converts the PNG file at [path] to a .font file.", "\n",
    "Emulator briefly shows up and then closes automatically when the operation is finished.", "\n",
};

void showHelp() {
    for (const char* line : kHelp) {
        std::printf("%s\n", line);   // Console.WriteLine("\n") wrote two line breaks; kept
        if (line[0] != '\n') LOGI("Continuum: help: %s", line);
    }
    std::fflush(stdout);
}

// FontConvertor.ConvertFont: a 16 x 6 grid of glyphs ' '..'~' -> a type-2 (variable width) font:
// [2, height, 95 widths, 95 x height row bytes]. A pixel is set when its alpha is not 0 (MonoGame
// zeroed the colour of alpha-0 pixels, so `!= Color.Transparent` was exactly that).
std::vector<uint8_t> convertFont(const stbi_uc* rgba, int width, int height) {
    const int cw = width / 16, ch = height / 6;
    auto set = [&](int x, int y) { return rgba[(size_t(y) * width + x) * 4 + 3] != 0; };
    std::vector<uint8_t> font{2, uint8_t(ch)}, rows;
    int cx = 0, cy = 0;
    for (int glyph = 0; glyph < 95; ++glyph) {
        int found = -1;   // width = rightmost column with a set pixel, + 1
        for (int x = cx + cw - 1; x >= cx && found == -1; --x)
            for (int y = cy; y < cy + ch; ++y)
                if (set(x, y)) { found = x - cx + 1; break; }
        font.push_back(uint8_t(found == -1 ? 3 : found));   // empty glyph: a space, 3 wide
        // Leftmost column in bit 7; columns past the 8th are dropped (the reference's byte cast).
        for (int y = cy; y < cy + ch; ++y) {
            uint8_t b = 0;
            for (int i = 0; i < cw && i < 8; ++i)
                if (set(cx + i, y)) b |= uint8_t(0x80 >> i);
            rows.push_back(b);
        }
        cx += cw;
        if (cx >= 16 * cw) { cx = 0; cy += ch; }
    }
    font.insert(font.end(), rows.begin(), rows.end());
    return font;
}

int png2font(const char* arg) {
    fs::path png(reinterpret_cast<const char8_t*>(arg));   // SDL_main hands us UTF-8 argv
    std::string ext = png.extension().string();
    for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    std::error_code ec;
    if (!fs::is_regular_file(png, ec) || ext != ".png") {
        LOGE("Continuum: file not found or not a PNG: %s; nothing converted", arg);
        return 1;
    }
    std::ifstream in(png, std::ios::binary);
    std::vector<uint8_t> file{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    int w = 0, h = 0, n = 0;
    stbi_uc* rgba = file.size() < (1u << 31) ? stbi_load_from_memory(file.data(), int(file.size()), &w, &h, &n, 4) : nullptr;
    if (!rgba) {   // the reference threw here and crashed
        LOGE("Continuum: cannot decode %s (%s); nothing converted", arg, stbi_failure_reason());
        return 1;
    }
    std::vector<uint8_t> font = convertFont(rgba, w, h);
    stbi_image_free(rgba);
    fs::path out = (fs::absolute(png, ec).parent_path() / png.stem()).make_preferred();
    out += ".font";
    std::ofstream o(out, std::ios::binary);
    o.write(reinterpret_cast<const char*>(font.data()), std::streamsize(font.size()));
    if (!o.flush()) {
        LOGE("Continuum: cannot write %s; font not saved", reinterpret_cast<const char*>(out.u8string().c_str()));
        return 1;
    }
    LOGI("Continuum: %dx%d PNG converted to %s (%zu bytes)", w, h,
         reinterpret_cast<const char*>(out.u8string().c_str()), font.size());
    return 0;
}

} // namespace

int runTool(int argc, char** argv) {
    if (argc < 2) return -1;
    LOGI("Continuum: processing %d command-line argument(s); the machine will not start", argc - 1);
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        for (char& c : a) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (a == "-docs" || a == "/docs" || a == "--docs") {
            LOGW("Continuum: -docs is not ported (the HTML docs come from the C# reference's metadata); nothing built");
            return 0;
        }
        if (a == "-help" || a == "/help" || a == "--help") { showHelp(); return 0; }
        if (a == "-png2font" || a == "/png2font") {
            if (i + 1 < argc) return png2font(argv[i + 1]);
            LOGE("Continuum: -png2font needs a path to a PNG file; nothing converted");
            return 1;
        }
        LOGW("Continuum: unrecognized argument '%s'; ignored", argv[i]);
    }
    return 0;
}

} // namespace c93
