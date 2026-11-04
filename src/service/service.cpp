// Service mode controller: ServiceState.cs, Controls/ServiceInput.cs, ServiceGraphics.cs (Update/Draw and the
// zoom animation), ServiceLayoutManager.cs, UI/EmulatorWindow.cs, and the service rules of Continuum.cs,
// Emulator/Window/WindowManager.cs (title, key routing) and Emulator/Controls/InputMouse.cs.
#include "service/service.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

#include "input.h"
#include "log.h"
#include "machine.h"
#include "service/json.h"
#include "service/state.h"
#include "service/ui.h"
#include "video.h"

namespace c93::svc {

std::vector<std::pair<int, WindowFactory>>& registry();   // window.cpp

namespace {

// UI/EmulatorWindow.cs: chrome around the live machine screen, which ServiceGraphics draws underneath.
class EmulatorWindow : public Window {
public:
    static constexpr int BaseViewportWidth = 480, BaseViewportHeight = 270, ViewPadding = 6;
    EmulatorWindow()
        : Window("Emulator", 16, 16, BaseViewportWidth + BorderWidth * 2, BaseViewportHeight + TitleBarHeight + BorderWidth, 0, true, false) {}
    Rect GetViewportRect(int sw, int sh) const {   // aspect-preserving, centred, inside a 6 px padding
        Rect a = ContentRect();
        a.Inflate(-ViewPadding, -ViewPadding);
        a.Width = std::max(a.Width, 1);
        a.Height = std::max(a.Height, 1);
        float scale = std::min(float(a.Width) / sw, float(a.Height) / sh);
        int w = int(std::nearbyint(double(sw * scale))), h = int(std::nearbyint(double(sh * scale)));   // Math.Round
        return {a.X + (a.Width - w) / 2, a.Y + (a.Height - h) / 2, w, h};
    }

protected:
    bool ShouldDrawBackground() const override { return false; }   // the feed shows through
    void OnResized() override {
        Window::OnResized();
        // SnapToAspectRatio: keep the content 16:9, changing whichever side needs the smaller change.
        const float aspect = float(BaseViewportWidth) / BaseViewportHeight;
        int cw = std::max(1, Width - BorderWidth * 2), ch = std::max(1, Height - TitleBarHeight - BorderWidth);
        int hFromW = int(std::nearbyint(double(cw / aspect))), wFromH = int(std::nearbyint(double(ch * aspect)));
        int aw = cw, ah = ch;
        if (std::abs(wFromH - cw) < std::abs(hFromW - ch)) aw = wFromH;
        else ah = hFromW;
        if (aw < BaseViewportWidth / 2) { aw = BaseViewportWidth / 2; ah = int(std::nearbyint(double(aw / aspect))); }
        if (ah < BaseViewportHeight / 2) { ah = BaseViewportHeight / 2; aw = int(std::nearbyint(double(ah * aspect))); }
        Width = aw + BorderWidth * 2;
        Height = ah + TitleBarHeight + BorderWidth;
    }
    void DrawContent(const Rect&) override {}
};

struct State {
    SDL_Window* window = nullptr;
    Video* video = nullptr;
    std::filesystem::path layoutPath;
    WindowManager wm;
    EmulatorWindow* emulator = nullptr;
    bool serviceMode = false, useServiceView = false, logicalOff = false;
    float anim = 0;   // 0 normal view .. 1 service view
    MouseState prevMouse;
    int wheel = 0;
    bool pressed[6]{};   // F1 F10 Enter F5 F8 F9 this frame
    bool resized = false;
    int pendingW = 0, pendingH = 0, preW = 0, preH = 0;
    bool hasPre = false;
    std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
    std::string title;
    std::vector<int> keysLive, keys, prevKeys;   // SDL keycodes held: live (events), this frame, the previous one
};
State* S = nullptr;

enum { kF1, kF10, kEnter, kF5, kF8, kF9 };

bool readFile(const std::filesystem::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return (x >= 'a' && x <= 'z' ? x - 32 : x) == (y >= 'a' && y <= 'z' ? y - 32 : y);
    });
}

