// UI/DisassemblerHoverPopup.cs and AutoDocs/MetaInfo/ASMMetaInfo.cs (the docs text, generated: asm_meta.inc).
#include <algorithm>
#include <string_view>
#include <vector>

#include "service/popups.h"

namespace c93::svc {

#include "service/asm_meta.inc"

const AsmMeta* GetAsmMeta(const std::string& op) {
    for (const AsmMeta& m : kAsmMeta)   // Operators.Contains(op): a whole word of the list
        for (std::string_view ops = m.Operators; !ops.empty();) {
            size_t sp = ops.find(' ');
            if (ops.substr(0, sp) == op) return &m;
            ops = sp == std::string_view::npos ? std::string_view{} : ops.substr(sp + 1);
        }
    return nullptr;
}

DisassemblerHoverPopup::DisassemblerHoverPopup(int x, int y, const std::string& instructionName, const std::string& fullInstruction,
                                               const std::string& opcodes)
    : Window(instructionName, x, y, 500, 400, 0, false, false), fullInstruction_(fullInstruction), opcodes_(opcodes),
      meta_(GetAsmMeta(instructionName)) {
    IsOnTop = true;
    Height = 400;   // no spawn animation
}

void DisassemblerHoverPopup::UpdateData(const std::string& instructionName, const std::string& fullInstruction, const std::string& opcodes) {
    fullInstruction_ = fullInstruction;
    opcodes_ = opcodes;
    meta_ = GetAsmMeta(instructionName);
    Title = instructionName;
    scrollOffset_ = 0;   // every frame the mouse rests on a line: the reference did too
}

namespace {
// string.Split(' '): empty entries kept.
std::vector<std::string> splitSpaces(const std::string& s) {
    std::vector<std::string> r;
    size_t p = 0;
    for (size_t sp; (sp = s.find(' ', p)) != std::string::npos; p = sp + 1) r.push_back(s.substr(p, sp - p));
    r.push_back(s.substr(p));
    return r;
}
} // namespace

void DisassemblerHoverPopup::DrawContent(const Rect& c) {
    const Font& font = fonts.ModernDOS_9x15;
    const uint8_t monoFlags = Monospace | DrawOutline, normalFlags = DrawOutline;
    const int lineHeight = 18, sectionSpacing = 10, contentWidth = c.Width - Padding * 2;

    // Greedy word wrap; calls line(text) per wrapped line. Empty text still takes a line.
    auto wrap = [&](const char* text, auto&& line) {
        std::string s = text ? text : "";
        if (s.empty()) { line(nullptr); return; }
        std::string current;
        for (const std::string& word : splitSpaces(s)) {
            std::string test = current.empty() ? word : current + " " + word;
            if (font.MeasureText(test, contentWidth, normalFlags).width > contentWidth && !current.empty()) {
                line(&current);
                current = word;
            } else current = test;
        }
        if (!current.empty()) line(&current);
    };

    // First pass: the content height, for the scroll limit.
    int tempY = c.Y + Padding, startY = tempY;
    auto measure = [&](const std::string*) { tempY += lineHeight; };
    tempY += lineHeight * 2 + sectionSpacing + lineHeight * 2 + sectionSpacing;   // Instruction, Opcodes
    if (meta_) {
        tempY += lineHeight; wrap(meta_->Description, measure); tempY += sectionSpacing;
        tempY += lineHeight; wrap(meta_->Application, measure); tempY += sectionSpacing;
        tempY += lineHeight; wrap(meta_->Format, measure);
    }
    // One extra line so the last one is fully visible.
    maxScrollOffset_ = std::max(0, tempY - startY - c.Height + lineHeight);
    scrollOffset_ = std::max(0, std::min(scrollOffset_, maxScrollOffset_));

    int y = c.Y + Padding - scrollOffset_;
    auto visible = [&] { return y >= c.Y - lineHeight && y < c.Bottom(); };
    auto mono = [&](const std::string& text, Color color) {
        if (visible()) DrawText(font, text, c.X + Padding, y, contentWidth, color, theme.TextOutline, monoFlags, 0xFF);
        y += lineHeight;
    };
    auto wrapped = [&](const char* text) {
        wrap(text, [&](const std::string* l) {
            if (l && visible()) DrawText(font, *l, c.X + Padding, y, contentWidth, theme.TextSecondary, theme.TextOutline, normalFlags, 0xFF);
            y += lineHeight;
        });
    };

    mono("Instruction:", theme.TextPrimary);
    mono(fullInstruction_, theme.DisassemblerInstructionColor);
    y += sectionSpacing;
    mono("Opcodes:", theme.TextPrimary);
    mono(opcodes_, theme.DisassemblerOpcodeColor);
    y += sectionSpacing;
    if (meta_) {
        mono("Description:", theme.TextPrimary);
        wrapped(meta_->Description);
        y += sectionSpacing;
        mono("Application:", theme.TextPrimary);
        wrapped(meta_->Application);
        y += sectionSpacing;
        mono("Operators:", theme.TextPrimary);
        wrapped(meta_->Format);
    }

    if (maxScrollOffset_ > 0 && scrollOffset_ < maxScrollOffset_)   // more below
        DrawText(font, "+", c.X + c.Width / 2 - 5, c.Bottom() - lineHeight - Padding, contentWidth, theme.TextPrimary, theme.TextOutline,
                 monoFlags, 0xFF);
}

bool DisassemblerHoverPopup::HandleInput(const MouseState& mouse, const MouseState& prevMouse) {
    if (!Visible || !Bounds().Contains(mouse.X, mouse.Y)) return false;
    if (int delta = mouse.ScrollWheelValue - prevMouse.ScrollWheelValue) {
        scrollOffset_ -= delta / 120 * 20;   // 20 px per notch
        int maxScroll = maxScrollOffset_ > 0 ? maxScrollOffset_ : 10000;
        scrollOffset_ = std::max(0, std::min(scrollOffset_, maxScroll));
    }
    return true;   // blocks the windows underneath
}

} // namespace c93::svc
