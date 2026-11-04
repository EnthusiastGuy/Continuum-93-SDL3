// Service-mode UI framework: colours, fonts, theme, drawing helpers, Window, WindowManager, Taskbar.
// Ports ServiceModule/ServiceFont.cs, Fonts.cs, ServiceGraphics.cs (DrawText), Themes/*.cs, UI/Window.cs,
// UI/WindowManager.cs, UI/Taskbar.cs and UI/TextWindow.cs. Names follow the C# (X, Width, IsFocused,
// theme.WindowBorder...) so tool windows port line by line. See docs/SERVICE.md.
//
// Everything here runs on the main thread. Coordinates are host pixels (the window's back buffer).
#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

namespace c93 { struct Machine; }

namespace c93::svc {

// --- Basics --------------------------------------------------------------------------------
struct Color { uint8_t r = 0, g = 0, b = 0, a = 255; };   // XNA Color: straight bytes, drawn premultiplied
Color lerp(Color a, Color b, float t);                     // Color.Lerp (clamped, truncating)
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }   // MathHelper.Lerp
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
namespace colors {   // the XNA named colours the service code uses
inline constexpr Color Transparent{0, 0, 0, 0}, Black{0, 0, 0, 255}, White{255, 255, 255, 255};
}

struct Rect {   // XNA Rectangle
    int X = 0, Y = 0, Width = 0, Height = 0;
    int Right() const { return X + Width; }
    int Bottom() const { return Y + Height; }
    int Left() const { return X; }
    int Top() const { return Y; }
    bool Contains(int px, int py) const { return X <= px && px < X + Width && Y <= py && py < Y + Height; }
    void Inflate(int h, int v) { X -= h; Y -= v; Width += h * 2; Height += v * 2; }
};
Rect intersect(Rect a, Rect b);   // Rectangle.Intersect: empty when disjoint; a negative size can survive

struct MouseState {   // Mouse.GetState(): host pixels, buttons, cumulative wheel (120 per notch)
    int X = 0, Y = 0;
    bool Left = false, Right = false, Middle = false;
    int ScrollWheelValue = 0;
};
// What the service code reads from the host; service.cpp refreshes it once per frame.
struct Host {
    SDL_Renderer* renderer = nullptr;
    Machine* machine = nullptr;
    MouseState mouse;     // this frame's Mouse.GetState()
    int w = 0, h = 0;     // back buffer size in pixels (GraphicsDevice.Viewport, Window.ClientBounds)
};
extern Host host;
inline const MouseState& mouse() { return host.mouse; }   // Mouse.GetState() anywhere in the C#
// Keyboard, polled once per frame like InputKeyboard's current/previous states. Keys are SDL keycodes
// (SDLK_A, SDLK_LSHIFT...): the C#'s Keys.A is SDLK_A here (D-11 maps them the same way).
bool keyDown(int sdlKeycode);      // KeyboardState.IsKeyDown, this frame
bool keyPressed(int sdlKeycode);   // down this frame, up the previous one (IsNewPress)

enum class Cursor { Arrow, SizeWE, SizeNS, SizeNWSE };
void setCursor(Cursor c);    // Mouse.SetCursor

// --- Fonts (ServiceFont.cs) ------------------------------------------------------------------
enum FontFlags : uint8_t {   // ServiceFontFlags
    Monospace = 1 << 0, MonospaceCentering = 1 << 1, DisableKerning = 1 << 2,
    Center = 1 << 3, Wrap = 1 << 4, DrawOutline = 1 << 5,
};
struct TextSize { int width = 0, height = 0, lines = 0; };

