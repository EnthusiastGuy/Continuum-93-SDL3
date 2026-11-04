#include "video.h"

#include <SDL3/SDL.h>
#include <cstring>

#include "log.h"
#include "machine.h"

namespace c93 {

bool Video::init(SDL_Window* window) {
    // SDL picks the best GPU backend (Direct3D 12/11, Vulkan, Metal, OpenGL) and falls back to
    // its software renderer itself. SDL_RENDER_DRIVER=software forces the CPU path for testing.
    r_ = SDL_CreateRenderer(window, nullptr);
    if (!r_) { LOGE("Video: no renderer (%s)", SDL_GetError()); return false; }
    const char* name = SDL_GetRendererName(r_);

    if (!SDL_SetRenderVSync(r_, 1))
        LOGW("Video: vsync unavailable (%s); frames are paced by the event loop", SDL_GetError());
    // Letterboxed 480x270 canvas; the window can be any size.
    SDL_SetRenderLogicalPresentation(r_, kW, kH, SDL_LOGICAL_PRESENTATION_LETTERBOX);

    gpu_ = std::strcmp(name, SDL_SOFTWARE_RENDERER) != 0 && initGpuLayers();
    if (gpu_) {
        LOGI("Video: renderer '%s'; layers are composited on the GPU", name);
    } else {
        frame_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, kW, kH);
        if (!frame_) { LOGE("Video: cannot create the frame texture (%s)", SDL_GetError()); return false; }
        SDL_SetTextureScaleMode(frame_, SDL_SCALEMODE_NEAREST);
        LOGI("Video: renderer '%s'; layers are composited on the CPU", name);
    }
    return true;
}

bool Video::initGpuLayers() {
    for (int b = 0; b < 8; ++b) {
        layer_[b] = SDL_CreateTexture(r_, SDL_PIXELFORMAT_INDEX8, SDL_TEXTUREACCESS_STREAMING, kW, kH);
        pal_[b] = SDL_CreatePalette(256);
        if (!layer_[b] || !pal_[b] || !SDL_SetTexturePalette(layer_[b], pal_[b])) {
            LOGW("Video: indexed textures unsupported (%s); falling back to CPU compositing", SDL_GetError());
            shutdown();
            return false;
        }
        SDL_SetTextureScaleMode(layer_[b], SDL_SCALEMODE_NEAREST);
        std::memset(palCache_[b], 0xFF, sizeof palCache_[b]);   // force the first upload
    }
    return true;
}

void Video::present(Machine& m) {
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
    SDL_RenderClear(r_);
    drawLayers(m);
    SDL_RenderPresent(r_);
}

SDL_Texture* Video::frame(Machine& m) {
    if (!screen_) {
        screen_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, kW, kH);
        static bool warned = false;
        if (!screen_ && !warned) { warned = true; LOGW("Video: no render target (%s); the service view shows no screen", SDL_GetError()); }
        if (!screen_) return nullptr;
        SDL_SetTextureScaleMode(screen_, SDL_SCALEMODE_NEAREST);
    }
    SDL_SetRenderTarget(r_, screen_);
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
    SDL_RenderClear(r_);
    drawLayers(m);
    SDL_SetRenderTarget(r_, nullptr);
    return screen_;
}

void Video::setLogical(bool on) {
    SDL_SetRenderLogicalPresentation(r_, on ? kW : 0, on ? kH : 0, on ? SDL_LOGICAL_PRESENTATION_LETTERBOX : SDL_LOGICAL_PRESENTATION_DISABLED);
}

