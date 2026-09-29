#include "lumen/Label.h"
#include "lumen/Painter.h"
#include <algorithm>

namespace lumen {

Size Label::Measure(Size available, const Theme&) {
    FontFamilyScope family(family_);
    if (text_.empty()) {
        if (role_ == TextRole::Display) return {0.0f, 56.0f};
        if (role_ == TextRole::Title) return {0.0f, 28.0f};
        if (role_ == TextRole::Subtitle) return {0.0f, 24.0f};
        if (role_ == TextRole::Overline) return {0.0f, 16.0f};
        return {0.0f, 20.0f};
    }
    if (wrap_) {
        // 无约束（Row 主轴、WrapPanel 首测）返回单行自然宽，由父级再按可用宽收缩重测；
        // 有约束时左对齐取 min(自然宽, 可用宽)，居中/右对齐仍占满可用宽以保持对齐效果。
        // 自然宽 +1 DIP 余量，避免按恰好等宽排版时末词因舍入折到下一行。
        const float natural = MeasureText(text_, role_, kUnbounded).w + 1.0f;
        float width = natural;
        if (Bounded(available.w)) {
            width = align_ == Align::Leading ? std::min(natural, available.w) : available.w;
        }
        width = std::max(width, 1.0f);
        return {width, MeasureWrapped(text_, role_, width)};
    }
    Size size = MeasureText(text_, role_, available.w > 0.0f ? available.w : 1.0e5f);
    if (glow_) size.w += 2.0f;
    return {std::max(size.w, 1.0f), size.h};
}

void Label::Draw(Painter& painter, const Theme& theme) {
    if (text_.empty()) return;
    FontFamilyScope family(family_);
    Color color = theme.text;
    if (!enabled_) color = theme.text_disabled;
    else if (foreground_.a > 0.0f) color = foreground_;
    else if (secondary_ || role_ == TextRole::Overline) color = theme.text_secondary;
    if (wrap_) {
        painter.DrawTextWrapped(text_, absolute_, role_, color, align_);
        return;
    }
    if (glow_ && enabled_) {
        painter.DrawTextGlow(text_, absolute_, role_, color, align_);
        return;
    }
    painter.DrawText(text_, absolute_, role_, color, align_);
}

} // namespace lumen
