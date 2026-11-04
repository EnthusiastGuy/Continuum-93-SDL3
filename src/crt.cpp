#include "crt.h"

#include <algorithm>
#include <cmath>

#include "machine.h"

namespace c93 {
namespace {

constexpr double kAspect = double(kW) / kH;
constexpr double kCurve = 0.03;     // barrel: the corners bulge out by 3%, the edge middles barely
constexpr double kCorner = 0.06;    // rounded corner radius, in half screen heights
constexpr double kBoost = 1.30;     // gives back the brightness the scanline gaps and the mask take
constexpr double kDark = 3.2;       // scanline beam falloff on a black pixel: thin line, deep gap
constexpr double kBright = 1.0;     // ...and on a white one: the beam fills most of the gap
constexpr double kMask = 0.22;      // dimming of the two phosphors a column does not light (half on white)

// Sharpens the blend between two source pixels (crisp texels, soft edges), then keeps 30% linear.
double sharpen(double f) {
    double s = f < 0.5 ? 4 * f * f * f : 1 - 4 * (1 - f) * (1 - f) * (1 - f);
    return s + (f - s) * 0.3;
}

} // namespace

void Crt::resize(int w, int h) {
    w_ = w;
    h_ = h;
    for (int d = 0; d < 64; ++d)
        for (int l = 0; l < 32; ++l) {
            double dd = (d + 0.5) / 64, lum = (l + 0.5) / 32;
            double beam = kBoost * std::exp(-(kDark + (kBright - kDark) * lum) * dd * dd);
            gain_[d][l][0] = uint16_t(std::lround(beam * 256));
            gain_[d][l][1] = uint16_t(std::lround(beam * (1 - kMask * (1 - 0.5 * lum)) * 256));
        }

    taps_.resize(size_t(w) * h);
    Tap* t = taps_.data();
    const double px = 2.0 / h;   // one output pixel, in the half-height units below
    for (int oy = 0; oy < h; ++oy)
        for (int ox = 0; ox < w; ++ox, ++t) {
            // Output position to source position through the curved glass (-1..1 on both axes).
            double x = 2 * (ox + 0.5) / w - 1, y = 2 * (oy + 0.5) / h - 1;
            double f = 1 + kCurve * (x * x * kAspect * kAspect + y * y) / (kAspect * kAspect + 1);
            x *= f;
            y *= f;
            // Coverage: signed distance to the rounded screen rectangle, a one-pixel soft rim.
            double qx = std::fabs(x) * kAspect - (kAspect - kCorner), qy = std::fabs(y) - (1 - kCorner);
            double dist = std::hypot(std::max(qx, 0.0), std::max(qy, 0.0)) + std::min(std::max(qx, qy), 0.0) - kCorner;
            t->edge = uint8_t(std::lround(std::clamp(0.5 - dist / px, 0.0, 1.0) * 255));

            double sv = (y + 1) / 2 * kH, sx = (x + 1) / 2 * kW - 0.5;
            int row = std::clamp(int(std::floor(sv)), 0, kH - 1);
            t->d = uint8_t(std::min(63, int(std::fabs(sv - std::floor(sv) - 0.5) * 2 * 64)));
            int x0 = int(std::floor(sx));
            double fx = sharpen(sx - x0);
            if (x0 < 0 || x0 >= kW - 1) { x0 = std::clamp(x0, 0, kW - 1); fx = 0; }
            t->at = uint32_t(row * kW + x0);
            t->wx = uint8_t(std::lround(fx * 255));
            t->mask = uint8_t(ox % 3);
        }
}

void Crt::apply(const uint32_t* src, uint32_t* dst, int pitch) const {
    const Tap* t = taps_.data();
    for (int y = 0; y < h_; ++y) {
        auto* out = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(dst) + size_t(y) * pitch);
        for (int x = 0; x < w_; ++x, ++t) {
            if (!t->edge) { out[x] = 0; continue; }
            uint32_t a = src[t->at], b = src[t->at + (t->wx != 0)];   // wx is 0 on the last column
            int w = t->wx;
            int c[3];
            for (int k = 0; k < 3; ++k) {
                int ca = int(a >> (16 - 8 * k) & 255), cb = int(b >> (16 - 8 * k) & 255);
                c[k] = ca + ((cb - ca) * w >> 8);
            }
            const uint16_t* g = gain_[t->d][(c[0] * 77 + c[1] * 150 + c[2] * 29) >> 11];   // luma / 8
            uint32_t px = 0;
            for (int k = 0; k < 3; ++k) {
                int gk = g[k != t->mask];
                if (t->edge != 255) gk = gk * t->edge >> 8;
                px |= uint32_t(std::min(255, c[k] * gk >> 8)) << (16 - 8 * k);
            }
            out[x] = px;
        }
    }
}

} // namespace c93