class Font {
public:
    bool load(const std::filesystem::path& png);   // the .txt next to it holds kerning pairs (optional)
    void DrawString(std::string_view text, float x, float y, Color color, int maxWidthPixels, uint8_t flags,
                    Color outlineColor, uint8_t outlinePattern) const;
    TextSize MeasureText(std::string_view text, int maxWidthPixels, uint8_t flags) const;
    int GlyphCellWidth = 0, GlyphCellHeight = 0;
    void release();   // frees the texture (before the renderer is destroyed)

private:
    int lineWidth(std::string_view line, bool mono, bool noKern) const;
    void wrap(std::string_view text, bool mono, bool noKern, int maxWidth, std::vector<std::string>& out) const;
    void lines(std::string_view text, int maxWidth, uint8_t flags, std::vector<std::string>& out) const;
    void renderLine(std::string_view line, int startX, int startY, bool mono, bool monoCenter, bool outline,
                    Color outlineColor, uint8_t pattern, Color color, bool noKern, int maxWidth) const;
    SDL_Texture* tex_ = nullptr;
    float texW_ = 1, texH_ = 1;
    bool batch_ = true;   // one SDL_RenderGeometry per string (GPU renderers)
    uint8_t widths_[96]{}, inkWidth_[96]{}, inkOffset_[96]{};   // ASCII 32..127
    int8_t kern_[96][96]{};
    int space_ = 0;
};

struct Fonts {   // Fonts.cs
    Font ModernDOS_9x15, ModernDOS_10x15, ModernDOS_10x16, ModernDOS_12x18, ModernDOS_12x18_thin;
};
extern Fonts fonts;

// --- Theme (Themes/Theme.cs defaults, loaded from Data/themes/default.json) -------------------
struct Theme {
    Font* PrimaryFont = nullptr;
    Color WindowBorder{128, 128, 128}, WindowTitleBarUnfocused{30, 30, 30}, WindowTitleBarFocused{40, 40, 80},
        WindowTitleBarOnTop{60, 60, 120}, WindowBackgroundUnfocused{5, 5, 5, 250}, WindowBackgroundFocused{5, 5, 5, 220},
        WindowResizeGrip{105, 105, 105}, WindowCloseButton{255, 0, 0}, WindowTitleText{255, 140, 0},
        WindowCloseButtonText{255, 255, 255};
    Color TextPrimary{255, 255, 255}, TextSecondary{255, 255, 0}, TextHighlight{255, 255, 0},
        VideoPaletteNumber{255, 0, 0}, TextOutline{0, 0, 0}, TextTitle{255, 255, 0}, TextInfo{240, 230, 140},
        TextTitleHeader{154, 205, 50}, TextCyan{0, 255, 255}, TextDarkOrange{255, 140, 0},
        TextGreenYellow{173, 255, 47}, TextIndianRed{205, 92, 92};
    Color MemoryAddressFull{80, 160, 255}, MemoryAddressZero{80, 160, 255, 22}, MemoryAddressZeroOutline{0, 0, 0, 22},
        MemoryByteColor{40, 80, 160}, MemoryAsciiColor{200, 200, 200}, MemoryAsciiNonAsciiColor{139, 69, 19};
    Color DisassemblerAddressFull{80, 160, 255}, DisassemblerAddressZero{80, 160, 255, 22},
        DisassemblerAddressZeroOutline{0, 0, 0, 22}, DisassemblerOpcodeColor{40, 80, 160},
        DisassemblerInstructionColor{255, 180, 50}, DisassemblerHoverBorder{80, 220, 80},
        DisassemblerCurrentInstructionBackground{0, 139, 139, 100};
    Color RegisterNameColor{127, 127, 255}, RegisterValueChangedColor{255, 140, 0},
        RegisterValueUnchangedColor{255, 255, 255}, NumberLeadingZeroes{20, 40, 64, 1},
        RegisterMemoryDataColor{40, 80, 160}, RegisterChangedStateColor{255, 69, 0},
        RegisterUnchangedStateColor{255, 255, 255};
    Color FlagNameColor{127, 127, 255}, FlagValueChangedColor{255, 140, 0}, FlagValueUnchangedColor{255, 255, 255},
        FlagValueOneColor{0, 128, 0}, FlagValueZeroColor{255, 0, 0};
    Color StackValueColor{127, 255, 212}, StackLastValueColor{64, 128, 160};
    Color TaskbarBackground{10, 10, 20, 220}, TaskbarBorder{80, 80, 120}, TaskbarHeaderBackground{30, 30, 60},
        TaskbarItemActiveBackground{80, 80, 140}, TaskbarItemMinimizedBackground{20, 20, 20},
        TaskbarItemNormalBackground{40, 40, 60};
    Color Background{0, 0, 0}, BackgroundTransparent{0, 0, 0, 0};
};
extern Theme theme;   // ServiceGraphics.Theme
void loadTheme(const std::filesystem::path& json);   // ThemeLoader.Load
bool parseColor(std::string_view s, Color& out);       // ColorParser.Parse (false where the C# threw)

