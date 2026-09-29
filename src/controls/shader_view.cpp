#include "lumen/ShaderView.h"
#include "lumen/Panel.h"
#include "lumen/Painter.h"
#include <algorithm>
#include <cmath>

namespace lumen {

void ShaderView::RelayoutParent() { Control::RelayoutParent(); }

ShaderView& ShaderView::Kind(ShaderKind value) {
    if (params_.kind == value) return *this;
    params_.kind = value;
    Invalidate();
    return *this;
}

ShaderView& ShaderView::Palette(const ShaderPalette& value) {
    ShaderPalette next = value;
    next.count = std::min<uint8_t>(next.count, static_cast<uint8_t>(kShaderMaxColors));
    if (params_.palette == next) return *this;
    params_.palette = next;
    Invalidate();
    return *this;
}

ShaderView& ShaderView::Shape(const ShaderShape& value) {
    if (params_.shape == value) return *this;
    params_.shape = value;
    Invalidate();
    return *this;
}

ShaderView& ShaderView::Play(ShaderPlay value) {
    if (play_ == value) return *this;
    play_ = value;
    if (Running()) {
        ticking_ = true;
        Animate();
    }
    Invalidate();
    return *this;
}

ShaderView& ShaderView::Speed(float value) {
    speed_ = std::clamp(value, 0.0f, 20.0f);
    return *this;
}

ShaderView& ShaderView::Intensity(float value) {
    value = Clamp(value, 0.0f, 1.0f);
    if (intensity_ == value) return *this;
    intensity_ = value;
    Invalidate();
    return *this;
}

ShaderView& ShaderView::PatternScale(float value) {
    value = std::clamp(value, 0.1f, 10.0f);
    if (params_.scale == value) return *this;
    params_.scale = value;
    Invalidate();
    return *this;
}

ShaderView& ShaderView::Grain(float value) {
    value = Clamp(value, 0.0f, 1.0f);
    if (params_.grain == value) return *this;
    params_.grain = value;
    Invalidate();
    return *this;
}

ShaderView& ShaderView::Seed(float value) {
    if (params_.seed == value) return *this;
    params_.seed = value;
    Invalidate();
    return *this;
}

ShaderView& ShaderView::Center(Point relative) {
    relative = {Clamp(relative.x, -1.0f, 2.0f), Clamp(relative.y, -1.0f, 2.0f)};
    params_.center = relative;
    if (!(follow_pointer_ && inside_)) center_now_ = relative;   // 离屏 setter 直接到位
    Invalidate();
    return *this;
}

ShaderView& ShaderView::FollowPointer(bool value) {
    follow_pointer_ = value;
    return *this;
}

ShaderView& ShaderView::MaxFps(float value) {
    max_fps_ = std::clamp(value, 1.0f, 120.0f);
    return *this;
}

ShaderView& ShaderView::Time(float seconds) {
    params_.time = seconds;
    Invalidate();
    return *this;
}

ShaderView& ShaderView::CornerRadius(float value) {
    radius_ = std::max(0.0f, value);
    Invalidate();
    return *this;
}

ShaderView& ShaderView::Height(float value) {
    value = std::max(1.0f, value);
    if (height_ == value) return *this;
    height_ = value;
    RelayoutParent();
    return *this;
}

bool ShaderView::Running() const noexcept {
    if (!window_ || MotionScale() <= 0.001f || speed_ <= 0.0f) return false;
    return play_ == ShaderPlay::Always || (play_ == ShaderPlay::Hover && inside_);
}

Size ShaderView::Measure(Size available, const Theme&) {
    const float w = available.w > 0.0f && available.w < 1.0e4f ? available.w : 320.0f;
    return {w, height_};
}

void ShaderView::OnMouseEnter() {
    Control::OnMouseEnter();
    inside_ = true;
    if (Running() || follow_pointer_) {
        ticking_ = true;
        Animate();
    }
}

void ShaderView::OnMouseLeave() {
    Control::OnMouseLeave();
    inside_ = false;
    // 跟随光心需要缓回原位；其余情况停在当前帧。
    if (follow_pointer_) {
        ticking_ = true;
        Animate();
    }
}

void ShaderView::OnMouseMove(Point local, uint32_t) {
    if (absolute_.w <= 0.0f || absolute_.h <= 0.0f) return;
    pointer_ = {local.x / absolute_.w, local.y / absolute_.h};
    if (follow_pointer_ && !ticking_) {
        ticking_ = true;
        Animate();
    }
}

bool ShaderView::OnAnimate(float dt_seconds) {
    const bool base = Control::OnAnimate(dt_seconds);
    bool more = false;
    bool dirty = false;
    // 光心：悬停跟随鼠标，离开后缓回；位置变化本身就要重绘。
    const Point target = follow_pointer_ && inside_ ? pointer_ : params_.center;
    const bool mx = EaseTo(center_now_.x, target.x, dt_seconds, 10.0f, 0.0005f);
    const bool my = EaseTo(center_now_.y, target.y, dt_seconds, 10.0f, 0.0005f);
    if (mx || my) {
        more = true;
        dirty = true;
    }
    if (Running()) {
        params_.time += dt_seconds * speed_ * MotionScale();
        frame_accum_ += dt_seconds;
        const float interval = 1.0f / max_fps_;
        // 时钟默认约 60Hz；按 MaxFps 抽帧，未到间隔不产生脏区，也就不 Present。
        if (frame_accum_ + 0.002f >= interval) {
            frame_accum_ = std::fmod(frame_accum_, interval);
            dirty = true;
        }
        more = true;
    }
    if (dirty) Invalidate();
    ticking_ = more;
    return base || more;
}

void ShaderView::Draw(Painter& painter, const Theme& theme) {
    if (!ticking_ && Running()) {
        // 构建期设置 Play(Always) 时还没有窗口，首帧补申请时钟。
        ticking_ = true;
        Animate();
    }
    const Rect r = absolute_;
    if (r.IsEmpty()) return;
    const float strength = intensity_ * theme.glow_intensity;
    if (strength <= 0.001f) return;
    const bool clip = radius_ > 0.0f;
    if (clip) painter.PushRoundedClip(r, radius_);
    ShaderParams p = params_;
    p.center = center_now_;
    p.intensity = strength;
    p.tint = Color{theme.accent.r, theme.accent.g, theme.accent.b, 1.0f};
    gpu_active_ = painter.DrawShader(r, p);
    if (!gpu_active_) {
        // 无 GPU 着色器时退化为静态径向光，保持同一视觉重心。
        const Point c{r.x + r.w * center_now_.x, r.y + r.h * center_now_.y};
        painter.FillRectRadial(r, c, std::max(r.w, r.h) * 0.7f,
                               Color{p.tint.r, p.tint.g, p.tint.b, 0.16f * strength},
                               Color{p.tint.r, p.tint.g, p.tint.b, 0.0f}, 0.0f);
    }
    if (clip) painter.PopRoundedClip();
}

} // namespace lumen
