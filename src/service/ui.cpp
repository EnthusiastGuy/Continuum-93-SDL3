// Colours, fonts (ServiceFont.cs), theme (Themes/*.cs) and the drawing helpers.
#include "service/ui.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include "log.h"
#include "service/json.h"

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "third_party/stb_image.h"   // the implementation lives in ints_fs.cpp

namespace c93::svc {

Fonts fonts;
Theme theme;
Host host;

Color lerp(Color a, Color b, float t) {
    t = clampf(t, 0, 1);
    auto l = [t](uint8_t x, uint8_t y) { return uint8_t(int(lerpf(x, y, t))); };
    return {l(a.r, b.r), l(a.g, b.g), l(a.b, b.b), l(a.a, b.a)};
}

Rect intersect(Rect a, Rect b) {
    if (!(b.X < a.Right() && a.X < b.Right() && b.Y < a.Bottom() && a.Y < b.Bottom())) return {};   // Intersects()
    int l = std::max(a.X, b.X), t = std::max(a.Y, b.Y);
    return {l, t, std::min(a.Right(), b.Right()) - l, std::min(a.Bottom(), b.Bottom()) - t};
}

void setCursor(Cursor c) {
    static SDL_Cursor* cursors[4];
    static int current = -1;
    if (int(c) == current) return;   // SDL_SetCursor re-shows the pointer: only on change
    static const SDL_SystemCursor ids[4] = {SDL_SYSTEM_CURSOR_DEFAULT, SDL_SYSTEM_CURSOR_EW_RESIZE,
                                            SDL_SYSTEM_CURSOR_NS_RESIZE, SDL_SYSTEM_CURSOR_NWSE_RESIZE};
    SDL_Cursor*& cur = cursors[int(c)];
    if (!cur) cur = SDL_CreateSystemCursor(ids[int(c)]);
    if (cur && SDL_SetCursor(cur)) current = int(c);
}

// --- Drawing -------------------------------------------------------------------------------------
void fill(const Rect& r, Color c) {
    if (r.Width <= 0 || r.Height <= 0) return;
    SDL_SetRenderDrawColor(host.renderer, c.r, c.g, c.b, c.a);
    SDL_FRect f{float(r.X), float(r.Y), float(r.Width), float(r.Height)};
    SDL_RenderFillRect(host.renderer, &f);
}

void setClip(const Rect* r) {
    if (!r) { SDL_SetRenderClipRect(host.renderer, nullptr); return; }
    SDL_Rect c{r->X, r->Y, std::max(0, r->Width), std::max(0, r->Height)};
    SDL_SetRenderClipRect(host.renderer, &c);
}

// --- Fonts ---------------------------------------------------------------------------------------
void Font::release() { if (tex_) SDL_DestroyTexture(tex_); tex_ = nullptr; }

static bool readFile(const std::filesystem::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool Font::load(const std::filesystem::path& png) {
    std::string file;
    int w, h, n;
    uint8_t* px = readFile(png, file) ? stbi_load_from_memory(reinterpret_cast<const uint8_t*>(file.data()), int(file.size()), &w, &h, &n, 4) : nullptr;
    if (!px) { LOGW("Service: font %s could not be loaded; its text is not drawn", png.string().c_str()); return false; }
    texW_ = float(w);
    texH_ = float(h);
    batch_ = std::strcmp(SDL_GetRendererName(host.renderer), SDL_SOFTWARE_RENDERER) != 0;
    GlyphCellWidth = w / 16;   // 16 x 6 grid: ASCII 32..127
    GlyphCellHeight = h / 6;

    // NormalizeFontTextureUsingSpaceCell: colours of the space cell (and anything within 2 per channel)
    // are background -> transparent; everything else is solid white so the draw colour tints it.
    std::vector<uint32_t> bg;
    for (int y = 0; y < GlyphCellHeight; ++y)
        for (int x = 0; x < GlyphCellWidth; ++x) {
            const uint8_t* p = px + (y * w + x) * 4;
            uint32_t c = uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
            if (std::find(bg.begin(), bg.end(), c) == bg.end()) bg.push_back(c);
        }
    for (int i = 0; i < w * h; ++i) {
        uint8_t* p = px + i * 4;
        bool isBg = std::any_of(bg.begin(), bg.end(), [p](uint32_t c) {
            return std::abs(p[0] - int(c >> 16)) <= 2 && std::abs(p[1] - int(c >> 8 & 255)) <= 2 && std::abs(p[2] - int(c & 255)) <= 2;
        });
        p[0] = p[1] = p[2] = p[3] = isBg ? 0 : 255;
    }
    // ComputeGlyphWidths: the ink box of each cell.
    for (int g = 0; g < 96; ++g) {
        int cx = g % 16 * GlyphCellWidth, cy = g / 16 * GlyphCellHeight, minX = GlyphCellWidth, maxX = -1;
        for (int x = 0; x < GlyphCellWidth; ++x)
            for (int y = 0; y < GlyphCellHeight; ++y)
                if (px[((cy + y) * w + cx + x) * 4 + 3]) { maxX = std::max(maxX, x); minX = std::min(minX, x); }
        if (maxX >= 0) { widths_[g] = inkWidth_[g] = uint8_t(maxX - minX + 1); inkOffset_[g] = uint8_t(minX); }
    }
    int maxGlyph = *std::max_element(widths_, widths_ + 96);
    space_ = int(std::nearbyint(double(float(maxGlyph) * 0.6f)));   // Math.Round: to even
    if (space_ <= 0) space_ = GlyphCellWidth;

    tex_ = SDL_CreateTexture(host.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (tex_) {
        SDL_UpdateTexture(tex_, nullptr, px, w * 4);
        SDL_SetTextureBlendMode(tex_, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
        SDL_SetTextureScaleMode(tex_, SDL_SCALEMODE_NEAREST);
    }
    stbi_image_free(px);

    // Kerning.cs: "ab:-1" lines next to the image (.txt), later pairs override earlier ones.
    std::string kern;
    if (readFile(std::filesystem::path(png).replace_extension(".txt"), kern)) {
        std::istringstream in(kern);
        for (std::string line; std::getline(in, line);) {
            auto trim = [](std::string s) {
                size_t a = s.find_first_not_of(" \t\r\n\v\f"), b = s.find_last_not_of(" \t\r\n\v\f");
                return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
            };
            line = trim(line);
            size_t colon = line.find(':');
            if (line.empty() || line[0] == '#' || colon == std::string::npos) continue;
            std::string pair = trim(line.substr(0, colon)), val = trim(line.substr(colon + 1));
            if (pair.size() != 2 || uint8_t(pair[0]) < 32 || uint8_t(pair[0]) > 127 || uint8_t(pair[1]) < 32 || uint8_t(pair[1]) > 127) continue;
            char* end;
            long v = std::strtol(val.c_str(), &end, 10);   // sbyte.TryParse
            if (val.empty() || *end || v < -128 || v > 127 || v == 0) continue;
            kern_[pair[0] - 32][pair[1] - 32] = int8_t(v);
        }
    }
    return tex_ != nullptr;
}

int Font::lineWidth(std::string_view line, bool mono, bool noKern) const {   // CalculateLineWidth
    int w = 0;
    uint8_t last = 0;
    for (char ch : line) {
        uint8_t c = uint8_t(ch);
        if (c < 32 || c > 127) continue;
        if (c == 32) { w += (mono ? GlyphCellWidth : space_) + 1; last = c; continue; }
        int k = !noKern && !mono && last ? kern_[last - 32][c - 32] : 0;
        w += (mono ? GlyphCellWidth : widths_[c - 32]) + 1 + k;
        last = c;
    }
    return w;
}

void Font::wrap(std::string_view text, bool mono, bool noKern, int maxWidth, std::vector<std::string>& out) const {
    if (text.empty()) { out.emplace_back(); return; }
    int spaceWidth = (mono ? GlyphCellWidth : space_) + 1, cur = 0;
    std::string line;
    auto trimEnd = [](std::string s) { while (!s.empty() && (s.back() == ' ' || (s.back() >= 9 && s.back() <= 13))) s.pop_back(); return s; };
    for (size_t a = 0;;) {   // text.Split(' '), empty words included
        size_t b = text.find(' ', a);
        std::string_view word = text.substr(a, b == std::string_view::npos ? std::string_view::npos : b - a);
        int ww = lineWidth(word, mono, noKern);
        if (maxWidth > 0 && cur + ww > maxWidth && !line.empty()) { out.push_back(trimEnd(line)); line.clear(); cur = 0; }
        line.append(word).push_back(' ');
        cur += ww + spaceWidth;
        if (b == std::string_view::npos) break;
        a = b + 1;
    }
    if (!line.empty()) out.push_back(trimEnd(line));
}

void Font::lines(std::string_view text, int maxWidth, uint8_t flags, std::vector<std::string>& out) const {
    std::string t;
    for (char c : text) if (c != '\r') t += c;
    for (size_t a = 0;;) {
        size_t b = t.find('\n', a);
        std::string_view p = std::string_view(t).substr(a, b == std::string::npos ? std::string::npos : b - a);
        if (flags & (Wrap | Center)) wrap(p, flags & Monospace, flags & DisableKerning, uint16_t(maxWidth), out);
        else out.emplace_back(p);
        if (b == std::string::npos) break;
        a = b + 1;
    }
}

// Glyph quads of one DrawString, drawn with a single SDL_RenderGeometry (in order, so overlaps match).
static std::vector<SDL_Vertex> gVerts;
static std::vector<int> gIndices;

void Font::DrawString(std::string_view text, float x, float y, Color color, int maxWidthPixels, uint8_t flags,
                      Color outlineColor, uint8_t outlinePattern) const {
    if (text.empty() || !tex_) return;
    int maxWidth = std::max(0, maxWidthPixels);
    bool mono = flags & Monospace, noKern = flags & DisableKerning;
    std::vector<std::string> ls;
    lines(text, maxWidth, flags, ls);
    int posY = int(y);
    gVerts.clear();
    for (auto& l : ls) {
        int startX = (flags & Center) && maxWidth > 0 ? int(x) + (maxWidth - lineWidth(l, mono, noKern)) / 2 : int(x);
        renderLine(l, startX, posY, mono, flags & MonospaceCentering, flags & DrawOutline, outlineColor, outlinePattern,
                   color, noKern, uint16_t(maxWidth));
        posY += GlyphCellHeight + 1;
    }
    int quads = int(gVerts.size() / 4);
    for (int q = int(gIndices.size() / 6); q < quads; ++q)
        for (int k : {0, 1, 2, 2, 1, 3}) gIndices.push_back(q * 4 + k);
    if (quads) SDL_RenderGeometry(host.renderer, tex_, gVerts.data(), quads * 4, gIndices.data(), quads * 6);
}

TextSize Font::MeasureText(std::string_view text, int maxWidthPixels, uint8_t flags) const {
    if (text.empty()) return {};
    int maxWidth = std::max(0, maxWidthPixels);
    std::vector<std::string> ls;
    lines(text, maxWidth, flags, ls);
    int widest = 0;
    for (auto& l : ls) {
        int w = lineWidth(l, flags & Monospace, flags & DisableKerning);
        widest = std::max(widest, maxWidth > 0 ? std::min(w, maxWidth) : w);
    }
    int n = int(ls.size()), pad = flags & DrawOutline ? 2 : 0;
    return {widest + pad, (n ? n * GlyphCellHeight + (n - 1) : 0) + pad, n};
}

void Font::renderLine(std::string_view line, int startX, int startY, bool mono, bool monoCenter, bool outline,
                      Color outlineColor, uint8_t pattern, Color color, bool noKern, int maxWidth) const {
    static const int8_t offsets[8][3] = {{-1, -1, 7}, {-1, 0, 6}, {-1, 1, 5}, {0, -1, 4}, {0, 1, 3}, {1, -1, 2}, {1, 0, 1}, {1, 1, 0}};
    int posX = startX;
    uint8_t last = 0;
    for (char ch : line) {
        uint8_t c = uint8_t(ch);
        if (c == 32) { posX += (mono ? GlyphCellWidth : space_) + 1; last = c; continue; }
        if (c < 32 || c > 127) continue;
        int g = c - 32, cellX = g % 16 * GlyphCellWidth, cellY = g / 16 * GlyphCellHeight;
        int actual = widths_[g], glyphW = mono ? GlyphCellWidth : actual;
        int k = !noKern && !mono && last ? kern_[last - 32][c - 32] : 0;
        // The C# draws `actual` columns from the cell's left edge (not from the ink offset).
        int srcX = cellX, srcW = actual, gx = posX + k;
        if (mono && monoCenter && inkWidth_[g] > 0) {   // centre the ink box in the cell
            srcX = cellX + inkOffset_[g];
            srcW = inkWidth_[g];
            gx = posX + k + (GlyphCellWidth - srcW) / 2;
        }
        if (!mono && maxWidth > 0 && (gx - startX) + glyphW > maxWidth) break;
        if (srcW > 0) {
            float u0 = float(srcX) / texW_, u1 = float(srcX + srcW) / texW_, v0 = float(cellY) / texH_, v1 = float(cellY + GlyphCellHeight) / texH_;
            auto blit = [&](int dx, int dy, Color col) {   // spriteBatch.Draw(texture, position, srcRect, color)
                float x0 = float(gx + dx), y0 = float(startY + dy), x1 = x0 + float(srcW), y1 = y0 + float(GlyphCellHeight);
                if (!batch_) {   // SDL's software renderer blends textured geometry wrongly: one copy per glyph there
                    SDL_SetTextureColorMod(tex_, col.r, col.g, col.b);
                    SDL_SetTextureAlphaMod(tex_, col.a);
                    SDL_FRect src{float(srcX), float(cellY), float(srcW), float(GlyphCellHeight)}, dst{x0, y0, x1 - x0, y1 - y0};
                    SDL_RenderTexture(host.renderer, tex_, &src, &dst);
                    return;
                }
                SDL_FColor c{col.r / 255.0f, col.g / 255.0f, col.b / 255.0f, col.a / 255.0f};
                gVerts.push_back({{x0, y0}, c, {u0, v0}});
                gVerts.push_back({{x1, y0}, c, {u1, v0}});
                gVerts.push_back({{x0, y1}, c, {u0, v1}});
                gVerts.push_back({{x1, y1}, c, {u1, v1}});
            };
            if (outline && pattern)
                for (auto& o : offsets) if (pattern >> o[2] & 1) blit(o[0], o[1], outlineColor);
            blit(0, 0, color);
        }
        posX += glyphW + 1 + k;
        last = c;
    }
}

// --- Theme (ThemeLoader.cs, ColorParser.cs) -----------------------------------------------------
namespace {
struct Named { const char* name; uint32_t rgb; };
// XNA/MonoGame Color's static properties (Transparent is the only one with alpha 0).
const Named kNamed[] = {
    {"AliceBlue", 0xF0F8FF}, {"AntiqueWhite", 0xFAEBD7}, {"Aqua", 0x00FFFF}, {"Aquamarine", 0x7FFFD4}, {"Azure", 0xF0FFFF},
    {"Beige", 0xF5F5DC}, {"Bisque", 0xFFE4C4}, {"Black", 0x000000}, {"BlanchedAlmond", 0xFFEBCD}, {"Blue", 0x0000FF},
    {"BlueViolet", 0x8A2BE2}, {"Brown", 0xA52A2A}, {"BurlyWood", 0xDEB887}, {"CadetBlue", 0x5F9EA0}, {"Chartreuse", 0x7FFF00},
    {"Chocolate", 0xD2691E}, {"Coral", 0xFF7F50}, {"CornflowerBlue", 0x6495ED}, {"Cornsilk", 0xFFF8DC}, {"Crimson", 0xDC143C},
    {"Cyan", 0x00FFFF}, {"DarkBlue", 0x00008B}, {"DarkCyan", 0x008B8B}, {"DarkGoldenrod", 0xB8860B}, {"DarkGray", 0xA9A9A9},
    {"DarkGreen", 0x006400}, {"DarkKhaki", 0xBDB76B}, {"DarkMagenta", 0x8B008B}, {"DarkOliveGreen", 0x556B2F},
    {"DarkOrange", 0xFF8C00}, {"DarkOrchid", 0x9932CC}, {"DarkRed", 0x8B0000}, {"DarkSalmon", 0xE9967A},
    {"DarkSeaGreen", 0x8FBC8B}, {"DarkSlateBlue", 0x483D8B}, {"DarkSlateGray", 0x2F4F4F}, {"DarkTurquoise", 0x00CED1},
    {"DarkViolet", 0x9400D3}, {"DeepPink", 0xFF1493}, {"DeepSkyBlue", 0x00BFFF}, {"DimGray", 0x696969},
    {"DodgerBlue", 0x1E90FF}, {"Firebrick", 0xB22222}, {"FloralWhite", 0xFFFAF0}, {"ForestGreen", 0x228B22},
    {"Fuchsia", 0xFF00FF}, {"Gainsboro", 0xDCDCDC}, {"GhostWhite", 0xF8F8FF}, {"Gold", 0xFFD700}, {"Goldenrod", 0xDAA520},
    {"Gray", 0x808080}, {"Green", 0x008000}, {"GreenYellow", 0xADFF2F}, {"Honeydew", 0xF0FFF0}, {"HotPink", 0xFF69B4},
    {"IndianRed", 0xCD5C5C}, {"Indigo", 0x4B0082}, {"Ivory", 0xFFFFF0}, {"Khaki", 0xF0E68C}, {"Lavender", 0xE6E6FA},
    {"LavenderBlush", 0xFFF0F5}, {"LawnGreen", 0x7CFC00}, {"LemonChiffon", 0xFFFACD}, {"LightBlue", 0xADD8E6},
    {"LightCoral", 0xF08080}, {"LightCyan", 0xE0FFFF}, {"LightGoldenrodYellow", 0xFAFAD2}, {"LightGray", 0xD3D3D3},
    {"LightGreen", 0x90EE90}, {"LightPink", 0xFFB6C1}, {"LightSalmon", 0xFFA07A}, {"LightSeaGreen", 0x20B2AA},
    {"LightSkyBlue", 0x87CEFA}, {"LightSlateGray", 0x778899}, {"LightSteelBlue", 0xB0C4DE}, {"LightYellow", 0xFFFFE0},
    {"Lime", 0x00FF00}, {"LimeGreen", 0x32CD32}, {"Linen", 0xFAF0E6}, {"Magenta", 0xFF00FF}, {"Maroon", 0x800000},
    {"MediumAquamarine", 0x66CDAA}, {"MediumBlue", 0x0000CD}, {"MediumOrchid", 0xBA55D3}, {"MediumPurple", 0x9370DB},
    {"MediumSeaGreen", 0x3CB371}, {"MediumSlateBlue", 0x7B68EE}, {"MediumSpringGreen", 0x00FA9A},
    {"MediumTurquoise", 0x48D1CC}, {"MediumVioletRed", 0xC71585}, {"MidnightBlue", 0x191970}, {"MintCream", 0xF5FFFA},
    {"MistyRose", 0xFFE4E1}, {"Moccasin", 0xFFE4B5}, {"MonoGameOrange", 0xE73C00}, {"NavajoWhite", 0xFFDEAD},
    {"Navy", 0x000080}, {"OldLace", 0xFDF5E6}, {"Olive", 0x808000}, {"OliveDrab", 0x6B8E23}, {"Orange", 0xFFA500},
    {"OrangeRed", 0xFF4500}, {"Orchid", 0xDA70D6}, {"PaleGoldenrod", 0xEEE8AA}, {"PaleGreen", 0x98FB98},
    {"PaleTurquoise", 0xAFEEEE}, {"PaleVioletRed", 0xDB7093}, {"PapayaWhip", 0xFFEFD5}, {"PeachPuff", 0xFFDAB9},
    {"Peru", 0xCD853F}, {"Pink", 0xFFC0CB}, {"Plum", 0xDDA0DD}, {"PowderBlue", 0xB0E0E6}, {"Purple", 0x800080},
    {"Red", 0xFF0000}, {"RosyBrown", 0xBC8F8F}, {"RoyalBlue", 0x4169E1}, {"SaddleBrown", 0x8B4513}, {"Salmon", 0xFA8072},
    {"SandyBrown", 0xF4A460}, {"SeaGreen", 0x2E8B57}, {"SeaShell", 0xFFF5EE}, {"Sienna", 0xA0522D}, {"Silver", 0xC0C0C0},
    {"SkyBlue", 0x87CEEB}, {"SlateBlue", 0x6A5ACD}, {"SlateGray", 0x708090}, {"Snow", 0xFFFAFA}, {"SpringGreen", 0x00FF7F},
    {"SteelBlue", 0x4682B4}, {"Tan", 0xD2B48C}, {"Teal", 0x008080}, {"Thistle", 0xD8BFD8}, {"Tomato", 0xFF6347},
    {"Turquoise", 0x40E0D0}, {"Violet", 0xEE82EE}, {"Wheat", 0xF5DEB3}, {"White", 0xFFFFFF}, {"WhiteSmoke", 0xF5F5F5},
    {"Yellow", 0xFFFF00}, {"YellowGreen", 0x9ACD32},
};

std::string_view trimView(std::string_view s) {
    auto ws = [](char c) { return c == ' ' || (c >= 9 && c <= 13); };
    while (!s.empty() && ws(s.front())) s.remove_prefix(1);
    while (!s.empty() && ws(s.back())) s.remove_suffix(1);
    return s;
}
bool parseByte(std::string_view s, int base, uint8_t& out) {   // byte.Parse (whitespace allowed around decimal)
    std::string t(base == 10 ? trimView(s) : s);
    if (t.empty()) return false;
    char* end;
    long v = std::strtol(t.c_str(), &end, base);
    if (*end || v < 0 || v > 255 || t[0] == '-' || (base == 16 && t[0] == '+')) return false;
    out = uint8_t(v);
    return true;
}
} // namespace

bool parseColor(std::string_view s, Color& out) {
    s = trimView(s);
    if (!s.empty() && s[0] == '#') {
        std::string_view h = s.substr(1);
        while (!h.empty() && h[0] == '#') h.remove_prefix(1);   // TrimStart('#')
        if (h.size() != 6 && h.size() != 8) return false;
        uint8_t c[4] = {0, 0, 0, 255};
        for (size_t i = 0; i < h.size() / 2; ++i) if (!parseByte(h.substr(i * 2, 2), 16, c[i])) return false;
        out = {c[0], c[1], c[2], c[3]};
        return true;
    }
    if (s == "Transparent") { out = colors::Transparent; return true; }
    for (auto& n : kNamed)
        if (s == n.name) { out = {uint8_t(n.rgb >> 16), uint8_t(n.rgb >> 8), uint8_t(n.rgb), 255}; return true; }
    if (s.find(',') == std::string_view::npos) return false;
    std::vector<uint8_t> parts;
    for (size_t a = 0;;) {
        size_t b = s.find(',', a);
        uint8_t v;
        if (!parseByte(s.substr(a, b == std::string_view::npos ? std::string_view::npos : b - a), 10, v)) return false;
        parts.push_back(v);
        if (b == std::string_view::npos) break;
        a = b + 1;
    }
    if (parts.size() != 3 && parts.size() != 4) return false;
    out = {parts[0], parts[1], parts[2], parts.size() == 4 ? parts[3] : uint8_t(255)};
    return true;
}

void loadTheme(const std::filesystem::path& path) {
    theme = Theme{};
    theme.PrimaryFont = &fonts.ModernDOS_12x18_thin;
    std::string text;
    json::Value v;
    if (!readFile(path, text) || !json::parse(text, v) || v.type != json::Value::Object) {
        LOGW("Service: theme %s missing or malformed; using the built-in colours", path.string().c_str());
        return;
    }
    // ThemeData property -> Theme field (two are renamed in the reference: TextAliceBlue, RegisterValueZeroTransparent).
    struct Field { const char* key; Color Theme::*field; };
    static const Field kFields[] = {
        {"Background", &Theme::Background}, {"BackgroundTransparent", &Theme::BackgroundTransparent},
        {"WindowBorder", &Theme::WindowBorder}, {"WindowTitleBarUnfocused", &Theme::WindowTitleBarUnfocused},
        {"WindowTitleBarFocused", &Theme::WindowTitleBarFocused}, {"WindowTitleBarOnTop", &Theme::WindowTitleBarOnTop},
        {"WindowBackgroundUnfocused", &Theme::WindowBackgroundUnfocused}, {"WindowBackgroundFocused", &Theme::WindowBackgroundFocused},
        {"WindowResizeGrip", &Theme::WindowResizeGrip}, {"WindowCloseButton", &Theme::WindowCloseButton},
        {"WindowTitleText", &Theme::WindowTitleText}, {"WindowCloseButtonText", &Theme::WindowCloseButtonText},
        {"TextPrimary", &Theme::TextPrimary}, {"TextSecondary", &Theme::TextSecondary}, {"TextHighlight", &Theme::TextHighlight},
        {"TextOutline", &Theme::TextOutline}, {"TextTitle", &Theme::TextTitle}, {"TextInfo", &Theme::TextInfo},
        {"TextTitleHeader", &Theme::TextTitleHeader}, {"TextCyan", &Theme::TextCyan}, {"TextAliceBlue", &Theme::VideoPaletteNumber},
        {"TextDarkOrange", &Theme::TextDarkOrange}, {"TextGreenYellow", &Theme::TextGreenYellow}, {"TextIndianRed", &Theme::TextIndianRed},
        {"MemoryAddressFull", &Theme::MemoryAddressFull}, {"MemoryAddressZero", &Theme::MemoryAddressZero},
        {"MemoryAddressZeroOutline", &Theme::MemoryAddressZeroOutline}, {"MemoryByteColor", &Theme::MemoryByteColor},
        {"MemoryAsciiColor", &Theme::MemoryAsciiColor}, {"MemoryAsciiNonAsciiColor", &Theme::MemoryAsciiNonAsciiColor},
        {"DisassemblerAddressFull", &Theme::DisassemblerAddressFull}, {"DisassemblerAddressZero", &Theme::DisassemblerAddressZero},
        {"DisassemblerAddressZeroOutline", &Theme::DisassemblerAddressZeroOutline}, {"DisassemblerOpcodeColor", &Theme::DisassemblerOpcodeColor},
        {"DisassemblerInstructionColor", &Theme::DisassemblerInstructionColor}, {"DisassemblerHoverBorder", &Theme::DisassemblerHoverBorder},
        {"DisassemblerCurrentInstructionBackground", &Theme::DisassemblerCurrentInstructionBackground},
        {"RegisterNameColor", &Theme::RegisterNameColor}, {"RegisterValueChangedColor", &Theme::RegisterValueChangedColor},
        {"RegisterValueUnchangedColor", &Theme::RegisterValueUnchangedColor}, {"RegisterValueZeroTransparent", &Theme::NumberLeadingZeroes},
        {"RegisterMemoryDataColor", &Theme::RegisterMemoryDataColor}, {"RegisterChangedStateColor", &Theme::RegisterChangedStateColor},
        {"RegisterUnchangedStateColor", &Theme::RegisterUnchangedStateColor},
        {"FlagNameColor", &Theme::FlagNameColor}, {"FlagValueChangedColor", &Theme::FlagValueChangedColor},
        {"FlagValueUnchangedColor", &Theme::FlagValueUnchangedColor}, {"FlagValueOneColor", &Theme::FlagValueOneColor},
        {"FlagValueZeroColor", &Theme::FlagValueZeroColor},
        {"StackValueColor", &Theme::StackValueColor}, {"StackLastValueColor", &Theme::StackLastValueColor},
        {"TaskbarBackground", &Theme::TaskbarBackground}, {"TaskbarBorder", &Theme::TaskbarBorder},
        {"TaskbarHeaderBackground", &Theme::TaskbarHeaderBackground}, {"TaskbarItemActiveBackground", &Theme::TaskbarItemActiveBackground},
        {"TaskbarItemMinimizedBackground", &Theme::TaskbarItemMinimizedBackground}, {"TaskbarItemNormalBackground", &Theme::TaskbarItemNormalBackground},
    };
    for (auto& f : kFields) {
        const json::Value* s = v.get(f.key);
        if (!s || s->type != json::Value::String || s->str.empty()) continue;
        if (!parseColor(s->str, theme.*f.field))   // the reference threw here and never opened
            LOGW("Service: theme %s: %s '%s' is not a colour; kept the default", path.string().c_str(), f.key, s->str.c_str());
    }
    LOGI("Service: theme %s loaded", path.string().c_str());
}

} // namespace c93::svc
