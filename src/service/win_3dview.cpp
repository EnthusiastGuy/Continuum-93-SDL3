// UI/VirtualScreen3DWindow.cs ("3D VIEW"): the video layers as a stack of planes in front of a starfield,
// rocking slowly; the left button rotates (and stops the rocking), the middle button pans, the wheel zooms.
//
// The C# drew with BasicEffect on the GPU. Here the same matrices (XNA's row-vector math) run on the CPU,
// every quad is cut into a grid so SDL_RenderGeometry's affine texturing stays within a fraction of a texel
// of perspective-correct, and triangles are clipped to the frustum as the GPU did. Works on any renderer.
// Video3DWindow.cs is not ported: ServiceGraphics never created it.
#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "log.h"
#include "machine.h"
#include "netrandom.h"
#include "service/video_parser.h"

namespace c93::svc {
namespace {

struct V3 { float x = 0, y = 0, z = 0; };
struct Mat {   // XNA Matrix: row vectors, m[r][c] = M(r+1)(c+1)
    float m[4][4]{};
    static Mat identity() { Mat r; for (int i = 0; i < 4; ++i) r.m[i][i] = 1; return r; }
};
Mat operator*(const Mat& a, const Mat& b) {
    Mat r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
    return r;
}
Mat translation(float x, float y, float z) { Mat r = Mat::identity(); r.m[3][0] = x; r.m[3][1] = y; r.m[3][2] = z; return r; }
Mat rotation(int axis, float a) {   // CreateRotationX/Y/Z
    float c = float(std::cos(double(a))), s = float(std::sin(double(a)));
    Mat r = Mat::identity();
    if (axis == 0) { r.m[1][1] = c; r.m[1][2] = s; r.m[2][1] = -s; r.m[2][2] = c; }
    if (axis == 1) { r.m[0][0] = c; r.m[0][2] = -s; r.m[2][0] = s; r.m[2][2] = c; }
    if (axis == 2) { r.m[0][0] = c; r.m[0][1] = s; r.m[1][0] = -s; r.m[1][1] = c; }
    return r;
}
V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 normalize(V3 v) { float l = 1.0f / std::sqrt(dot(v, v)); return {v.x * l, v.y * l, v.z * l}; }   // NaN at 0, as XNA
Mat lookAt(V3 pos, V3 target, V3 up) {   // CreateLookAt
    V3 z = normalize(sub(pos, target)), x = normalize(cross(up, z)), y = cross(z, x);
    Mat r = Mat::identity();
    r.m[0][0] = x.x; r.m[0][1] = y.x; r.m[0][2] = z.x;
    r.m[1][0] = x.y; r.m[1][1] = y.y; r.m[1][2] = z.y;
    r.m[2][0] = x.z; r.m[2][1] = y.z; r.m[2][2] = z.z;
    r.m[3][0] = -dot(x, pos); r.m[3][1] = -dot(y, pos); r.m[3][2] = -dot(z, pos);
    return r;
}
Mat perspective(float fov, float aspect, float n, float f) {   // CreatePerspectiveFieldOfView (clip z in 0..w)
    float ys = 1.0f / float(std::tan(double(fov) * 0.5));
    Mat r;
    r.m[0][0] = ys / aspect; r.m[1][1] = ys; r.m[2][2] = f / (n - f); r.m[2][3] = -1; r.m[3][2] = n * f / (n - f);
    return r;
}

struct CV { float x, y, z, w, u, v; };   // clip-space vertex with its texture coordinate
CV xform(V3 p, float u, float v, const Mat& m) {
    return {p.x * m.m[0][0] + p.y * m.m[1][0] + p.z * m.m[2][0] + m.m[3][0], p.x * m.m[0][1] + p.y * m.m[1][1] + p.z * m.m[2][1] + m.m[3][1],
            p.x * m.m[0][2] + p.y * m.m[1][2] + p.z * m.m[2][2] + m.m[3][2], p.x * m.m[0][3] + p.y * m.m[1][3] + p.z * m.m[2][3] + m.m[3][3], u, v};
}
// Signed distances to the six frustum planes: 0 <= z <= w, -w <= x, y <= w.
float plane(const CV& c, int i) {
    switch (i) {
    case 0: return c.z;
    case 1: return c.w - c.z;
    case 2: return c.w + c.x;
    case 3: return c.w - c.x;
    case 4: return c.w + c.y;
    default: return c.w - c.y;
    }
}
CV mix(const CV& a, const CV& b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t, a.u + (b.u - a.u) * t, a.v + (b.v - a.v) * t};
}

// Collects screen-space triangles for one SDL_RenderGeometry call.
struct Batch {
    std::vector<SDL_Vertex> v;
    float W = 0, H = 0;
    SDL_Vertex screen(const CV& c) const {   // viewport transform; SDL's pixel centres are at +0.5, like D3D's
        float iw = 1.0f / c.w;
        return {{(c.x * iw + 1) * 0.5f * W, (1 - c.y * iw) * 0.5f * H}, {1, 1, 1, 1}, {c.u, c.v}};
    }
    void triangle(const CV& a, const CV& b, const CV& c) {
        bool inside = true;
        for (int p = 0; p < 6 && inside; ++p) inside = plane(a, p) >= 0 && plane(b, p) >= 0 && plane(c, p) >= 0;
        if (inside) { v.push_back(screen(a)); v.push_back(screen(b)); v.push_back(screen(c)); return; }
        CV poly[12] = {a, b, c}, out[12];   // Sutherland-Hodgman: each plane adds at most one vertex
        int n = 3;
        for (int p = 0; p < 6 && n; ++p) {
            int m = 0;
            for (int i = 0; i < n; ++i) {
                const CV &s = poly[i], &e = poly[(i + 1) % n];
                float ds = plane(s, p), de = plane(e, p);
                if (ds >= 0) out[m++] = s;
                if ((ds >= 0) != (de >= 0)) out[m++] = mix(s, e, ds / (ds - de));
            }
            n = m;
            for (int i = 0; i < n; ++i) poly[i] = out[i];
        }
        for (int i = 1; i + 1 < n; ++i) { v.push_back(screen(poly[0])); v.push_back(screen(poly[i])); v.push_back(screen(poly[i + 1])); }
    }
    // A textured quad (corners top-left, top-right, bottom-left, bottom-right; uv 0..1) as an n x n grid.
    void quad(const V3 c[4], const Mat& wvp, int n) {
        std::vector<CV> g((n + 1) * (n + 1));
        for (int j = 0; j <= n; ++j)
            for (int i = 0; i <= n; ++i) {
                float s = float(i) / n, t = float(j) / n;
                auto lerp3 = [](V3 a, V3 b, float k) { return V3{a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k}; };
                g[j * (n + 1) + i] = xform(lerp3(lerp3(c[0], c[1], s), lerp3(c[2], c[3], s), t), s, t, wvp);
            }
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {   // the strip's split: (0,1,2), (1,3,2)
                const CV &p0 = g[j * (n + 1) + i], &p1 = g[j * (n + 1) + i + 1], &p2 = g[(j + 1) * (n + 1) + i], &p3 = g[(j + 1) * (n + 1) + i + 1];
                triangle(p0, p1, p2);
                triangle(p1, p3, p2);
            }
    }
    void draw(SDL_Texture* t) {
        if (!v.empty()) SDL_RenderGeometry(renderer(), t, v.data(), int(v.size()), nullptr, 0);
        v.clear();
    }
};

// A GPU line (diamond-exit rule): along the major axis, every pixel whose centre the segment spans, in the
// row (column) the line crosses at that centre. SDL_RenderLine's Bresenham steps half a pixel elsewhere.
void line(const Batch& b, CV p, CV q, std::vector<SDL_FPoint>& pts) {
    float t0 = 0, t1 = 1;
    for (int i = 0; i < 6; ++i) {
        float dp = plane(p, i), dq = plane(q, i);
        if (dp < 0 && dq < 0) return;
        if (dp < 0) t0 = std::max(t0, dp / (dp - dq));
        else if (dq < 0) t1 = std::min(t1, dp / (dp - dq));
    }
    if (t0 > t1) return;
    SDL_FPoint a = b.screen(mix(p, q, t0)).position, c = b.screen(mix(p, q, t1)).position;
    bool xMajor = std::fabs(c.x - a.x) >= std::fabs(c.y - a.y);
    float a0 = xMajor ? a.x : a.y, a1 = xMajor ? c.x : c.y, o0 = xMajor ? a.y : a.x, o1 = xMajor ? c.y : c.x;
    if (a0 > a1) { std::swap(a0, a1); std::swap(o0, o1); }
    float lim = xMajor ? b.W : b.H, other = xMajor ? b.H : b.W;
    for (int i = std::max(0, int(std::ceil(a0 - 0.5f))); i < lim && i + 0.5f < a1; ++i) {
        float o = std::floor(o0 + (o1 - o0) * ((i + 0.5f - a0) / (a1 - a0)));
        if (o < 0 || o >= other) continue;
        pts.push_back(xMajor ? SDL_FPoint{float(i), o} : SDL_FPoint{o, float(i)});
    }
}

constexpr Color BorderColor{138, 43, 226};   // Color.BlueViolet
constexpr float LabelBaseY = 1.1f, LabelStepY = -0.32f, LabelX = -2.55f, LabelZOffsetTowardCamera = -0.0005f, SkyboxSize = 50;
// Measured against the reference at fitted angles: 64 leaves ~15 of 104k pixels different, 24 left ~80.
constexpr int LayerGrid = 64, SkyGrid = 64, LabelGrid = 8;

// GenerateStarfieldTexture / GetStarColor: System.Random(42), so the same sky as the reference.
std::vector<Color> starfield(int width, int height) {
    NetRandom random(42);
    std::vector<Color> data(size_t(width) * height, Color{5, 8, 15, 255});
    auto starColor = [&](int b) -> Color {
        float temp = float(random.nextDouble());
        auto k = [b](float f) { return uint8_t(int(float(b) * f)); };
        if (temp > 0.95f) return {uint8_t(b), uint8_t(b), 255, 255};
        if (temp > 0.8f) return {uint8_t(b), uint8_t(b), uint8_t(b), 255};
        if (temp > 0.5f) return {uint8_t(b), uint8_t(b), k(0.8f), 255};
        if (temp > 0.2f) return {uint8_t(b), k(0.8f), k(0.5f), 255};
        return {uint8_t(b), k(0.6f), k(0.4f), 255};
    };
    int numStars = width * height / 100;
    for (int i = 0; i < numStars; ++i) {
        int x = random.next(0, width), y = random.next(0, height), idx = y * width + x;
        float brightness = float(random.nextDouble());
        if (brightness > 0.98f) {
            Color s = starColor(240 + random.next(0, 15));
            data[idx] = s;
            if (x > 0) data[idx - 1] = lerp(data[idx - 1], s, 0.3f);
            if (x < width - 1) data[idx + 1] = lerp(data[idx + 1], s, 0.3f);
            if (y > 0) data[idx - width] = lerp(data[idx - width], s, 0.3f);
            if (y < height - 1) data[idx + width] = lerp(data[idx + width], s, 0.3f);
        } else if (brightness > 0.95f) data[idx] = starColor(200 + random.next(0, 40));
        else if (brightness > 0.85f) data[idx] = starColor(150 + random.next(0, 50));
        else if (brightness > 0.7f) data[idx] = starColor(100 + random.next(0, 50));
        else data[idx] = starColor(60 + random.next(0, 40));
    }
    for (int i = 0; i < 50; ++i) {   // faint nebulae; they lower the alpha too (Color.Lerp lerps it)
        int cx = random.next(0, width), cy = random.next(0, height), radius = 20 + random.next(0, 60), kind = random.next(0, 3);
        Color neb = kind == 0 ? Color{20, 15, 30, 30} : kind == 1 ? Color{15, 20, 35, 30} : Color{30, 15, 20, 30};
        for (int dy = -radius; dy <= radius; ++dy)
            for (int dx = -radius; dx <= radius; ++dx) {
                int px = cx + dx, py = cy + dy;
                if (px < 0 || px >= width || py < 0 || py >= height) continue;
                float distance = std::sqrt(float(dx * dx + dy * dy));
                if (distance > radius) continue;
                float falloff = 1.0f - distance / radius;
                falloff *= falloff;
                Color& d = data[py * width + px];
                d = lerp(d, neb, falloff * 0.15f);
            }
    }
    return data;
}

class VirtualScreen3DWindow : public Window {
public:
    VirtualScreen3DWindow() : Window("3D VIEW", 1480, 240, 400, 300, 0.8f, true, false) {}
    ~VirtualScreen3DWindow() override {   // the renderer is still alive (service shutdown runs first)
        for (SDL_Texture* t : labels_) if (t) SDL_DestroyTexture(t);
        if (sky_) SDL_DestroyTexture(sky_);
        if (rt_) SDL_DestroyTexture(rt_);
    }