// ServiceLayoutManager.LoadIfPresent: positions by title (case-insensitive), the size to use in service
// mode, the immediate/watcher texts. A malformed file is ignored as a whole.
void loadLayout() {
    std::string text;
    if (!readFile(S->layoutPath, text)) return;
    json::Value v;
    auto num = [](const json::Value* x, int& out) {
        if (!x) return true;   // missing: 0
        if (x->type != json::Value::Number || x->num != std::floor(x->num) || std::abs(x->num) > 2147483647.0) return false;
        out = int(x->num);
        return true;
    };
    struct Dto { std::string title; bool hasTitle; int x = 0, y = 0, w = 0, h = 0; bool visible = false; };
    std::vector<Dto> dtos;
    int mw = 0, mh = 0;
    bool ok = json::parse(text, v) && v.type == json::Value::Object && num(v.get("MainWidth"), mw) && num(v.get("MainHeight"), mh);
    const json::Value* ws = ok ? v.get("Windows") : nullptr;
    if (ok && ws && ws->type == json::Value::Array) {
        for (auto& w : ws->arr) {
            Dto d;
            const json::Value* t = w.get("Title");
            const json::Value* vis = w.get("Visible");
            ok = ok && w.type == json::Value::Object && num(w.get("X"), d.x) && num(w.get("Y"), d.y) && num(w.get("Width"), d.w) &&
                 num(w.get("Height"), d.h) && (!vis || vis->type == json::Value::Bool) && (!t || t->type == json::Value::String || t->type == json::Value::Null);
            d.hasTitle = t && t->type == json::Value::String;
            if (d.hasTitle) d.title = t->str;
            d.visible = vis && vis->b;
            dtos.push_back(d);
        }
    } else if (ok && ws && ws->type != json::Value::Null) ok = false;
    if (!ok) { LOGW("Service: %s is malformed; using the default layout", S->layoutPath.string().c_str()); return; }
    S->pendingW = mw;
    S->pendingH = mh;
    for (auto& d : dtos)
        for (Window* w : S->wm.Windows)
            if (d.hasTitle && iequals(w->Title, d.title)) { w->ApplyLayout(d.x, d.y, d.w, d.h, d.visible); break; }
    for (const char* key : {"ImmediateText", "WatcherText"})
        if (const json::Value* t = v.get(key); t && t->type == json::Value::String && !t->str.empty())
            for (Window* w : S->wm.Windows)
                if (w->LayoutTextKey() && std::strcmp(w->LayoutTextKey(), key) == 0) { w->SetText(t->str); break; }
    LOGI("Service: layout loaded from %s", S->layoutPath.string().c_str());
}

void clientSize(int& w, int& h) { SDL_GetWindowSizeInPixels(S->window, &w, &h); }

void setClientSize(int w, int h) {   // Renderer.SetPreferredBackBufferSize
    float d = SDL_GetWindowPixelDensity(S->window);
    if (d <= 0) d = 1;
    SDL_SetWindowSize(S->window, int(std::lround(w / d)), int(std::lround(h / d)));
}

void updateTitle() {   // Emulator/Window/WindowManager.cs (no tools client: D-3)
    std::string t = std::string("Continuum 93    ") + C93_VERSION + " " +
                    (S->serviceMode ? "- Service mode, \"F1\" exits" : "") + " " + (machine().stepByStep.load() ? ", debugging" : "");
    if (t != S->title) { S->title = t; SDL_SetWindowTitle(S->window, t.c_str()); }
}

// Renderer.GetDestinationRectangle: the normal view, letterboxed.
Rect fullRect(int sw, int sh) {
    float s = std::min(float(host.w) / sw, float(host.h) / sh);
    int w = int(sw * s), h = int(sh * s);
    return {(host.w - w) / 2, (host.h - h) / 2, w, h};
}

} // namespace

bool serviceMode() { return S && S->serviceMode; }
bool keyDown(int k) { return S && std::find(S->keys.begin(), S->keys.end(), k) != S->keys.end(); }
bool keyPressed(int k) { return keyDown(k) && std::find(S->prevKeys.begin(), S->prevKeys.end(), k) == S->prevKeys.end(); }
bool stepByStep() { return host.machine && host.machine->stepByStep.load(); }

