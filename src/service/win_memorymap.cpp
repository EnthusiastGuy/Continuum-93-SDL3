// UI/MemoryMapWindow.cs + UI/MemoryMapHoverPopup.cs: all of RAM as 8x8 bricks (grey = the bytes' average),
// pages read flash green and written red, fading over 0.5 s while the CPU runs. Wheel zooms (bytes per
// brick), Ctrl+wheel or the bar scrolls, hover a brick for its bytes. Tracking: Machine::trackActivity (D-26).
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "machine.h"
#include "service/ui.h"

namespace c93::svc {
namespace {

constexpr uint32_t kRamSizeRef = 0x1000003;   // MEMC.RAM.Size: 16 MB + 3
// The C# wrote `0x1000000d` for "16 MB as a double"; in a hex literal the d is a digit: 0x1000000D, about
// 16x the RAM. Bricks hold 16x the bytes the window has room for and MapRows counts 16x the bricks. Kept.
constexpr double kHexD = 0x1000000D;

class MemoryMapHoverPopup : public Window {
public:
    MemoryMapHoverPopup(int x, int y, uint32_t startAddress, int byteCount, const uint8_t* data)
        : Window("", x, y, 360, 280, 0, false, false) {
        IsOnTop = true;
        Height = 280;
        Width = 360;
        UpdateData(startAddress, byteCount, data);
    }
    void Update(float dt) override {
        if (!Visible) return;
        int delta = mouse().ScrollWheelValue - previousScroll_;   // wherever the mouse is
        if (delta != 0) scrollOffset_ = std::clamp(scrollOffset_ + (delta > 0 ? -1 : 1), 0, std::max(0, byteCount_ - 1));
        previousScroll_ = mouse().ScrollWheelValue;
        UpdateContent(dt);
    }
    void UpdateData(uint32_t startAddress, int byteCount, const uint8_t* data) {
        startAddress_ = startAddress;
        byteCount_ = std::max(0, byteCount);
        scrollOffset_ = std::clamp(scrollOffset_, 0, std::max(0, byteCount_ - 1));
        data_.assign(data, data + byteCount_);   // MEMC.DumpMemAt: a snapshot
        uint32_t end = byteCount_ > 0 ? std::min(0xFFFFFFu, startAddress_ + uint32_t(std::max(0, byteCount_ - 1))) : startAddress_;
        char t[40];
        std::snprintf(t, sizeof t, "0x%06X -> 0x%06X", startAddress_, end);
        titleText_ = t;
    }
    bool HandleInput(const MouseState& mouse, const MouseState&) override { return Visible && Bounds().Contains(mouse.X, mouse.Y); }

protected:
    void DrawChrome() override {
        Rect b = Bounds();
        fill(b, theme.WindowBackgroundFocused);
        Rect title{b.X + BorderWidth, b.Y + BorderWidth, b.Width - BorderWidth * 2, TitleBarHeight};
        fill(title, theme.WindowTitleBarFocused);
        DrawText(*theme.PrimaryFont, titleText_, title.X + Padding, title.Y + 2, title.Width - Padding * 2, theme.WindowTitleText,
                 theme.TextOutline, DrawOutline, 0xFF);
        fill({b.X, b.Y, b.Width, BorderWidth}, theme.WindowBorder);
        fill({b.X, b.Bottom() - BorderWidth, b.Width, BorderWidth}, theme.WindowBorder);
        fill({b.X, b.Y, BorderWidth, b.Height}, theme.WindowBorder);
        fill({b.Right() - BorderWidth, b.Y, BorderWidth, b.Height}, theme.WindowBorder);
    }
    void DrawContent(const Rect& c) override {
        const Font& font = fonts.ModernDOS_9x15;
        uint8_t flags = Monospace | DrawOutline;
        int y = c.Y + Padding;
        char t[64];
        auto line = [&](const char* s, Color col) {
            DrawText(font, s, c.X + Padding, y, c.Width - Padding * 2, col, theme.TextOutline, flags, 0xFF);
            y += 16;
        };
        std::snprintf(t, sizeof t, "Start: 0x%06X (%u)", startAddress_, startAddress_); line(t, theme.TextPrimary);
        std::snprintf(t, sizeof t, "Bytes: %d", byteCount_); line(t, theme.TextPrimary);
        line("Values:", theme.TextSecondary);
        int linesDrawn = 0;
        for (int i = 0; i < ItemsPerPage; ++i) {
            int index = scrollOffset_ + i;
            if (index >= byteCount_ || index >= int(data_.size())) break;
            uint8_t v = data_[index];
            std::snprintf(t, sizeof t, "%06X : %02X (%u)", startAddress_ + uint32_t(index), v, v);
            line(t, theme.TextPrimary);
            ++linesDrawn;
        }
        if (linesDrawn == 0) line("(no data)", theme.TextInfo);
    }

private:
    static constexpr int ItemsPerPage = 10;
    uint32_t startAddress_ = 0;
    int byteCount_ = 0, scrollOffset_ = 0, previousScroll_ = 0;
    std::vector<uint8_t> data_;
    std::string titleText_;
};

class MemoryMapWindow : public Window {
public:
    MemoryMapWindow() : Window("MEMORY MAP", 1480, 560, 400, 320, 0.55f, true, false) {}
    Window* HoverPopup() override { return popup_.get(); }
    void DrawOverlay() override { if (popup_ && popup_->Visible) popup_->Draw(); }   // ServiceGraphics' second pass

