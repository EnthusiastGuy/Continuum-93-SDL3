// UI/PalettesWindow.cs: each video page's 256 colours as a strip, and Parsers/Video.cs behind it.
#include <SDL3/SDL.h>

#include <cstring>
#include <string>

#include "log.h"
#include "machine.h"
#include "service/video_parser.h"

namespace c93::svc {

namespace video {
int PaletteCount = 16;
Color Palettes[16][256];
bool PaletteSet[16];
SDL_Texture* Layers[8];   // freed with the renderer
static int updateFrameCounter = 0;
static uint32_t layerColorData[kVSize];   // RGBA32 bytes, straight alpha (XNA Color)

static bool layersMade = false;

void InitializeLayers() {   // once (the C# ran it while Layers was empty)
    if (layersMade) return;
    layersMade = true;
    for (auto& t : Layers) {
        t = SDL_CreateTexture(renderer(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, kW, kH);
        if (!t) { LOGW("Service: layer texture: %s; 3D VIEW shows no layer", SDL_GetError()); continue; }
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);   // BlendState.NonPremultiplied (alpha is 0 or 255)
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);  // SamplerState.PointClamp, left by the SpriteBatches
        std::memset(layerColorData, 0, sizeof layerColorData);
        SDL_UpdateTexture(t, nullptr, layerColorData, kW * 4);
    }
}

static uint32_t rgba(Color c) {   // SDL_PIXELFORMAT_RGBA32 is byte order R, G, B, A
    uint32_t v;
    uint8_t b[4] = {c.r, c.g, c.b, c.a};
    std::memcpy(&v, b, 4);
    return v;
}

void Update() {
    Machine& m = machine();
    PaletteCount = m.vramPages;
    for (int p = 0; p < PaletteCount && p < 8; ++p) {   // GetPalettesBuffer: back buffer b's palette
        PaletteSet[p] = true;
        for (int i = 0; i < 256; ++i) Palettes[p][i] = {m.backPal[p][i * 3], m.backPal[p][i * 3 + 1], m.backPal[p][i * 3 + 2], 255};
    }
    InitializeLayers();
    // Throttled: both PALETTES and 3D VIEW call this every frame, so the textures follow every frame while
    // both are open and every second frame with one.
    if (++updateFrameCounter % 2 != 0) return;
    for (int p = 0; p < PaletteCount && p < 8; ++p) {
        if (!Layers[p]) continue;
        uint32_t pal[256];
        for (int i = 0; i < 256; ++i) pal[i] = rgba(Palettes[p][i]);
        if (p < 7) pal[0] = 0;   // index 0 is see-through on layers 0..6; the C# keeps it on layer 7 only
        const uint8_t* src = m.back[p];
        for (uint32_t i = 0; i < kVSize; ++i) layerColorData[i] = pal[src[i]];
        SDL_UpdateTexture(Layers[p], nullptr, layerColorData, kW * 4);
    }
}
} // namespace video

namespace {

class PalettesWindow : public Window {
public:
    PalettesWindow() : Window("PALETTES", 1480, 16, 300, 200, 0.7f, true, false) {}

protected:
    void UpdateContent(float) override { video::Update(); }
    void DrawContent(const Rect& c) override {
        int startY = c.Y + Padding;
        for (int p = 0; p < video::PaletteCount; ++p) {
            if (!video::PaletteSet[p]) continue;
            DrawText(fonts.ModernDOS_12x18, std::to_string(p), c.X + Padding, startY + p * 11, c.Width - Padding * 2,
                     theme.VideoPaletteNumber, theme.TextOutline, DrawOutline, 0xFF);
            for (int i = 0; i < 256; ++i) fill({c.X + Padding + 30 + i * 4, startY + p * 11, 4, 10}, video::Palettes[p][i]);
        }
    }
};

} // namespace

C93_SERVICE_WINDOW(100, PalettesWindow);

} // namespace c93::svc
