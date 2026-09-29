#include "lumen/Badge.h"
#include "lumen/Panel.h"
#include "lumen/Painter.h"
#include "../core/text_service.h"

namespace lumen {
namespace {

Color WithAlpha(Color c, float a) noexcept { return {c.r, c.g, c.b, a}; }

} // namespace

void Badge::RelayoutParent() { Control::RelayoutParent(); }

Size Badge::Measure(Size, const Theme&) {
    const float text_w = MeasureText(text_, TextRole::Caption).w + 20.0f;
    return {std::max(text_w, 28.0f), 22.0f};
}

void Badge::Draw(Painter& painter, const Theme& theme) {
    Color fill, text, border{0, 0, 0, 0};
    bool glow = false;
    switch (tone_) {
    case BadgeTone::Accent:
        fill = theme.accent;
        text = theme.primary_text;
        glow = true;
        break;
    // 状态徽标：同色低透明底 + 状态色文字/描边，和中性、强调徽标一眼可分。
    case BadgeTone::Success:
        fill = theme.success_subtle;
        text = theme.success;
        border = WithAlpha(theme.success, 0.45f);
        break;
    case BadgeTone::Warning:
        fill = theme.warning_subtle;
        text = theme.warning;
        border = WithAlpha(theme.warning, 0.45f);
        break;
    case BadgeTone::Neutral:
    default:
        fill = theme.fill_input;
        text = theme.text_secondary;
        border = theme.stroke_card;
        break;
    }
    const float radius = absolute_.h * 0.5f;
    if (glow) painter.DrawGlow(absolute_, radius, theme.glow_sm);
    painter.FillRoundedRect(absolute_, radius, fill);
    if (border.a > 0.0f) painter.StrokeRoundedRect(absolute_, radius, border);
    painter.DrawText(text_, absolute_, TextRole::Caption, text, Align::Center);
}

} // namespace lumen