    bool HandleInput(const MouseState& mouse, const MouseState& prevMouse) override {
        if (!Visible) return false;
        if (Window::HandleInput(mouse, prevMouse)) return true;
        return Bounds().Contains(mouse.X, mouse.Y);   // consume: nothing bubbles to the windows under
    }

protected:
    void UpdateContent(float dt) override {
        const MouseState& m = mouse();
        CalculateLayout(ContentRect());
        HandleZoom(m);
        HandleScrollbars(m);
        ClampScroll();
        pollAccumulator_ += dt;
        if (pollAccumulator_ >= PollIntervalSeconds) {
            RefreshActivity(pollAccumulator_);
            RefreshVisibleBricks();
            pollAccumulator_ = 0;
        }
        HandleClearButton(m);
        UpdateHover(m.X, m.Y, dt);
        previousMouse_ = m;
        previousWheel_ = m.ScrollWheelValue;
    }

    void DrawContent(const Rect&) override {
        const Color red{255, 0, 0}, limeGreen{50, 205, 50}, shadow{0, 60, 0};
        for (int i = 0; i < int(bricks_.size()); ++i) {
            const BrickVisual& b = bricks_[i];
            Color base{b.Average, b.Average, b.Average}, col = base;
            if (b.WriteIntensity > 0) col = lerp(base, red, clampf(b.WriteIntensity, 0, 1));
            else if (b.ReadIntensity > 0) col = lerp(base, limeGreen, clampf(b.ReadIntensity, 0, 1));
            fill(b.R, col);
            fill({b.R.Right() - 1, b.R.Y, 1, b.R.Height}, shadow);
            fill({b.R.X, b.R.Bottom() - 1, b.R.Width, 1}, shadow);
            if (i == hoveredIndex_) fill({b.R.X - 1, b.R.Y - 1, b.R.Width + 2, b.R.Height + 2}, {0, 120, 0, 80});
        }
        if (layout_.ShowVScroll) {   // DrawScrollbars
            fill(layout_.VScrollRect, theme.TaskbarItemNormalBackground);
            fill(layout_.VThumbRect, theme.TaskbarItemActiveBackground);
        }
        DrawControlBar();
    }

private:
    static constexpr int BrickSize = 8, BrickSpacing = 2, ControlBarHeight = 36, ScrollbarThickness = 12, MinZoomSteps = -6;
    static constexpr float HoverDelay = 0.3f, PollIntervalSeconds = 1.0f / 30.0f, ActivityFadeSeconds = 0.5f;

    struct BrickVisual {
        Rect R;
        uint32_t StartAddress = 0;
        int ByteCount = 0;
        uint8_t Average = 0;
        float ReadIntensity = 0, WriteIntensity = 0;
    };
    struct LayoutState {
        int BrickStride = 0, BrickCapacity = 0, MapColumns = 0, MapRows = 0, VisibleColumns = 0, VisibleRows = 0, ScrollRow = 0;
        Rect AreaRect, ControlRect;
        bool ShowVScroll = false;
        Rect VScrollRect, VThumbRect, ClearButtonRect;
    };

