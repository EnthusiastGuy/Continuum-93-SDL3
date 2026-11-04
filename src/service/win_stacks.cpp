// UI/StacksWindow.cs + Parsers/Stacks.cs: the top of the register stack (bytes) and of the call stack
// (addresses), newest last in a darker blue; the wheel scrolls back through older values.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "machine.h"
#include "service/ui.h"

namespace c93::svc {
namespace {

// Parsers/Stacks.cs: up to 120 register-stack bytes and 64 call-stack cells below the stack pointers.
struct Stacks {
    std::vector<std::string> RegisterStack, CallStack;
    void Update() {
        RegisterStack.clear();
        CallStack.clear();
        Machine& m = machine();
        char b[16];
        uint32_t sp = m.spr, limit = std::min<uint32_t>(sp, 120);
        for (uint32_t i = 0; i < limit; ++i) {
            std::snprintf(b, sizeof b, "%02X", m.rs[(sp - limit + i) & (kRsSize - 1)]);
            RegisterStack.push_back(b);
        }
        sp = m.spc; limit = std::min<uint32_t>(sp, 64);
        for (uint32_t i = 0; i < limit; ++i) {
            std::snprintf(b, sizeof b, "%06X", m.cs[(sp - limit + i) & (kCsSize - 1)]);
            CallStack.push_back(b);
        }
    }
} stacks;

class StacksWindow : public Window {
public:
    StacksWindow() : Window("STACKS", 640, 540, 500, 200, 0.4f, true, false) {}

protected:
    static constexpr int MaxVisibleValues = 40;

    void UpdateContent(float) override {
        stacks.Update();
        const MouseState& m = mouse();
        if (!ContentRect().Contains(m.X, m.Y)) return;
        int lineHeight = theme.PrimaryFont->GlyphCellHeight + 1;
        int regStackAreaStart = ContentRect().Y + Padding + lineHeight, regStackAreaEnd = regStackAreaStart + MaxVisibleValues * lineHeight;
        // The previous wheel value only follows while the mouse is over an area: re-entering applies the
        // whole delta since it left, as in the reference.
        auto scroll = [&](int& offset, int& previous, int total) {
            int delta = m.ScrollWheelValue - previous;
            if (delta != 0) {
                if (total > MaxVisibleValues) {
                    int step = std::abs(delta) / 120;
                    offset = delta > 0 ? std::min(offset + step, total - MaxVisibleValues) : std::max(0, offset - step);
                } else {
                    offset = 0;
                }
            }
            previous = m.ScrollWheelValue;
        };
        if (m.Y >= regStackAreaStart && m.Y < regStackAreaEnd) {
            scroll(regScroll_, prevRegWheel_, int(stacks.RegisterStack.size()));
        } else {
            int callStackAreaStart = regStackAreaEnd + lineHeight * 2 + lineHeight;   // fixed 40 rows, not the drawn layout
            if (m.Y >= callStackAreaStart && m.Y < callStackAreaStart + MaxVisibleValues * lineHeight)
                scroll(callScroll_, prevCallWheel_, int(stacks.CallStack.size()));
        }
    }

    void DrawContent(const Rect& c) override {
        const Font& f = *theme.PrimaryFont;
        uint8_t flags = Monospace | DrawOutline;
        int lineHeight = f.GlyphCellHeight + 1, charWidth = f.MeasureText("M", 0, flags).width;
        int labelWidth = f.MeasureText("Regs", 0, flags).width;
        int valuesAreaWidth = c.Width - Padding * 2 - labelWidth - 10;
        int startY = c.Y + Padding, valuesX = c.X + Padding + labelWidth + 10;

        DrawText(f, "Regs", c.X + Padding, startY, c.Width - Padding * 2, theme.TextPrimary, theme.TextOutline, flags, 0xFF);
        // The last MaxVisibleValues values; the scroll offset reaches back to older ones. Returns the last row used.
        auto drawStack = [&](const std::vector<std::string>& stack, int& offset, int cellChars, int y0) {
            int perRow = std::max(1, valuesAreaWidth / (charWidth * cellChars)), count = int(stack.size());
            offset = std::clamp(offset, 0, std::max(0, count - MaxVisibleValues));
            int first = std::max(0, count - MaxVisibleValues - offset), visible = std::min(MaxVisibleValues, count - first), maxRow = 0;
            for (int i = 0; i < visible; ++i) {
                int row = i / perRow, y = y0 + row * lineHeight;
                if (y + lineHeight > c.Bottom()) break;
                maxRow = std::max(maxRow, row);
                DrawText(f, stack[first + i], valuesX + i % perRow * (charWidth * cellChars), y, c.Width - Padding * 2,
                         first + i == count - 1 ? theme.StackLastValueColor : theme.StackValueColor, theme.TextOutline, flags, 0xFF);
            }
            return maxRow;
        };
        int regStackY = startY + lineHeight;
        int maxRegRow = drawStack(stacks.RegisterStack, regScroll_, 3, regStackY);
        int callLabelY = regStackY + (maxRegRow + 1) * lineHeight + lineHeight;
        DrawText(f, "Calls", c.X + Padding, callLabelY, c.Width - Padding * 2, theme.TextPrimary, theme.TextOutline, flags, 0xFF);
        drawStack(stacks.CallStack, callScroll_, 7, callLabelY + lineHeight);
    }

private:
    int regScroll_ = 0, callScroll_ = 0, prevRegWheel_ = 0, prevCallWheel_ = 0;
};

} // namespace

C93_SERVICE_WINDOW(60, StacksWindow);

} // namespace c93::svc
