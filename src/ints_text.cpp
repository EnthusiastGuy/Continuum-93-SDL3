// Video group (1): text, sprites, tile maps and paths.
// (Interrupts/InterruptsVideo.cs: DrawTileMapSprite, DrawSpriteToVideoPage, DrawString, LinePath;
//  InterruptsVideoDrawText.cs; InterruptsVideoDrawBezierPath.cs; InterruptsVideoPerlinPath.cs.)
//
// Reference quirks reproduced on purpose:
// - Strings are decoded as UTF-8 to UTF-16 (Encoding.UTF8.GetString) and each code unit is then
//   truncated to a byte: U+0141 draws as 'A', surrogate halves draw their low byte, invalid bytes
//   become U+FFFD (skipped). TrimEnd after wrapping trims Unicode white space (U+2028 etc.).
// - DrawString: bits come from (row << x) with C#'s shift count masked to 5 bits, so glyphs wider
//   than 8 repeat their row at x = 32..39, ...; a glyph's width byte for '\x7F' overlaps the space
//   glyph's first data byte; overflow is only flagged on set pixels; x wraps at 16 bits.
// - DrawText: wrapped words are measured without cross-word kerning; a monospaced space in the
//   wrapper is cellWidth wide (no spacing); maxWidth clipping stops a line only when not monospaced;
//   centring uses the unclamped line width; GetTextMetrics pads 2 px of outline per line and casts
//   (does not clamp) its width.
// - DrawSprite: a sprite clipped on the left and on the right copies 480 + |x| pixels per row
//   (spilling into the next rows, and into the next sprite row on the source side); page 0 copies
//   opaquely, other pages skip colour 0.
// - Tile map sprite: rotation uses the reference's own cos/sin values (table below).
// - Bezier: a zero-length step resets the outline lists and sets the end points to (0, 0).
// - Perlin path: pattern 2's registers wrap round the A..Z ring (with A as regId, its zoom is the
//   page register and its shift the start x).
// Where the reference threw (reading past RAM, Math.Abs(-32768)), the call stops; writes to
// pages past 128 (addresses that wrap below 0) wrap at 24 bits instead of throwing.
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <utility>
#include <vector>

#include "ints.h"

