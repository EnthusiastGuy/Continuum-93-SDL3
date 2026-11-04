// UI/FloatRegWindow.cs: F0..F15 of the current float bank, as .NET prints a float.
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "service/state.h"
#include "service/ui.h"

namespace c93::svc {
namespace {

// float.ToString() on .NET Core 3.0+ (invariant digits, en-US symbols): the shortest round-trip digits,
// fixed notation for decimal exponents -5 < e < 9, otherwise d.dddE+XX.
std::string netFloat(float v) {
    if (std::isnan(v)) return "NaN";
    if (std::isinf(v)) return v < 0 ? "-\xE2\x88\x9E" : "\xE2\x88\x9E";   // "∞": the service font skips it, as the C#'s
    std::string s = std::signbit(v) ? "-" : "";
    if (v == 0) return s + "0";
    char buf[32];
    auto r = std::to_chars(buf, buf + sizeof buf, std::fabs(v), std::chars_format::scientific);   // "1.2345e+07"
    std::string sci(buf, r.ptr), digits;
    size_t e = sci.find('e');
    for (size_t i = 0; i < e; ++i) if (sci[i] != '.') digits += sci[i];
    int exp = std::atoi(sci.c_str() + e + 1), digPos = exp + 1, n = int(digits.size());
    if (digPos > 9 || digPos < -3) {
        s += digits.substr(0, 1);
        if (n > 1) s += "." + digits.substr(1);
        char eb[8];
        std::snprintf(eb, sizeof eb, "E%c%02d", exp < 0 ? '-' : '+', std::abs(exp));
        return s + eb;
    }
    if (digPos <= 0) return s + "0." + std::string(-digPos, '0') + digits;
    if (n <= digPos) return s + digits + std::string(digPos - n, '0');
    return s + digits.substr(0, digPos) + "." + digits.substr(digPos);
}

class FloatRegWindow : public Window {
public:
    FloatRegWindow() : Window("FLOAT REGISTERS", 1160, 16, 300, 300, 0.2f, true, false) {}

protected:
    void UpdateContent(float) override { cpuState.Update(); }
    void DrawContent(const Rect& c) override {
        const int lineHeight = 18;
        DrawText(fonts.ModernDOS_12x18, "Float register view", c.X + Padding, c.Y + Padding, c.Width - Padding * 2, theme.TextTitle,
                 theme.TextOutline, DrawOutline, 0xFF);
        int startY = c.Y + Padding + 24;
        for (int i = 0; i < 16; ++i) {
            int y = startY + i * lineHeight;
            if (y + lineHeight > c.Bottom()) break;
            float v = cpuState.FRegs[i], old = cpuState.FRegsOld[i];
            bool changed = !(v == old || (std::isnan(v) && std::isnan(old)));   // float.Equals: NaN equals NaN
            DrawText(*theme.PrimaryFont, "F" + std::to_string(i) + ":", c.X + Padding, y, c.Width - Padding * 2, theme.RegisterNameColor,
                     theme.TextOutline, Monospace | DrawOutline, 0xFF);
            DrawText(*theme.PrimaryFont, netFloat(v), c.X + Padding + 60, y, c.Width - Padding * 2,
                     changed ? theme.RegisterValueChangedColor : theme.RegisterValueUnchangedColor, theme.TextOutline,
                     Monospace | DrawOutline, 0xFF);
        }
    }
};

} // namespace

C93_SERVICE_WINDOW(40, FloatRegWindow);

} // namespace c93::svc
