// UI/RegisterWindow.cs: the 26 registers of the current bank as 32/8/16/24-bit views, changes in orange,
// and the six bytes each register points at.
#include "service/popups.h"
#include "service/state.h"
#include "service/ui.h"

namespace c93::svc {
namespace {

class RegisterWindow : public Window {
public:
    RegisterWindow() : Window("REGISTERS", 640, 16, 500, 500, 0.1f, true, false) {}
    Window* HoverPopup() override { return popup_.get(); }

protected:
    void UpdateContent(float dt) override {
        cpuState.Update();
        UpdateHoverPopup(dt);
        if (popup_ && popup_->Visible) popup_->Update(dt);   // the reference updates it here too
    }

    void DrawContent(const Rect& c) override {
        const int lineHeight = 18, charWidth = 13, ColumnSpacing = 20;
        const Font& f = *theme.PrimaryFont;
        const MouseState& m = mouse();
        hoveredReg_ = hoveredCol_ = -1;
        DrawText(fonts.ModernDOS_12x18, "Register view (bank: " + std::to_string(cpuState.RegPageIndex) + ")", c.X + Padding, c.Y + Padding,
                 c.Width - Padding * 2, theme.TextTitle, theme.TextOutline, DrawOutline, 0xFF);
        int startY = c.Y + Padding + 24;
        uint8_t flags = Monospace | DrawOutline;
        int byteStride = f.MeasureText("FF", 0, flags).width + 4;   // a little air between bytes
        Rect content = ContentRect();
        for (int i = 0; i < 26; ++i) {
            int y = startY + i * lineHeight;
            if (y + lineHeight > c.Bottom()) break;
            int x = c.X + Padding;
            Color nameColor = cpuState.IsRegisterChanged(i) ? theme.RegisterChangedStateColor : theme.RegisterUnchangedStateColor;
            for (int j = 0; j < 4; ++j) {
                int len = 1 + (j + 3) % 4;   // 32-bit first, then 8, 16, 24: reads as address:value pairs
                std::string name = regName(i, len), hex = regHex(i, len, cpuState.RegPage0), old = regHex(i, len, cpuState.RegPageOld);
                int colWidth = f.MeasureText(name + ":" + hex, c.Width - Padding * 2, flags).width;
                if (i == 0) { colX_[j] = x; colW_[j] = colWidth; }
                if (content.Contains(m.X, m.Y) && m.Y >= y && m.Y < y + lineHeight && m.X >= x && m.X < x + colWidth) { hoveredReg_ = i; hoveredCol_ = j; }
                DrawText(f, name + ":", x, y, c.Width + Padding * 9, nameColor, theme.TextOutline, flags, 0xFF);
                int hexX = x + f.MeasureText(name + ":", c.Width - Padding * 2, flags).width;
                Color hexColor = hex != old ? theme.RegisterValueChangedColor : theme.RegisterValueUnchangedColor;
                bool nonZero = false;
                for (size_t k = 0; k < hex.size(); ++k) {   // leading zeros faded, changed digits orange
                    if (hex[k] != '0') nonZero = true;
                    Color cc = !nonZero && hex[k] == '0' ? theme.NumberLeadingZeroes : hexColor;
                    if (k < old.size() && hex[k] != old[k]) cc = theme.RegisterValueChangedColor;
                    DrawText(f, std::string(1, hex[k]), hexX + int(k) * charWidth, y, c.Width - Padding * 2, cc, theme.TextOutline, flags, 0xFF);
                }
                x += colWidth + ColumnSpacing;
            }
            DrawText(f, ">", c.X + Padding + 530, y, c.Width - Padding * 2, theme.TextPrimary, theme.TextOutline, flags, 0xFF);
            static const char* const hexd = "0123456789ABCDEF";
            for (int j = 0; j < 6; ++j) {
                uint8_t b = cpuState.RegMemoryData[i * 16 + j];
                DrawText(f, std::string{hexd[b >> 4], hexd[b & 15]}, c.X + Padding + 550 + j * byteStride, y, c.Width - Padding * 2,
                         theme.RegisterMemoryDataColor, theme.TextOutline, flags, 0xFF);
            }
        }
    }

private:
    // Hover popup timing (0.3 s on a value).
    void UpdateHoverPopup(float dt) {
        const MouseState& m = mouse();
        bool overPopup = popup_ && popup_->Visible && popup_->Bounds().Contains(m.X, m.Y);
        if (hoveredReg_ != prevReg_ || hoveredCol_ != prevCol_) { timer_ = 0; prevReg_ = hoveredReg_; prevCol_ = hoveredCol_; }
        if (hoveredReg_ >= 0 && hoveredCol_ >= 0 && !overPopup) {
            timer_ += dt;
            if (timer_ >= 0.3f) ShowHoverPopup();
        } else if (!overPopup) {
            if (popup_ && popup_->Visible) popup_->Visible = false;
            timer_ = 0;
        }
    }
    void ShowHoverPopup() {
        int bitWidth = (hoveredCol_ + 3) % 4 + 1;
        auto value = [&](const uint8_t regs[26]) { return uint32_t(std::stoul(regHex(hoveredReg_, bitWidth, regs), nullptr, 16)); };
        uint32_t current = value(cpuState.RegPage0), previous = value(cpuState.RegPageOld);
        if (popup_ && popup_->Visible) { popup_->UpdateData(hoveredReg_, bitWidth, current, previous); return; }   // stays where it opened
        int popupX = colX_[hoveredCol_] + colW_[hoveredCol_] + 10, popupY = ContentRect().Y + Padding + 24 + hoveredReg_ * 18;
        const int popupWidth = 350, popupHeight = 200;   // the reference checks with these; the popup is 500x250
        if (popupX + popupWidth > screenW()) popupX = colX_[hoveredCol_] - popupWidth - 10;
        if (popupY + popupHeight > screenH()) popupY = screenH() - popupHeight - 10;
        if (!popup_) { popup_ = std::make_unique<RegisterHoverPopup>(popupX, popupY, hoveredReg_, bitWidth, current, previous); return; }
        popup_->X = popupX;
        popup_->Y = popupY;
        popup_->UpdateData(hoveredReg_, bitWidth, current, previous);
        popup_->Visible = true;
    }

    std::unique_ptr<RegisterHoverPopup> popup_;
    int hoveredReg_ = -1, hoveredCol_ = -1, prevReg_ = -1, prevCol_ = -1;
    float timer_ = 0;
    int colX_[4]{}, colW_[4]{};
};

} // namespace

C93_SERVICE_WINDOW(30, RegisterWindow);

} // namespace c93::svc
