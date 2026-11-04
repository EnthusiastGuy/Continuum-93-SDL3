// Presents the machine's video layers in the window.
//
// GPU path (default): each layer is an 8-bit indexed texture with its own palette; the GPU does
// the palette lookup, transparency (index 0) and scaling. The CPU only uploads changed bytes.
// Software path (no usable GPU renderer): layers are composited into one RGB texture on the CPU.
// CRT filter (crtFilter=true in init.cfg): composited on the CPU, filtered (crt.h), one texture.
#pragma once
#include <cstdint>
#include <vector>

#include "crt.h"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Palette;

namespace c93 {

struct Machine;

class Video {
public:
    bool init(SDL_Window* window);
    void present(Machine& m);       // main thread, once per display frame
    SDL_Texture* frame(Machine& m); // the screen as a 480x270 texture (service view), instead of present
    void setLogical(bool on);       // letterboxed 480x270 canvas (normal view) or host pixels (service view)
    void setCrt(bool on);           // CRT look for the normal view
    void setAspect(float screen);   // the monitor's physical shape (w/h), 0 = square pixels
    void shutdown();
    SDL_Renderer* renderer() const { return r_; }

private:
    bool initGpuLayers();
    void drawLayers(Machine& m, bool crt = false);
    void presentGpu(Machine& m, const int* order, int count);
    void presentSoftware(Machine& m, const int* order, int count);
    void presentCrt(Machine& m, const int* order, int count);
    void composite(Machine& m, const int* order, int count, uint32_t* dst, int pitch);

    SDL_Renderer* r_ = nullptr;
    SDL_Texture* layer_[8]{};   // GPU path: one indexed texture per back buffer
    SDL_Palette* pal_[8]{};
    SDL_Texture* frame_ = nullptr;   // software path: composited RGB frame
    SDL_Texture* screen_ = nullptr;  // render target for frame()
    SDL_Texture* crtTex_ = nullptr;  // CRT path: the filtered frame at (about) the window's size
    std::vector<uint32_t> rgb_;      // CRT path: the composited 480x270 frame
    Crt crt_;
    bool crtOn_ = false;
    bool logical_ = true;            // normal view: the letterboxed canvas is on
    float aspect_ = 0;               // screenAspect; 0 = square pixels
    int lh_ = 0;                     // the canvas height in use, 480 x lh_ (270 with square pixels)
    uint8_t palCache_[8][768];       // last palette sent per layer, to skip redundant uploads
    bool palBottom_[8]{};            // whether that palette was sent with index 0 opaque
    bool gpu_ = false;
    uint8_t pages_ = 0;
};

} // namespace c93