void saveLayout() {   // ServiceLayoutManager.Save, System.Text.Json's indented layout
    if (!S) return;
    const char* nl =
#ifdef _WIN32
        "\r\n";
#else
        "\n";
#endif
    int cw, ch;
    clientSize(cw, ch);
    std::string o = std::string("{") + nl + "  \"MainWidth\": " + std::to_string(cw) + "," + nl + "  \"MainHeight\": " + std::to_string(ch) + "," + nl;
    if (S->wm.Windows.empty()) o += std::string("  \"Windows\": [],") + nl;
    else {
        o += std::string("  \"Windows\": [") + nl;
        for (size_t i = 0; i < S->wm.Windows.size(); ++i) {
            Window* w = S->wm.Windows[i];
            o += std::string("    {") + nl + "      \"Title\": " + json::quote(w->Title) + "," + nl +
                 "      \"X\": " + std::to_string(w->X) + "," + nl + "      \"Y\": " + std::to_string(w->Y) + "," + nl +
                 "      \"Width\": " + std::to_string(w->Width) + "," + nl + "      \"Height\": " + std::to_string(w->Height) + "," + nl +
                 "      \"Visible\": " + (w->Visible ? "true" : "false") + nl + "    }" + (i + 1 < S->wm.Windows.size() ? "," : "") + nl;
        }
        o += std::string("  ],") + nl;
    }
    auto text = [](const char* key) {
        for (Window* w : S->wm.Windows) if (w->LayoutTextKey() && std::strcmp(w->LayoutTextKey(), key) == 0) return w->GetText();
        return std::string();
    };
    o += "  \"ImmediateText\": " + json::quote(text("ImmediateText")) + "," + nl + "  \"WatcherText\": " + json::quote(text("WatcherText")) + nl + "}";
    std::error_code ec;
    std::filesystem::create_directories(S->layoutPath.parent_path(), ec);
    std::ofstream f(S->layoutPath, std::ios::binary);
    if (!(f << o)) LOGW("Service: cannot write %s; the window layout is not kept", S->layoutPath.string().c_str());
}

} // namespace c93::svc

namespace c93::service {
using namespace c93::svc;

void init(SDL_Window* window, Video& video, Machine& m, const std::filesystem::path& data) {
    S = new State;
    S->window = window;
    S->video = &video;
    S->layoutPath = data / "serviceState" / "layout.json";
    host.renderer = video.renderer();
    host.machine = &m;
    clientSize(host.w, host.h);

    auto f = data / "fonts";
    fonts.ModernDOS_9x15.load(f / "ModernDOS-9x15.png");
    fonts.ModernDOS_10x15.load(f / "ModernDOS-10x15.png");
    fonts.ModernDOS_10x16.load(f / "ModernDOS-10x16.png");
    fonts.ModernDOS_12x18.load(f / "ModernDOS-12x18.png");
    fonts.ModernDOS_12x18_thin.load(f / "ModernDOS-12x18-thin.png");
    loadTheme(data / "themes" / "default.json");

    // ServiceGraphics.Initialize: the Emulator window first, then the tool windows in reference order.
    auto emu = std::make_unique<EmulatorWindow>();
    S->emulator = emu.get();
    S->wm.Add(std::move(emu));
    auto& reg = registry();
    std::stable_sort(reg.begin(), reg.end(), [](auto& a, auto& b) { return a.first < b.first; });
    for (auto& [order, make] : reg) S->wm.Add(make());
    std::error_code ec;
    std::filesystem::create_directories(S->layoutPath.parent_path(), ec);
    loadLayout();
    updateTitle();
    LOGI("Service: ready, %d windows; F1 opens service mode", int(S->wm.Windows.size()));
}

void event(const SDL_Event& e) {
    if (!S) return;
    if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
        auto& v = S->keysLive;
        auto it = std::find(v.begin(), v.end(), int(e.key.key));
        if (e.type == SDL_EVENT_KEY_DOWN && it == v.end()) v.push_back(int(e.key.key));
        if (e.type == SDL_EVENT_KEY_UP && it != v.end()) v.erase(it);
    } else if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) S->keysLive.clear();
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {   // InputKeyboard.KeyPressed
        switch (e.key.key) {
        case SDLK_F1: S->pressed[kF1] = true; break;
        case SDLK_F10: S->pressed[kF10] = true; break;
        case SDLK_RETURN: case SDLK_KP_ENTER: S->pressed[kEnter] = true; break;
        case SDLK_F5: S->pressed[kF5] = true; break;
        case SDLK_F8: S->pressed[kF8] = true; break;
        case SDLK_F9: S->pressed[kF9] = true; break;
        default: break;
        }
    } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
        S->wheel += e.wheel.integer_y * 120;   // MonoGame's ScrollWheelValue
    } else if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        S->resized = true;
    }
}

