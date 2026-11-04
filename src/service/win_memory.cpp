// UI/MemoryWindow.cs + UI/MemoryHoverPopup.cs + Parsers/Memory.cs: a hex/ASCII dump of 18 lines of
// 16 bytes, wheel to scroll (Shift: x10), hover a byte for its address and value in a popup.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>

#include "machine.h"
#include "service/ui.h"

namespace c93::svc {
namespace {

// Parsers/Memory.cs
struct MemLine {
    int Address = 0;
    std::string TextAddress;   // "X6"
    uint8_t Bytes[16]{};       // HexBytes "BB BB ... " split back into bytes
};
struct MemoryParser {
    int Address = 0x000000, FetchLines = 18;
    std::vector<MemLine> Lines;
    void Update() {
        Machine& m = machine();
        bool track = m.trackingActivity();
        Lines.clear();
        for (int i = 0; i < FetchLines * 16; i += 16) {
            MemLine l;
            l.Address = Address + i;
            char t[16];
            std::snprintf(t, sizeof t, "%06X", unsigned(l.Address));
            l.TextAddress = t;
            for (int j = 0; j < 16 && Address + i + j < 0x1000000; ++j) {
                uint32_t a = uint32_t(Address + i + j);
                if (track) m.markActivity(a, 1, Machine::kReadFlag);   // read through Get8bitFromRAM: the map sees it
                l.Bytes[j] = m.ram[a];
            }
            Lines.push_back(std::move(l));
        }
    }
};
MemoryParser Memory;

std::string hex2(uint8_t b) { char t[3]; std::snprintf(t, sizeof t, "%02X", b); return t; }

class MemoryHoverPopup : public Window {
public:
    MemoryHoverPopup(int x, int y, int address, uint8_t value) : Window("", x, y, 300, 150, 0, false, false), address_(address), value_(value) {
        IsOnTop = true;
        Height = 150;   // no spawn animation
    }
    void Update(float dt) override { if (Visible) UpdateContent(dt); }
    void UpdateData(int address, uint8_t value) { address_ = address; value_ = value; }
    bool HandleInput(const MouseState& mouse, const MouseState&) override {
        return Visible && Bounds().Contains(mouse.X, mouse.Y);   // blocks the windows underneath
    }

protected:
    void DrawChrome() override {   // no title bar: background and a 1 px border
        Rect b = Bounds();
        fill(b, theme.WindowBackgroundFocused);
        fill({b.X, b.Y, b.Width, BorderWidth}, theme.WindowBorder);
        fill({b.X, b.Bottom() - BorderWidth, b.Width, BorderWidth}, theme.WindowBorder);
        fill({b.X, b.Y, BorderWidth, b.Height}, theme.WindowBorder);
        fill({b.Right() - BorderWidth, b.Y, BorderWidth, b.Height}, theme.WindowBorder);
    }
    // The C#'s `new ContentRect` (no title bar) hid a non-virtual property: Draw still passes the base one.
    void DrawContent(const Rect& c) override {
        uint8_t flags = Monospace | DrawOutline;
        int y = c.Y + Padding;
        char t[64];
        auto line = [&](const char* s, Color col) {
            DrawText(*theme.PrimaryFont, s, c.X + Padding, y, c.Width - Padding * 2, col, theme.TextOutline, flags, 0xFF);
            y += 18;
        };
        std::snprintf(t, sizeof t, "Address: 0x%06X", unsigned(address_)); line(t, theme.TextPrimary);
        std::snprintf(t, sizeof t, "Hex:     0x%02X", value_); line(t, theme.TextPrimary);
        char bits[9];
        for (int i = 0; i < 8; ++i) bits[i] = char('0' + (value_ >> (7 - i) & 1));
        bits[8] = 0;
        std::snprintf(t, sizeof t, "Binary:  %sb", bits); line(t, theme.TextPrimary);
        std::snprintf(t, sizeof t, "Decimal: %u", value_); line(t, theme.TextPrimary);
        bool isAscii = value_ >= 32 && value_ <= 127;
        std::snprintf(t, sizeof t, "ASCII:   '%c'", isAscii ? char(value_) : '?');
        line(t, isAscii ? theme.MemoryAsciiColor : theme.MemoryAsciiNonAsciiColor);
    }

private:
    int address_;
    uint8_t value_;
};

class MemoryWindow : public Window {
public:
    MemoryWindow() : Window("MEMORY", 16, 440, 600, 300, 0.5f, true, false) {}
    Window* HoverPopup() override { return popup_.get(); }
    // ServiceGraphics drew MemoryWindow.DrawHoverPopup() again after every window (and whether or not this
    // window is visible): the popup's translucent background is drawn twice.
    void DrawOverlay() override { if (popup_ && popup_->Visible) popup_->Draw(); }

protected:
    void UpdateContent(float dt) override {
        Memory.Update();
        const MouseState& mouse = svc::mouse();
        if (ContentRect().Contains(mouse.X, mouse.Y)) {
            int scrollDelta = mouse.ScrollWheelValue - previousScrollValue_;
            if (scrollDelta != 0) {
                int jumpDistance = (keyDown(SDLK_LSHIFT) ? 10 : 1) * 16;
                if (scrollDelta > 0) Memory.Address = Memory.Address >= jumpDistance ? Memory.Address - jumpDistance : 0;
                else if (Memory.Address <= 0xFFFFFF - (jumpDistance + Memory.FetchLines * 16)) Memory.Address += jumpDistance;
                else Memory.Address = 0xFFFFFF - Memory.FetchLines * 16;
                Memory.Update();
            }
            previousScrollValue_ = mouse.ScrollWheelValue;   // only while inside, as there
        }
        UpdateHoverPopup(dt, mouse.X, mouse.Y);
        if (popup_ && popup_->Visible) popup_->Update(dt);
    }