    void CalculateLayout(const Rect& c) {
        int pad = Padding;
        int availableWidth = std::max(1, c.Width - pad * 2), availableHeight = std::max(1, c.Height - pad * 2 - ControlBarHeight);
        LayoutState l;
        l.BrickStride = BrickSize + BrickSpacing;
        ComputeLayoutForArea(l, availableWidth, availableHeight);   // only a vertical bar: narrow the area when needed
        bool needV = l.MapRows > l.VisibleRows;
        int adjustedWidth = availableWidth - (needV ? ScrollbarThickness : 0), adjustedHeight = availableHeight;
        ComputeLayoutForArea(l, adjustedWidth, adjustedHeight);
        int totalBricksData = l.BrickCapacity > 0 ? int(std::ceil(kRamSizeRef / double(l.BrickCapacity))) : 0;
        int totalRowsData = l.MapColumns > 0 ? int(std::ceil(totalBricksData / double(l.MapColumns))) : 0;
        int maxScrollRows = std::max(0, totalRowsData - l.VisibleRows);
        // Math.Round: ties to even, which nearbyint does in the default rounding mode.
        l.ScrollRow = maxScrollRows == 0 ? 0 : int(std::nearbyint(double(clampf(scrollY_, 0, 1) * float(maxScrollRows))));
        l.ShowVScroll = l.MapRows > l.VisibleRows;
        l.AreaRect = {c.X + pad, c.Y + pad, adjustedWidth, adjustedHeight};
        l.ControlRect = {c.X + pad, c.Bottom() - ControlBarHeight, c.Width - pad * 2, ControlBarHeight};
        if (l.ShowVScroll) {
            l.VScrollRect = {l.AreaRect.Right(), l.AreaRect.Y, ScrollbarThickness, l.AreaRect.Height};
            float ratio = l.VisibleRows / float(l.MapRows);
            int thumbHeight = std::max(8, int(l.VScrollRect.Height * ratio));
            int maxThumbTravel = std::max(1, l.VScrollRect.Height - thumbHeight);
            scrollY_ = clampf(scrollY_, 0, 1);
            l.VThumbRect = {l.VScrollRect.X, l.VScrollRect.Y + int(maxThumbTravel * scrollY_), ScrollbarThickness, thumbHeight};
        }
        const int buttonWidth = 195;   // the clear button, on the right of the control bar
        l.ClearButtonRect = {l.ControlRect.Right() - buttonWidth - pad * 4, l.ControlRect.Y + 6, buttonWidth, l.ControlRect.Height - 12};
        layout_ = l;
    }

    void ComputeLayoutForArea(LayoutState& l, int areaWidth, int areaHeight) const {
        int bricksX = std::max(1, areaWidth / l.BrickStride), bricksY = std::max(1, areaHeight / l.BrickStride);
        l.BrickCapacity = CalculateBrickCapacity(bricksX, bricksY);
        int totalBricks = int(std::ceil(kHexD / double(l.BrickCapacity)));
        l.MapColumns = bricksX;   // wraps horizontally; no horizontal scroll
        l.MapRows = (totalBricks + l.MapColumns - 1) / l.MapColumns;
        l.VisibleColumns = std::min(bricksX, l.MapColumns);
        l.VisibleRows = std::min(bricksY, l.MapRows);
    }

    int CalculateBrickCapacity(int bricksX, int bricksY) const {
        long long bricks = std::max(1, bricksX * bricksY);
        double baseBytes = std::ceil(kHexD / double(bricks));
        int capacity = NextPowerOfTwo(int(std::max(1.0, baseBytes)));
        if (zoomSteps_ > 0) capacity = std::max(1, capacity >> zoomSteps_);
        else if (zoomSteps_ < 0) capacity = std::min(0x1000000, capacity << -zoomSteps_);
        return std::max(1, capacity);
    }
    static int NextPowerOfTwo(int v) {
        --v; v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
        return v + 1;
    }

    void HandleZoom(const MouseState& m) {
        if (!ContentRect().Contains(m.X, m.Y)) return;
        int delta = m.ScrollWheelValue - previousWheel_;
        if (delta == 0) return;
        bool ctrl = keyDown(SDLK_LCTRL) || keyDown(SDLK_RCTRL);
        if (popup_ && popup_->Visible && popup_->Bounds().Contains(m.X, m.Y)) return;   // the popup's wheel
        if (ctrl) {   // Ctrl + wheel scrolls
            float step = layout_.MapRows > 0 ? layout_.VisibleRows / float(std::max(1, layout_.MapRows)) : 0.1f;
            scrollY_ = clampf(scrollY_ + (delta > 0 ? -1 : 1) * step, 0, 1);
            return;
        }
        if (delta > 0 && layout_.BrickCapacity <= 1) return;   // 1 byte per brick: no further in
        zoomSteps_ = std::max(MinZoomSteps, zoomSteps_ + (delta > 0 ? 1 : -1));
    }

