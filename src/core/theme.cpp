#include "lumen/Theme.h"
#include <algorithm>
#include <windows.h>

namespace lumen {
namespace {

Color Rgba(uint32_t rgb, float alpha) {
    return Color::Hex(rgb, alpha);
}

// 辉光 token 为白色系光；LightTone 只染极淡的冷/暖色，glow_intensity 只缩放 alpha。
Color LightRgb(LightTone tone) {
    switch (tone) {
    case LightTone::Cool: return Color::Hex(0xD9E8FF);
    case LightTone::Warm: return Color::Hex(0xFFE9D1);
    case LightTone::Neutral:
    default: return Color{1.0f, 1.0f, 1.0f, 1.0f};
    }
}

Color Glow(Color rgb, float alpha, float intensity) {
    return Color{rgb.r, rgb.g, rgb.b, alpha * intensity};
}

} // namespace

Theme MakeTheme(float glow_intensity, LightTone tone) {
    const float i = Clamp(glow_intensity, 0.0f, 1.0f);
    const Color light = LightRgb(tone);
    Theme t;
    t.glow_intensity = i;
    t.light_tone = tone;

    // 阶梯：void 黑 -> carbon -> surface -> surface-light，拉大发光体与暗面的对比
    t.bg = Color::Hex(0x000000);
    t.text = Color::Hex(0xF4F4F4);
    t.text_secondary = Rgba(0xFFFFFF, 0.60f);
    t.text_disabled = Rgba(0xFFFFFF, 0.35f);

    t.fill_hover = Rgba(0xFFFFFF, 0.06f);
    t.fill_pressed = Rgba(0xFFFFFF, 0.10f);
    t.fill_selected = Rgba(0xFFFFFF, 0.12f);
    t.fill_input = Color::Hex(0x050505);
    t.fill_input_hover = Color::Hex(0x0A0A0A);
    t.fill_input_pressed = Color::Hex(0x111111);
    t.fill_input_focus = Color::Hex(0x050505);
    t.fill_input_disabled = Rgba(0xFFFFFF, 0.03f);

    t.stroke_card = Rgba(0xFFFFFF, 0.10f);
    t.stroke_divider = Rgba(0xFFFFFF, 0.06f);
    t.control_stroke = Rgba(0xFFFFFF, 0.20f);
    t.stroke_input_bottom = Rgba(0xFFFFFF, 0.20f);
    t.edge_light = Glow(light, 0.20f, i);

    t.glow_sm = Glow(light, 0.35f, i);
    t.glow_md = Glow(light, 0.45f, i);
    t.glow_lg = Glow(light, 0.65f, i);
    t.spotlight_fill = Glow(light, 0.12f, i);
    t.spotlight_border = Glow(light, 0.50f, i);
    t.specular_line = Glow(light, 0.60f, i);
    t.ambient_flare = Glow(light, 0.08f, i);
    t.grid_line = Rgba(0xFFFFFF, 0.03f);

    t.accent = Color::Hex(0xFFFFFF);
    t.accent_hover = Color::Hex(0xE4E4E4);
    t.accent_pressed = Color::Hex(0xD4D4D4);
    t.accent_text = Color::Hex(0x000000);
    t.primary_text = Color::Hex(0x000000);
    t.primary_text_pressed = Rgba(0x000000, 0.63f);

    t.surface_flyout = Color::Hex(0x141414);
    t.scrollbar_thumb = Color::Hex(0x282828);
    t.scrollbar_thumb_hover = Color::Hex(0x555555);
    // 状态色：暗底上提亮的 400 阶，保证小字号文字与 1px 描边可读；subtle 统一 14% alpha。
    t.danger = Color::Hex(0xF87171);
    t.danger_hover = Color::Hex(0xFCA5A5);
    t.danger_pressed = Color::Hex(0xEF4444);
    t.danger_text = Color::Hex(0x000000);
    t.danger_subtle = Rgba(0xF87171, 0.14f);
    t.warning = Color::Hex(0xFBBF24);
    t.warning_subtle = Rgba(0xFBBF24, 0.14f);
    t.success = Color::Hex(0x4ADE80);
    t.success_subtle = Rgba(0x4ADE80, 0.14f);
    t.info = Color::Hex(0x60A5FA);
    t.info_subtle = Rgba(0x60A5FA, 0.14f);
    // 图表类别色：cyan / orange / violet / lime / pink / yellow，冷暖交替。
    t.chart_series[0] = Color::Hex(0x22D3EE);
    t.chart_series[1] = Color::Hex(0xFB923C);
    t.chart_series[2] = Color::Hex(0xA78BFA);
    t.chart_series[3] = Color::Hex(0xA3E635);
    t.chart_series[4] = Color::Hex(0xF472B6);
    t.chart_series[5] = Color::Hex(0xFDE047);

    t.duration_fast = 0.12f;
    t.duration_normal = 0.24f;
    t.duration_slow = 0.40f;
    t.ease_standard = Ease::Material;
    t.ease_enter = Ease::CssEaseOut;
    t.ease_exit = Ease::CssEaseIn;
    BOOL anim = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &anim, 0);
    t.motion_scale = anim ? 1.0f : 0.0f;
    t.elevation_spread[0] = 0.0f;
    t.elevation_spread[1] = 0.70f;
    t.elevation_spread[2] = 1.15f;
    t.elevation_spread[3] = 1.55f;
    t.elevation_glow[0] = 0.0f;
    t.elevation_glow[1] = 0.35f;
    t.elevation_glow[2] = 0.55f;
    t.elevation_glow[3] = 0.75f;
    t.elevation_specular[0] = 0.0f;
    t.elevation_specular[1] = 0.35f;
    t.elevation_specular[2] = 0.55f;
    t.elevation_specular[3] = 0.80f;
    return t;
}

Theme ApplyThemeOverride(const Theme& base, const ThemeOverride& o) noexcept {
    Theme t = base;
    float scale = 1.0f;
    switch (o.density) {
    case Density::Comfortable: scale = 1.15f; break;
    case Density::Compact: scale = 0.85f; break;
    case Density::Normal: scale = 1.0f; break;
    case Density::Inherit:
    default: break;
    }
    if (o.density != Density::Inherit) {
        t.button_height *= scale;
        t.input_height *= scale;
        t.menu_item_height *= scale;
        t.list_row_height *= scale;
        t.space_sm *= scale;
        t.space_md *= scale;
        t.space_lg *= scale;
        t.space_xl *= scale;
    }
    auto apply = [](float& dst, float v) {
        if (v >= 0.0f) dst = v;
    };
    apply(t.radius_control, o.radius_control);
    apply(t.radius_card, o.radius_card);
    apply(t.button_height, o.button_height);
    apply(t.input_height, o.input_height);
    apply(t.list_row_height, o.list_row_height);
    apply(t.space_sm, o.space_sm);
    apply(t.space_md, o.space_md);
    apply(t.space_lg, o.space_lg);
    return t;
}

} // namespace lumen