void Video::drawLayers(Machine& m) {
    uint8_t pages = m.vramPages.load();
    if (pages != pages_) { pages_ = pages; std::memset(palBottom_, 0, sizeof palBottom_); }

    // Auto-mode pages are snapshotted every frame, exactly when the original did it.
    uint8_t autoBits = m.layerAuto.load(std::memory_order_relaxed);
    for (int i = 0; i < pages; ++i)
        if (autoBits >> i & 1) m.updateBack(pages - 1 - i);

    // Visible back buffers, top-most first. The bottom one shows its colour 0 as background.
    int order[8], count = 0;
    uint8_t vis = m.layerVisible.load(std::memory_order_relaxed);
    for (int b = 0; b < pages; ++b)
        if (vis >> b & 1) order[count++] = b;

    if (count) gpu_ ? presentGpu(m, order, count) : presentSoftware(m, order, count);
}

void Video::presentGpu(Machine& m, const int* order, int count) {
    uint8_t dirty = m.backDirty.exchange(0, std::memory_order_acquire);
    for (int k = count - 1; k >= 0; --k) {   // bottom first, painter's order
        int b = order[k];
        bool bottom = k == count - 1;
        if (dirty >> b & 1) SDL_UpdateTexture(layer_[b], nullptr, m.back[b], kW);
        // Palettes rarely change; compare 768 bytes instead of re-uploading every frame.
        if (bottom != palBottom_[b] || std::memcmp(palCache_[b], m.backPal[b], kPalSize)) {
            std::memcpy(palCache_[b], m.backPal[b], kPalSize);
            palBottom_[b] = bottom;
            SDL_Color c[256];
            for (int i = 0; i < 256; ++i)
                c[i] = {palCache_[b][i * 3], palCache_[b][i * 3 + 1], palCache_[b][i * 3 + 2], 255};
            if (!bottom) c[0].a = 0;         // index 0 is transparent on every layer above the bottom
            SDL_SetPaletteColors(pal_[b], c, 0, 256);
        }
        SDL_SetTextureBlendMode(layer_[b], bottom ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
        SDL_RenderTexture(r_, layer_[b], nullptr, nullptr);
    }
    // Not-yet-drawn dirty buffers (hidden layers) stay dirty for when they become visible.
    uint8_t drawn = 0;
    for (int k = 0; k < count; ++k) drawn |= uint8_t(1u << order[k]);
    if (uint8_t left = dirty & ~drawn) m.backDirty.fetch_or(left, std::memory_order_relaxed);
}

void Video::presentSoftware(Machine& m, const int* order, int count) {
    void* pixels;
    int pitch;
    if (!SDL_LockTexture(frame_, nullptr, &pixels, &pitch)) return;
    m.backDirty.store(0, std::memory_order_relaxed);
    // Bottom layer paints every pixel, the layers above only their non-zero pixels. Sequential
    // passes over each layer beat the per-pixel "first opaque layer" search of the original.
    for (int k = count - 1; k >= 0; --k) {
        int b = order[k];
        const uint8_t* src = m.back[b];
        const uint8_t* pal = m.backPal[b];
        uint32_t lut[256];
        for (int i = 0; i < 256; ++i) lut[i] = uint32_t(pal[i * 3]) << 16 | uint32_t(pal[i * 3 + 1]) << 8 | pal[i * 3 + 2];
        bool bottom = k == count - 1;
        for (int y = 0; y < kH; ++y, src += kW) {
            auto* dst = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(pixels) + y * pitch);
            if (bottom) for (int x = 0; x < kW; ++x) dst[x] = lut[src[x]];
            else        for (int x = 0; x < kW; ++x) if (src[x]) dst[x] = lut[src[x]];
        }
    }
    SDL_UnlockTexture(frame_);
    SDL_RenderTexture(r_, frame_, nullptr, nullptr);
}

void Video::shutdown() {
    for (int b = 0; b < 8; ++b) {
        if (layer_[b]) SDL_DestroyTexture(layer_[b]);
        if (pal_[b]) SDL_DestroyPalette(pal_[b]);
        layer_[b] = nullptr;
        pal_[b] = nullptr;
    }
    if (frame_) SDL_DestroyTexture(frame_);
    if (screen_) SDL_DestroyTexture(screen_);
    frame_ = screen_ = nullptr;
}

} // namespace c93
