// The text editor UI/ImmediateWindow.cs and UI/WatcherWindow.cs each carry (identical copies there): lines,
// caret, keys, scrolling, drawing. The two windows derive from it and add what differs.
#pragma once
#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <string>
#include <vector>

#include "service/service.h"
#include "service/ui.h"

namespace c93::svc {

class EditorWindow : public Window {
public:
    using Window::Window;

    std::string GetText() const override {
        std::string t;
        for (size_t i = 0; i < lines_.size(); ++i) t += (i ? "\n" : "") + lines_[i];
        return t;
    }
    void SetText(const std::string& text) override {
        lines_.clear();
        std::string s;
        for (char c : text) if (c != '\r') s += c;
        for (size_t b = 0;;) {   // Split('\n'), empty text gives one empty line too
            size_t e = s.find('\n', b);
            lines_.push_back(s.substr(b, e == std::string::npos ? std::string::npos : e - b));
            if (e == std::string::npos) break;
            b = e + 1;
        }
        cursorLine_ = std::clamp(cursorLine_, 0, int(lines_.size()) - 1);
        cursorColumn_ = std::clamp(cursorColumn_, 0, len(cursorLine_));
        dirty_ = false;
        ++version_;
    }

protected:
    static constexpr float CaretBlinkSeconds = 0.5f;
    const Font& font() const { return fonts.ModernDOS_12x18; }
    int lineHeight() const { return font().GlyphCellHeight; }

    std::vector<std::string> lines_{""};
    int cursorLine_ = 0, cursorColumn_ = 0, firstVisibleLine_ = 0;
    bool dirty_ = false, caretVisible_ = true;
    float blinkTimer_ = 0;
    unsigned version_ = 0;   // bumped on every text change

    void Blink(float dt) {
        blinkTimer_ += dt;
        if (blinkTimer_ >= CaretBlinkSeconds) { blinkTimer_ = 0; caretVisible_ = !caretVisible_; }
    }
    bool KeysActive() const { return service::view() && IsFocused; }   // Service.STATE.UseServiceView && IsFocused
    static bool Ctrl() { return keyDown(SDLK_LCTRL) || keyDown(SDLK_RCTRL); }
    static bool EnterPressed() { return keyPressed(SDLK_RETURN) || keyPressed(SDLK_KP_ENTER); }   // both are Keys.Enter

    // HandleNavigation, HandleEditing, EnsureCursorVisible(GetLinesPerPage()).
    void HandleKeys() {
        if (keyPressed(SDLK_LEFT)) MoveLeft();
        if (keyPressed(SDLK_RIGHT)) MoveRight();
        if (keyPressed(SDLK_UP)) MoveUp();
        if (keyPressed(SDLK_DOWN)) MoveDown();
        if (keyPressed(SDLK_HOME)) cursorColumn_ = 0;
        if (keyPressed(SDLK_END)) cursorColumn_ = len(cursorLine_);
        HandleEditing();
        EnsureCursorVisible(GetLinesPerPage());
    }

    // DrawContent; lineColor: the colour of each line, status: the bottom status line (empty: none).
    template <class LineColor> void DrawLines(const Rect& c, LineColor lineColor, const std::string& status) {
        const int lh = lineHeight();
        int linesPerPage = std::max(1, std::max(1, c.Height - 4 - Padding) / lh);
        EnsureCursorVisible(linesPerPage);
        int maxLineIndex = std::min(int(lines_.size()), firstVisibleLine_ + linesPerPage);
        int y = c.Y + Padding, maxWidth = c.Width - Padding * 2;
        const uint8_t flags = DrawOutline | Monospace | DisableKerning;
        for (int i = firstVisibleLine_; i < maxLineIndex; ++i) {
            if (IsFocused && i == cursorLine_) {   // theme.WindowBackgroundFocused * 0.5f: every channel, truncated
                Color b = theme.WindowBackgroundFocused;
                fill({c.X + 1, y - 1, c.Width - 2, lh + 2}, {uint8_t(b.r * 0.5f), uint8_t(b.g * 0.5f), uint8_t(b.b * 0.5f), uint8_t(b.a * 0.5f)});
            }
            DrawText(font(), lines_[size_t(i)], c.X + Padding, y, maxWidth, lineColor(lines_[size_t(i)]), theme.TextOutline, flags, 0xFF);
            if (IsFocused && i == cursorLine_ && caretVisible_)
                fill({c.X + Padding + font().GlyphCellWidth * cursorColumn_, y, 2, lh - 2}, theme.TextPrimary);
            y += lh;
        }
        if (!status.empty())
            DrawText(font(), status, c.X + Padding, c.Bottom() - lh - Padding / 2, maxWidth, theme.TextPrimary, theme.TextOutline, flags, 0xFF);
    }

private:
    int len(int line) const { return int(lines_[size_t(line)].size()); }

    void HandleEditing() {
        bool shift = keyDown(SDLK_LSHIFT) || keyDown(SDLK_RSHIFT);
        if (!Ctrl()) {
            if (EnterPressed()) { InsertNewLine(); return; }
            if (keyPressed(SDLK_TAB)) { for (char c : std::string("    ")) InsertChar(c); return; }
        }
        if (keyPressed(SDLK_BACKSPACE)) { Backspace(); return; }
        if (keyPressed(SDLK_DELETE)) { Delete(); return; }
        TryInsertCharacter(shift);
        if (dirty_) { dirty_ = false; saveLayout(); }   // skipped on the frames above that returned, as there
    }

