// UI/ImmediateWindow.cs: an editor for assembly snippets; Ctrl+Enter assembles them at 0xC00000 and runs
// them (service/immediate.h). The text is kept in layout.json ("ImmediateText").
#include <chrono>
#include <thread>

#include "log.h"
#include "machine.h"
#include "service/editor.h"
#include "service/immediate.h"

namespace c93::svc {
namespace {

class ImmediateWindow : public EditorWindow {
public:
    ImmediateWindow() : EditorWindow("IMMEDIATE", 1160, 560, 420, 220, 0.15f, true, false) {}
    const char* LayoutTextKey() const override { return "ImmediateText"; }

protected:
    static constexpr float StatusSeconds = 3.0f;

    void UpdateContent(float dt) override {
        Blink(dt);
        if (statusTimer_ > 0) {
            statusTimer_ -= dt;
            if (statusTimer_ <= 0) statusMessage_.clear();
        }
        if (!KeysActive()) return;
        if (Ctrl() && EnterPressed()) { CompileAndRun(); return; }
        HandleKeys();
    }

    void DrawContent(const Rect& c) override {
        DrawLines(c, [](const std::string&) { return theme.TextPrimary; }, statusMessage_);
    }

private:
    std::string statusMessage_;
    float statusTimer_ = 0;

    // The reference wrote IP and ran the snippet from the UI thread while the CPU thread polled its trap. Here the
    // CPU is first parked at an instruction boundary (it already is when stepping), so the two never overlap.
    void CompileAndRun() {
        Machine& m = machine();
        bool step = m.stepByStep.load();
        if (!step) m.debugSet(true, false);
        // Parked and not about to step (an F9 this frame): moveNext is read first, the CPU clears it after `parked`.
        auto stopped = [&m] { return (!m.moveNext.load() && m.parked.load()) || !m.running.load(); };
        auto until = std::chrono::steady_clock::now() + std::chrono::seconds(1);   // a WAIT may hold the CPU a while
        while (!stopped() && std::chrono::steady_clock::now() < until) std::this_thread::yield();
        if (!stopped()) {
            if (!step) m.debugSet(false, false);
            LOGW("Service: the CPU did not stop for the immediate code; nothing ran");
            SetStatus("Machine not ready", true);
            return;
        }
        immediate::Status s = immediate::compileAndRun(m, lines_, step);
        if (!step) m.debugSet(false, true);   // DebugState.MoveNext = true: the machine runs the snippet
        SetStatus(s.message, s.isError);
    }

    void SetStatus(const std::string& message, bool isError) {
        statusMessage_ = message;
        statusTimer_ = StatusSeconds;
        if (isError) caretVisible_ = true;
        saveLayout();
    }
};

} // namespace

C93_SERVICE_WINDOW(10, ImmediateWindow);

} // namespace c93::svc
