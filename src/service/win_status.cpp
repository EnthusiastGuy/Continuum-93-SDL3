// UI/StatusBarWindow.cs: the instructions stepped through (red), the next one (orange), CPU state.
#include <algorithm>

#include "service/popups.h"
#include "service/state.h"
#include "service/ui.h"

namespace c93::svc {
namespace {

class StatusBarWindow : public Window {
public:
    StatusBarWindow() : Window("STATUS", 16, 760, 1500, 30, 0.6f, true, false) {}
    Window* HoverPopup() override { return popup_.get(); }

protected:
    void UpdateContent(float dt) override {
        UpdateHoverPopup(dt);
        if (popup_ && popup_->Visible) popup_->Update(dt);
    }

    void DrawContent(const Rect& c) override {
        const Font& font = fonts.ModernDOS_12x18;
        uint8_t flags = DrawOutline;
        int arrowWidth = font.MeasureText(">", 0, flags).width;
        bool step = stepByStep();
        Color statusColor = step ? theme.TextGreenYellow : theme.VideoPaletteNumber;
        const char* status = step ? "CPU debugging" : "CPU running";   // a machine always exists here
        int statusWidth = font.MeasureText(status, 0, flags).width;
        int x = c.X + Padding + font.MeasureText("HISTORY", 0, flags).width + 10, y = c.Y + Padding;
        DrawText(font, "HISTORY", c.X + Padding, y, c.Width - Padding * 2, theme.TextTitle, theme.TextOutline, flags, 0xFF);
        int available = std::max(0, c.Width - Padding - x - statusWidth - Padding);
        historyArea_ = {x, y, available, font.GlyphCellHeight + 1};

        const DissLine* current = disassembled.GetCurentInstruction();
        int currentAddress = current ? current->Address : 0;
        std::vector<DissLine> latest = stepHistory.GetFittingWidth(available, font, flags, arrowWidth, current ? &currentAddress : nullptr);
        for (auto& l : latest) {
            DrawText(font, l.Instruction, x, y, c.Width - Padding * 2, theme.VideoPaletteNumber, theme.TextOutline, flags, 0xFF);
            x += font.MeasureText(l.Instruction, 0, flags).width;
            DrawText(font, ">", x, y, c.Width - Padding * 2, theme.VideoPaletteNumber, theme.TextOutline, flags, 0xFF);
            x += arrowWidth + 10;
        }
        if (current && (latest.empty() || latest.back().Address != current->Address))
            DrawText(font, current->Instruction, x, y, c.Width - Padding * 2, theme.TextDarkOrange, theme.TextOutline, flags, 0xFF);
        DrawText(font, status, c.Right() - Padding - statusWidth, y, c.Width - Padding * 2, statusColor, theme.TextOutline, flags, 0xFF);
    }

private:
    // Hover popup timing (0.3 s to open, 0.5 s to close).
    void UpdateHoverPopup(float dt) {
        if (!Visible) { if (popup_ && popup_->Visible) popup_->Visible = false; hoverTimer_ = 0; return; }
        const MouseState& m = mouse();
        bool overPopup = popup_ && popup_->Visible && popup_->Bounds().Contains(m.X, m.Y);
        bool overHistory = historyArea_.Contains(m.X, m.Y);
        if (overHistory || overPopup) {
            closeTimer_ = 0;
            if (overHistory && !overPopup) {
                hoverTimer_ += dt;
                if (hoverTimer_ >= 0.3f) ShowHoverPopup();
            } else if (overPopup) hoverTimer_ = 0;
        } else {
            hoverTimer_ = 0;
            if (popup_ && popup_->Visible && (closeTimer_ += dt) >= 0.5f) { popup_->Visible = false; closeTimer_ = 0; }
        }
    }
    void ShowHoverPopup() {
        bool hasCurrent = disassembled.GetCurentInstruction() != nullptr;
        if (popup_ && popup_->Visible) { popup_->UpdateCurrentInstruction(hasCurrent); return; }
        const int popupWidth = 600, popupHeight = 500;
        int popupX = historyArea_.X + historyArea_.Width / 2 - popupWidth / 2, popupY = historyArea_.Y - popupHeight - 10;
        if (popupX < 0) popupX = 10;
        if (popupX + popupWidth > screenW()) popupX = screenW() - popupWidth - 10;
        if (popupY < 0) popupY = historyArea_.Bottom() + 10;
        if (!popup_) { popup_ = std::make_unique<HistoryHoverPopup>(popupX, popupY, hasCurrent); return; }
        popup_->X = popupX;
        popup_->Y = popupY;
        popup_->UpdateCurrentInstruction(hasCurrent);
        popup_->ResetScrollToBottom();
        popup_->Visible = true;
    }

    std::unique_ptr<HistoryHoverPopup> popup_;
    Rect historyArea_;
    float hoverTimer_ = 0, closeTimer_ = 0;
};

} // namespace

C93_SERVICE_WINDOW(90, StatusBarWindow);

} // namespace c93::svc