// --- Drawing (SpriteBatch with BlendState.AlphaBlend: colours are premultiplied by the GPU blend) --
inline SDL_Renderer* renderer() { return host.renderer; }
inline int screenW() { return host.w; }   // GraphicsDevice.Viewport / Window.ClientBounds size, in pixels
inline int screenH() { return host.h; }
void fill(const Rect& r, Color c);   // spriteBatch.Draw(pixel, rect, color)
// ServiceGraphics.DrawText
inline void DrawText(const Font& f, std::string_view text, int x, int y, int maxWidth, Color color, Color outline,
                     uint8_t flags, uint8_t outlinePattern) {
    f.DrawString(text, float(x), float(y), color, maxWidth, flags, outline, outlinePattern);
}
void setClip(const Rect* r);   // scissor; nullptr turns it off

// --- Windows (UI/Window.cs) -------------------------------------------------------------------
class WindowManager;

class Window {
public:
    static constexpr int SnapGap = 4, SnapDetectDistance = 8;
    static constexpr int TitleBarHeight = 24, BorderWidth = 1, MinWidth = 100, MaxWidth = 1920, MinHeight = 80,
                         MaxHeight = 1200, Padding = 8, ResizeGripSize = 12;

    Window(std::string title, int x, int y, int width, int height, float spawnDelaySeconds = 0,
           bool canResize = true, bool canClose = false);
    virtual ~Window() = default;

    WindowManager* Manager = nullptr;
    std::string Title;
    int X, Y, Width, Height;
    bool Visible = true, RefreshRequired = true, IsFocused = false, IsOnTop = false;

    void ApplyLayout(int x, int y, int width, int height, bool visible);   // no spawn animation
    Rect Bounds() const { return {X, Y, Width, Height}; }
    Rect TitleBarRect() const { return {X, Y, Width, TitleBarHeight}; }
    Rect ContentRect() const { return {X + BorderWidth, Y + TitleBarHeight, Width - BorderWidth * 2, Height - TitleBarHeight - BorderWidth}; }
    Rect ResizeGripRect() const { return {X + Width - ResizeGripSize, Y + Height - ResizeGripSize, ResizeGripSize, ResizeGripSize}; }

    virtual bool HandleInput(const MouseState& mouse, const MouseState& prevMouse);   // true: captured the mouse
    virtual void Update(float dt);
    void Draw();
    virtual void UpdateCursor(const MouseState& mouse);

    // Hooks the C# hard-coded per window type in WindowManager / ServiceGraphics / ServiceLayoutManager:
    virtual Window* HoverPopup() { return nullptr; }   // a visible popup gets input first and is drawn on top
    virtual void DrawOverlay() {}                      // after everything (MemoryWindow.DrawHoverPopup's 2nd pass)
    virtual const char* LayoutTextKey() const { return nullptr; }   // "ImmediateText" / "WatcherText" in layout.json
    virtual std::string GetText() const { return {}; }
    virtual void SetText(const std::string&) {}
    virtual void Tick() {}   // every frame, visible or not, in service mode or not (the WATCHER's CPU-side check)

protected:
    virtual bool ShouldDrawBackground() const { return true; }
    virtual void DrawChrome();
    virtual void OnResized() { RefreshRequired = true; }
    virtual void UpdateContent(float) {}
    virtual void DrawContent(const Rect& contentRect) = 0;
    void DrawContentClipped(const Rect& contentRect);

private:
    enum class Hit { None, Left, Right, Top, Bottom, BottomRight };
    Hit GetResizeHit(int px, int py) const;
    Rect CloseButtonRect() const { return {X + Width - 20, Y + 4, 16, 16}; }
    void ClampToScreen();
    void SnapToOtherWindows();
    void SnapResizeToOtherWindows(int& newX, int& newY, int& newWidth, int& newHeight, bool fromLeft, bool fromRight,
                                  bool fromTop, bool fromBottom);
    Cursor CursorForResizeFlags() const;

