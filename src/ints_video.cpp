// Video interrupts (INT 1): pages, palettes, layers, primitives, flood fill, scroll, copy.
// (Interrupts/InterruptsVideo.cs, InterruptsVideoFill.cs, InterruptsVideoScrolling.cs,
// InterruptsVideoCopying.cs; Graphics.cs for pages, palettes and layer bits.)
// Text, fonts, sprites, tile maps and paths are in ints_text.cpp. Mode 7 is not wired in the
// reference's dispatcher, so it is not here either.
//
// Reproduced quirks:
// - 0x03/0x04 read the page from reg+1 and write the 24-bit address over it (BCD for INT 1, A).
// - Pages are not range-checked: any page number gives 0x1000000 - V_SIZE * (page + 1).
// - 0x20 Plot takes unsigned coordinates; everything else signed, except fill/scroll/copy.
// - 0x09 outlines a zero-radius corner as one pixel per corner; both rounded rectangles clamp
//   the radius to half the width, then to half the height.
// - 0x22 seeds its decision variables with Math.Round (half-even) and squares radii in 32 bits.
// - 0x23 tests x*x*ry*ry + y*y*rx*rx <= rx*rx*ry*ry in wrapping 32-bit ints.
// - 0x28 compares the start pixel even when (x, y) is off screen (no effect, then nothing).
// - 0x40 clears vacated pixels to 0 when not rolling; the rectangle is clipped, never wrapped.
// - 0x41 scales nearest-neighbour with float steps; destination pixels whose source falls
//   outside the screen become 0 (the reference's zeroed buffer is copied whole).
// Where the reference throws (address past RAM, |radius| = 32768, a w*h that overflows int),
// this code masks the address or skips the call.
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "ints.h"

