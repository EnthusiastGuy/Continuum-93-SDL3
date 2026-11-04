// UI/FlagWindow.cs: the eight flags and their negations, green when the condition holds.
#include "service/state.h"
#include "service/ui.h"

namespace c93::svc {
namespace {

class FlagWindow : public Window {
public:
    FlagWindow() : Window("FLAGS", 1160, 340, 300, 200, 0.3f, true, false) {}

protected:
    void UpdateContent(float) override { cpuState.Update(); }
    void DrawContent(const Rect& c) override {
        static const char* const names[16] = {"NZ", "NC", "SP", "NO", "PE", "NE", "LTE", "GTE", "Z", "C", "SN", "OV", "PO", "EQ", "GT", "LT"};
        const int charWidth = 13, lineHeight = theme.PrimaryFont->GlyphCellHeight + 1;
        uint8_t flags = cpuState.Flags;
        for (int i = 0; i < 8; ++i) {
            int x = c.X + Padding + i * int(charWidth * 4.5f), y = c.Y + Padding;
            bool v = CPUState::GetBitValue(flags, i);
            DrawText(*theme.PrimaryFont, names[8 + i], x, y, c.Width - Padding * 2, v ? theme.FlagValueOneColor : theme.FlagValueZeroColor,
                     theme.TextOutline, Monospace | DrawOutline, 0xFF);
            DrawText(*theme.PrimaryFont, names[i], x, y + lineHeight, c.Width - Padding * 2, v ? theme.FlagValueZeroColor : theme.FlagValueOneColor,
                     theme.TextOutline, Monospace | DrawOutline, 0xFF);
        }
    }
};

} // namespace

C93_SERVICE_WINDOW(50, FlagWindow);

} // namespace c93::svc