    void TryInsertCharacter(bool shift) {
        for (int k = SDLK_A; k <= SDLK_Z; ++k)
            if (keyPressed(k)) { InsertChar(char((shift ? 'A' : 'a') + (k - SDLK_A))); return; }
        // The C#'s else-if chain, in its order: the first key newly down wins.
        static const struct { int key; char plain, shifted; } keys[] = {
            {SDLK_0, '0', ')'}, {SDLK_1, '1', '!'}, {SDLK_2, '2', '@'}, {SDLK_3, '3', '#'}, {SDLK_4, '4', '$'},
            {SDLK_5, '5', '%'}, {SDLK_6, '6', '^'}, {SDLK_7, '7', '&'}, {SDLK_8, '8', '*'}, {SDLK_9, '9', '('},
            {SDLK_KP_0, '0', '0'}, {SDLK_KP_1, '1', '1'}, {SDLK_KP_2, '2', '2'}, {SDLK_KP_3, '3', '3'}, {SDLK_KP_4, '4', '4'},
            {SDLK_KP_5, '5', '5'}, {SDLK_KP_6, '6', '6'}, {SDLK_KP_7, '7', '7'}, {SDLK_KP_8, '8', '8'}, {SDLK_KP_9, '9', '9'},
            {SDLK_SPACE, ' ', ' '}, {SDLK_COMMA, ',', '<'}, {SDLK_PERIOD, '.', '>'}, {SDLK_SLASH, '/', '?'},
            {SDLK_SEMICOLON, ';', ':'}, {SDLK_APOSTROPHE, '\'', '"'}, {SDLK_LEFTBRACKET, '[', '{'}, {SDLK_RIGHTBRACKET, ']', '}'},
            {SDLK_MINUS, '-', '_'}, {SDLK_EQUALS, '=', '+'},
            {SDLK_BACKSLASH, '\\', '|'}, {SDLK_LESS, '\\', '|'},   // Keys.OemPipe, Keys.OemBackslash (the ISO key)
            {SDLK_GRAVE, '`', '~'},
        };
        for (auto& k : keys)
            if (keyPressed(k.key)) { InsertChar(shift ? k.shifted : k.plain); return; }
    }

    void InsertChar(char c) {
        lines_[size_t(cursorLine_)].insert(size_t(cursorColumn_), 1, c);
        ++cursorColumn_;
        EnsureCursorInBounds();
        Changed();
    }
    void InsertNewLine() {
        std::string& line = lines_[size_t(cursorLine_)];
        size_t at = std::min(size_t(cursorColumn_), line.size());
        std::string right = line.substr(at);
        line.resize(at);
        lines_.insert(lines_.begin() + cursorLine_ + 1, right);
        ++cursorLine_;
        cursorColumn_ = 0;
        Changed();
    }
    void Backspace() {
        if (cursorColumn_ > 0) {
            lines_[size_t(cursorLine_)].erase(size_t(cursorColumn_ - 1), 1);
            --cursorColumn_;
        } else if (cursorLine_ > 0) {
            int prevLen = len(cursorLine_ - 1);
            lines_[size_t(cursorLine_ - 1)] += lines_[size_t(cursorLine_)];
            lines_.erase(lines_.begin() + cursorLine_);
            --cursorLine_;
            cursorColumn_ = prevLen;
        }
        EnsureCursorInBounds();
        Changed();
    }
    void Delete() {
        std::string& line = lines_[size_t(cursorLine_)];
        if (cursorColumn_ < int(line.size())) line.erase(size_t(cursorColumn_), 1);
        else if (cursorLine_ < int(lines_.size()) - 1) {
            line += lines_[size_t(cursorLine_ + 1)];
            lines_.erase(lines_.begin() + cursorLine_ + 1);
        }
        EnsureCursorInBounds();
        Changed();
    }
    void Changed() { dirty_ = true; ++version_; }

    void MoveLeft() {
        if (cursorColumn_ > 0) --cursorColumn_;
        else if (cursorLine_ > 0) { --cursorLine_; cursorColumn_ = len(cursorLine_); }
    }
    void MoveRight() {
        if (cursorColumn_ < len(cursorLine_)) ++cursorColumn_;
        else if (cursorLine_ < int(lines_.size()) - 1) { ++cursorLine_; cursorColumn_ = 0; }
    }
    void MoveUp() {
        if (cursorLine_ > 0) { --cursorLine_; cursorColumn_ = std::min(cursorColumn_, len(cursorLine_)); }
    }
    void MoveDown() {
        if (cursorLine_ < int(lines_.size()) - 1) { ++cursorLine_; cursorColumn_ = std::min(cursorColumn_, len(cursorLine_)); }
    }

    void EnsureCursorInBounds() {
        cursorLine_ = std::clamp(cursorLine_, 0, int(lines_.size()) - 1);
        cursorColumn_ = std::clamp(cursorColumn_, 0, len(cursorLine_));
    }
    void EnsureCursorVisible(int linesPerPage) {
        firstVisibleLine_ = std::clamp(firstVisibleLine_, 0, std::max(0, int(lines_.size()) - linesPerPage));
        if (cursorLine_ < firstVisibleLine_) firstVisibleLine_ = cursorLine_;
        else if (cursorLine_ >= firstVisibleLine_ + linesPerPage) firstVisibleLine_ = cursorLine_ - linesPerPage + 1;
    }
    int GetLinesPerPage() const { return std::max(1, std::max(1, ContentRect().Height - 4 - Padding) / lineHeight()); }
};

} // namespace c93::svc