namespace c93 {

namespace {

// Register helpers: parameter k of the call, as GetNextRegister(regId, k).
uint8_t u8(Machine& m, uint8_t r, int k) { return uint8_t(m.get<8>(nextReg(r, k))); }
uint16_t u16(Machine& m, uint8_t r, int k) { return uint16_t(m.get<16>(nextReg(r, k))); }
int16_t s16(Machine& m, uint8_t r, int k) { return int16_t(m.get<16>(nextReg(r, k))); }

bool onScreen(int x, int y) { return x >= 0 && x < kW && y >= 0 && y < kH; }
void px(Machine& m, uint32_t base, int x, int y, uint8_t c) { m.ram[(base + uint32_t(y * kW + x)) & kRamMask] = c; }
void plot(Machine& m, uint32_t base, int x, int y, uint8_t c) { if (onScreen(x, y)) px(m, base, x, y, c); }
void fillRow(Machine& m, uint32_t a, int n, uint8_t c) {
    if (a + uint32_t(n) <= kRamSize) std::memset(m.ram + a, c, size_t(n));
    else for (int i = 0; i < n; ++i) m.ram[(a + uint32_t(i)) & kRamMask] = c;
}

// Scratch for scroll/copy/fill snapshots: one page, reused (no allocation per call).
uint8_t gTmp[kVSize];

// --- Pages, palettes, layers ---------------------------------------------------------------
void readResolution(Machine& m, uint8_t r) { m.set<16>(nextReg(r, 1), kW); m.set<16>(nextReg(r, 3), kH); }
void readPages(Machine& m, uint8_t r) { m.set<8>(r, m.vramPages); }
void setPages(Machine& m, uint8_t r) { m.setVramPages(u8(m, r, 1)); }
void readPageAddr(Machine& m, uint8_t r) { m.set<24>(nextReg(r, 1), m.pageAddr(u8(m, r, 1))); }
void readPalAddr(Machine& m, uint8_t r) { m.set<24>(nextReg(r, 1), m.paletteAddr(u8(m, r, 1))); }
void readVisible(Machine& m, uint8_t r) { m.set<8>(r, m.layerVisible); }
void setVisible(Machine& m, uint8_t r) { m.layerVisible = u8(m, r, 1); }
void readAuto(Machine& m, uint8_t r) { m.set<8>(r, m.layerAuto); }
void setAuto(Machine& m, uint8_t r) { m.layerAuto = u8(m, r, 1); }

void clearPage(Machine& m, uint8_t r) {
    uint32_t a = m.pageAddr(u8(m, r, 1));
    if (a <= kRamSize - kVSize) std::memset(m.ram + a, u8(m, r, 2), kVSize);   // else the reference threw
}

// --- Rectangles ------------------------------------------------------------------------------
// Common parameters: page, x, y (signed), w, h (unsigned), colour, [radius].
struct Rect { uint32_t base; int x, y, w, h; uint8_t c; };
Rect rect(Machine& m, uint8_t r) {
    return {m.pageAddr(u8(m, r, 1)), s16(m, r, 2), s16(m, r, 4), u16(m, r, 6), u16(m, r, 8), u8(m, r, 10)};
}
int radius(Machine& m, uint8_t r, const Rect& q) {
    int rad = u8(m, r, 11);
    if (rad > q.w / 2) rad = q.w / 2;
    if (rad > q.h / 2) rad = q.h / 2;
    return rad;
}

void filledRect(Machine& m, uint8_t r) {
    Rect q = rect(m, r);
    int x0 = q.x > 0 ? q.x : 0, x1 = q.x + q.w < kW ? q.x + q.w : kW;
    int y0 = q.y > 0 ? q.y : 0, y1 = q.y + q.h < kH ? q.y + q.h : kH;
    if (x1 <= x0 || y1 <= y0) return;
    for (int y = y0; y < y1; ++y) fillRow(m, (q.base + uint32_t(y * kW + x0)) & kRamMask, x1 - x0, q.c);
}

void rectOutline(Machine& m, uint8_t r) {
    Rect q = rect(m, r);
    for (int i = 0; i < q.w; ++i) { plot(m, q.base, q.x + i, q.y, q.c); plot(m, q.base, q.x + i, q.y + q.h - 1, q.c); }
    for (int i = 0; i < q.h; ++i) { plot(m, q.base, q.x, q.y + i, q.c); plot(m, q.base, q.x + q.w - 1, q.y + i, q.c); }
}

void filledRoundRect(Machine& m, uint8_t r) {
    Rect q = rect(m, r);
    int rad = radius(m, r, q), rr = rad * rad;
    int cl = q.x + rad, cr = q.x + q.w - rad - 1, ct = q.y + rad, cb = q.y + q.h - rad - 1;
    // Off-screen pixels are never written, so the loops only visit the visible part.
    int x0 = q.x > 0 ? q.x : 0, x1 = q.x + q.w < kW ? q.x + q.w : kW;
    int y0 = q.y > 0 ? q.y : 0, y1 = q.y + q.h < kH ? q.y + q.h : kH;
    for (int y = y0; y < y1; ++y) {
        bool midY = y >= q.y + rad && y < q.y + q.h - rad;
        for (int x = x0; x < x1; ++x) {
            bool in = midY || (x >= q.x + rad && x < q.x + q.w - rad) || rad == 0;
            if (!in) {
                bool left = x < q.x + rad, top = y < q.y + rad;
                bool right = x >= q.x + q.w - rad, bottom = y >= q.y + q.h - rad;
                int dx = 0, dy = 0;
                if (left && top) { dx = cl - x; dy = ct - y; in = true; }
                else if (right && top) { dx = x - cr; dy = ct - y; in = true; }
                else if (left && bottom) { dx = cl - x; dy = y - cb; in = true; }
                else if (right && bottom) { dx = x - cr; dy = y - cb; in = true; }
                in = in && dx * dx + dy * dy <= rr;
            }
            if (in) px(m, q.base, x, y, q.c);
        }
    }
}

void roundRect(Machine& m, uint8_t r) {
    Rect q = rect(m, r);
    int rad = radius(m, r, q);
    for (int i = rad; i < q.w - rad; ++i) { plot(m, q.base, q.x + i, q.y, q.c); plot(m, q.base, q.x + i, q.y + q.h - 1, q.c); }
    for (int i = rad; i < q.h - rad; ++i) { plot(m, q.base, q.x, q.y + i, q.c); plot(m, q.base, q.x + q.w - 1, q.y + i, q.c); }
    // Bresenham circle, one octant mirrored into each corner's quarter.
    int cl = q.x + rad, cr = q.x + q.w - rad - 1, ct = q.y + rad, cb = q.y + q.h - rad - 1;
    for (int xo = 0, yo = rad, d = 3 - 2 * rad; xo <= yo; ++xo) {
        plot(m, q.base, cl - xo, ct - yo, q.c); plot(m, q.base, cl - yo, ct - xo, q.c);
        plot(m, q.base, cr + xo, ct - yo, q.c); plot(m, q.base, cr + yo, ct - xo, q.c);
        plot(m, q.base, cl - yo, cb + xo, q.c); plot(m, q.base, cl - xo, cb + yo, q.c);
        plot(m, q.base, cr + xo, cb + yo, q.c); plot(m, q.base, cr + yo, cb + xo, q.c);
        if (d < 0) d += 4 * xo + 6;
        else { d += 4 * (xo - yo) + 10; --yo; }
    }
}

// --- Plot, line, ellipses ------------------------------------------------------------------
void plotPixel(Machine& m, uint8_t r) {
    uint16_t x = u16(m, r, 2), y = u16(m, r, 4);
    if (x < kW && y < kH) px(m, m.pageAddr(u8(m, r, 1)), x, y, u8(m, r, 6));
}

void line(Machine& m, uint8_t r) {
    uint32_t base = m.pageAddr(u8(m, r, 1));
    int16_t x1 = s16(m, r, 2), y1 = s16(m, r, 4), x2 = s16(m, r, 6), y2 = s16(m, r, 8);
    uint8_t c = u8(m, r, 10);
    int dx = std::abs(x2 - x1), dy = std::abs(y2 - y1);
    int sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1, err = dx - dy;
    for (;;) {
        plot(m, base, x1, y1, c);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 = int16_t(x1 + sx); }
        if (e2 < dx) { err += dx; y1 = int16_t(y1 + sy); }
    }
}

// .NET Math.Round: half to even, which is the default floating-point rounding mode.
int64_t roundEven(double v) { return int64_t(std::nearbyint(v)); }

void ellipse(Machine& m, uint8_t r) {
    uint32_t base = m.pageAddr(u8(m, r, 1));
    int xc = s16(m, r, 2), yc = s16(m, r, 4);
    int16_t rxv = s16(m, r, 6), ryv = s16(m, r, 8);
    uint8_t c = u8(m, r, 10);
    if (rxv == -32768 || ryv == -32768) return;   // Math.Abs threw
    int rx = std::abs(rxv), ry = std::abs(ryv);
    int64_t rxSq = int32_t(uint32_t(rx * rx)), rySq = int32_t(uint32_t(ry * ry));   // short*short is an int
    int64_t twoRxSq = 2 * rxSq, twoRySq = 2 * rySq;
    int16_t x = 0, y = int16_t(ry);
    // UpdatePixel already mirrors into four quadrants; PlotEllipsePoints' extra calls repeat them.
    auto put = [&] {
        plot(m, base, xc + x, yc + y, c); plot(m, base, xc - x, yc + y, c);
        plot(m, base, xc + x, yc - y, c); plot(m, base, xc - x, yc - y, c);
    };
    put();
    int64_t p = roundEven(double(rySq - rxSq * ry) + 0.25 * double(rxSq));
    while (twoRySq * x < twoRxSq * y) {
        ++x;
        if (p < 0) p += twoRySq * x + rySq;
        else { --y; p += twoRySq * x - twoRxSq * y + rySq; }
        put();
    }
    p = roundEven(double(rySq) * (x + 0.5) * (x + 0.5) + double(rxSq * (y - 1) * (y - 1)) - double(rxSq * rySq));
    while (y > 0) {
        --y;
        if (p > 0) p += rxSq - twoRxSq * y;
        else { ++x; p += twoRySq * x - twoRxSq * y + rxSq; }
        put();
    }
}

void filledEllipse(Machine& m, uint8_t r) {
    uint32_t base = m.pageAddr(u8(m, r, 1));
    int xc = s16(m, r, 2), yc = s16(m, r, 4);
    int16_t rxv = s16(m, r, 6), ryv = s16(m, r, 8);
    uint8_t c = u8(m, r, 10);
    if (rxv == -32768 || ryv == -32768) return;   // Math.Abs threw
    int32_t rx = std::abs(rxv), ry = std::abs(ryv);
    auto mul = [](uint32_t a, uint32_t b, uint32_t c2, uint32_t d) { return int32_t(a * b * c2 * d); };   // wraps like C# int
    int32_t lim = mul(rx, rx, ry, ry);
    // Only on-screen pixels are written, so the loops skip the rest of the bounding box.
    int ya = -ry > -yc ? -ry : -yc, yb = ry < kH - 1 - yc ? ry : kH - 1 - yc;
    int xa = -rx > -xc ? -rx : -xc, xb = rx < kW - 1 - xc ? rx : kW - 1 - xc;
    for (int y = ya; y <= yb; ++y)
        for (int x = xa; x <= xb; ++x) {
            int32_t v = int32_t(uint32_t(mul(x, x, ry, ry)) + uint32_t(mul(y, y, rx, rx)));
            if (v <= lim) px(m, base, xc + x, yc + y, c);
        }
}

// --- Fill ----------------------------------------------------------------------------------
// The reference pushes all four neighbours and filters on pop; the filled set is the same
// (4-connected region), so neighbours are filtered here before the push.
uint32_t gStack[4 * kVSize + 1];

void fill(Machine& m, uint8_t r) {
    uint32_t base = m.pageAddr(u8(m, r, 1));
    uint16_t x = u16(m, r, 2), y = u16(m, r, 4);
    uint8_t fillC = u8(m, r, 6), border = u8(m, r, 7);
    bool bordered = u8(m, r, 8) & 2;
    uint8_t target = m.ram[(base + uint32_t(y) * kW + x) & kRamMask];
    if (target == fillC || x >= kW || y >= kH) return;
    auto at = [&](uint32_t i) -> uint8_t& { return m.ram[(base + i) & kRamMask]; };
    auto wanted = [&](uint8_t v) { return bordered ? v != border && v != fillC : v == target; };
    size_t n = 0;
    gStack[n++] = uint32_t(y) * kW + x;
    while (n) {
        uint32_t i = gStack[--n];
        if (!wanted(at(i))) continue;
        at(i) = fillC;
        uint32_t cx = i % kW;
        if (cx + 1 < kW) gStack[n++] = i + 1;
        if (cx > 0) gStack[n++] = i - 1;
        if (i + kW < kVSize) gStack[n++] = i + kW;
        if (i >= uint32_t(kW)) gStack[n++] = i - kW;
    }
}

// --- Scroll and copy -----------------------------------------------------------------------
void scroll(Machine& m, uint8_t r) {
    uint32_t base = m.pageAddr(u8(m, r, 1));
    int x = u16(m, r, 2), y = u16(m, r, 4), w = u16(m, r, 6), h = u16(m, r, 8);
    int scX = s16(m, r, 10), scY = s16(m, r, 12);
    uint8_t flags = u8(m, r, 14);
    if (x >= kW || y >= kH) return;
    if (x + w > kW) w = kW - x;
    if (y + h > kH) h = kH - y;
    if (w == 0 || h == 0) return;
    for (int i = 0; i < h; ++i)
        for (int j = 0; j < w; ++j) gTmp[i * w + j] = m.ram[(base + uint32_t((y + i) * kW + x + j)) & kRamMask];
    bool rollX = flags & 1, rollY = flags & 2;
    int ex = rollX ? ((scX % w) + w) % w : scX, ey = rollY ? ((scY % h) + h) % h : scY;
    for (int i = 0; i < h; ++i) {
        int sy = rollY ? (i - ey + h) % h : i - scY;
        for (int j = 0; j < w; ++j) {
            int sx = rollX ? (j - ex + w) % w : j - scX;
            bool in = sx >= 0 && sx < w && sy >= 0 && sy < h;   // vacated pixels become 0
            m.ram[(base + uint32_t((y + i) * kW + x + j)) & kRamMask] = in ? gTmp[sy * w + sx] : 0;
        }
    }
}

void copyRect(Machine& m, uint8_t r) {
    uint32_t src = m.pageAddr(u8(m, r, 1));
    int sx = u16(m, r, 2), sy = u16(m, r, 4), sw = u16(m, r, 6), sh = u16(m, r, 8);
    uint32_t dst = m.pageAddr(u8(m, r, 10));
    int dx = u16(m, r, 11), dy = u16(m, r, 13), dw = u16(m, r, 15), dh = u16(m, r, 17);
    if (sw == 0 || sh == 0 || dw == 0 || dh == 0) return;
    if (int32_t(uint32_t(sw * sh)) < 0) return;   // new byte[] with a negative size threw
    if (sx >= kW || sy >= kH) return;
    int aw = sx + sw > kW ? kW - sx : sw, ah = sy + sh > kH ? kH - sy : sh;
    if (int32_t(uint32_t(dw * dh)) < 0) return;
    if (dx >= kW || dy >= kH) return;
    int endX = dx + dw > kW ? kW - dx : dw, endY = dy + dh > kH ? kH - dy : dh;
    // Snapshot the visible source first: source and destination may overlap.
    for (int i = 0; i < ah; ++i)
        for (int j = 0; j < aw; ++j) gTmp[i * aw + j] = m.ram[(src + uint32_t((sy + i) * kW + sx + j)) & kRamMask];
    float scaleX = float(sw) / float(dw), scaleY = float(sh) / float(dh);
    for (int y = 0; y < endY; ++y) {
        int iy = int(float(y) * scaleY);
        if (iy >= sh) iy = sh - 1;
        for (int x = 0; x < endX; ++x) {
            int ix = int(float(x) * scaleX);
            if (ix >= sw) ix = sw - 1;
            uint8_t v = ix < aw && iy < ah ? gTmp[iy * aw + ix] : 0;
            m.ram[(dst + uint32_t((dy + y) * kW + dx + x)) & kRamMask] = v;
        }
    }
}

} // namespace

void registerVideoInts() {
    IntFn* t = gInt[1];
    t[0x00] = readResolution;
    t[0x01] = readPages;
    t[0x02] = setPages;
    t[0x03] = readPageAddr;
    t[0x04] = readPalAddr;
    t[0x05] = clearPage;
    t[0x06] = filledRect;
    t[0x07] = rectOutline;
    t[0x08] = filledRoundRect;
    t[0x09] = roundRect;
    t[0x20] = plotPixel;
    t[0x21] = line;
    t[0x22] = ellipse;
    t[0x23] = filledEllipse;
    t[0x28] = fill;
    t[0x30] = readVisible;
    t[0x31] = setVisible;
    t[0x32] = readAuto;
    t[0x33] = setAuto;
    t[0x40] = scroll;
    t[0x41] = copyRect;
}

} // namespace c93
