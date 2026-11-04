// The hover popups of the disassembler, register and status windows (UI/*HoverPopup.cs).
// Owned by their window, returned from its HoverPopup(); no spawn animation, always on top.
#pragma once
#include <cstdint>
#include <string>

#include "service/ui.h"

namespace c93::svc {

struct AsmMeta { const char *Operators, *Description, *Application, *Format; };
const AsmMeta* GetAsmMeta(const std::string& op);   // ASMMetaInfo.GetMeta: null when the op has no docs

// UI/DisassemblerHoverPopup.cs: the hovered instruction, its opcodes and its ASMMetaInfo docs; wheel scrolls.
class DisassemblerHoverPopup : public Window {
public:
    DisassemblerHoverPopup(int x, int y, const std::string& instructionName, const std::string& fullInstruction, const std::string& opcodes);
    void Update(float dt) override { if (Visible) UpdateContent(dt); }
    void UpdateData(const std::string& instructionName, const std::string& fullInstruction, const std::string& opcodes);
    bool HandleInput(const MouseState& mouse, const MouseState& prevMouse) override;

protected:
    void DrawContent(const Rect& c) override;

private:
    std::string fullInstruction_, opcodes_;
    const AsmMeta* meta_ = nullptr;
    int scrollOffset_ = 0, maxScrollOffset_ = 0;
};

// UI/RegisterHoverPopup.cs: one register view as hex/decimal/binary/octal, now and before the last step.
class RegisterHoverPopup : public Window {
public:
    RegisterHoverPopup(int x, int y, int regIndex, int bitWidth, uint32_t currentValue, uint32_t previousValue);
    void Update(float dt) override { if (Visible) UpdateContent(dt); }
    void UpdateData(int regIndex, int bitWidth, uint32_t currentValue, uint32_t previousValue);
    bool HandleInput(const MouseState& mouse, const MouseState&) override { return Visible && Bounds().Contains(mouse.X, mouse.Y); }

protected:
    void DrawChrome() override;
    void DrawContent(const Rect& c) override;

private:
    int bitWidth_;
    uint32_t current_, previous_;
};

// UI/HistoryHoverPopup.cs: the whole StepHistory, newest at the bottom, the current instruction in orange.
class HistoryHoverPopup : public Window {
public:
    HistoryHoverPopup(int x, int y, bool hasCurrent);
    void Update(float dt) override;
    void UpdateCurrentInstruction(bool hasCurrent) { hasCurrent_ = hasCurrent; }
    void ResetScrollToBottom() { scrollOffset_ = INT32_MAX; shouldScrollToBottom_ = true; }
    bool HandleInput(const MouseState& mouse, const MouseState&) override { return Visible && Bounds().Contains(mouse.X, mouse.Y); }

protected:
    void UpdateContent(float dt) override;
    void DrawContent(const Rect& c) override;

private:
    int scrollOffset_ = 0, maxScrollOffset_ = 0;
    MouseState prevMouse_;
    bool hasCurrent_, wasVisible_ = false, shouldScrollToBottom_ = true;
};

} // namespace c93::svc
