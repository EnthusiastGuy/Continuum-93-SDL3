// Parsers/Video.cs: the palettes and the eight back buffers as RGBA textures, for PALETTES and 3D VIEW.
// Implemented in win_palettes.cpp.
#pragma once
#include "service/ui.h"

namespace c93::svc::video {

extern int PaletteCount;            // VRAM_PAGES once the machine runs (16 before)
extern Color Palettes[16][256];
extern bool PaletteSet[16];         // the C#'s Palettes[p] != null
extern SDL_Texture* Layers[8];      // back buffer b, straight RGBA; null until InitializeLayers (or on failure)
void InitializeLayers();
void Update();                      // palettes every call, textures every second call (shared counter)

} // namespace c93::svc::video