namespace c93 {
namespace {

constexpr uint32_t kRefRam = kRamSize + 3;   // the reference's RAM array length

uint32_t vpage(uint8_t p) { return 0x1000000u - kVSize * (uint32_t(p) + 1); }
void put(Machine& m, uint32_t a, uint8_t c) { m.ram[a & kRamMask] = c; }
uint8_t peek(const Machine& m, uint32_t a) { return m.ram[a & kRamMask]; }
int16_t s16(const Machine& m, uint8_t r, int d) { return int16_t(m.get<16>(nextReg(r, d))); }
uint32_t u16(const Machine& m, uint8_t r, int d) { return m.get<16>(nextReg(r, d)); }
uint8_t u8(const Machine& m, uint8_t r, int d) { return uint8_t(m.get<8>(nextReg(r, d))); }
uint32_t u24(const Machine& m, uint8_t r, int d) { return m.get<24>(nextReg(r, d)); }
// .NET Math.Round / MathF.Round: half to even (the default FP rounding mode).
int roundEven(double v) { return int(std::nearbyint(v)); }

// --- Strings ---------------------------------------------------------------------------------
// MemoryController.GetStringAt: zero-terminated, UTF-8 decoded; no terminator before the end of
// RAM gives "". Out: UTF-16 code units. Invalid sequences become one U+FFFD per maximal subpart.
void guestString(const Machine& m, uint32_t a, std::vector<uint16_t>& out) {
    out.clear();
    uint32_t end = a;
    while (end < kRefRam && m.ram[end]) ++end;
    if (end >= kRefRam) return;
    for (uint32_t i = a; i < end;) {
        uint8_t b = m.ram[i++];
        if (b < 0x80) { out.push_back(b); continue; }
        int need = b >= 0xC2 && b <= 0xDF ? 1 : b >= 0xE0 && b <= 0xEF ? 2 : b >= 0xF0 && b <= 0xF4 ? 3 : -1;
        if (need < 0) { out.push_back(0xFFFD); continue; }
        uint32_t cp = b & (0x3F >> need);
        uint8_t lo = b == 0xE0 ? 0xA0 : b == 0xF0 ? 0x90 : 0x80;   // second-byte range rules
        uint8_t hi = b == 0xED ? 0x9F : b == 0xF4 ? 0x8F : 0xBF;
        int got = 0;
        for (; got < need && i < end; ++got) {
            uint8_t c = m.ram[i];
            if (c < (got ? 0x80 : lo) || c > (got ? 0xBF : hi)) break;
            cp = cp << 6 | (c & 0x3F);
            ++i;
        }
        if (got < need) { out.push_back(0xFFFD); continue; }
        if (cp < 0x10000) out.push_back(uint16_t(cp));
        else { cp -= 0x10000; out.push_back(uint16_t(0xD800 + (cp >> 10))); out.push_back(uint16_t(0xDC00 + (cp & 0x3FF))); }
    }
}

bool isWhite(uint16_t c) {   // char.IsWhiteSpace
    return (c >= 9 && c <= 13) || c == 0x20 || c == 0x85 || c == 0xA0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

// --- DrawText family -----------------------------------------------------------------------
struct Span { uint32_t b, e; };   // a line: [b, e) into the decoded string

struct Text {
    uint32_t font, glyphData;
    uint8_t cellW, cellH;
    bool mono, monoCenter, noKern, center, wrap, outline;
    uint16_t maxWidth;
    uint8_t widths[96];
    int8_t kern[96][96];          // [first - 32][second - 32]; other pairs can never be looked up
    std::vector<uint16_t> s;
    std::vector<Span> lines;

    Text(const Machine& m, uint8_t r) {
        font = u24(m, r, 1);
        maxWidth = uint16_t(u16(m, r, 13));
        uint8_t flags = u8(m, r, 15);
        mono = flags & 1; monoCenter = flags & 2; noKern = flags & 4;
        center = flags & 8; wrap = flags & 16; outline = flags & 32;
        cellW = peek(m, font + 1); cellH = peek(m, font + 2);
        uint8_t nk = peek(m, font + 5);
        for (int i = 0; i < 96; ++i) widths[i] = peek(m, font + 6 + i);
        for (auto& row : kern) for (auto& k : row) k = 0;
        uint32_t kp = font + 6 + 96;
        for (int i = 0; i < nk; ++i) {
            uint8_t a = peek(m, kp + i * 3), b = peek(m, kp + i * 3 + 1);
            if (a >= 32 && a <= 127 && b >= 32 && b <= 127) kern[a - 32][b - 32] = int8_t(peek(m, kp + i * 3 + 2));
        }
        glyphData = kp + nk * 3;
        guestString(m, u24(m, r, 4), s);
        if (wrap || center) wrapLines();
        else lines.push_back({0, uint32_t(s.size())});
    }

    int kerning(uint8_t last, uint8_t c) const {
        return !noKern && !mono && last != 0 ? kern[last - 32][c - 32] : 0;
    }
    int width(uint32_t b, uint32_t e) const {   // CalculateLineWidth
        int w = 0;
        uint8_t last = 0;
        for (uint32_t i = b; i < e; ++i) {
            uint8_t c = uint8_t(s[i]);
            if (c < 32 || c > 127) continue;
            w += (mono ? cellW : widths[c - 32]) + 1 + kerning(last, c);
            last = c;
        }
        return w;
    }
    // WrapText: split on ' ', greedy fill; a line is a span of the original text, TrimEnd'ed.
    void wrapLines() {
        int spaceW = mono ? cellW : widths[0] + 1;
        int lineW = 0;
        uint32_t lineB = 0;
        bool any = false;   // currentLine.Length > 0
        auto push = [&](uint32_t e) { while (e > lineB && isWhite(s[e - 1])) --e; lines.push_back({lineB, e}); };
        uint32_t n = uint32_t(s.size());
        for (uint32_t wb = 0; wb <= n;) {
            uint32_t we = wb;
            while (we < n && s[we] != ' ') ++we;
            int ww = width(wb, we);
            if (lineW + ww > maxWidth && any) { push(wb - 1); lineB = wb; lineW = 0; }
            any = true;
            lineW += ww + spaceW;
            wb = we + 1;
        }
        push(n);
    }
    // Measured box as DrawText / GetTextRectangle report it.
    void box(Machine& m, uint8_t r) const {
        int widest = 0;
        for (Span l : lines) {
            int w = width(l.b, l.e);
            if (maxWidth > 0 && maxWidth < w) w = maxWidth;
            if (w > widest) widest = w;
        }
        int n = int(lines.size());
        int pad = outline ? 2 : 0;
        auto clamp = [](int v) { return uint32_t(v < 0 ? 0 : v > 0xFFFF ? 0xFFFF : v); };
        m.set<16>(r, clamp(widest + pad));
        m.set<16>(nextReg(r, 2), clamp((n ? n * cellH + (n - 1) : 0) + pad));
    }
};

// GetBitPosition: 3x3 neighbourhood bit, row by row from the top-left = bit 7, centre excluded.
constexpr int kOutlineBit[3][3] = {{7, 6, 5}, {4, -1, 3}, {2, 1, 0}};

void renderLine(Machine& m, const Text& t, Span l, int startX, int startY, uint8_t color,
                uint8_t outlineColor, uint8_t pattern, uint32_t vStart) {
    int bpr = (t.cellW + 7) / 8, perGlyph = bpr * t.cellH;
    int posX = startX;
    uint8_t last = 0;
    for (uint32_t i = l.b; i < l.e; ++i) {
        uint8_t c = uint8_t(t.s[i]);
        if (c == 32) { posX += t.mono ? t.cellW + 1 : t.widths[0] + 1; last = c; continue; }
        if (c < 32 || c > 127) continue;
        int k = t.kerning(last, c);
        int gw = t.mono ? t.cellW : t.widths[c - 32];
        uint32_t data = t.glyphData + uint32_t((c - 33) * perGlyph);   // the space glyph is not stored
        int gx = posX + k;
        if (t.mono && t.monoCenter) gx += (t.cellW - t.widths[c - 32]) / 2;
        if (!t.mono && t.maxWidth > 0 && gx - startX + gw > t.maxWidth) break;
        for (int pass = t.outline ? 0 : 1; pass < 2; ++pass) {   // outline first, then the glyph
            for (int y = 0; y < t.cellH; ++y)
                for (int x = 0; x < gw; ++x) {
                    if (!(peek(m, data + y * bpr + x / 8) >> (7 - x % 8) & 1)) continue;
                    int px = gx + x, py = startY + y;
                    if (pass == 1) {
                        if (px >= 0 && px < kW && py >= 0 && py < kH) put(m, vStart + uint32_t(py * kW + px), color);
                        continue;
                    }
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            int bit = kOutlineBit[dy + 1][dx + 1];
                            if (bit < 0 || !(pattern >> bit & 1)) continue;
                            int ox = px + dx, oy = py + dy;
                            if (ox >= 0 && ox < kW && oy >= 0 && oy < kH) put(m, vStart + uint32_t(oy * kW + ox), outlineColor);
                        }
                }
        }
        posX += gw + 1 + k;
        last = c;
    }
}

void drawText(Machine& m, uint8_t r) {
    Text t(m, r);
    int16_t x = s16(m, r, 7), y = s16(m, r, 9);
    uint8_t color = u8(m, r, 11), page = u8(m, r, 12), oc = u8(m, r, 16), pat = u8(m, r, 17);
    int cy = y;
    for (Span l : t.lines) {
        int cx = t.center && t.maxWidth > 0 ? x + (t.maxWidth - t.width(l.b, l.e)) / 2 : x;
        renderLine(m, t, l, cx, cy, color, oc, pat, vpage(page));
        cy += t.cellH + 1;
    }
    t.box(m, r);
}

void getTextRectangle(Machine& m, uint8_t r) { Text(m, r).box(m, r); }

void getTextMetrics(Machine& m, uint8_t r) {
    Text t(m, r);
    int n = int(t.lines.size()), pad = t.outline ? 2 : 0, widest = 0;
    for (Span l : t.lines) {
        int w = t.width(l.b, l.e) + pad;
        if (w > widest) widest = w;
    }
    m.set<16>(nextReg(r, 1), uint32_t(widest));
    m.set<16>(nextReg(r, 3), uint32_t((t.cellH + 1) * n + pad * n));
    m.set<16>(nextReg(r, 5), uint32_t(n));
}

// Obsolete DrawString: font = [?, height, 96 widths, glyph rows from +97, one byte per row].
void drawString(Machine& m, uint8_t r) {
    uint32_t font = u24(m, r, 1);
    int16_t x0 = s16(m, r, 7), y0 = s16(m, r, 9);
    uint8_t color = u8(m, r, 11), page = u8(m, r, 12);
    uint32_t maxWidth = u16(m, r, 13);
    uint32_t v = vpage(page);
    std::vector<uint16_t> s;
    guestString(m, u24(m, r, 4), s);
    uint8_t h = peek(m, font + 1), overflow = 0;
    int16_t posX = 0;
    for (uint16_t ch : s) {
        uint8_t c = uint8_t(ch);
        if (c < 32 || c > 127) continue;
        uint8_t w = peek(m, font + 2 + c - 32);
        uint32_t d = font + 97 + uint32_t((c - 32) * h);
        for (int y = 0; y < h; ++y) {
            uint32_t bits = peek(m, d + y);
            for (int x = 0; x < w; ++x) {
                if (!((bits << (x & 31)) & 0x80)) continue;   // C# masks int shift counts to 5 bits
                int16_t cx = int16_t(x0 + posX + x), cy = int16_t(y0 + y);
                if (maxWidth != 0 && posX > int(maxWidth)) { overflow = 1; continue; }
                if (cy < 0 || cy >= kH || cx < 0 || cx >= kW) continue;
                put(m, v + uint32_t(cy * kW + cx), color);
            }
        }
        posX = int16_t(posX + w + 1);
    }
    m.set<16>(r, uint16_t(posX));
    m.set<8>(nextReg(r, 2), overflow);
}

// --- Sprites -------------------------------------------------------------------------------
void drawSprite(Machine& m, uint8_t r) {
    uint32_t src = u24(m, r, 1);
    uint8_t page = u8(m, r, 4);
    int16_t xv = s16(m, r, 5), yv = s16(m, r, 7);
    uint32_t w = u16(m, r, 9), h = u16(m, r, 11);
    uint32_t v = vpage(page);
    for (uint32_t y = 0; y < h && int64_t(yv) + y < kH; ++y) {
        if (int64_t(yv) + y < 0) continue;
        if (xv == -32768) return;                       // Math.Abs(short.MinValue) threw
        uint32_t xoff = xv < 0 ? uint32_t(-xv) : 0;
        uint32_t cy = uint32_t(yv + int(y));
        int64_t a = uint32_t(w - xoff), b = int64_t(kW) - xv;   // uint subtraction wraps, as the reference
        int vis = int(a < b ? a : b);
        if (vis <= 0 || xv + int(w) <= 0) continue;
        uint32_t s0 = src + w * y + xoff, d0 = v + cy * kW + uint32_t(xv) + xoff;
        if (page != 0) {
            for (uint32_t x = 0; x < uint32_t(vis); ++x) {
                if (s0 + x >= kRefRam) return;   // the reference threw
                uint8_t c = m.ram[s0 + x];
                if (!c) continue;
                if (d0 + x >= kRefRam) return;
                m.ram[d0 + x] = c;
            }
        } else {
            if (uint64_t(s0) + vis > kRefRam || uint64_t(d0) + vis > kRefRam) return;   // Array.Copy threw
            std::memmove(m.ram + d0, m.ram + s0, size_t(vis));
        }
    }
}

// Math.Cos / Math.Sin of k * 22.5 degrees as the reference computes them (bit patterns from .NET 8),
// so the rotated bounding boxes and nearest-neighbour picks match on every libm.
constexpr uint64_t kTrig[16][2] = {
    {0x3FF0000000000000, 0x0000000000000000}, {0x3FED906BCF328D46, 0x3FD87DE2A6AEA963},
    {0x3FE6A09E667F3BCD, 0x3FE6A09E667F3BCD}, {0x3FD87DE2A6AEA964, 0x3FED906BCF328D46},
    {0x3C91A62633145C07, 0x3FF0000000000000}, {0xBFD87DE2A6AEA962, 0x3FED906BCF328D46},
    {0xBFE6A09E667F3BCC, 0x3FE6A09E667F3BCD}, {0xBFED906BCF328D46, 0x3FD87DE2A6AEA965},
    {0xBFF0000000000000, 0x3CA1A62633145C07}, {0xBFED906BCF328D47, 0xBFD87DE2A6AEA961},
    {0xBFE6A09E667F3BCE, 0xBFE6A09E667F3BCC}, {0xBFD87DE2A6AEA95E, 0xBFED906BCF328D47},
    {0xBCAA79394C9E8A0A, 0xBFF0000000000000}, {0x3FD87DE2A6AEA967, 0xBFED906BCF328D45},
    {0x3FE6A09E667F3BCB, 0xBFE6A09E667F3BCE}, {0x3FED906BCF328D47, 0xBFD87DE2A6AEA95F},
};

void drawTileMapSprite(Machine& m, uint8_t r) {
    uint32_t src = u24(m, r, 1), mapW = u16(m, r, 4);
    int16_t spX = s16(m, r, 6), spY = s16(m, r, 8);
    int sw = int(u16(m, r, 10)), sh = int(u16(m, r, 12));
    uint8_t page = u8(m, r, 14);
    int16_t vx = s16(m, r, 15), vy = s16(m, r, 17);
    uint8_t fx = u8(m, r, 19);
    bool flipH = fx & 1, flipV = fx & 2;
    int tilesH = fx & 4 ? u8(m, r, 20) : 1, tilesV = fx & 8 ? u8(m, r, 21) : 1;
    double cosA = std::bit_cast<double>(kTrig[fx >> 4][0]), sinA = std::bit_cast<double>(kTrig[fx >> 4][1]);
    int rw = int(std::ceil(std::fabs(sw * cosA) + std::fabs(sh * sinA)));
    int rh = int(std::ceil(std::fabs(sw * sinA) + std::fabs(sh * cosA)));
    double cx = (sw - 1) / 2.0, cy = (sh - 1) / 2.0, cxd = (rw - 1) / 2.0, cyd = (rh - 1) / 2.0;
    uint32_t v = vpage(page);
    for (int ty = 0; ty < tilesV; ++ty)
        for (int tx = 0; tx < tilesH; ++tx)
            for (int dy = 0; dy < rh; ++dy) {
                int y = vy + ty * rh + dy;
                if (y < 0 || y >= kH) continue;
                for (int dx = 0; dx < rw; ++dx) {
                    int x = vx + tx * rw + dx;
                    if (x < 0 || x >= kW) continue;
                    double rx = dx - cxd, ry = dy - cyd;
                    int sx = int(std::floor(cx + rx * cosA + ry * sinA + 0.5));
                    int sy = int(std::floor(cy - rx * sinA + ry * cosA + 0.5));
                    if (flipH) sx = sw - 1 - sx;
                    if (flipV) sy = sh - 1 - sy;
                    if (sx < 0 || sx >= sw || sy < 0 || sy >= sh) continue;
                    uint32_t s = src + mapW * uint32_t(spY + sy) + uint32_t(spX + sx);
                    if (s >= kRefRam) return;   // the reference threw
                    if (uint8_t c = m.ram[s]) m.ram[(v + uint32_t(y * kW + x)) & kRamMask] = c;
                }
            }
}

// --- Paths ---------------------------------------------------------------------------------
// Bresenham as InterruptsVideo.DrawLine (LinePath). Shared shape with the Line primitive.
void lineSeg(Machine& m, int x1, int y1, int x2, int y2, uint8_t color, uint32_t v) {
    int dx = std::abs(x2 - x1), dy = std::abs(y2 - y1);
    int sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1, err = dx - dy;
    for (;;) {
        if (x1 >= 0 && x1 < kW && y1 >= 0 && y1 < kH) put(m, v + uint32_t(y1 * kW + x1), color);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 < dx) { err += dx; y1 += sy; }
    }
}

// Points are unsigned 16-bit pairs; the path ends at the first point equal to the previous one.
void linePath(Machine& m, uint8_t r) {
    uint32_t p = u24(m, r, 1);
    uint8_t page = u8(m, r, 4), color = u8(m, r, 5);
    if (p + 3 >= kRefRam) return;
    int lx = int(m.rd<16>(p)), ly = int(m.rd<16>(p + 2));
    for (uint32_t a = p + 4;; a += 4) {
        if (a + 3 >= kRefRam) return;   // never terminated: the reference threw at the end of RAM
        int x = int(m.rd<16>(a)), y = int(m.rd<16>(a + 2));
        if (x == lx && y == ly) return;
        lineSeg(m, lx, ly, x, y, color, vpage(page));
        lx = x; ly = y;
    }
}

struct PF { float x, y; };

float flen(float dx, float dy) { return float(std::sqrt(double(dx * dx + dy * dy))); }

void scanLine(Machine& m, int xs, int xe, int y, uint8_t c, uint32_t v) {
    if (y < 0 || y >= kH) return;
    if (xs > xe) std::swap(xs, xe);
    if (xs < 0) xs = 0;
    if (xe > kW - 1) xe = kW - 1;
    for (int x = xs; x <= xe; ++x) put(m, v + uint32_t(y * kW + x), c);
}

void fillTriangle(Machine& m, float x1, float y1, float x2, float y2, float x3, float y3, uint8_t c, uint32_t v) {
    int x[3] = {roundEven(x1), roundEven(x2), roundEven(x3)};
    int y[3] = {roundEven(y1), roundEven(y2), roundEven(y3)};
    if (y[0] > y[1]) { std::swap(y[0], y[1]); std::swap(x[0], x[1]); }
    if (y[1] > y[2]) { std::swap(y[1], y[2]); std::swap(x[1], x[2]); }
    if (y[0] > y[1]) { std::swap(y[0], y[1]); std::swap(x[0], x[1]); }
    float dx1 = y[1] - y[0] > 0 ? float(x[1] - x[0]) / float(y[1] - y[0]) : 0;
    float dx2 = y[2] - y[0] > 0 ? float(x[2] - x[0]) / float(y[2] - y[0]) : 0;
    float xs = float(x[0]), xe = float(x[0]);
    if (y[1] != y[0])
        for (int yy = y[0]; yy < y[1]; ++yy) { scanLine(m, int(xs), int(xe), yy, c, v); xs += dx1; xe += dx2; }
    if (y[2] != y[1]) {
        xs = float(x[1]);
        dx1 = y[2] - y[1] > 0 ? float(x[2] - x[1]) / float(y[2] - y[1]) : 0;
        for (int yy = y[1]; yy <= y[2]; ++yy) { scanLine(m, int(xs), int(xe), yy, c, v); xs += dx1; xe += dx2; }
    }
}

// Bresenham as InterruptsVideoDrawBezierPath.DrawLine (rounded float ends; differs from lineSeg).
void outlineSeg(Machine& m, PF a, PF b, uint8_t c, uint32_t v) {
    int x0 = roundEven(a.x), y0 = roundEven(a.y), x1 = roundEven(b.x), y1 = roundEven(b.y);
    int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < kW && y0 >= 0 && y0 < kH) put(m, v + uint32_t(y0 * kW + x0), c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// Closed polygon: the left side forward, then the right side backward.
void outline(Machine& m, const std::vector<PF>& left, const std::vector<PF>& right, uint8_t c, uint32_t v) {
    size_t n = left.size() + right.size();
    if (n < 2) return;
    auto at = [&](size_t i) { return i < left.size() ? left[i] : right[right.size() - 1 - (i - left.size())]; };
    for (size_t i = 0; i < n; ++i) outlineSeg(m, at(i), at((i + 1) % n), c, v);
}

void drawBezierPath(Machine& m, uint8_t r) {
    uint32_t a = u24(m, r, 1);
    uint8_t page = u8(m, r, 4), brush = u8(m, r, 5), fill = u8(m, r, 6), empty = u8(m, r, 7);
    uint8_t color = u8(m, r, 8), oc = u8(m, r, 9);
    uint8_t p0 = u8(m, r, 10) < 100 ? u8(m, r, 10) : 100, p1 = u8(m, r, 11) < 100 ? u8(m, r, 11) : 100;
    if (p0 > p1) std::swap(p0, p1);

    // Sample 50 points per quadratic segment; the start point itself is not a path point.
    uint32_t x1 = m.rd<16>(a); a += 2;
    if (x1 == 0xFFFF) return;
    uint32_t y1 = m.rd<16>(a); a += 2;
    if (y1 == 0xFFFF) return;
    std::vector<PF> pts;
    float total = 0;
    for (;;) {
        uint32_t c[4];
        int k = 0;
        for (; k < 4; ++k) { c[k] = m.rd<16>(a); a += 2; if (c[k] == 0xFFFF) break; }
        if (k < 4) break;
        float sx = float(x1), sy = float(y1), cx = float(c[0]), cy = float(c[1]), ex = float(c[2]), ey = float(c[3]);
        float lx = sx, ly = sy;
        for (int i = 1; i <= 50; ++i) {
            float t = float(i) / 50;
            float x = (1 - t) * (1 - t) * sx + 2 * (1 - t) * t * cx + t * t * ex;
            float y = (1 - t) * (1 - t) * sy + 2 * (1 - t) * t * cy + t * t * ey;
            total += flen(x - lx, y - ly);
            pts.push_back({x, y});
            lx = x; ly = y;
        }
        x1 = c[2]; y1 = c[3];
    }
    if (pts.empty() || total == 0) return;

    float acc = 0;
    size_t from = 0, to = pts.size() - 1;
    for (size_t i = 1; i < pts.size(); ++i) {
        acc += flen(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
        float pct = acc / total * 100;
        if (from == 0 && pct >= p0) from = i - 1;
        if (pct >= p1) { to = i; break; }
    }

    uint32_t v = vpage(page);
    bool drawing = true, lists = false;   // lists: the reference's side-point lists are non-null
    float cur = 0;
    std::vector<PF> left, right;
    PF le{}, re{};
    for (size_t i = from + 1; i <= to; ++i) {
        PF s = pts[i - 1], e = pts[i];
        float dx = e.x - s.x, dy = e.y - s.y, len = flen(dx, dy);
        if (!drawing) {
            cur += len;
            if (cur >= empty) { drawing = true; cur = 0; }
            continue;
        }
        if (cur == 0 || !lists) { left.clear(); right.clear(); lists = true; }
        if (len == 0) le = re = PF{0, 0};   // DrawBrushStroke: nothing drawn, end points zeroed
        else {
            float nx = dx / len, ny = dy / len, half = brush / 2.0f;
            float ox = -ny * half, oy = nx * half;
            PF q1{s.x + ox, s.y + oy}, q2{s.x - ox, s.y - oy}, q3{e.x - ox, e.y - oy}, q4{e.x + ox, e.y + oy};
            fillTriangle(m, q1.x, q1.y, q2.x, q2.y, q3.x, q3.y, color, v);
            fillTriangle(m, q1.x, q1.y, q3.x, q3.y, q4.x, q4.y, color, v);
            left.push_back(q1); right.push_back(q2);
            le = q4; re = q3;
        }
        cur += len;
        if (cur >= fill) {
            drawing = false; cur = 0;
            left.push_back(le); right.push_back(re);
            if (oc) outline(m, left, right, oc, v);
            lists = false;
        }
    }
    if (drawing && oc && lists) {
        left.push_back(le); right.push_back(re);
        outline(m, left, right, oc, v);
    }
}

// Deterministic 1D Perlin noise (InterruptsVideoPerlinPath.Noise1D), all in float.
float noise1D(float x, uint16_t seed) {
    auto grad = [seed](int xi) {
        uint32_t h = uint32_t(xi) ^ uint32_t(seed) * 0x9E3779B1u;
        h ^= h << 13; h ^= h >> 17; h ^= h << 5;
        int v = int(h >> 8) & 0x7FFF;
        return v / 16383.5f * 2.0f - 1.0f;
    };
    int xi0 = int(std::floor(x));
    float xf = x - float(xi0);
    float d0 = grad(xi0) * xf, d1 = grad(xi0 + 1) * (xf - 1.0f);
    float u = xf * xf * xf * (xf * (xf * 6.0f - 15.0f) + 10.0f);
    return d0 + (d1 - d0) * u;
}
float mapRange(float v, float lo, float hi) { return lo + (v - -1.0f) / (1.0f - -1.0f) * (hi - lo); }

void drawPerlinPath(Machine& m, uint8_t r) {
    uint8_t page = u8(m, r, 1), color = u8(m, r, 10);
    int16_t sx = s16(m, r, 2), sy = s16(m, r, 4), ex = s16(m, r, 6), ey = s16(m, r, 8);
    uint16_t seed1 = uint16_t(u16(m, r, 12)), seed2 = uint16_t(u16(m, r, 21));
    float min1 = s16(m, r, 14), max1 = s16(m, r, 16), min2 = s16(m, r, 23), max2 = s16(m, r, 25);
    uint8_t zoom1 = u8(m, r, 18), zoom2 = u8(m, r, 27);
    int16_t shift1 = s16(m, r, 19), shift2 = s16(m, r, 28);
    int dx = ex - sx, dy = ey - sy;
    int steps = std::max(std::abs(dx), std::abs(dy));
    if (steps <= 0) return;
    float scale1 = float(std::max(1, int(zoom1))), scale2 = float(std::max(1, int(zoom2)));
    uint32_t v = vpage(page);
    for (int i = 0; i <= steps; ++i) {
        float t = float(i) / float(steps);
        float fx = float(sx) + t * float(dx), fy = float(sy) + t * float(dy);
        int x = roundEven(fx);
        float o1 = mapRange(noise1D(float(x + shift1) / scale1, seed1), min1, max1);
        float o2 = zoom2 ? mapRange(noise1D(float(x + shift2) / scale2, seed2), min2, max2) : 0.0f;
        int y = roundEven(fy + o1 + o2);
        if (x >= 0 && x < kW && y >= 0 && y < kH) put(m, v + uint32_t(y * kW + x), color);
    }
}

} // namespace

void registerTextInts() {
    IntFn* t = gInt[1];
    t[0x0E] = drawTileMapSprite;
    t[0x10] = drawSprite;
    t[0x11] = getTextMetrics;
    t[0x12] = drawString;
    t[0x14] = drawText;
    t[0x15] = getTextRectangle;
    t[0x24] = linePath;
    t[0x25] = drawBezierPath;
    t[0x26] = drawPerlinPath;
}

} // namespace c93
