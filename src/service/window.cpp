// UI/Window.cs, UI/WindowManager.cs, UI/Taskbar.cs, UI/TextWindow.cs.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <random>

#include "service/ui.h"

namespace c93::svc {

// --- Window ----------------------------------------------------------------------------------
Window::Window(std::string title, int x, int y, int width, int height, float spawnDelaySeconds, bool canResize, bool canClose)
    : Title(std::move(title)), X(x), Y(y), Width(width), Height(TitleBarHeight), spawnDelay_(spawnDelaySeconds),
      initialTargetHeight_(height), canResize_(canResize), canClose_(canClose) {}

void Window::ApplyLayout(int x, int y, int width, int height, bool visible) {
    X = x; Y = y; Width = width; Height = height; Visible = visible;
    initialTargetHeight_ = height;
    isSpawning_ = false;
    spawnFade_ = 1;
}

bool Window::HandleInput(const MouseState& mouse, const MouseState& prevMouse) {
    if (!Visible) return false;
    int mx = mouse.X, my = mouse.Y;
    bool leftPressed = mouse.Left, leftJustPressed = leftPressed && !prevMouse.Left, leftJustReleased = !leftPressed && prevMouse.Left;

    if (canClose_ && leftJustPressed && CloseButtonRect().Contains(mx, my)) {
        if (Manager) Manager->Remove(this);
        return true;
    }

    Hit hit = canResize_ && !isResizing_ && !isDragging_ ? GetResizeHit(mx, my) : Hit::None;
    if (canResize_ && leftJustPressed && hit != Hit::None && !isDragging_) {
        isResizing_ = true;
        IsFocused = true;
        fromLeft_ = hit == Hit::Left;
        fromRight_ = hit == Hit::Right || hit == Hit::BottomRight;
        fromTop_ = hit == Hit::Top;
        fromBottom_ = hit == Hit::Bottom || hit == Hit::BottomRight;
        resizeStartMouseX_ = mx; resizeStartMouseY_ = my;
        resizeStartW_ = Width; resizeStartH_ = Height;
        resizeStartX_ = X; resizeStartY_ = Y;
        resizeStartRight_ = X + Width; resizeStartBottom_ = Y + Height;
        return true;
    }

    if (leftJustPressed && hit == Hit::None && TitleBarRect().Contains(mx, my) && !isResizing_) {
        isDragging_ = true;
        IsFocused = true;
        dragOffX_ = mx - X; dragOffY_ = my - Y;
        return true;
    }

    if (isDragging_) {
        if (leftPressed) { X = mx - dragOffX_; Y = my - dragOffY_; dragChanged_ = true; }
        ClampToScreen();
        SnapToOtherWindows();
        if (leftJustReleased) {
            isDragging_ = false;
            if (dragChanged_) { saveLayout(); dragChanged_ = false; }
        }
        return true;
    }

    if (canResize_ && isResizing_) {
        if (leftPressed) {
            int dx = mx - resizeStartMouseX_, dy = my - resizeStartMouseY_;
            int nx = resizeStartX_, ny = resizeStartY_, nw = resizeStartW_, nh = resizeStartH_;
            if (fromLeft_) { nx = resizeStartX_ + dx; nw = resizeStartRight_ - nx; }
            else if (fromRight_) nw = resizeStartW_ + dx;
            if (fromTop_) { ny = resizeStartY_ + dy; nh = resizeStartBottom_ - ny; }
            else if (fromBottom_) nh = resizeStartH_ + dy;
            if (nw < MinWidth) { int d = MinWidth - nw; nw = MinWidth; if (fromLeft_) nx -= d; }
            if (nw > MaxWidth) nw = MaxWidth;
            if (nh < MinHeight) { int d = MinHeight - nh; nh = MinHeight; if (fromTop_) ny -= d; }
            if (nh > MaxHeight) nh = MaxHeight;
            SnapResizeToOtherWindows(nx, ny, nw, nh, fromLeft_, fromRight_, fromTop_, fromBottom_);
            X = nx; Y = ny; Width = nw; Height = nh;
            dragChanged_ = true;
            OnResized();
        }
        if (leftJustReleased) {
            isResizing_ = false;
            fromLeft_ = fromRight_ = fromTop_ = fromBottom_ = false;
            if (dragChanged_) { saveLayout(); dragChanged_ = false; }
        }
        return true;
    }

    if (leftJustPressed && Bounds().Contains(mx, my)) {   // click focusing
        IsFocused = true;
        saveLayout();
        return true;
    }
    return false;
}

void Window::Update(float dt) {
    if (!Visible) return;
    float target = IsFocused ? 1.0f : 0.0f;
    if (isSpawning_) {
        if (spawnDelay_ > 0) { spawnDelay_ -= dt; return; }
        spawnTime_ += dt;
        float t = clampf(spawnTime_ / spawnDuration_, 0, 1);
        float eased = t * t * (3 - 2 * t);
        Height = int(lerpf(float(TitleBarHeight), float(initialTargetHeight_), eased));   // grows from the title bar
        spawnFade_ = eased;
        if (t >= 1) isSpawning_ = false;
    }
    focusBlend_ = lerpf(focusBlend_, target, dt * 5.0f);   // ~200 ms
    UpdateContent(dt);
}

void Window::Draw() {
    if (!Visible) return;
    DrawChrome();
    DrawContentClipped(ContentRect());
}

void Window::DrawChrome() {
    Rect b = Bounds();
    Color title = IsOnTop ? lerp(theme.WindowTitleBarFocused, theme.WindowTitleBarOnTop, focusBlend_)
                          : lerp(theme.WindowTitleBarUnfocused, theme.WindowTitleBarFocused, focusBlend_);
    if (ShouldDrawBackground()) fill(b, lerp(theme.WindowBackgroundUnfocused, theme.WindowBackgroundFocused, focusBlend_));
    fill(TitleBarRect(), title);
    fill({b.X, b.Y, b.Width, BorderWidth}, theme.WindowBorder);
    fill({b.X, b.Bottom() - BorderWidth, b.Width, BorderWidth}, theme.WindowBorder);
    fill({b.X, b.Y, BorderWidth, b.Height}, theme.WindowBorder);
    fill({b.Right() - BorderWidth, b.Y, BorderWidth, b.Height}, theme.WindowBorder);
    if (canResize_) fill(ResizeGripRect(), theme.WindowResizeGrip);
    DrawText(fonts.ModernDOS_12x18, Title, b.X + Padding, b.Y + 4, b.Width - Padding * 2, theme.WindowTitleText,
             theme.TextOutline, DrawOutline, 0xFF);
    if (canClose_) {
        Rect c = CloseButtonRect();
        fill(c, theme.WindowCloseButton);
        DrawText(fonts.ModernDOS_12x18, "X", c.X + 3, c.Y - 2, 20, theme.WindowCloseButtonText, theme.TextOutline, DrawOutline, 0xFF);
    }
}

void Window::DrawContentClipped(const Rect& contentRect) {
    // Rectangle.Intersect: Intersects() ignores the sign of a size, so a window shorter than its padding
    // (STATUS at height 30) yields a negative scissor. OpenGL rejects that and MonoGame keeps the last valid
    // one, the full screen: such content draws unclipped, past the window. Reproduced.
    Rect r = intersect({contentRect.X + 4, contentRect.Y + 4, contentRect.Width - 8, contentRect.Height - 8}, {0, 0, screenW(), screenH()});
    setClip(r.Width < 0 || r.Height < 0 ? nullptr : &r);
    DrawContent(contentRect);
    setClip(nullptr);
}

void Window::ClampToScreen() {
    if (X < 0) X = 0;
    if (X + Width > screenW()) X = screenW() - Width;
    if (Y < 0) Y = 0;
    if (Y + Height > screenH()) Y = screenH() - Height;
}

static int magneticSnap(int current, int target) {
    return std::abs(current - target) <= Window::SnapDetectDistance ? target : current;
}

void Window::SnapToOtherWindows() {
    if (!Manager) return;
    int left = X, right = X + Width, top = Y, bottom = Y + Height;
    auto setX = [&](int x) { X = x; left = X; right = X + Width; };
    auto setY = [&](int y) { Y = y; top = Y; bottom = Y + Height; };
    for (Window* o : Manager->Windows) {
        if (o == this || !o->Visible) continue;
        int oL = o->X, oR = o->X + o->Width, oT = o->Y, oB = o->Y + o->Height;
        bool vOverlap = bottom > oT && top < oB, hOverlap = right > oL && left < oR;
        bool vAttached = std::abs(top - (oB + SnapGap)) <= SnapDetectDistance || std::abs(bottom - (oT - SnapGap)) <= SnapDetectDistance;
        bool hAttached = std::abs(left - (oR + SnapGap)) <= SnapDetectDistance || std::abs(right - (oL - SnapGap)) <= SnapDetectDistance;
        if (vOverlap) {
            if (int s = magneticSnap(left, oR + SnapGap); s != left) setX(s);
            if (int s = magneticSnap(right, oL - SnapGap); s != right) setX(s - Width);
        }
        if (hOverlap) {
            if (int s = magneticSnap(top, oB + SnapGap); s != top) setY(s);
            if (int s = magneticSnap(bottom, oT - SnapGap); s != bottom) setY(s - Height);
        }
        if (vAttached) {
            if (int s = magneticSnap(left, oL); s != left) setX(s);
            if (int s = magneticSnap(right, oR); s != right) setX(s - Width);
        }
        if (hAttached) {
            if (int s = magneticSnap(top, oT); s != top) setY(s);
            if (int s = magneticSnap(bottom, oB); s != bottom) setY(s - Height);
        }
    }
    // Screen edges, SnapGap in.
    if (int s = magneticSnap(X, SnapGap); s != X) X = s;
    if (int s = magneticSnap(X + Width, screenW() - SnapGap); s != X + Width) X = s - Width;
    if (int s = magneticSnap(Y, SnapGap); s != Y) Y = s;
    if (int s = magneticSnap(Y + Height, screenH() - SnapGap); s != Y + Height) Y = s - Height;
}

void Window::SnapResizeToOtherWindows(int& newX, int& newY, int& newWidth, int& newHeight, bool fromLeft, bool fromRight,
                                      bool fromTop, bool fromBottom) {
    if (!Manager) return;
    int nL = newX, nT = newY, nR = newX + newWidth, nB = newY + newHeight;
    for (Window* o : Manager->Windows) {
        if (o == this || !o->Visible) continue;
        int oL = o->X, oR = o->X + o->Width, oT = o->Y, oB = o->Y + o->Height;
        bool hOverlap = nR > oL && nL < oR, vOverlap = nB > oT && nT < oB;
        bool vAttached = std::abs(nT - (oB + SnapGap)) <= SnapDetectDistance || std::abs(nB - (oT - SnapGap)) <= SnapDetectDistance;
        bool hAttached = std::abs(nL - (oR + SnapGap)) <= SnapDetectDistance || std::abs(nR - (oL - SnapGap)) <= SnapDetectDistance;
        if (hOverlap || hAttached) {
            if (fromBottom)
                for (int target : {oT - SnapGap, oB + SnapGap, oB})
                    if (int s = magneticSnap(nB, target); s != nB) { nB = s; newHeight = nB - newY; }
            if (fromTop)
                for (int target : {oB + SnapGap, oT})
                    if (int s = magneticSnap(nT, target); s != nT) { nT = s; newY = nT; newHeight = resizeStartBottom_ - newY; nB = newY + newHeight; }
        }
        if (vOverlap || vAttached) {
            if (fromRight)
                for (int target : {oL - SnapGap, oR + SnapGap, oR})
                    if (int s = magneticSnap(nR, target); s != nR) { nR = s; newWidth = nR - newX; }
            if (fromLeft)
                for (int target : {oR + SnapGap, oL})
                    if (int s = magneticSnap(nL, target); s != nL) { nL = s; newX = nL; newWidth = resizeStartRight_ - newX; nR = newX + newWidth; }
        }
    }
    newX = nL; newY = nT; newWidth = nR - nL; newHeight = nB - nT;
}

Window::Hit Window::GetResizeHit(int px, int py) const {
    if (!canResize_) return Hit::None;
    if (ResizeGripRect().Contains(px, py)) return Hit::BottomRight;
    if (Rect{X, Y + TitleBarHeight, BorderWidth * 4, Height - TitleBarHeight - BorderWidth}.Contains(px, py)) return Hit::Left;
    if (Rect{X + Width - BorderWidth * 4, Y + TitleBarHeight, BorderWidth * 4, Height - TitleBarHeight - BorderWidth}.Contains(px, py)) return Hit::Right;
    if (Rect{X + BorderWidth, Y, Width - BorderWidth * 2, BorderWidth * 4}.Contains(px, py)) return Hit::Top;
    if (Rect{X + BorderWidth, Y + Height - BorderWidth * 4, Width - BorderWidth * 2, BorderWidth * 4}.Contains(px, py)) return Hit::Bottom;
    return Hit::None;
}

void Window::UpdateCursor(const MouseState& mouse) {
    if (!canResize_ || !Visible) return;
    if (isResizing_) { setCursor(CursorForResizeFlags()); return; }
    switch (GetResizeHit(mouse.X, mouse.Y)) {
    case Hit::Left: case Hit::Right: setCursor(Cursor::SizeWE); break;
    case Hit::Top: case Hit::Bottom: setCursor(Cursor::SizeNS); break;
    case Hit::BottomRight: setCursor(Cursor::SizeNWSE); break;
    default: break;
    }
}

Cursor Window::CursorForResizeFlags() const {
    bool horz = fromLeft_ || fromRight_, vert = fromTop_ || fromBottom_;
    return horz && vert ? Cursor::SizeNWSE : horz ? Cursor::SizeWE : vert ? Cursor::SizeNS : Cursor::Arrow;
}

void TextWindow::DrawContent(const Rect& c) {
    DrawText(*theme.PrimaryFont, text_, c.X + Padding, c.Y + Padding, c.Width - Padding * 2, theme.TextTitle,
             theme.TextOutline, Wrap | DrawOutline | MonospaceCentering, 0xFF);
}

// --- WindowManager -------------------------------------------------------------------------------
void WindowManager::Add(std::unique_ptr<Window> w) {
    w->Manager = this;
    Windows.push_back(w.get());
    owned_.push_back(std::move(w));
}

void WindowManager::Remove(Window* w) {
    auto it = std::find(Windows.begin(), Windows.end(), w);
    if (it != Windows.end()) Windows.erase(it);
}

void WindowManager::BringToFront(Window* w) {
    auto it = std::find(Windows.begin(), Windows.end(), w);
    if (it != Windows.end()) { Windows.erase(it); Windows.push_back(w); }
    for (Window* x : Windows) x->IsOnTop = false;
    if (!Windows.empty()) Windows.back()->IsOnTop = true;
}

// The first visible popup (in list order) under the mouse, and its owner.
static Window* popupUnder(const std::vector<Window*>& ws, int x, int y, Window** owner) {
    for (Window* w : ws) {
        if (!w->Visible) continue;
        Window* p = w->HoverPopup();
        if (p && p->Visible && p->Bounds().Contains(x, y)) { if (owner) *owner = w; return p; }
    }
    return nullptr;
}

void WindowManager::HandleInput(const MouseState& mouse, const MouseState& prevMouse) {
    setCursor(Cursor::Arrow);
    if (focused_ && !focused_->Visible) focused_ = nullptr;
    if (Window* popup = popupUnder(Windows, mouse.X, mouse.Y, nullptr)) {   // a popup blocks everything under it
        popup->HandleInput(mouse, prevMouse);
        popup->UpdateCursor(mouse);
        return;
    }
    for (int i = int(Windows.size()) - 1; i >= 0; --i)
        if (Windows[i]->Visible && Windows[i]->Bounds().Contains(mouse.X, mouse.Y)) { Windows[i]->UpdateCursor(mouse); break; }
    for (int i = int(Windows.size()) - 1; i >= 0; --i) {
        Window* w = Windows[i];
        if (w->Visible && w->HandleInput(mouse, prevMouse)) { focused_ = w; BringToFront(w); break; }
    }
    for (Window* w : Windows) w->IsFocused = w == focused_;
}

void WindowManager::Update(float dt) {
    const MouseState& m = mouse();
    Window* popupOwner = nullptr;
    popupUnder(Windows, m.X, m.Y, &popupOwner);
    Window* top = nullptr;
    if (!popupOwner)
        for (int i = int(Windows.size()) - 1; i >= 0; --i)
            if (Windows[i]->Visible && Windows[i]->Bounds().Contains(m.X, m.Y)) { top = Windows[i]; break; }
    // Copy: an update may add a window (none do today, but the C# iterated the live list).
    std::vector<Window*> ws = Windows;
    for (Window* w : ws) {
        if (!w->Visible) continue;
        bool under = w->Bounds().Contains(m.X, m.Y);
        // Only the window under the mouse (or the popup's owner) gets hover-sensitive updates.
        if (popupOwner ? (w == popupOwner || !under) : (!top || w == top || !under)) w->Update(dt);
    }
    for (Window* w : ws)
        if (w->Visible)
            if (Window* p = w->HoverPopup(); p && p->Visible) p->Update(dt);
}

void WindowManager::Draw() {
    for (Window* w : Windows) if (w->Visible) w->Draw();
    taskbar.Draw();
    for (Window* w : Windows)
        if (w->Visible)
            if (Window* p = w->HoverPopup(); p && p->Visible) p->Draw();
}

// --- Taskbar ---------------------------------------------------------------------------------------
bool Taskbar::Update(float dt, const MouseState& mouse, const MouseState& prevMouse) {
    bool captured = false;
    int sw = screenW(), sh = screenH();
    Rect bar = barRect(sw, sh);
    const int HotZoneWidth = 4;
    bool inHot = Rect{sw - HotZoneWidth, 0, HotZoneWidth, sh}.Contains(mouse.X, mouse.Y);
    bool onBar = bar.Width > 0 && bar.Contains(mouse.X, mouse.Y);
    open_ = clampf(lerpf(open_, onBar || inHot ? 1.0f : 0.0f, dt * 10.0f), 0, 1);
    bar = barRect(sw, sh);
    if (open_ > 0.2f && bar.Contains(mouse.X, mouse.Y)) scrollOffset_ -= mouse.Y - prevMouse.Y;   // Mac-like scroll
    if (open_ > 0.2f && mouse.Left && !prevMouse.Left && bar.Contains(mouse.X, mouse.Y)) {
        if (Rect{bar.X + 4, bar.Y + 4, bar.Width - 8, HeaderHeight - 8}.Contains(mouse.X, mouse.Y)) {
            SpawnDummyWindow();
            captured = true;
        } else {
            int y = bar.Y + HeaderHeight - scrollOffset_;
            for (Window* w : manager_.Windows) {
                if (Rect{bar.X + 4, y, bar.Width - 8, ItemHeight}.Contains(mouse.X, mouse.Y)) {
                    if (!w->Visible) { w->Visible = true; manager_.BringToFront(w); }
                    else w->Visible = false;   // minimise
                    saveLayout();
                    captured = true;
                    break;
                }
                y += ItemHeight + ItemSpacing;
            }
        }
    }
    return captured;
}

void Taskbar::Draw() {
    if (open_ <= 0.01f) return;
    Rect bar = barRect(screenW(), screenH());
    fill(bar, theme.TaskbarBackground);
    fill({bar.X, bar.Y, 1, bar.Height}, theme.TaskbarBorder);
    Rect header{bar.X + 4, bar.Y + 4, bar.Width - 8, HeaderHeight - 8};
    fill(header, theme.TaskbarHeaderBackground);
    DrawText(fonts.ModernDOS_12x18, "[ Settings ]", header.X + 6, header.Y + 4, header.Width - 12, theme.TextCyan,
             theme.TextOutline, DrawOutline, 0xFF);
    int y = bar.Y + HeaderHeight - scrollOffset_;
    contentHeight_ = 0;
    for (Window* w : manager_.Windows) {
        Rect item{bar.X + 4, y, bar.Width - 8, ItemHeight};
        contentHeight_ += ItemHeight + ItemSpacing;
        if (item.Bottom() < bar.Top() || item.Top() > bar.Bottom()) { y += ItemHeight + ItemSpacing; continue; }
        Color bg = w->IsOnTop && w->Visible ? theme.TaskbarItemActiveBackground
                 : !w->Visible ? theme.TaskbarItemMinimizedBackground : theme.TaskbarItemNormalBackground;
        fill(item, bg);
        DrawText(*theme.PrimaryFont, w->Title, item.X + 6, item.Y + 4, item.Width - 12, theme.TextPrimary, theme.TextOutline,
                 DrawOutline, 0xFF);
        y += ItemHeight + ItemSpacing;
    }
    int visible = bar.Height - HeaderHeight;
    if (contentHeight_ <= visible) scrollOffset_ = 0;
    else scrollOffset_ = std::clamp(scrollOffset_, 0, contentHeight_ - visible);
}

void Taskbar::SpawnDummyWindow() {   // placeholder for the "[ Settings ]" button, as the reference
    static std::mt19937 rnd{std::random_device{}()};
    int x = 40 + int(rnd() % 120), y = 40 + int(rnd() % 80);
    int w = std::min(360, screenW() - x - 40), h = std::min(220, screenH() - y - 40);
    auto win = std::make_unique<TextWindow>("Taskbar test", "This window was spawned from the taskbar settings button.\n\n",
                                            x, y, w, h, 0.05f, true, true);
    Window* p = win.get();
    manager_.Add(std::move(win));
    manager_.BringToFront(p);
}

// --- Registration --------------------------------------------------------------------------------
std::vector<std::pair<int, WindowFactory>>& registry() {
    static std::vector<std::pair<int, WindowFactory>> r;
    return r;
}
bool registerWindow(int order, WindowFactory make) {
    registry().emplace_back(order, make);
    return true;
}

} // namespace c93::svc
