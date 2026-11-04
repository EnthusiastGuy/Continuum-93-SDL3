// UI/HistoryHoverPopup.cs: StepHistory as a scrollable list, oldest first, the current instruction last in orange.
#include <algorithm>
#include <cstdlib>
#include <vector>

#include "service/popups.h"
#include "service/state.h"

namespace c93::svc {

HistoryHoverPopup::HistoryHoverPopup(int x, int y, bool hasCurrent)
    : Window("Instruction History", x, y, 600, 500, 0, false, false), prevMouse_(mouse()), hasCurrent_(hasCurrent) {
    IsOnTop = true;
    Height = 500;   // no spawn animation
}

void HistoryHoverPopup::Update(float dt) {
    if (Visible && !wasVisible_) shouldScrollToBottom_ = true;
    wasVisible_ = Visible;
    if (!Visible) return;
    UpdateContent(dt);
}

void HistoryHoverPopup::UpdateContent(float) {
    // Called twice a frame (owner and WindowManager), as in the reference: the second call sees no wheel delta.
    const MouseState& m = mouse();
    if (ContentRect().Contains(m.X, m.Y))
        if (int delta = m.ScrollWheelValue - prevMouse_.ScrollWheelValue) {
            int step = std::abs(delta) / 120;   // one line per notch
            scrollOffset_ = delta > 0 ? std::max(0, scrollOffset_ - step) : std::min(maxScrollOffset_, scrollOffset_ + step);
        }
    prevMouse_ = m;
}

void HistoryHoverPopup::DrawContent(const Rect& c) {
    const Font& font = fonts.ModernDOS_12x18;
    const int lineHeight = font.GlyphCellHeight + 1;
    const std::vector<DissLine>& all = stepHistory.history;
    const DissLine* current = hasCurrent_ ? disassembled.GetCurentInstruction() : nullptr;
    // The last history item is "current" when it is the instruction at IP; otherwise IP's line is appended.
    bool lastIsCurrent = !all.empty() && current && all.back().Address == current->Address;
    bool appendCurrent = current && !lastIsCurrent;
    int totalItems = int(all.size()) + (appendCurrent ? 1 : 0);

    int linesPerPage = std::max(1, (c.Height - Padding * 2) / lineHeight);
    maxScrollOffset_ = std::max(0, totalItems - linesPerPage);
    if (shouldScrollToBottom_) { scrollOffset_ = maxScrollOffset_; shouldScrollToBottom_ = false; }
    scrollOffset_ = std::clamp(scrollOffset_, 0, maxScrollOffset_);

    int end = std::min(scrollOffset_ + linesPerPage, totalItems), y = c.Y + Padding;
    for (int i = scrollOffset_; i < end; ++i) {
        bool isAppended = i == int(all.size());
        const DissLine& item = isAppended ? *current : all[i];
        bool isCurrent = isAppended || (i == int(all.size()) - 1 && lastIsCurrent);
        DrawText(font, item.Instruction, c.X + Padding, y, c.Width - Padding * 2, isCurrent ? theme.TextDarkOrange : theme.VideoPaletteNumber,
                 theme.TextOutline, DrawOutline, 0xFF);
        y += lineHeight;
    }
}

} // namespace c93::svc