    bool HandleInput(const MouseState& mouse, const MouseState& prevMouse) override {
        bool baseHandled = Window::HandleInput(mouse, prevMouse);
        V3 current{float(mouse.X), float(mouse.Y), float(mouse.ScrollWheelValue)};
        if (!ContentRect().Contains(mouse.X, mouse.Y) || mouse.Y <= Y + TitleBarHeight) {
            last_ = current;   // no jump when the mouse comes back
            return baseHandled;
        }
        if (current.z > last_.z) cam_.z -= 0.1f;
        else if (current.z < last_.z) cam_.z += 0.1f;
        if (mouse.Left) {
            autoRotate_ = false;
            if (!prevMouse.Left) last_ = current;
            rot_.y += (current.x - last_.x) * 0.01f;
            rot_.x += (current.y - last_.y) * 0.01f;
        } else if (mouse.Middle) {
            if (!prevMouse.Middle) last_ = current;
            pan_.x += (current.x - last_.x) * 0.01f;
            pan_.y -= (current.y - last_.y) * 0.01f;
        }
        last_ = current;
        return true;   // hovering the view takes the input (and focus), as in the reference
    }

protected:
    void OnResized() override {
        Window::OnResized();
        if (rt_) { SDL_DestroyTexture(rt_); rt_ = nullptr; }
    }