bool guestKeys() { return !S || !S->serviceMode; }

void update() {
    if (!S) return;
    Machine& m = machine();
    auto now = std::chrono::steady_clock::now();
    float dt = std::min(std::chrono::duration<float>(now - S->last).count(), 0.1f);
    S->last = now;
    S->prevKeys = S->keys;   // InputKeyboard.Update
    S->keys = S->keysLive;

    // Mouse.GetState(): global (it keeps tracking outside the window), relative to the client area.
    clientSize(host.w, host.h);
    float gx, gy, d = SDL_GetWindowPixelDensity(S->window);
    int wx = 0, wy = 0;
    SDL_MouseButtonFlags b = SDL_GetGlobalMouseState(&gx, &gy);
    SDL_GetWindowPosition(S->window, &wx, &wy);
    host.mouse = {int(std::floor((gx - wx) * d)), int(std::floor((gy - wy) * d)), (b & SDL_BUTTON_LMASK) != 0,
                  (b & SDL_BUTTON_RMASK) != 0, (b & SDL_BUTTON_MMASK) != 0, S->wheel};

    if (S->resized) {   // the user resized the game window: in service mode that size is kept
        S->resized = false;
        if (S->serviceMode) saveLayout();
    }
    if (m.debugRequest.exchange(false)) {   // ExDEBUG (window size untouched, as there)
        S->serviceMode = true;
        LOGI("Service: DEBUG instruction at 0x%06X; service mode, step by step", unsigned(ops::ip()));
        ops::Disassemble();
    }

    // --- ServiceInput.Update ---
    bool was = S->serviceMode;
    if (S->pressed[kF1]) {
        if (was) ops::ContinueExecution();   // D-24: leaving resumes the machine (the reference left it paused)
        S->serviceMode = !S->serviceMode;
        if (!was && S->serviceMode) {
            {   // KeyboardStateExt.ClearState: the program sees no keys held while the service UI is open
                std::lock_guard l(gInput.mu);
                std::memset(gInput.keys, 0, sizeof gInput.keys);
            }
            m.clearActivity();       // ActivityTracker.Clear, then Enabled = true (not on DEBUG, as there)
            m.trackActivity(true);
            clientSize(S->preW, S->preH);   // CapturePreServiceSize
            S->hasPre = true;
            if (S->pendingW > 0 && S->pendingH > 0) setClientSize(S->pendingW, S->pendingH);   // OnServiceModeEntered
            m.debugSet(true, false);
            ops::Disassemble(true);
            LOGI("Service: service mode on; CPU stopped at 0x%06X", unsigned(ops::ip()));
        } else if (was && !S->serviceMode) {
            m.trackActivity(false);  // ActivityTracker.Enabled = false; Clear
            m.clearActivity();
            if (S->hasPre && S->preW > 0 && S->preH > 0) setClientSize(S->preW, S->preH);   // RestorePreServiceSize
            LOGI("Service: service mode off; CPU resumed at 0x%06X", unsigned(ops::ip()));
        }
    }
    if (S->serviceMode) {
        if (S->pressed[kF10]) ops::Disassemble();
        if (S->pressed[kEnter]) { ops::Disassemble(); ops::UpdateAll(); }
        if (S->pressed[kF5]) ops::ToggleStepByStepMode();
        if (S->pressed[kF8]) ops::ContinueExecution();
        if (S->pressed[kF9]) ops::AdvanceStepByStep();
    }
    std::memset(S->pressed, 0, sizeof S->pressed);
    if (S->useServiceView && !S->wm.taskbar.Update(dt, host.mouse, S->prevMouse)) S->wm.HandleInput(host.mouse, S->prevMouse);
    S->prevMouse = host.mouse;

    // --- ServiceGraphics.Update ---
    float target = S->serviceMode ? 1.0f : 0.0f;
    if (S->anim < target) S->anim = std::min(target, S->anim + 4.0f * dt);
    else if (S->anim > target) S->anim = std::max(target, S->anim - 4.0f * dt);
    S->useServiceView = S->serviceMode || S->anim > 0.001f;
    if (S->useServiceView) ops::UpdateAll();
    S->wm.Update(dt);
    for (Window* w : S->wm.Windows) w->Tick();

    if (S->useServiceView != S->logicalOff) {   // the service view draws at host resolution
        S->logicalOff = S->useServiceView;
        S->video->setLogical(!S->logicalOff);
        if (!S->logicalOff) SDL_SetCursor(SDL_GetDefaultCursor());
    }
    updateTitle();
}

