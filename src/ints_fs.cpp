// File system (INT 0x04) and strings (INT 0x05).
// (Interrupts/FileSystem/FileSystemGeneral.cs, FileSystemPNG.cs, FileSystemFont.cs,
//  GraphicsAccelerators/PNGContainer.cs, GFXImageProcessing.cs, GFXFontExtractor.cs,
//  States/ImageLoadState.cs, Utils/Kerning.cs, Interrupts/InterruptsStrings.cs.)
//
// Reference quirks kept:
// - PNGContainer is its own indexed-PNG reader. It copies the *filtered* scanline bytes as pixels
//   (filter type ignored), takes the row count from the inflated size, accepts bit depths 1/2/4 but
//   reads them as 8-bit, and a 256-colour PLTE wraps its byte colour count to 0 (error 9).
// - Merge8BitPNG reads <count> palette BYTES (the custom-transparency variant reads count*3) and
//   merges before checking the error, so any PNG error there halts the machine.
// - Transparency correction swaps palette entries and pixels but not the tRNS table.
// - Listings are in NTFS order (case-folded ordinal), names written as ASCII ('?' for the rest).
// - Numbers are formatted like .NET 8 under en-US: the reference used the current culture and its
//   own tests expect en-US ("12.45%"); invariant would differ in P, C and the infinity symbol.
//   Custom formats of floats round twice (7 significant digits, then the format), as .NET does.
// Whatever made the reference throw (missing file, bad PNG data, bad format string) halts the
// machine: the exception escaped the CPU thread's run loop and stopped it.
// Stricter than the reference: guest paths cannot leave gFsRoot (the reference passed "..",
// "C:\..." and "\..." straight to Path.Combine, opening the whole host disk to guest programs).
#include "ints.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#if defined(_MSC_VER)
#pragma warning(push, 0)   // third-party header: its warnings are not ours
#pragma warning(disable : 4505)
#endif
#include "third_party/stb_image.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "log.h"