    void HandleScrollbars(const MouseState& m) {
        bool leftPressed = m.Left, leftJustPressed = leftPressed && !previousMouse_.Left, leftJustReleased = !leftPressed && previousMouse_.Left;
        if (leftJustPressed && layout_.ShowVScroll && layout_.VThumbRect.Contains(m.X, m.Y)) {
            draggingV_ = true;
            dragOffsetV_ = m.Y - layout_.VThumbRect.Y;
        }
        if (draggingV_) {
            if (leftPressed) {
                int travel = layout_.VScrollRect.Height - layout_.VThumbRect.Height;
                scrollY_ = clampf((m.Y - layout_.VScrollRect.Y - dragOffsetV_) / float(std::max(1, travel)), 0, 1);
            } else if (leftJustReleased) {
                draggingV_ = false;
            }
        }
    }

    void ClampScroll() {
        int maxScrollRows = std::max(0, layout_.MapRows - layout_.VisibleRows);
        scrollY_ = maxScrollRows == 0 ? 0.0f : clampf(scrollY_, 0, 1);
    }

    void RefreshActivity(float elapsedSeconds) {
        machine().consumeActivity(activityScratch_);
        for (int i = 0; i < Machine::kPageCount; ++i) {
            if (activityScratch_[i] & Machine::kReadFlag) pageRead_[i] = 1;
            if (activityScratch_[i] & Machine::kWriteFlag) pageWrite_[i] = 1;
        }
        if (!stepByStep()) {   // stopped in the debugger, the colours hold
            float decay = elapsedSeconds / ActivityFadeSeconds;
            for (int i = 0; i < Machine::kPageCount; ++i) {
                pageRead_[i] = std::max(0.0f, pageRead_[i] - decay);
                pageWrite_[i] = std::max(0.0f, pageWrite_[i] - decay);
            }
        }
    }

    void RefreshVisibleBricks() {
        bricks_.clear();
        if (layout_.BrickCapacity <= 0) return;
        const uint8_t* ram = machine().ram;
        for (int row = 0; row < layout_.VisibleRows; ++row)
            for (int col = 0; col < layout_.VisibleColumns; ++col) {
                int globalIndex = (layout_.ScrollRow + row) * layout_.MapColumns + col;
                uint32_t start = uint32_t(globalIndex * layout_.BrickCapacity);
                if (start >= kRamSizeRef) continue;
                int count = int(std::min<uint32_t>(uint32_t(layout_.BrickCapacity), kRamSizeRef - start));
                BrickVisual b;
                b.R = {layout_.AreaRect.X + col * layout_.BrickStride, layout_.AreaRect.Y + row * layout_.BrickStride, BrickSize, BrickSize};
                b.StartAddress = start;
                b.ByteCount = count;
                uint64_t sum = 0;   // ComputeAverage
                for (uint32_t i = start, e = start + uint32_t(count); i < e; ++i) sum += ram[i];
                b.Average = uint8_t(sum / uint32_t(count));
                b.ReadIntensity = MaxIntensity(pageRead_, start, count);
                b.WriteIntensity = MaxIntensity(pageWrite_, start, count);
                bricks_.push_back(b);
            }
    }
    static float MaxIntensity(const float* v, uint32_t start, int count) {
        if (count <= 0) return 0;
        uint32_t first = start >> Machine::kPageShift, last = (start + uint32_t(count - 1)) >> Machine::kPageShift;
        float mx = 0;
        for (uint32_t p = first; p <= last && p < uint32_t(Machine::kPageCount); ++p) mx = std::max(mx, v[p]);
        return mx;
    }

    void UpdateHover(int mx, int my, float dt) {
        bool overPopup = popup_ && popup_->Visible && popup_->Bounds().Contains(mx, my);
        if (!layout_.AreaRect.Contains(mx, my) && !overPopup) {
            if (popup_) popup_->Visible = false;
            hoveredIndex_ = -1;
            hoverTimer_ = 0;
            return;
        }
        int hovered = -1;
        for (int i = 0; i < int(bricks_.size()); ++i) if (bricks_[i].R.Contains(mx, my)) { hovered = i; break; }
        if (hovered != previousHoveredIndex_) { hoverTimer_ = 0; previousHoveredIndex_ = hovered; }
        hoveredIndex_ = hovered;
        if (hoveredIndex_ >= 0 && !overPopup) {
            hoverTimer_ += dt;
            if (hoverTimer_ >= HoverDelay) ShowHoverPopup(bricks_[hoveredIndex_]);   // every frame: fresh bytes
        } else if (!overPopup) {
            if (popup_) popup_->Visible = false;
            hoverTimer_ = 0;
        }
        if (popup_ && popup_->Visible) popup_->Update(0);
    }