bool view() { return S && S->useServiceView; }

void draw() {
    Machine& m = machine();
    SDL_Renderer* r = host.renderer;
    float t = S->anim, eased = t * t * (3 - 2 * t);
    SDL_Texture* screen = S->video->frame(m);

    // DrawThumbnail: the live screen, zooming between the full view and the Emulator window.
    Rect from = fullRect(kW, kH), to = S->emulator->GetViewportRect(kW, kH);
    if (to.Width <= 0 || to.Height <= 0) to = {16, 16, kW * 2, kH * 2};
    Color bg = lerp(colors::Black, theme.Background, eased);
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderClear(r);
    auto lr = [eased](int a, int b) { return int(lerpf(float(a), float(b), clampf(eased, 0, 1))); };
    if (screen) {
        SDL_FRect dst{float(lr(from.X, to.X)), float(lr(from.Y, to.Y)), float(lr(from.Width, to.Width)), float(lr(from.Height, to.Height))};
        SDL_SetTextureBlendMode(screen, SDL_BLENDMODE_NONE);
        SDL_RenderTexture(r, screen, nullptr, &dst);
    }

    // DrawViews: windows, taskbar, popups; SpriteBatch's AlphaBlend is premultiplied.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
    S->wm.Draw();
    for (Window* w : S->wm.Windows) w->DrawOverlay();
    SDL_RenderPresent(r);
}

int guestMouse(float px, float py, uint16_t& x, uint16_t& y) {
    if (!S || !S->useServiceView) return -1;
    int mx = int(std::floor(px)), my = int(std::floor(py));
    if (!S->serviceMode) {   // zooming back: the normal, letterboxed mapping (logical presentation is off)
        Rect v = fullRect(kW, kH);
        if (v.Width <= 0 || v.Height <= 0) return 0;
        x = uint16_t(int(float(mx - v.X) * kW / v.Width));   // truncating, as the normal path's (ushort)(float)
        y = uint16_t(int(float(my - v.Y) * kH / v.Height));
        return 1;
    }
    // InputMouse.ReadMachineMouse: only inside the Emulator window's view; elsewhere the guest keeps the
    // last position and sees no buttons, so clicks meant for the service UI do not leak into the program.
    Rect v = S->emulator->Visible ? S->emulator->GetViewportRect(kW, kH) : Rect{};
    if (v.Width <= 0 || v.Height <= 0 || !v.Contains(mx, my)) return 0;
    x = uint16_t((mx - v.X) * kW / v.Width);
    y = uint16_t((my - v.Y) * kH / v.Height);
    return 1;
}

void shutdown() {
    if (!S) return;
    for (Font* f : {&fonts.ModernDOS_9x15, &fonts.ModernDOS_10x15, &fonts.ModernDOS_10x16, &fonts.ModernDOS_12x18, &fonts.ModernDOS_12x18_thin})
        f->release();
    delete S;   // windows may own textures: before the renderer goes
    S = nullptr;
}

} // namespace c93::service
