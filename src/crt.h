// CRT look over the 480x270 screen: curved glass, rounded corners, scanlines that widen on bright
// pixels, an RGB phosphor triad. Our own filter (MIT), computed on the CPU so every renderer, the web
// included, shows the same pixels. Everything that depends only on the output position is a table
// built on resize; a frame is then a 2-tap blend and two table lookups per output pixel.
#pragma once
#include <cstdint>
#include <vector>

namespace c93 {

class Crt {
public:
    void resize(int w, int h);                                     // output size in pixels
    void apply(const uint32_t* src, uint32_t* dst, int pitch) const;   // src: kW x kH XRGB; pitch in bytes
    int width() const { return w_; }
    int height() const { return h_; }

private:
    struct Tap {
        uint32_t at;     // source texel (row * kW + x0); x0 + 1 is blended in by wx
        uint8_t wx;      // horizontal blend toward x0 + 1, 0..255
        uint8_t d;       // distance from the scanline's centre, 0..63
        uint8_t edge;    // glass coverage: 0 outside, 255 inside, between on the rounded rim
        uint8_t mask;    // phosphor colour this column lights: 0 R, 1 G, 2 B
    };
    std::vector<Tap> taps_;
    uint16_t gain_[64][32][2]{};   // [d][luma / 8][off-colour?] gain x256, scanline and mask together
    int w_ = 0, h_ = 0;
};

} // namespace c93
