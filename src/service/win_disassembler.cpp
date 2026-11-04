// UI/DisassemblerWindow.cs: 200 instructions from IP, the current one highlighted, a hover box per line.
#include <algorithm>

#include "service/popups.h"
#include "service/state.h"
#include "service/ui.h"

namespace c93::svc {
namespace {

class DisassemblerWindow : public Window {
public:
    DisassemblerWindow() : Window("DISASSEMBLER", 16, 16, 600, 400, 0, true, false) {}

    void Update(float dt) override {
        Window::Update(dt);
        if (!Visible) return;
        UpdateHoverPopup(dt);
    }
    Window* HoverPopup() override { return popup_.get(); }

protected:
    void DrawContent(const Rect& c) override {
        auto& lines = disassembled.Lines;
        if (lines.empty()) return;
        const int lineHeight = 18, charWidth = 13;
        // The clipped area loses 4 px (DrawContentClipped) plus the top padding.
        int visibleHeight = std::max(1, c.Height - 4 - Padding);
        int linesPerPage = std::max(1, visibleHeight / lineHeight);
        int visibleCount = std::min(linesPerPage, int(lines.size()));
        int ip = int(ops::ip());

        int focus = 0;   // keep the current IP in view
        for (int i = 0; i < int(lines.size()); ++i) if (lines[i].Address == ip) { focus = i; break; }
        int start = std::max(0, focus - visibleCount / 2);
        if (start + visibleCount > int(lines.size())) start = std::max(0, int(lines.size()) - visibleCount);
        if (focus < start) start = focus;
        if (focus >= start + visibleCount) start = std::max(0, focus - visibleCount + 1);

        size_t maxAddrLen = 0;
        for (int i = 0; i < visibleCount; ++i) maxAddrLen = std::max(maxAddrLen, lines[start + i].TextAddress.size());
        int addrX0 = c.X + Padding, instrX = addrX0 + int(maxAddrLen) * charWidth + charWidth * 2;
        uint8_t flags = Monospace | DrawOutline;
        const MouseState& m = mouse();

        int hover = -1;
        hoveredLine_ = hoveredVisible_ = -1;
        if (c.Contains(m.X, m.Y)) {
            int localY = m.Y - (c.Y + Padding);
            if (localY >= 0 && localY / lineHeight < visibleCount) {
                hover = localY / lineHeight;
                hoveredLine_ = start + hover;
                hoveredVisible_ = hover;
            }
        }

        for (int i = 0; i < visibleCount; ++i) {
            const DissLine& l = lines[start + i];
            int y = c.Y + Padding + i * lineHeight;
            if (l.Address == ip) fill({c.X + 1, y - 1, c.Width - 2, lineHeight + 2}, theme.DisassemblerCurrentInstructionBackground);
            if (i == hover) {
                Rect h{c.X + 1, y, c.Width - 2, lineHeight};
                fill({h.X, h.Y - 1, h.Width, 1}, theme.DisassemblerHoverBorder);
                fill({h.X, h.Bottom() - 1, h.Width, 1}, theme.DisassemblerHoverBorder);
                fill({h.X, h.Y, 1, h.Height}, theme.DisassemblerHoverBorder);
                fill({h.Right() - 1, h.Y, 1, h.Height}, theme.DisassemblerHoverBorder);
            }
            // Address with faded leading zeros.
            std::string addr = l.TextAddress;
            if (addr.size() < maxAddrLen) addr.insert(0, maxAddrLen - addr.size(), '0');
            size_t nz = 0;
            while (nz < addr.size() && addr[nz] == '0') ++nz;
            int ax = addrX0;
            if (nz) {
                DrawText(*theme.PrimaryFont, addr.substr(0, nz), ax, y, c.Width - Padding * 2, theme.DisassemblerAddressZero,
                         theme.DisassemblerAddressZeroOutline, flags, 0xFF);
                ax += int(nz) * charWidth;
            }
            if (nz < addr.size())
                DrawText(*theme.PrimaryFont, addr.substr(nz), ax, y, c.Width - Padding * 2, theme.DisassemblerAddressFull, theme.TextOutline, flags, 0xFF);
            DrawText(*theme.PrimaryFont, l.Instruction, instrX, y, c.Width - Padding * 2, theme.DisassemblerInstructionColor, theme.TextOutline, flags, 0xFF);
        }
    }

private:
    // Hover popup timing (0.5 s on a line).
    void UpdateHoverPopup(float dt) {
        const MouseState& m = mouse();
        bool overPopup = popup_ && popup_->Visible && popup_->Bounds().Contains(m.X, m.Y);
        if (hoveredLine_ != prevHovered_) { timer_ = 0; prevHovered_ = hoveredLine_; }
        if (hoveredLine_ >= 0 && !overPopup) {
            timer_ += dt;
            if (timer_ >= 0.5f) ShowHoverPopup();
        } else if (!overPopup) {
            if (popup_ && popup_->Visible) popup_->Visible = false;
            timer_ = 0;
        }
    }
    void ShowHoverPopup() {
        if (hoveredLine_ < 0 || hoveredVisible_ < 0 || hoveredLine_ >= int(disassembled.Lines.size())) return;
        const DissLine& l = disassembled.Lines[hoveredLine_];
        std::string name = l.Instruction.substr(0, l.Instruction.find(' '));
        if (name.empty()) return;
        if (popup_ && popup_->Visible) { popup_->UpdateData(name, l.Instruction, l.OpCodes); return; }   // stays where it opened
        int popupX = Bounds().Right() + 10, popupY = ContentRect().Y + Padding + hoveredVisible_ * 18;
        const int popupWidth = 500, popupHeight = 400;
        if (popupX + popupWidth > screenW()) popupX = Bounds().Left() - popupWidth - 10;
        if (popupY + popupHeight > screenH()) popupY = screenH() - popupHeight - 10;
        if (!popup_) { popup_ = std::make_unique<DisassemblerHoverPopup>(popupX, popupY, name, l.Instruction, l.OpCodes); return; }
        popup_->X = popupX;
        popup_->Y = popupY;
        popup_->UpdateData(name, l.Instruction, l.OpCodes);
        popup_->Visible = true;
    }

    std::unique_ptr<DisassemblerHoverPopup> popup_;
    int hoveredLine_ = -1, hoveredVisible_ = -1, prevHovered_ = -1;
    float timer_ = 0;
};

} // namespace

C93_SERVICE_WINDOW(20, DisassemblerWindow);

} // namespace c93::svc
