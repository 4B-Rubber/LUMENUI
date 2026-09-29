// lumen/Theme.h — LUMEN 设计 token。纯黑底光感体系：亮白发光体压在黑面上建立层次；
// 危险/警告/成功/信息等特殊状态另用少量状态色区分。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: 非布局控件头，或见类声明
#pragma once
#include "Animate.h"
#include "Core.h"
#include <cstddef>

namespace lumen {

enum class Elevation : uint8_t { Flat = 0, Raised = 1, Overlay = 2, Modal = 3 };

// 光的色温：只给辉光/聚光/镜面线等光感 token 染一层极淡的冷或暖色，文字、accent 与
// 填充阶梯不变。Neutral 为纯白光（默认）。
enum class LightTone : uint8_t { Neutral, Cool, Warm };

// 语义状态，供瞬时光效（如 Button::Flash）等按状态取色；颜色见 StatusColor。
enum class StatusTone : uint8_t { Info, Success, Warning, Danger };

// 图表类别色数量（Theme::chart_series）。
inline constexpr size_t kChartSeriesCount = 6;

struct Theme {
    // 几何 token（DIP）
    float radius_control = 8.0f;    // 按钮/输入 rounded-lg
    float radius_card = 16.0f;      // 卡片 rounded-2xl
    float radius_flyout = 12.0f;    // 弹出层/菜单
    float focus_ring_width = 2.0f;  // 键盘焦点环宽度（白色，外扩 1px）
    float button_height = 44.0f;
    float input_height = 40.0f;
    float menu_item_height = 36.0f;
    float list_row_height = 28.0f;
    float space_sm = 4.0f;
    float space_md = 8.0f;
    float space_lg = 12.0f;
    float space_xl = 16.0f;
    float status_bar_height = 28.0f;
    float toast_duration = 2.4f;
    float tooltip_delay = 0.6f;

    // 基础色
    Color bg;                    // 窗口背景（void 黑）
    Color text;                  // 主要文本（zinc-100）
    Color text_secondary;        // 次要文本
    Color text_disabled;         // 禁用文本

    // 填充层（hover/pressed 为瞬时状态切换）
    Color fill_hover;            // 通用悬停层
    Color fill_pressed;          // 通用按下层
    Color fill_selected;         // 列表选中
    Color fill_input;            // 输入控件/标准按钮默认底（carbon）
    Color fill_input_hover;      // surface
    Color fill_input_pressed;    // surface-light
    Color fill_input_focus;
    Color fill_input_disabled;

    // 描边
    Color stroke_card;           // 卡片描边
    Color stroke_divider;        // 分隔线
    Color control_stroke;        // 控件（按钮等）静态描边
    Color stroke_input_bottom;   // 输入框非聚焦底线
    Color edge_light;            // 按钮顶部镜面高光线

    // 光感 token（除 grid_line 外均受 glow_intensity 全局缩放）
    Color glow_sm;               // 近距致密白光
    Color glow_md;               // 中距
    Color glow_lg;               // 远距大面积漫射
    Color spotlight_fill;        // 鼠标聚光内部光斑峰值（600px 圆）
    Color spotlight_border;      // 边缘折射光环峰值（400px 圆）
    Color specular_line;         // 浮层顶边 1px 镜面线
    Color ambient_flare;         // 窗口顶部环境辉光（晕影中心）
    Color grid_line;             // 背景网格线（恒定，不随强度缩放）
    float glow_intensity;        // 当前全局光效强度 0..1
    LightTone light_tone = LightTone::Neutral;  // 光感 token 的色温（见 LightTone）

    // 强调色（LUMEN 恒为纯白阶，accent 即"光"）
    Color accent;                // 纯白
    Color accent_hover;
    Color accent_pressed;
    Color accent_text;           // 白底上的文字（黑）
    Color primary_text;          // Primary 按钮文字（黑）
    Color primary_text_pressed;

    Color surface_flyout;        // 菜单/弹层底
    Color scrollbar_thumb;
    Color scrollbar_thumb_hover;

    // 状态色：只用于需要和常规状态区分的特殊状态（危险/错误、警告、成功、信息）。
    // 常规悬停/按压/选中/勾选/焦点仍走白色亮度阶梯。状态色不随 glow_intensity 缩放；
    // 颜色是附加提示，控件仍保留字形或文字。*_subtle 为同色低 alpha 底（字形井、行底、徽标底）。
    Color danger;                // 危险/错误：文字、描边、状态点、Danger 实心底
    Color danger_hover;
    Color danger_pressed;        // Danger 实心底按下
    Color danger_text;           // Danger 实心底上的文字
    Color danger_subtle;
    Color warning;               // 警告
    Color warning_subtle;
    Color success;               // 成功/在线
    Color success_subtle;
    Color info;                  // 信息
    Color info_subtle;

    // 图表类别色：暗底提亮的 400 阶，冷暖交替排列，相邻系列/切片色相拉开；刻意避开
    // 状态色的纯红/纯绿，避免系列被误读为错误/成功。仅用于数据系列，不作装饰或控件状态。
    Color chart_series[kChartSeriesCount];

    // 动效 token。motion_scale=0 表示系统关闭客户区动画（SPI_GETCLIENTAREAANIMATION）。
    float duration_fast = 0.12f;
    float duration_normal = 0.24f;
    float duration_slow = 0.40f;
    Ease ease_standard = Ease::Material;
    Ease ease_enter = Ease::CssEaseOut;
    Ease ease_exit = Ease::CssEaseIn;
    float motion_scale = 1.0f;

    // 海拔：spread 乘到 DrawGlow；glow/specular 再乘对应 alpha。
    float elevation_spread[4] = {0.0f, 0.70f, 1.15f, 1.55f};
    float elevation_glow[4] = {0.0f, 0.35f, 0.55f, 0.75f};
    float elevation_specular[4] = {0.0f, 0.35f, 0.55f, 0.80f};
};

// 子树密度 / token 覆盖。负值表示不改该项。Draw/Measure 经 Control::EffectiveTheme 合成。
enum class Density : uint8_t { Inherit, Comfortable, Normal, Compact };

struct ThemeOverride {
    Density density = Density::Inherit;
    float radius_control = -1.0f;
    float radius_card = -1.0f;
    float button_height = -1.0f;
    float input_height = -1.0f;
    float list_row_height = -1.0f;
    float space_sm = -1.0f;
    float space_md = -1.0f;
    float space_lg = -1.0f;
};

Theme ApplyThemeOverride(const Theme& base, const ThemeOverride& o) noexcept;

// glow_intensity ∈ [0,1]：缩放全部辉光/聚光 token 的 alpha；tone 为光感 token 色温。
Theme MakeTheme(float glow_intensity = 1.0f, LightTone tone = LightTone::Neutral);

// 状态 → 主题状态色（danger / warning / success / info）。
inline Color StatusColor(const Theme& theme, StatusTone tone) noexcept {
    switch (tone) {
    case StatusTone::Success: return theme.success;
    case StatusTone::Warning: return theme.warning;
    case StatusTone::Danger: return theme.danger;
    case StatusTone::Info:
    default: return theme.info;
    }
}

// 第 index 个图表系列色；超过 kChartSeriesCount 后循环并逐轮压暗，保持可区分。
inline Color ChartSeriesColor(const Theme& theme, size_t index) noexcept {
    const Color base = theme.chart_series[index % kChartSeriesCount];
    const size_t round = index / kChartSeriesCount;
    const float k = round == 0 ? 1.0f : (round == 1 ? 0.72f : 0.52f);
    return {base.r * k, base.g * k, base.b * k, base.a};
}

} // namespace lumen