    void UpdateContent(float) override {
        if (!initialized_) Initialize();
        video::Update();
        if (autoRotate_) {   // GameTimePlus.GetTotalMs: time since start, as a float
            float time = float(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - machine().powerOn).count());
            rot_.y = 0.7f * std::sin(0.0001f * time);
        }
        Render3DContent();
    }

    void DrawContent(const Rect& c) override {
        if (!initialized_ || !rt_) return;
        SDL_FRect d{float(c.X), float(c.Y), float(c.Width), float(c.Height)};   // a stale target stretches, as in the C#
        SDL_RenderTexture(renderer(), rt_, nullptr, &d);
    }

private:
    void Initialize() {
        SDL_Renderer* r = renderer();
        video::InitializeLayers();
        // GenerateLayerLabelTextures: the digit centred in 64x32, BlueViolet, no outline.
        SDL_Texture* old = SDL_GetRenderTarget(r);
        const Font& font = fonts.ModernDOS_12x18;
        for (int i = 0; i < 8; ++i) {
            SDL_Texture* t = labels_[i] = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 64, 32);
            if (!t) continue;
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
            SDL_SetRenderTarget(r, t);
            SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
            SDL_RenderClear(r);
            std::string label = std::to_string(i);
            TextSize sz = font.MeasureText(label, 0, 0);
            font.DrawString(label, (64 - sz.width) * 0.5f, (32 - sz.height) * 0.5f, BorderColor, 64, 0, colors::Black, 0);
        }
        SDL_SetRenderTarget(r, old);
        std::vector<Color> sky = starfield(1024, 1024);
        sky_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 1024, 1024);
        if (sky_) {
            SDL_UpdateTexture(sky_, nullptr, sky.data(), 1024 * 4);
            SDL_SetTextureBlendMode(sky_, SDL_BLENDMODE_NONE);   // BlendState.Opaque: alpha is written too
            SDL_SetTextureScaleMode(sky_, SDL_SCALEMODE_NEAREST);
        }
        if (!sky_ || !labels_[0]) LOGW("Service: 3D view textures: %s; parts of the view are missing", SDL_GetError());
        initialized_ = true;
    }

    void Render3DContent() {
        Rect c = ContentRect();
        if (c.Width <= 0 || c.Height <= 0) return;
        SDL_Renderer* r = renderer();
        if (!rt_ || rtW_ != c.Width || rtH_ != c.Height) {
            if (rt_) SDL_DestroyTexture(rt_);
            rt_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, c.Width, c.Height);
            if (!rt_) return;
            rtW_ = c.Width; rtH_ = c.Height;
            SDL_SetTextureBlendMode(rt_, SDL_BLENDMODE_BLEND_PREMULTIPLIED);   // SpriteBatch AlphaBlend
            SDL_SetTextureScaleMode(rt_, SDL_SCALEMODE_NEAREST);
        }
        SDL_Texture* old = SDL_GetRenderTarget(r);
        SDL_SetRenderTarget(r, rt_);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);

        Mat view = lookAt(cam_, {}, {0, 1, 0});
        Mat proj = perspective(0.7853982f, float(c.Width) / float(c.Height), 0.01f, 100.0f);
        Mat rotationMatrix = translation(pan_.x, pan_.y, pan_.z) * rotation(0, rot_.x) * rotation(1, rot_.y) * rotation(2, rot_.z);
        Batch b;
        b.W = float(c.Width); b.H = float(c.Height);

        if (sky_) {   // RenderSkybox: rotates a fifth as much as the scene and never pans
            const float s = SkyboxSize;
            static const V3 faces[6][4] = {
                {{-s, s, -s}, {s, s, -s}, {-s, -s, -s}, {s, -s, -s}}, {{s, s, s}, {-s, s, s}, {s, -s, s}, {-s, -s, s}},
                {{-s, s, s}, {-s, s, -s}, {-s, -s, s}, {-s, -s, -s}}, {{s, s, -s}, {s, s, s}, {s, -s, -s}, {s, -s, s}},
                {{-s, s, s}, {s, s, s}, {-s, s, -s}, {s, s, -s}},     {{-s, -s, -s}, {s, -s, -s}, {-s, -s, s}, {s, -s, s}}};
            Mat wvp = rotation(0, rot_.x * 0.2f) * rotation(1, rot_.y * 0.2f) * view * proj;
            for (auto& f : faces) b.quad(f, wvp, SkyGrid);
            b.draw(sky_);
        }

        static const V3 quad[4] = {{-2.40f, 1.35f, 0}, {2.40f, 1.35f, 0}, {-2.40f, -1.35f, 0}, {2.40f, -1.35f, 0}};
        static const V3 labelQuad[4] = {{-0.35f, 0.15f, 0}, {0.35f, 0.15f, 0}, {-0.35f, -0.15f, 0}, {0.35f, -0.15f, 0}};
        int layerCount = std::min(video::PaletteCount, 8);
        for (int idx = layerCount - 1; idx >= 0; --idx) {   // far to near: layer 7 at z = 0, layer 0 at 1.75
            if (!video::Layers[idx]) continue;
            float z = float(7 - idx) * 0.25f;
            Mat wvp = translation(0, 0, z) * rotationMatrix * view * proj;   // World * View * Projection
            b.quad(quad, wvp, LayerGrid);
            b.draw(video::Layers[idx]);
            // DrawLayerBorder: a line strip around the quad
            CV p[4] = {xform(quad[0], 0, 0, wvp), xform(quad[1], 0, 0, wvp), xform(quad[3], 0, 0, wvp), xform(quad[2], 0, 0, wvp)};
            std::vector<SDL_FPoint> pts;
            for (int i = 0; i < 4; ++i) line(b, p[i], p[(i + 1) % 4], pts);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, BorderColor.r, BorderColor.g, BorderColor.b, 255);
            SDL_RenderPoints(r, pts.data(), int(pts.size()));
            // DrawLayerLabel: placed by draw index, numbered from the other end
            if (SDL_Texture* t = labels_[layerCount - idx - 1]) {
                Mat lw = translation(LabelX, LabelBaseY + LabelStepY * idx, 0) * translation(0, 0, z + LabelZOffsetTowardCamera) * rotationMatrix * view * proj;
                b.quad(labelQuad, lw, LabelGrid);
                b.draw(t);
            }
        }
        SDL_SetRenderTarget(r, old);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND_PREMULTIPLIED);   // what the service drawing expects
    }

    SDL_Texture* rt_ = nullptr;
    SDL_Texture* sky_ = nullptr;
    SDL_Texture* labels_[8]{};
    int rtW_ = 0, rtH_ = 0;
    bool initialized_ = false, autoRotate_ = true;
    V3 rot_, pan_, cam_{0, 0, 6}, last_;
};

} // namespace

C93_SERVICE_WINDOW(110, VirtualScreen3DWindow);

} // namespace c93::svc
