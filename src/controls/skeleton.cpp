#include "lumen/Skeleton.h"
#include "lumen/Painter.h"
#include "../core/text_service.h"
#include <algorithm>
#include <cmath>

namespace lumen {

Size Skeleton::Measure(Size, const Theme&) {
    const float w = custom_width_ > 0.5f ? custom_width_ : 160.0f;
    const float h = custom_height_ > 0.5f
                        ? custom_height_
                        : 12.0f * static_cast<float>(lines_);
    return {w, h};
}

bool Skeleton::OnAnimate(float dt_seconds) {
    if (!active_ || MotionScale() <= 0.001f) return Control::OnAnimate(dt_seconds);
    phase_ = std::fmod(phase_ + dt_seconds * 1.4f, 6.2831853f);
    Invalidate();
    return true;   // 加载占位属显式播放期，时钟在其间持续运转
}

void Skeleton::Draw(Painter& painter, const Theme& theme) {
    if (absolute_.IsEmpty()) return;
    // active_ 默认即为 true，Active(true) 不会再触发 Animate()；首帧在此自举时钟，
    // 脱离后重新挂载同样会补回（名单标记由窗口维护，重复调用无副作用）。
    if (active_ && WindowOf() && !AnimationListed() && MotionScale() > 0.001f) Animate();
    const float radius = round_ ? std::min(absolute_.h, 9999.0f) : theme.radius_control;
    const float line_h = absolute_.h / static_cast<float>(lines_);
    // 呼吸：底色在 fill_hover 与 fill_selected 之间往返（黑底上约 2 倍亮度差，肉眼可辨）。
    const float breathe = active_ ? 0.5f + 0.5f * std::sin(phase_) : 0.0f;
    Color fill = theme.fill_hover;
    fill.a = Lerp(theme.fill_hover.a, theme.fill_selected.a, breathe);
    // 扫光：每个呼吸周期横扫两次的柔光斑，强度取 2 倍 spotlight_fill（随 glow_intensity 缩放）；
    // 骨架条仅数 DIP 高，1 倍在细条上难以察觉。
    const bool sweep = active_ && theme.spotlight_fill.a > 0.004f;
    const float sweep_t = std::fmod(phase_ * (2.0f / 6.2831853f), 1.0f);
    const float reach = std::max(48.0f, line_h * 6.0f);
    const float sweep_x = absolute_.x - reach + (absolute_.w + reach * 2.0f) * sweep_t;
    const Color hot{theme.spotlight_fill.r, theme.spotlight_fill.g, theme.spotlight_fill.b,
                    std::min(1.0f, theme.spotlight_fill.a * 2.0f)};
    const Color cold{hot.r, hot.g, hot.b, 0.0f};
    for (int i = 0; i < lines_; ++i) {
        const bool last = i == lines_ - 1 && lines_ > 1;
        const float w = last ? absolute_.w * 0.6f : absolute_.w;
        const Rect bar{absolute_.x, absolute_.y + static_cast<float>(i) * line_h, w,
                       line_h - (lines_ > 1 ? 6.0f : 0.0f)};
        painter.FillRoundedRect(bar, radius, fill);
        if (sweep && sweep_x + reach > bar.x && sweep_x - reach < bar.Right()) {
            painter.FillRoundedRectRadial(bar, radius, {sweep_x, bar.y + bar.h * 0.5f}, reach, hot,
                                          cold);
        }
    }
}

} // namespace lumen