    void DrawContent(const Rect& c) override {
        const int lineHeight = 18, ColumnSpacing = 20;
        const Font& f = *theme.PrimaryFont;
        int startY = c.Y + Padding;
        auto& lines = Memory.Lines;
        int visibleLines = std::min(int(lines.size()), (c.Height - (startY - c.Y)) / lineHeight);
        uint8_t flags = Monospace | DrawOutline;
        const MouseState& m = mouse();
        int charWidth = f.MeasureText("M", 0, flags).width;
        int spaceWidth = f.MeasureText(" ", 0, flags).width;

        hoveredLine_ = hoveredByte_ = -1;
        if (c.Contains(m.X, m.Y)) {
            int localY = m.Y - startY;
            if (localY >= 0) {
                hoveredLine_ = localY / lineHeight;   // kept even past the last line (no byte then), as there
                if (hoveredLine_ < visibleLines) {
                    const MemLine& l = lines[hoveredLine_];
                    int localX = m.X - (c.X + Padding + f.MeasureText(l.TextAddress, c.Width - Padding * 2, flags).width + ColumnSpacing);
                    if (localX >= 0) {
                        int currentX = 0;
                        for (int j = 0; j < 16; ++j) {
                            int byteWidth = f.MeasureText(hex2(l.Bytes[j]), 0, flags).width;
                            if (localX >= currentX && localX < currentX + byteWidth) { hoveredByte_ = j; break; }
                            currentX += byteWidth;
                            if (j < 15) currentX += spaceWidth;
                        }
                    }
                }
            }
        }

        for (int i = 0; i < visibleLines; ++i) {
            int y = startY + i * lineHeight;
            if (y + lineHeight > c.Bottom()) break;
            const MemLine& l = lines[i];
            const std::string& address = l.TextAddress;
            bool nonZero = false;
            for (size_t k = 0; k < address.size(); ++k) {   // leading zeros faded
                if (address[k] != '0') nonZero = true;
                Color cc = !nonZero && address[k] == '0' ? theme.NumberLeadingZeroes : theme.TextPrimary;
                DrawText(f, std::string(1, address[k]), c.X + Padding + int(k) * charWidth, y, c.Width - Padding * 2, cc, theme.TextOutline, flags, 0xFF);
            }
            int addressWidth = f.MeasureText(address, c.Width - Padding * 2, flags).width;
            int hexColumnX = c.X + Padding + addressWidth + ColumnSpacing;
            int hexBytesWidth = 0;
            for (int j = 0; j < 16; ++j) hexBytesWidth += f.MeasureText(hex2(l.Bytes[j]), 0, flags).width + (j < 15 ? spaceWidth : 0);
            int x = hexColumnX;
            for (int j = 0; j < 16; ++j) {
                bool hovered = i == hoveredLine_ && j == hoveredByte_;
                std::string h = hex2(l.Bytes[j]);
                DrawText(f, h, x, y, c.Width - Padding * 2, hovered ? theme.TextHighlight : theme.MemoryByteColor, theme.TextOutline, flags, 0xFF);
                if (j < 15) x += f.MeasureText(h, 0, flags).width + spaceWidth;
            }
            x = hexColumnX + hexBytesWidth + ColumnSpacing;
            for (int j = 0; j < 16; ++j) {
                bool hovered = i == hoveredLine_ && j == hoveredByte_;
                uint8_t b = l.Bytes[j];
                bool isAscii = b >= 32 && b <= 127;
                Color col = hovered ? theme.TextHighlight : isAscii ? theme.MemoryAsciiColor : theme.MemoryAsciiNonAsciiColor;
                DrawText(f, std::string(1, isAscii ? char(b) : '?'), x, y, c.Width - Padding * 2, col, theme.TextOutline, flags, 0xFF);
                x += charWidth;
            }
        }
    }

private:
    void UpdateHoverPopup(float dt, int mx, int my) {
        if (!Visible) {
            if (popup_ && popup_->Visible) popup_->Visible = false;
            hoverTimer_ = 0;
            return;
        }
        bool overPopup = popup_ && popup_->Visible && popup_->Bounds().Contains(mx, my);
        if (hoveredLine_ != prevLine_ || hoveredByte_ != prevByte_) { hoverTimer_ = 0; prevLine_ = hoveredLine_; prevByte_ = hoveredByte_; }
        if (hoveredLine_ >= 0 && hoveredByte_ >= 0 && !overPopup) {
            hoverTimer_ += dt;
            if (hoverTimer_ >= 0.3f) {
                if (!popup_ || !popup_->Visible) ShowHoverPopup();
                else if (hoveredLine_ < int(Memory.Lines.size()))   // UpdateHoverPopupData
                    popup_->UpdateData(Memory.Lines[hoveredLine_].Address + hoveredByte_, Memory.Lines[hoveredLine_].Bytes[hoveredByte_]);
            }
        } else if (!overPopup) {
            if (popup_ && popup_->Visible) popup_->Visible = false;
            hoverTimer_ = 0;
        }
    }
    void ShowHoverPopup() {
        if (hoveredLine_ < 0 || hoveredByte_ < 0 || hoveredLine_ >= int(Memory.Lines.size())) return;
        const MemLine& l = Memory.Lines[hoveredLine_];
        const Font& f = *theme.PrimaryFont;
        uint8_t flags = Monospace | DrawOutline;
        Rect c = ContentRect();
        int byteY = c.Y + Padding + hoveredLine_ * 18;
        int byteX = c.X + Padding + f.MeasureText(l.TextAddress, c.Width - Padding * 2, flags).width + 20;
        for (int j = 0; j < hoveredByte_; ++j) byteX += f.MeasureText(hex2(l.Bytes[j]), 0, flags).width + (j < 15 ? f.MeasureText(" ", 0, flags).width : 0);
        int popupX = byteX + 20, popupY = byteY;
        const int popupWidth = 200, popupHeight = 100;   // not the popup's real 300x150, as there
        if (popupX + popupWidth > screenW()) popupX = byteX - popupWidth - 10;
        if (popupY + popupHeight > screenH()) popupY = screenH() - popupHeight - 10;
        int address = l.Address + hoveredByte_;
        uint8_t value = l.Bytes[hoveredByte_];
        if (!popup_) popup_ = std::make_unique<MemoryHoverPopup>(popupX, popupY, address, value);
        else { popup_->X = popupX; popup_->Y = popupY; popup_->UpdateData(address, value); popup_->Visible = true; }
    }

    std::unique_ptr<MemoryHoverPopup> popup_;
    int previousScrollValue_ = 0;
    int hoveredLine_ = -1, hoveredByte_ = -1, prevLine_ = -1, prevByte_ = -1;
    float hoverTimer_ = 0;
};

} // namespace

C93_SERVICE_WINDOW(70, MemoryWindow);

} // namespace c93::svc