    float spawnTime_ = 0, spawnDuration_ = 1, spawnDelay_ = 0, spawnFade_ = 0, focusBlend_ = 0;
    int initialTargetHeight_;
    bool isSpawning_ = true, canResize_, canClose_;
    bool isDragging_ = false, dragChanged_ = false, isResizing_ = false;
    int dragOffX_ = 0, dragOffY_ = 0;
    int resizeStartMouseX_ = 0, resizeStartMouseY_ = 0, resizeStartW_ = 0, resizeStartH_ = 0, resizeStartX_ = 0,
        resizeStartY_ = 0, resizeStartRight_ = 0, resizeStartBottom_ = 0;
    bool fromLeft_ = false, fromRight_ = false, fromTop_ = false, fromBottom_ = false;
};

// UI/TextWindow.cs: the taskbar's "[ Settings ]" test window.
class TextWindow : public Window {
public:
    TextWindow(std::string title, std::string text, int x, int y, int w, int h, float delay, bool canResize, bool canClose)
        : Window(std::move(title), x, y, w, h, delay, canResize, canClose), text_(std::move(text)) {}

protected:
    void DrawContent(const Rect& contentRect) override;

private:
    std::string text_;
};

class WindowManager;

// UI/Taskbar.cs: slides in from the right edge; lists every window, a click shows or minimises it.
class Taskbar {
public:
    explicit Taskbar(WindowManager& m) : manager_(m) {}
    bool Update(float dt, const MouseState& mouse, const MouseState& prevMouse);   // true: captured the click
    void Draw();

private:
    static constexpr int BarWidth = 200, HeaderHeight = 40, ItemHeight = 28, ItemSpacing = 2;
    Rect barRect(int screenW, int screenH) const { int w = int(BarWidth * open_); return {screenW - w, 0, w, screenH}; }
    void SpawnDummyWindow();
    WindowManager& manager_;
    float open_ = 0;
    int scrollOffset_ = 0, contentHeight_ = 0;
};

// UI/WindowManager.cs. Windows are drawn in list order: the last one is on top.
class WindowManager {
public:
    WindowManager() : taskbar(*this) {}
    std::vector<Window*> Windows;
    Taskbar taskbar;
    void Add(std::unique_ptr<Window> w);   // keeps ownership: a closed window stays alive (it may be focused)
    void Remove(Window* w);
    void BringToFront(Window* w);
    void HandleInput(const MouseState& mouse, const MouseState& prevMouse);
    void Update(float dt);
    void Draw();

private:
    std::vector<std::unique_ptr<Window>> owned_;
    Window* focused_ = nullptr;
};

// --- Registration (ServiceGraphics.Initialize) --------------------------------------------------
// A tool window file registers a factory; windows are created in `order` (the reference's creation
// order, which is the initial z-order and the taskbar order). See docs/SERVICE.md for the numbers.
using WindowFactory = std::unique_ptr<Window> (*)();
bool registerWindow(int order, WindowFactory make);
#define C93_SERVICE_WINDOW(order, Type) \
    static const bool c93Registered##Type = ::c93::svc::registerWindow(order, [] { return std::unique_ptr<::c93::svc::Window>(new Type()); })

// --- Shared state --------------------------------------------------------------------------------
inline Machine& machine() { return *host.machine; }   // read freely; the CPU thread may be running
void saveLayout();        // ServiceLayoutManager.Save
bool serviceMode();        // Service.STATE.ServiceMode
bool stepByStep();         // DebugState.StepByStep

} // namespace c93::svc
