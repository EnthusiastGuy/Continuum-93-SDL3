// UI/WatcherWindow.cs: one condition per line on registers, float registers or memory (service/watch.h);
// when every line is a valid condition and all hold, the machine switches to step-by-step. The text is kept
// in layout.json ("WatcherText").
//
// The reference re-parsed the text after every instruction on the CPU thread (CheckConditionsSynchronously).
// Here the main thread parses it when it changes and hands the list to the CPU, which checks it in
// serveCallbacks while Machine::kWatchBit is armed: only in service mode, only while there are conditions.
#include <atomic>
#include <memory>

#include "machine.h"
#include "service/editor.h"
#include "service/watch.h"

namespace c93::svc {
namespace {

using Conditions = std::vector<watch::Condition>;
std::atomic<Conditions*> gNext{nullptr};   // main thread -> CPU thread
Conditions* gLive = nullptr;               // owned by the CPU thread

// Machine::watchHook, on the CPU thread after every instruction.
void checkConditionsSynchronously(Machine& m) {
    if (gNext.load(std::memory_order_relaxed)) [[unlikely]]
        if (Conditions* n = gNext.exchange(nullptr, std::memory_order_acquire)) { delete gLive; gLive = n; }
    if (gLive && !gLive->empty() && !m.stepByStep.load(std::memory_order_relaxed) && watch::allTrue(m, *gLive)) m.debugSet(true, false);
}

class WatcherWindow : public EditorWindow {
public:
    WatcherWindow() : EditorWindow("WATCHER", 1160, 800, 420, 200, 0.85f, true, false) { machine().watchHook = checkConditionsSynchronously; }
    const char* LayoutTextKey() const override { return "WatcherText"; }

    void Tick() override {
        Machine& m = machine();
        if (published_ != version_) {   // the CPU gets the new list (empty when any line is invalid)
            published_ = version_;
            auto* list = new Conditions;
            hasConditions_ = watch::parseAll(lines_, *list) && !list->empty();
            if (!hasConditions_) list->clear();
            delete gNext.exchange(list, std::memory_order_release);
        }
        bool arm = serviceMode() && hasConditions_;   // CheckConditionsSynchronously ran only in service mode
        if (arm != armed_) {
            armed_ = arm;
            if (arm) m.callbacksPending.fetch_or(Machine::kWatchBit);
            else m.callbacksPending.fetch_and(~Machine::kWatchBit);
        }
    }

protected:
    void UpdateContent(float dt) override {
        Blink(dt);
        if (serviceMode()) ParseAndEvaluateConditions();   // also once per frame, as there
        if (!KeysActive()) return;
        HandleKeys();
    }

    void DrawContent(const Rect& c) override {
        DrawLines(c, [](const std::string& line) {   // red: a line that is not a condition
            std::string_view t = watch::trim(line);
            return !t.empty() && !watch::parse(t) ? theme.TextIndianRed : theme.TextPrimary;
        }, {});
    }

private:
    Conditions conditions_;
    unsigned published_ = ~0u;
    bool hasConditions_ = false, armed_ = false;

    void ParseAndEvaluateConditions() {
        Machine& m = machine();
        if (watch::parseAll(lines_, conditions_) && !conditions_.empty() && watch::allTrue(m, conditions_) && !m.stepByStep.load())
            m.debugSet(true, false);
    }
};

} // namespace

C93_SERVICE_WINDOW(120, WatcherWindow);

} // namespace c93::svc
