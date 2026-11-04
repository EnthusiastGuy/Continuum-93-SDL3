// UI/RegisterHoverPopup.cs: the hovered register view in four bases, current and previous.
#include <cstdio>

#include "service/popups.h"
#include "service/state.h"

namespace c93::svc {

RegisterHoverPopup::RegisterHoverPopup(int x, int y, int regIndex, int bitWidth, uint32_t currentValue, uint32_t previousValue)
    : Window("", x, y, 500, 250, 0, false, false) {
    IsOnTop = true;
    Height = 250;   // no spawn animation
    UpdateData(regIndex, bitWidth, currentValue, previousValue);
}

void RegisterHoverPopup::UpdateData(int regIndex, int bitWidth, uint32_t currentValue, uint32_t previousValue) {
    bitWidth_ = bitWidth;
    current_ = currentValue;
    previous_ = previousValue;
    Title = "Register: " + regName(regIndex, bitWidth) + " (" + std::to_string(bitWidth * 8) + "-bit)";
}

void RegisterHoverPopup::DrawChrome() {
    // The base chrome without the focus blend: focused background, the on-top title bar.
    Rect b = Bounds();
    if (ShouldDrawBackground()) fill(b, theme.WindowBackgroundFocused);
    fill(TitleBarRect(), IsOnTop ? theme.WindowTitleBarOnTop : theme.WindowTitleBarFocused);
    fill({b.X, b.Y, b.Width, BorderWidth}, theme.WindowBorder);
    fill({b.X, b.Bottom() - BorderWidth, b.Width, BorderWidth}, theme.WindowBorder);
    fill({b.X, b.Y, BorderWidth, b.Height}, theme.WindowBorder);
    fill({b.Right() - BorderWidth, b.Y, BorderWidth, b.Height}, theme.WindowBorder);
    DrawText(fonts.ModernDOS_12x18, Title, b.X + Padding, b.Y + 4, b.Width - Padding * 2, theme.WindowTitleText, theme.TextOutline,
             DrawOutline, 0xFF);
}

namespace {
std::string hex(uint32_t v, int digits) { char b[16]; std::snprintf(b, sizeof b, "0x%0*X", digits, unsigned(v)); return b; }
std::string octal(uint32_t v, int digits) { char b[16]; std::snprintf(b, sizeof b, "%0*o", digits, unsigned(v)); return b; }
// Convert.ToString(v, 2) padded to the width, a space between bytes from 16 bits up.
std::string binary(uint32_t v, int bitWidth) {
    std::string r;
    for (int i = bitWidth * 8 - 1; i >= 0; --i) {
        r += char('0' + (v >> i & 1));
        if (i && i % 8 == 0) r += ' ';
    }
    return r;
}
} // namespace

void RegisterHoverPopup::DrawContent(const Rect& c) {
    const Font& font = fonts.ModernDOS_9x15;
    const uint8_t flags = Monospace | DrawOutline;
    const int lineHeight = 18, octalDigits = (bitWidth_ * 8 + 2) / 3;
    int y = c.Y + Padding;
    auto line = [&](const std::string& text, Color color) {
        DrawText(font, text, c.X + Padding, y, c.Width - Padding * 2, color, theme.TextOutline, flags, 0xFF);
        y += lineHeight;
    };
    auto values = [&](uint32_t v, Color color) {
        line("  Hex:     " + hex(v, bitWidth_ * 2), color);
        line("  Decimal: " + std::to_string(v), color);
        line("  Binary:  " + binary(v, bitWidth_), color);
        line("  Octal:   " + octal(v, octalDigits), color);
    };
    line("Current:", theme.TextPrimary);
    values(current_, theme.RegisterValueChangedColor);
    y += 2;
    line("Previous:", theme.TextPrimary);
    if (current_ == previous_) line("  no change", theme.TextSecondary);
    else values(previous_, theme.TextPrimary);
}

} // namespace c93::svc