namespace c93 {
namespace {

namespace fs = std::filesystem;

// --- Guest memory and paths -----------------------------------------------------------------
// MemoryController.GetStringAt: bytes up to the next 0; no 0 before the end of RAM -> "".
std::string guestStr(const Machine& m, uint32_t a) {
    a &= kRamMask;
    const uint8_t* p = m.ram + a;
    const void* end = std::memchr(p, 0, kRamSize - a);
    return end ? std::string(reinterpret_cast<const char*>(p), static_cast<const uint8_t*>(end) - p) : std::string();
}
void putBytes(Machine& m, uint32_t a, const uint8_t* p, size_t n) {   // Computer.LoadMemAt; wraps at 24 bits
    for (size_t i = 0; i < n; ++i) m.ram[(a + i) & kRamMask] = p[i];
}
void putBytes(Machine& m, uint32_t a, const std::vector<uint8_t>& v) { putBytes(m, a, v.data(), v.size()); }
void putStr(Machine& m, uint32_t a, const std::string& s) {           // SetStringAt: bytes + 0
    putBytes(m, a, reinterpret_cast<const uint8_t*>(s.data()), s.size());
    m.ram[(a + s.size()) & kRamMask] = 0;
}
void halt(Machine& m, const char* what, const std::string& path) {   // what: "FS: ..." or "Machine: ..."
    LOGE("%s '%s'; machine halted", what, path.c_str());
    m.running.store(false, std::memory_order_relaxed);
}

// Guest path (UTF-8, '\' or '/') -> host path under gFsRoot; false if it would leave the root.
bool hostPath(const std::string& guest, fs::path& out) {
    std::string s = guest;
    std::replace(s.begin(), s.end(), '\\', '/');   // DataConverter.GetCrossPlatformPath
    fs::path rel = fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
    if (rel.has_root_name() || rel.has_root_directory()) return false;
    rel = rel.lexically_normal();
    if (!rel.empty() && *rel.begin() == "..") return false;
    out = fs::path(gFsRoot) / rel;
    return true;
}
bool endsWithSep(const std::string& s) { return !s.empty() && (s.back() == '/' || s.back() == '\\'); }
// File.Exists: anything but a directory; a trailing separator never names a file.
bool isFile(const std::string& guest, fs::path& p) {
    std::error_code ec;
    if (!hostPath(guest, p) || endsWithSep(guest)) return false;
    auto st = fs::status(p, ec);
    return fs::exists(st) && !fs::is_directory(st);
}
bool readAll(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return !f.bad();
}

// --- UTF-8 <-> code points (strings arrive as UTF-8; .NET works on UTF-16) ------------------
std::u32string decode(const std::string& s) {
    std::u32string out;
    for (size_t i = 0; i < s.size();) {
        uint8_t c = uint8_t(s[i]);
        int n = c < 0x80 ? 0 : c >= 0xC2 && c < 0xE0 ? 1 : c >= 0xE0 && c < 0xF0 ? 2 : c >= 0xF0 && c < 0xF5 ? 3 : -1;
        char32_t cp = n == 0 ? c : n == 1 ? c & 0x1F : n == 2 ? c & 0x0F : c & 0x07;
        bool ok = n >= 0;
        for (int k = 1; ok && k <= n; ++k) {
            if (i + k >= s.size() || (uint8_t(s[i + k]) & 0xC0) != 0x80) ok = false;
            else cp = cp << 6 | (uint8_t(s[i + k]) & 0x3F);
        }
        if (ok && ((n == 2 && (cp < 0x800 || (cp >= 0xD800 && cp < 0xE000))) || (n == 3 && (cp < 0x10000 || cp > 0x10FFFF)))) ok = false;
        if (!ok) { out += U'\uFFFD'; ++i; continue; }   // invalid byte -> replacement character
        out += cp;
        i += n + 1;
    }
    return out;
}
void put(std::string& o, char32_t c) {
    if (c < 0x80) o += char(c);
    else if (c < 0x800) { o += char(0xC0 | c >> 6); o += char(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { o += char(0xE0 | c >> 12); o += char(0x80 | (c >> 6 & 0x3F)); o += char(0x80 | (c & 0x3F)); }
    else { o += char(0xF0 | c >> 18); o += char(0x80 | (c >> 12 & 0x3F)); o += char(0x80 | (c >> 6 & 0x3F)); o += char(0x80 | (c & 0x3F)); }
}

// --- INT 0x04: files and directories ---------------------------------------------------------
void fileExists(Machine& m, uint8_t r) {
    fs::path p;
    m.set<8>(r, isFile(guestStr(m, m.get<24>(nextReg(r, 1))), p) ? 0xFF : 0x00);
}
void dirExists(Machine& m, uint8_t r) {
    fs::path p;
    std::error_code ec;
    m.set<8>(r, hostPath(guestStr(m, m.get<24>(nextReg(r, 1))), p) && fs::is_directory(p, ec) ? 0xFF : 0x00);
}

// Directory.GetFiles/GetDirectories order on NTFS: ordinal over upper-cased characters.
bool ntfsLess(const std::u32string& a, const std::u32string& b) {
    auto up = [](char32_t c) { return c >= 'a' && c <= 'z' ? c - 32 : c; };
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
        [&](char32_t x, char32_t y) { return up(x) < up(y); });
}
// Mode 0 files, 1 directories, 2 directories then files (with counts and the files' offset).
template <int Mode> void listDir(Machine& m, uint8_t r) {
    uint8_t adr = nextReg(r, 1);
    std::string guest = guestStr(m, m.get<24>(adr));
    uint32_t target = m.get<24>(nextReg(r, 4));
    fs::path p;
    std::error_code ec;
    if (!hostPath(guest, p) || !fs::is_directory(p, ec)) { m.set<24>(adr, 0xFFFFFF); return; }
    std::vector<std::u32string> files, dirs;
    for (fs::directory_iterator it(p, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        auto name = it->path().filename().u8string();
        (it->is_directory(e2) ? dirs : files).push_back(decode(std::string(name.begin(), name.end())));
    }
    if (ec) return halt(m, "FS: cannot list", guest);
    std::sort(files.begin(), files.end(), ntfsLess);
    std::sort(dirs.begin(), dirs.end(), ntfsLess);
    if (Mode == 2) {
        uint32_t filesAt = 1;   // name lengths in UTF-16 units, as string.Length
        for (auto& d : dirs) for (char32_t c : d) filesAt += c >= 0x10000 ? 2 : 1;
        filesAt += uint32_t(dirs.size());
        uint8_t dReg = nextReg(adr, 3), fReg = nextReg(dReg, 3);
        m.set<24>(dReg, uint32_t(dirs.size()));
        m.set<24>(fReg, uint32_t(files.size()));
        m.set<24>(nextReg(fReg, 3), filesAt);
    }
    std::string out;
    uint32_t total = 0;
    auto group = [&](const std::vector<std::u32string>& g) {
        for (auto& n : g) { for (char32_t c : n) out += c < 0x80 ? char(c) : '?'; out += '\0'; }
        out += '\0';
        total += uint32_t(g.size());
    };
    if (Mode != 0) group(dirs);
    if (Mode != 1) group(files);
    putBytes(m, target, reinterpret_cast<const uint8_t*>(out.data()), out.size());
    m.set<24>(adr, total);
}

void saveFile(Machine& m, uint8_t r) {
    std::string guest = guestStr(m, m.get<24>(nextReg(r, 1)));
    uint32_t src = m.get<24>(nextReg(r, 4)), len = m.get<24>(nextReg(r, 7));
    fs::path p;
    if (!hostPath(guest, p)) { LOGW("FS: save path '%s' leaves the file system; not saved", guest.c_str()); return; }
    std::vector<uint8_t> data(len);
    for (uint32_t i = 0; i < len; ++i) data[i] = m.ram[(src + i) & kRamMask];
    std::ofstream f(p, std::ios::binary | std::ios::trunc);   // FileMode.Create
    if (!f.write(reinterpret_cast<const char*>(data.data()), data.size())) halt(m, "FS: cannot save", guest);
}
void loadFile(Machine& m, uint8_t r) {
    std::string guest = guestStr(m, m.get<24>(nextReg(r, 1)));
    fs::path p;
    std::vector<uint8_t> data;
    if (!isFile(guest, p) || !readAll(p, data)) return halt(m, "FS: cannot load", guest);
    putBytes(m, m.get<24>(nextReg(r, 4)), data);
}
void fileSize(Machine& m, uint8_t r) {
    uint8_t adr = nextReg(r, 1);
    std::string guest = guestStr(m, m.get<24>(adr));
    fs::path p;
    std::error_code ec;
    if (!isFile(guest, p)) return halt(m, "FS: no such file", guest);
    auto size = fs::file_size(p, ec);
    if (ec) return halt(m, "FS: no such file", guest);
    m.set<32>(adr, uint32_t(size));
}

// --- PNGContainer.cs -----------------------------------------------------------------------
struct Png {
    int32_t w = 0, h = 0;
    uint8_t depth = 0, ctype = 0, comp = 0, filter = 0, interlace = 0, err = 0, colors = 0;
    std::vector<uint8_t> plte, trns, pix;
    bool hasTrns = false;
    bool threw = false;   // the reference threw: the machine halts
};

bool matchTrns(Png& p) {   // MatchTransparencyValues; false where Array.Copy threw
    std::vector<uint8_t> t(p.colors, 255);
    if (p.hasTrns) {
        if (p.colors == p.trns.size()) return true;
        if (p.trns.size() > t.size()) return false;
        std::copy(p.trns.begin(), p.trns.end(), t.begin());
    }
    p.trns = std::move(t);
    p.hasTrns = true;
    return true;
}

Png loadPng(const std::string& guest) {
    Png png;
    fs::path path;
    std::vector<uint8_t> d;
    if (!isFile(guest, path)) { png.err = 1; return png; }
    if (!readAll(path, d)) { png.threw = true; return png; }
    // FileStream reads: short at the end of the file (the rest stays 0), position moves by what was read.
    size_t pos = 0;
    auto rd = [&](uint8_t* out, size_t n) {
        size_t k = pos < d.size() ? std::min(n, d.size() - pos) : 0;
        if (k) std::memcpy(out, d.data() + pos, k);
        pos += k;
    };
    static const uint8_t sig[8] = {137, 'P', 'N', 'G', 13, 10, 26, 10};
    uint8_t s[8]{};
    rd(s, 8);
    if (std::memcmp(s, sig, 8)) { png.err = 2; return png; }
    std::vector<uint8_t> idat;
    while (pos < d.size()) {
        uint8_t lb[4]{}, type[4]{};
        rd(lb, 4);
        int32_t len = int32_t(uint32_t(lb[0]) << 24 | uint32_t(lb[1]) << 16 | uint32_t(lb[2]) << 8 | lb[3]);
        rd(type, 4);
        // shortcut: chunks over 64 MB are refused like negative lengths (which threw); no real PNG has one.
        if (len < 0 || len > (64 << 20)) { png.threw = true; return png; }
        std::vector<uint8_t> c(size_t(len), 0);
        rd(c.data(), c.size());
        if (!std::memcmp(type, "IHDR", 4)) {
            if (len < 13) { png.threw = true; return png; }
            png.w = int32_t(uint32_t(c[0]) << 24 | uint32_t(c[1]) << 16 | uint32_t(c[2]) << 8 | c[3]);
            png.h = int32_t(uint32_t(c[4]) << 24 | uint32_t(c[5]) << 16 | uint32_t(c[6]) << 8 | c[7]);
            png.depth = c[8]; png.ctype = c[9]; png.comp = c[10]; png.filter = c[11]; png.interlace = c[12];
        } else if (!std::memcmp(type, "PLTE", 4)) {
            png.plte = std::move(c);
            png.colors = uint8_t(png.plte.size() / 3);   // 256 colours wrap to 0
        } else if (!std::memcmp(type, "tRNS", 4)) {
            png.trns = std::move(c);
            png.hasTrns = true;
        } else if (!std::memcmp(type, "IDAT", 4)) {
            idat.insert(idat.end(), c.begin(), c.end());
        }
        pos += 4;   // CRC
    }
    png.err = png.w > 65535 || png.h > 65535 ? 3
            : png.depth != 8 && png.depth != 4 && png.depth != 2 && png.depth != 1 ? 4
            : png.ctype != 3 ? 5 : png.comp ? 6 : png.filter ? 7 : png.interlace ? 8 : png.colors == 0 ? 9 : 0;
    if (!matchTrns(png)) { png.threw = true; return png; }   // runs even after an error, as the reference
    if (png.err) return png;
    // IDAT: skip the 2-byte zlib header, inflate the rest (DeflateStream).
    std::vector<uint8_t> raw;
    if (idat.size() > 2) {
        int n = 0;
        char* z = stbi_zlib_decode_noheader_malloc(reinterpret_cast<const char*>(idat.data()) + 2, int(idat.size() - 2), &n);
        if (!z) { png.threw = true; return png; }
        raw.assign(z, z + n);
        STBI_FREE(z);
    }
    // SetPixelData: rows = inflated size / (width + 1); each row drops its filter byte, nothing more.
    int64_t line = int64_t(png.w) + 1;
    if (line == 0) { png.threw = true; return png; }
    int64_t rows = int64_t(raw.size()) / line, size = int64_t(png.w) * rows;
    if (size < 0 || size > (64 << 20)) { png.threw = true; return png; }
    png.pix.assign(size_t(size), 0);
    for (int64_t y = 0; y < rows; ++y)
        std::copy_n(raw.begin() + (y * line + 1), png.w, png.pix.begin() + y * png.w);
    return png;
}

bool mergePalette(Png& p, const std::vector<uint8_t>& ext) {   // MergePalette; false where it threw
    if (p.plte.size() % 3) return false;
    std::vector<uint8_t> merged = ext;
    int map[256];
    std::fill(std::begin(map), std::end(map), -1);
    for (size_t i = 0; i < p.plte.size(); i += 3) {
        uint8_t r = p.plte[i], g = p.plte[i + 1], b = p.plte[i + 2];
        bool found = false;
        for (size_t j = 3; j < ext.size() && !found; j += 3) {   // entry 0 (transparent) is never matched
            if (ext[j] != r) continue;
            if (j + 1 >= ext.size()) return false;
            if (ext[j + 1] != g) continue;
            if (j + 2 >= ext.size()) return false;
            if (ext[j + 2] != b) continue;
            map[uint8_t(i / 3)] = uint8_t(j / 3);
            found = true;
        }
        if (!found) {
            map[uint8_t(i / 3)] = uint8_t(merged.size() / 3);
            merged.insert(merged.end(), {r, g, b});
        }
    }
    for (uint8_t& px : p.pix) if (map[px] >= 0) px = uint8_t(map[px]);
    p.plte = std::move(merged);
    p.colors = uint8_t(p.plte.size() / 3);
    return matchTrns(p);
}

bool applyTransparency(Png& p, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {   // false where it threw
    auto& pal = p.plte;
    for (size_t i = 0; i < p.trns.size(); ++i) {
        if (i * 3 >= pal.size()) return false;
        if (pal[i * 3] != r) continue;
        if (i * 3 + 1 >= pal.size()) return false;
        if (pal[i * 3 + 1] != g) continue;
        if (i * 3 + 2 >= pal.size()) return false;
        if (pal[i * 3 + 2] != b || p.trns[i] != a) continue;
        uint8_t t[3] = {pal[0], pal[1], pal[2]};
        pal[0] = r; pal[1] = g; pal[2] = b;
        pal[i * 3] = t[0]; pal[i * 3 + 1] = t[1]; pal[i * 3 + 2] = t[2];
        for (uint8_t& px : p.pix) px = px == 0 ? uint8_t(i) : px == i ? 0 : px;
        break;
    }
    return true;
}

// Load8BitPNG / ...CustomTransparency / Merge8BitPNG / ...CustomTransparency.
template <bool Merge, bool Custom> void pngInt(Machine& m, uint8_t r) {
    constexpr int at = Merge ? 1 : 0;   // the merge variants have the palette size register first
    uint32_t pathA = m.get<24>(nextReg(r, 1)), palA = m.get<24>(nextReg(r, 4)), imgA = m.get<24>(nextReg(r, 7 + at));
    uint8_t palSize = uint8_t(m.get<8>(nextReg(r, 7)));
    uint8_t c[4];
    for (int k = 0; k < 4; ++k) c[k] = uint8_t(m.get<8>(nextReg(r, 10 + at + k)));
    std::string guest = guestStr(m, pathA);
    std::vector<uint8_t> ext;
    if (Merge) for (uint32_t i = 0; i < (Custom ? palSize * 3u : palSize); ++i) ext.push_back(m.ram[(palA + i) & kRamMask]);
    Png png = loadPng(guest);
    if (png.threw || (Merge && !Custom && (png.err || !mergePalette(png, ext)))) return halt(m, "FS: bad PNG", guest);
    m.set<8>(r, png.err);
    if (png.err) return;
    if ((Merge && Custom && !mergePalette(png, ext)) || (Custom && !applyTransparency(png, c[0], c[1], c[2], c[3])))
        return halt(m, "FS: bad PNG", guest);
    putBytes(m, palA, png.plte);
    putBytes(m, imgA, png.pix);
    m.set<16>(nextReg(r, 1), uint32_t(png.w));
    m.set<16>(nextReg(r, 3), uint32_t(png.h));
    m.set<8>(nextReg(r, 5), png.colors);
}

// --- Fonts (FileSystemFont.cs, GFXFontExtractor.cs, Kerning.cs) ------------------------------
bool isWs(char32_t c) {   // char.IsWhiteSpace
    return (c >= 9 && c <= 13) || c == ' ' || c == 0x85 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
           c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}
std::u32string trim(const std::u32string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isWs(s[a])) ++a;
    while (b > a && isWs(s[b - 1])) --b;
    return s.substr(a, b - a);
}
// Kerning.cs: "ab:-1" lines, '#' comments; pairs of printable ASCII (32..127), sbyte value, not 0.
std::vector<uint8_t> loadKerning(const std::string& guest) {
    std::vector<uint8_t> out, d;
    fs::path p;
    if (!isFile(guest, p) || !readAll(p, d)) return out;
    std::string text(d.begin(), d.end());
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);   // StreamReader drops the BOM
    std::u32string all = decode(text);
    for (size_t i = 0; i < all.size();) {   // ReadLine; the extra empty line of \r\n is skipped anyway
        size_t e = std::min(all.find_first_of(U"\r\n", i), all.size());
        std::u32string line = trim(all.substr(i, e - i));
        i = e + 1;
        if (line.empty() || line[0] == '#') continue;
        size_t colon = line.find(':');
        if (colon == std::u32string::npos) continue;
        std::u32string pair = trim(line.substr(0, colon)), val = trim(line.substr(colon + 1));
        if (pair.size() != 2 || pair[0] < 32 || pair[0] > 127 || pair[1] < 32 || pair[1] > 127) continue;
        // sbyte.TryParse (NumberStyles.Integer): [ws][sign]digits[ws]
        size_t k = 0;
        while (k < val.size() && ((val[k] >= 9 && val[k] <= 13) || val[k] == ' ')) ++k;
        bool neg = k < val.size() && val[k] == '-';
        if (k < val.size() && (val[k] == '-' || val[k] == '+')) ++k;
        size_t first = k;
        int64_t v = 0;
        while (k < val.size() && val[k] >= '0' && val[k] <= '9' && v < 1000) v = v * 10 + (val[k++] - '0');
        while (k < val.size() && val[k] >= '0' && val[k] <= '9') ++k;
        size_t digitsEnd = k;
        while (k < val.size() && ((val[k] >= 9 && val[k] <= 13) || val[k] == ' ')) ++k;
        if (k != val.size() || digitsEnd == first) continue;
        if (neg) v = -v;
        if (v < -128 || v > 127 || v == 0) continue;
        out.insert(out.end(), {uint8_t(pair[0]), uint8_t(pair[1]), uint8_t(int8_t(v))});
    }
    return out;
}
std::string changeExtension(const std::string& s) {   // Path.ChangeExtension(path, ".txt")
    if (s.empty()) return s;
    size_t cut = s.size();
    for (size_t i = s.size(); i-- > 0;) {
        if (s[i] == '.') { cut = i; break; }
        if (s[i] == '/' || s[i] == '\\') break;
    }
    return s.substr(0, cut) + ".txt";
}

void loadPngFont(Machine& m, uint8_t r) {
    std::string guest = guestStr(m, m.get<24>(nextReg(r, 1)));
    uint32_t dest = m.get<24>(nextReg(r, 4));
    Png png = loadPng(guest);
    if (png.threw) return halt(m, "FS: bad PNG", guest);
    std::vector<uint8_t> kern = loadKerning(changeExtension(guest));
    m.set<8>(r, png.err);
    if (png.err || png.w % 16 + png.h % 6 > 0) return;   // only a 16 x 6 glyph grid is accepted
    int w = png.w, cw = w / 16, ch = png.h / 6;
    const auto& px = png.pix;
    // The extractor reads every pixel of the grid; a short image, or cells under 2 pixels, threw.
    if (w <= 0 || png.h <= 0 || int64_t(w) * png.h > int64_t(px.size()) || uint8_t(cw) < 2 || uint8_t(ch) < 2)
        return halt(m, "FS: bad font image", guest);
    bool clear[256]{};   // every colour of the space cell is background
    for (int y = 0; y < ch; ++y) for (int x = 0; x < cw; ++x) clear[px[size_t(y) * w + x]] = true;
    auto pixel = [&](int g, int x, int y) { return px[size_t((g / 16) * ch + y) * w + (g % 16) * cw + x]; };
    uint8_t gw[96]{};
    for (int g = 1; g < 96; ++g)       // width: rightmost column holding a glyph pixel
        for (int x = cw - 1; x >= 0 && gw[g] == 0; --x)
            for (int y = 0; y < ch; ++y) if (!clear[pixel(g, x, y)]) { gw[g] = uint8_t(x + 1); break; }
    uint8_t alpha = 0, widest = 0;
    for (int g = 33; g <= 58; ++g) alpha = std::max(alpha, gw[g]);
    for (int g = 65; g <= 90; ++g) alpha = std::max(alpha, gw[g]);
    gw[0] = alpha / 2;                 // space: half the widest letter
    for (uint8_t v : gw) widest = std::max(widest, v);
    if (widest == 0) return halt(m, "FS: font has no glyphs", guest);
    std::vector<uint8_t> out = {1, uint8_t(cw), uint8_t(ch), widest, uint8_t(ch), uint8_t(kern.size() / 3)};
    out.insert(out.end(), gw, gw + 96);
    out.insert(out.end(), kern.begin(), kern.end());
    int rowBytes = (cw + 7) / 8;
    for (int g = 1; g < 96; ++g) {     // 1 bit per pixel, MSB first, rows padded to bytes; no space glyph
        size_t base = out.size();
        out.resize(base + size_t(rowBytes) * ch, 0);
        for (int y = 0; y < ch; ++y)
            for (int x = 0; x < cw; ++x)
                if (!clear[pixel(g, x, y)]) out[base + size_t(y) * rowBytes + x / 8] |= uint8_t(0x80 >> (x % 8));
    }
    putBytes(m, dest, out);
    m.set<24>(r, uint32_t(out.size()));
}

// --- Whole images (GFXImageProcessing.cs via ImageLoadState.cs) -------------------------------
// The reference paused the CPU and finished on the game thread; done in place here, same result.
// MonoGame's Texture2D.FromFile (StbImageSharp) gave RGBA with the colour of fully transparent
// pixels zeroed; colours are indexed by first appearance (the byte index wraps past 256).
// shortcut: PNG only (MonoGame also read JPG/BMP/GIF/TGA); add the stb decoders if a program needs them.
template <int Mode> void imageInt(Machine& m, uint8_t r) {   // 0 image + palette, 1 image, 2 palette
    std::string guest = guestStr(m, m.get<24>(nextReg(r, 1)));
    uint32_t dest = m.get<24>(nextReg(r, 4));
    fs::path p;
    std::vector<uint8_t> file;
    int w = 0, h = 0, n = 0;
    stbi_uc* rgba = isFile(guest, p) && readAll(p, file) && file.size() < (1u << 31)
        ? stbi_load_from_memory(file.data(), int(file.size()), &w, &h, &n, 4) : nullptr;
    if (!rgba) return halt(m, "FS: cannot decode image", guest);
    std::unordered_map<uint32_t, uint8_t> index;
    std::vector<uint8_t> pal, img(size_t(w) * h);
    for (size_t i = 0; i < img.size(); ++i) {
        const stbi_uc* q = rgba + i * 4;
        uint32_t c = q[3] ? uint32_t(q[0]) | q[1] << 8 | q[2] << 16 | uint32_t(q[3]) << 24 : 0;
        auto [it, added] = index.try_emplace(c, uint8_t(index.size()));
        if (added) pal.insert(pal.end(), {uint8_t(c), uint8_t(c >> 8), uint8_t(c >> 16)});
        img[i] = it->second;
    }
    stbi_image_free(rgba);
    if (Mode == 0) { putBytes(m, dest, img); putBytes(m, dest - uint32_t(pal.size()), pal); }
    if (Mode == 1) putBytes(m, dest, img);
    // Reference bug: LoadPalette wrote the (null) pixel buffer, crashing the game thread, and never
    // resumed the CPU. Port: the evident intent, the palette at the destination and the count.
    if (Mode == 2) putBytes(m, dest, pal);
    if (Mode != 1) m.set<8>(r, uint8_t(index.size()));
}

// --- INT 0x05: .NET numeric formatting (Number.Formatting.cs, en-US) -------------------------
// A number as .NET's NumberBuffer: value = 0.dig x 10^scale.
struct Num {
    std::string dig;
    int scale = 0;
    bool neg = false;
    bool fp = false;   // floating point: keeps -0; standard formats arrive correctly rounded
    char at(size_t i) const { return i < dig.size() ? dig[i] : 0; }
};
constexpr int kMaxPrecision = 1 << 24;   // past this the reference ran out of RAM to write into

void roundNumber(Num& n, int pos, bool exact) {   // RoundNumber: half up on the digit string
    int i = 0;
    while (i < pos && n.at(i)) ++i;
    if (i == pos && n.at(i) && !exact && n.at(i) >= '5') {
        while (i > 0 && n.dig[i - 1] == '9') --i;
        if (i > 0) n.dig[i - 1]++;
        else { n.scale++; n.dig[0] = '1'; i = 1; }
    } else {
        while (i > 0 && n.dig[i - 1] == '0') --i;
    }
    if (i == 0) { if (!n.fp) n.neg = false; n.scale = 0; }
    n.dig.resize(size_t(i));
}

// Exact decimal expansion of a finite, non-zero float (at most 112 significant digits).
void exactDigits(float v, Num& n) {
    uint32_t b = std::bit_cast<uint32_t>(v);
    uint32_t e = b >> 23 & 0xFF, mant = b & 0x7FFFFF;
    int exp2 = e ? int(e) - 150 : -149;
    if (e) mant |= 0x800000;
    std::vector<uint32_t> big{mant};   // base 1e9, little-endian: mant * 2^exp2 or mant * 5^-exp2
    auto mul = [&](uint32_t k) {
        uint64_t carry = 0;
        for (uint32_t& limb : big) { uint64_t t = uint64_t(limb) * k + carry; limb = uint32_t(t % 1000000000); carry = t / 1000000000; }
        if (carry) big.push_back(uint32_t(carry));
    };
    for (int i = 0; i < std::abs(exp2); ++i) mul(exp2 > 0 ? 2 : 5);
    std::string s = std::to_string(big.back());
    char buf[16];
    for (size_t i = big.size() - 1; i-- > 0;) { std::snprintf(buf, sizeof buf, "%09u", big[i]); s += buf; }
    n.scale = int(s.size()) + (exp2 < 0 ? exp2 : 0);
    s.erase(s.find_last_not_of('0') + 1);
    n.dig = s;
}

// Dragon4/Grisu digits of a float: shortest round-trip (prec -1), prec significant digits, or
// prec digits after the point; exact rounding, ties to the even digit.
Num floatNum(float v, int prec, bool significant) {
    Num n;
    n.fp = true;
    n.neg = std::signbit(v);
    if (v == 0) return n;
    if (prec == -1) {
        char buf[64];
        auto res = std::to_chars(buf, buf + sizeof buf, std::fabs(v), std::chars_format::scientific);
        std::string s(buf, res.ptr);
        size_t e = s.find('e');
        for (size_t i = 0; i < e; ++i) if (s[i] != '.') n.dig += s[i];
        n.scale = std::atoi(s.c_str() + e + 1) + 1;
        n.dig.erase(n.dig.find_last_not_of('0') + 1);
        return n;
    }
    exactDigits(std::fabs(v), n);
    int64_t keep = significant ? prec : int64_t(n.scale) + prec;
    if (keep >= int64_t(n.dig.size())) return n;
    if (keep <= 0) {   // first digit past the cutoff: only a value above half of the cutoff unit survives
        bool up = keep == 0 && (n.dig[0] > '5' || (n.dig[0] == '5' && n.dig.size() > 1));
        n.dig = up ? "1" : "";
        n.scale = up ? n.scale + 1 : 0;
        return n;
    }
    size_t k = size_t(keep);
    char next = n.dig[k];
    bool up = next > '5' || (next == '5' && (n.dig.size() > k + 1 || (n.dig[k - 1] - '0') % 2));
    n.dig.resize(k);
    if (up) {
        while (k > 0 && n.dig[k - 1] == '9') --k;
        if (k == 0) { n.dig = "1"; n.scale++; }
        else { n.dig.resize(k); n.dig[k - 1]++; }
    }
    n.dig.erase(n.dig.find_last_not_of('0') + 1);
    return n;
}

void appendExponent(std::string& o, int e, char c, int minDigits, bool plus) {   // FormatExponent
    o += c;
    if (e < 0) { o += '-'; e = -e; } else if (plus) o += '+';
    std::string d = std::to_string(e);
    if (int(d.size()) < minDigits) o.append(size_t(minDigits) - d.size(), '0');
    o += d;
}
void appendFixed(std::string& o, const Num& n, int decimals, bool group) {   // FormatFixed, groups of 3
    size_t k = 0;
    int digPos = n.scale;
    auto next = [&] { return n.at(k) ? n.dig[k++] : '0'; };
    if (digPos > 0) {
        for (int i = 0; i < digPos; ++i) {
            if (group && i > 0 && (digPos - i) % 3 == 0) o += ',';
            o += next();
        }
    } else {
        o += '0';
    }
    if (decimals > 0) {
        o += '.';
        if (digPos < 0) { int z = std::min(-digPos, decimals); o.append(size_t(z), '0'); decimals -= z; }
        while (decimals-- > 0) o += next();
    }
}

// NumberToString: the standard format letters. False: FormatException.
bool numberToString(std::string& o, Num& n, char fmt, int d) {
    bool exact = n.fp;
    switch (fmt) {
    case 'C': case 'c':   // en-US: $n, ($n)
        if (d < 0) d = 2;
        roundNumber(n, n.scale + d, exact);
        o += n.neg ? "($" : "$";
        appendFixed(o, n, d, true);
        if (n.neg) o += ')';
        return true;
    case 'F': case 'f': case 'N': case 'n':
        if (d < 0) d = 2;
        roundNumber(n, n.scale + d, exact);
        if (n.neg) o += '-';
        appendFixed(o, n, d, fmt == 'N' || fmt == 'n');
        return true;
    case 'E': case 'e': {
        if (d < 0) d = 6;
        d++;
        roundNumber(n, d, exact);
        if (n.neg) o += '-';
        size_t k = 0;
        o += n.at(k) ? n.dig[k++] : '0';
        if (d != 1) o += '.';
        while (--d > 0) o += n.at(k) ? n.dig[k++] : '0';
        appendExponent(o, n.at(0) ? n.scale - 1 : 0, fmt, 3, true);
        return true;
    }
    case 'R': case 'r': case 'G': case 'g': {   // R is G for every type that gets here
        if (d < 1) d = int(n.dig.size());
        roundNumber(n, d, exact);
        if (n.neg) o += '-';
        int digPos = n.scale;
        bool sci = digPos > d || digPos < -3;
        if (sci) digPos = 1;
        size_t k = 0;
        if (digPos > 0) { do { o += n.at(k) ? n.dig[k++] : '0'; } while (--digPos > 0); }
        else o += '0';
        if (n.at(k) || digPos < 0) {
            o += '.';
            for (; digPos < 0; ++digPos) o += '0';
            while (n.at(k)) o += n.dig[k++];
        }
        if (sci) appendExponent(o, n.scale - 1, fmt == 'G' || fmt == 'R' ? 'E' : 'e', 2, true);
        return true;
    }
    case 'P': case 'p':   // en-US: n%, -n%
        if (d < 0) d = 2;
        n.scale += 2;
        roundNumber(n, n.scale + d, exact);
        if (n.neg) o += '-';
        appendFixed(o, n, d, true);
        o += '%';
        return true;
    }
    return false;
}

int findSection(const std::u32string& f, int section) {   // FindSection: start of section 1 or 2
    if (section == 0) return 0;
    for (size_t src = 0; src < f.size();) {
        char32_t ch = f[src++];
        if (ch == '\'' || ch == '"') { while (src < f.size() && f[src++] != ch) {} }
        else if (ch == '\\') { if (src < f.size()) src++; }
        else if (ch == ';' && --section == 0) return src < f.size() && f[src] != ';' ? int(src) : 0;
    }
    return 0;
}

// NumberToStringFormat: custom formats ("0.00", "#,##0", "'text' 0", "0;(0);zero", "0.0E+00"...).
void customFormat(std::string& o, Num& n, const std::u32string& f) {
    int section = findSection(f, !n.at(0) ? 2 : n.neg ? 1 : 0);
    int digitCount, decimalPos, firstDigit, lastDigit, thousandPos, thousandCount = 0, scaleAdjust;
    bool scientific, thousandSeps;
    size_t src;
    char32_t ch;
    while (true) {   // pass 1: measure the section
        digitCount = 0; decimalPos = -1; firstDigit = 0x7FFFFFFF; lastDigit = 0; scientific = false;
        thousandPos = -1; thousandSeps = false; scaleAdjust = 0; src = size_t(section);
        while (src < f.size() && (ch = f[src++]) != ';') {
            switch (ch) {
            case '#': digitCount++; break;
            case '0': if (firstDigit == 0x7FFFFFFF) firstDigit = digitCount; digitCount++; lastDigit = digitCount; break;
            case '.': if (decimalPos < 0) decimalPos = digitCount; break;
            case ',':
                if (digitCount > 0 && decimalPos < 0) {
                    if (thousandPos >= 0) {
                        if (thousandPos == digitCount) { thousandCount++; break; }
                        thousandSeps = true;
                    }
                    thousandPos = digitCount;
                    thousandCount = 1;
                }
                break;
            case '%': scaleAdjust += 2; break;
            case U'\u2030': scaleAdjust += 3; break;
            case '\'': case '"': while (src < f.size() && f[src++] != ch) {} break;
            case '\\': if (src < f.size()) src++; break;
            case 'E': case 'e':
                if ((src < f.size() && f[src] == '0') || (src + 1 < f.size() && (f[src] == '+' || f[src] == '-') && f[src + 1] == '0')) {
                    while (++src < f.size() && f[src] == '0') {}
                    scientific = true;
                }
                break;
            }
        }
        if (decimalPos < 0) decimalPos = digitCount;
        if (thousandPos >= 0) {
            if (thousandPos == decimalPos) scaleAdjust -= thousandCount * 3;   // trailing ',' divides by 1000
            else thousandSeps = true;
        }
        if (n.at(0)) {
            n.scale += scaleAdjust;
            roundNumber(n, scientific ? digitCount : n.scale + digitCount - decimalPos, false);
            if (!n.at(0)) {   // rounded to zero: use the zero section if there is one
                int s2 = findSection(f, 2);
                if (s2 != section) { section = s2; continue; }
            }
        } else {
            if (!n.fp) n.neg = false;
            n.scale = 0;
        }
        break;
    }
    firstDigit = firstDigit < decimalPos ? decimalPos - firstDigit : 0;
    lastDigit = lastDigit > decimalPos ? decimalPos - lastDigit : 0;
    int digPos = scientific ? decimalPos : std::max(n.scale, decimalPos);
    int adjust = scientific ? 0 : n.scale - decimalPos;
    src = size_t(section);
    std::vector<int> seps;   // digit positions that get a group separator after them
    if (thousandSeps) {
        int total = digPos + (adjust < 0 ? adjust : 0), digits = std::max(firstDigit, total);
        for (int g = 3; digits > g; g += 3) seps.push_back(g);
    }
    int sep = int(seps.size()) - 1;
    if (n.neg && section == 0 && n.scale != 0) o += '-';
    bool decimalWritten = false;
    size_t k = 0;
    auto digit = [&](char c) {
        o += c;
        if (thousandSeps && digPos > 1 && sep >= 0 && digPos == seps[size_t(sep)] + 1) { o += ','; sep--; }
    };
    while (src < f.size() && (ch = f[src++]) != ';') {   // pass 2: write
        if (adjust > 0 && (ch == '#' || ch == '0' || ch == '.'))
            for (; adjust > 0; --adjust, --digPos) digit(n.at(k) ? n.dig[k++] : '0');   // digits beyond the format
        switch (ch) {
        case '#': case '0': {
            char c;
            if (adjust < 0) { adjust++; c = digPos <= firstDigit ? '0' : 0; }
            else c = n.at(k) ? n.dig[k++] : digPos > lastDigit ? '0' : 0;
            if (c) digit(c);
            digPos--;
            break;
        }
        case '.':
            if (digPos != 0 || decimalWritten) break;
            if (lastDigit < 0 || (decimalPos < digitCount && n.at(k))) { o += '.'; decimalWritten = true; }
            break;
        case U'\u2030': put(o, ch); break;
        case '%': o += '%'; break;
        case ',': break;
        case '\'': case '"':
            while (src < f.size() && f[src] != ch) put(o, f[src++]);
            if (src < f.size()) src++;
            break;
        case '\\': if (src < f.size()) put(o, f[src++]); break;
        case 'E': case 'e': {
            if (!scientific) {   // a plain E, echoed with any sign and zeros after it
                put(o, ch);
                if (src < f.size() && (f[src] == '+' || f[src] == '-')) put(o, f[src++]);
                while (src < f.size() && f[src] == '0') put(o, f[src++]);
                break;
            }
            bool plus = false;
            int zeros = 0;
            if (src < f.size() && f[src] == '0') zeros++;
            else if (src + 1 < f.size() && f[src] == '+' && f[src + 1] == '0') plus = true;
            else if (!(src + 1 < f.size() && f[src] == '-' && f[src + 1] == '0')) { put(o, ch); break; }
            while (++src < f.size() && f[src] == '0') zeros++;
            appendExponent(o, n.at(0) ? n.scale - decimalPos : 0, char(ch), std::min(zeros, 10), plus);
            scientific = false;
            break;
        }
        default: put(o, ch);
        }
    }
    if (n.neg && section == 0 && n.scale == 0 && !o.empty()) o.insert(0, "-");
}

// ParseFormatSpecifier: a letter with optional digits is standard ('\0' = custom); -2 = invalid.
char parseSpec(const std::u32string& f, int& digits) {
    digits = -1;
    if (f.empty()) return 'G';
    char32_t c = f[0];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) return 0;
    int64_t v = 0;
    size_t i = 1;
    for (; i < f.size() && f[i] >= '0' && f[i] <= '9'; ++i) {
        if (v >= 100'000'000) { digits = -2; return char(c); }
        v = v * 10 + (f[i] - '0');
    }
    if (i < f.size()) return 0;
    if (f.size() > 1) digits = int(v);
    return char(c);
}

// int.ToString(format) / uint.ToString(format). False: FormatException.
bool formatInteger(std::string& o, uint32_t raw, bool isSigned, const std::u32string& f) {
    int digits;
    char fmt = parseSpec(f, digits);
    if (digits == -2 || digits > kMaxPrecision) return false;
    bool neg = isSigned && int32_t(raw) < 0;
    uint32_t mag = neg ? 0u - raw : raw;
    char up = char(fmt & 0xDF);
    auto padded = [&](std::string s) { return digits > int(s.size()) ? std::string(size_t(digits) - s.size(), '0') + s : s; };
    if (fmt && (up == 'G' ? digits < 1 : up == 'D')) { o = (neg ? "-" : "") + padded(std::to_string(mag)); return true; }
    if (up == 'X' || up == 'B') {   // the two's complement bits of the value
        char buf[40];
        auto res = std::to_chars(buf, buf + sizeof buf, raw, up == 'X' ? 16 : 2);
        std::string s(buf, res.ptr);
        if (fmt == 'X') for (char& ch : s) ch = char(std::toupper(uint8_t(ch)));
        o = padded(s);
        return true;
    }
    Num n;
    if (mag) n.dig = std::to_string(mag);
    n.scale = int(n.dig.size());
    n.neg = neg;
    if (fmt) return numberToString(o, n, fmt, digits);
    customFormat(o, n, f);
    return true;
}

// float.ToString(format). False: FormatException.
bool formatFloat(std::string& o, float v, const std::u32string& f) {
    if (std::isnan(v)) { o = "NaN"; return true; }
    if (std::isinf(v)) { o = v < 0 ? "-\xE2\x88\x9E" : "\xE2\x88\x9E"; return true; }   // en-US "-∞" / "∞"
    int digits;
    char fmt = parseSpec(f, digits);
    if (digits == -2 || digits > kMaxPrecision) return false;
    int prec = fmt ? digits : 7;   // custom formats start from 7 significant digits
    bool significant = true;
    switch (fmt) {
    case 0: break;
    case 'C': case 'c': case 'F': case 'f': case 'N': case 'n': if (prec == -1) prec = 2; significant = false; break;
    case 'E': case 'e': if (prec == -1) prec = 6; prec++; break;
    case 'G': case 'g': if (prec == 0) prec = -1; break;
    case 'P': case 'p': if (prec == -1) prec = 2; prec += 2; significant = false; break;
    case 'R': case 'r': prec = -1; break;
    default: return false;
    }
    Num n = floatNum(v, prec, significant);
    if (!fmt) { customFormat(o, n, f); return true; }
    if (prec == -1) digits = std::max(int(n.dig.size()), 9);   // shortest: plain notation up to 9 digits
    return numberToString(o, n, fmt, digits);
}

void floatToString(Machine& m, uint8_t r) {
    float v = m.fr(uint8_t(m.get<8>(nextReg(r, 1))));
    std::string fmt = guestStr(m, m.get<24>(nextReg(r, 2))), out;
    if (!formatFloat(out, v, decode(fmt))) return halt(m, "Machine: bad number format", fmt);
    putStr(m, m.get<24>(nextReg(r, 5)), out);
}
template <bool Signed> void intToString(Machine& m, uint8_t r) {
    uint32_t v = m.get<32>(nextReg(r, 1));
    std::string fmt = guestStr(m, m.get<24>(nextReg(r, 5))), out;
    if (!formatInteger(out, v, Signed, decode(fmt))) return halt(m, "Machine: bad number format", fmt);
    putStr(m, m.get<24>(nextReg(r, 8)), out);
}

} // namespace

void registerFileInts() {
    IntFn* t = gInt[4];
    t[0x02] = fileExists;
    t[0x03] = dirExists;
    t[0x04] = listDir<0>;
    t[0x05] = listDir<1>;
    t[0x06] = saveFile;
    t[0x07] = loadFile;
    t[0x15] = listDir<2>;
    t[0x20] = fileSize;
    t[0x30] = imageInt<0>;
    t[0x31] = imageInt<1>;
    t[0x32] = imageInt<2>;
    t[0x33] = pngInt<false, false>;
    t[0x34] = pngInt<false, true>;
    t[0x35] = pngInt<true, false>;
    t[0x36] = pngInt<true, true>;
    t[0x40] = loadPngFont;
}

void registerStringInts() {
    IntFn* t = gInt[5];
    t[0x01] = floatToString;
    t[0x02] = intToString<false>;
    t[0x03] = intToString<true>;
}

} // namespace c93