    void ShowHoverPopup(const BrickVisual& b) {
        const uint8_t* data = machine().ram + b.StartAddress;
        const int offset = 16, popupWidth = 280, popupHeight = 260;   // not the popup's real 360x280, as there
        int popupX = b.R.Right() + offset, popupY = b.R.Y;
        if (popupX + popupWidth > screenW()) popupX = b.R.Left() - popupWidth - offset;
        if (popupY + popupHeight > screenH()) popupY = screenH() - popupHeight - offset;
        if (!popup_) popup_ = std::make_unique<MemoryMapHoverPopup>(popupX, popupY, b.StartAddress, b.ByteCount, data);
        else { popup_->X = popupX; popup_->Y = popupY; popup_->UpdateData(b.StartAddress, b.ByteCount, data); popup_->Visible = true; }
    }

    void HandleClearButton(const MouseState& m) {
        if (m.Left && !previousMouse_.Left && layout_.ClearButtonRect.Contains(m.X, m.Y)) {   // ClearActivity
            std::fill(std::begin(pageRead_), std::end(pageRead_), 0.0f);
            std::fill(std::begin(pageWrite_), std::end(pageWrite_), 0.0f);
            machine().clearActivity();
        }
    }

    void DrawControlBar() {
        const LayoutState& l = layout_;
        fill(l.ControlRect, theme.WindowBackgroundFocused);
        uint8_t flags = Monospace | DrawOutline;
        int cap = l.BrickCapacity;
        char human[32], t[128];
        if (cap >= 1024 * 1024) FormatScaled(human, cap / (1024.0 * 1024.0), "MB");
        else if (cap >= 1024) FormatScaled(human, cap / 1024.0, "KB");
        else std::snprintf(human, sizeof human, "%d B", cap);
        std::snprintf(t, sizeof t, "Bytes/brick: %d (0x%X) [%s]  Zoom: %d", cap, unsigned(cap), human, zoomSteps_);
        DrawText(*theme.PrimaryFont, t, l.ControlRect.X + Padding, l.ControlRect.Y + 8, l.ControlRect.Width - Padding * 2,
                 theme.TextPrimary, theme.TextOutline, flags, 0xFF);
        const MouseState& m = mouse();
        fill(l.ClearButtonRect, l.ClearButtonRect.Contains(m.X, m.Y) ? theme.TaskbarItemActiveBackground : theme.TaskbarItemNormalBackground);
        DrawText(fonts.ModernDOS_9x15, "Clear Activity Map", l.ClearButtonRect.X + 10, l.ClearButtonRect.Y + 6, l.ClearButtonRect.Width - 20,
                 theme.TextPrimary, theme.TextOutline, flags, 0xFF);
    }
    static void FormatScaled(char* out, double v, const char* unit) {   // "{v:0.##} unit"
        char n[32];
        std::snprintf(n, sizeof n, "%.2f", v);
        char* e = n + std::strlen(n);
        while (e[-1] == '0') *--e = 0;
        if (e[-1] == '.') *--e = 0;
        std::snprintf(out, 32, "%s %s", n, unit);
    }

    std::vector<BrickVisual> bricks_;
    uint8_t activityScratch_[Machine::kPageCount]{};
    float pageRead_[Machine::kPageCount]{}, pageWrite_[Machine::kPageCount]{};
    std::unique_ptr<MemoryMapHoverPopup> popup_;
    float hoverTimer_ = 0, scrollY_ = 0, pollAccumulator_ = 0;
    int hoveredIndex_ = -1, previousHoveredIndex_ = -1, previousWheel_ = 0, zoomSteps_ = 0, dragOffsetV_ = 0;
    MouseState previousMouse_;
    bool draggingV_ = false;
    LayoutState layout_;
};

} // namespace

C93_SERVICE_WINDOW(80, MemoryMapWindow);

} // namespace c93::svc
