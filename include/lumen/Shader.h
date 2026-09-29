// lumen/Shader.h — 程序化着色器公共类型（GPU 像素着色器，单色光）。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: 非布局控件头，或见类声明
// 使用方：Painter::DrawShader（立即模式）、ShaderView（控件）、Window::BackdropShader（窗口背景层）。
#pragma once
#include "Core.h"
#include <array>
#include <cstdint>
#include <initializer_list>

namespace lumen {

enum class ShaderKind : uint8_t {
    Mist,      // 域扭曲噪声云雾，漂移翻涌
    Glow,      // 径向光晕，呼吸 + 轻微绕行
    DotGrid,   // 点阵，噪声波与自光心扩散的环
    Rays,      // 自 center 扇出的光束
    Flow,      // 流体：大尺度丝绸般的光带缓慢折叠（Paper Warp / Mesh gradient 的单色版）
    Liquid,    // 液态金属：铬色高光条纹随流场扭动（Paper Liquid metal 的单色版）
    // 以下两种移植自 Paper Shaders（Apache-2.0，见 third_party/paper-shaders）：
    MeshGradient,  // 网格渐变：palette 各色斑沿各自轨迹漂移、有机扭曲 + 涡旋混合（Paper Mesh Gradient）
    LiquidMetal,   // 液态金属材质：铬条纹 + RGB 色散 + palette[0] 颜色加深叠色（Paper Liquid Metal 全幅模式）
};

inline constexpr size_t kShaderMaxColors = 8;

// 着色器调色板。count = 0 为单色（LUMEN 默认：tint 白光）；count > 0 时：
//   Mist/Glow/DotGrid/Rays/Flow/Liquid 按亮度在 colors[0..count) 间取色（暗 → 亮）；
//   MeshGradient 每个颜色是一个漂移色斑（alpha = 该色斑不透明度）；
//   LiquidMetal 取 colors[0] 作颜色加深叠色（alpha = 叠色强度），back 为底色。
// 彩色是显式选择：设计语言下常规界面保持单色，状态红/绿不宜进入大面积背景。
struct ShaderPalette {
    std::array<Color, kShaderMaxColors> colors{};
    uint8_t count = 0;
    Color back{0.0f, 0.0f, 0.0f, 0.0f};   // LiquidMetal 底色（a = 0 透明）

    static ShaderPalette Of(std::initializer_list<Color> list, Color back = {0.0f, 0.0f, 0.0f, 0.0f}) {
        ShaderPalette p;
        for (const Color& c : list) {
            if (p.count >= kShaderMaxColors) break;
            p.colors[p.count++] = c;
        }
        p.back = back;
        return p;
    }
    // 冷色极光：cyan / indigo / violet / 淡品红（避开状态红绿）。
    static ShaderPalette Aurora() {
        return Of({Color::Hex(0x22D3EE), Color::Hex(0x6366F1), Color::Hex(0xA78BFA), Color::Hex(0xF0ABFC)});
    }
    // 暖色余烬：深紫 / 粉 / 橙 / 淡杏。
    static ShaderPalette Ember() {
        return Of({Color::Hex(0x7C3AED), Color::Hex(0xF472B6), Color::Hex(0xFB923C), Color::Hex(0xFED7AA)});
    }
    bool operator==(const ShaderPalette& o) const noexcept {
        if (count != o.count || !Same(back, o.back)) return false;
        for (uint8_t i = 0; i < count; ++i) {
            if (!Same(colors[i], o.colors[i])) return false;
        }
        return true;
    }

private:
    static bool Same(const Color& a, const Color& b) noexcept {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    }
};

// Paper 风格形态参数（默认值与 Paper 一致）。仅对应种类读取，其余种类忽略。
struct ShaderShape {
    float distortion = 0.8f;        // MeshGradient 有机扭曲 0..1
    float swirl = 0.1f;             // MeshGradient 涡旋 0..1
    float repetition = 2.0f;        // LiquidMetal 条纹密度 1..10
    float softness = 0.1f;          // LiquidMetal 过渡柔和度 0..1
    float shift_red = 0.3f;         // LiquidMetal R 通道色散 -1..1（仅彩色调色板生效，单色保持中性灰）
    float shift_blue = 0.3f;        // LiquidMetal B 通道色散 -1..1（同上）
    float metal_distortion = 0.07f; // LiquidMetal 条纹噪声扭曲 0..1
    float contour = 0.4f;           // LiquidMetal 边缘扭曲 0..1
    float angle = 70.0f;            // LiquidMetal 条纹方向（度）
    bool operator==(const ShaderShape&) const = default;
};

struct ShaderParams {
    ShaderKind kind = ShaderKind::Mist;
    float time = 0.0f;         // 秒；调用方推进，内部按周期回绕
    float scale = 1.0f;        // 图案尺度
    float intensity = 1.0f;    // 0..1（调用方已乘 glow_intensity）
    float grain = 0.0f;        // 0..1 胶片颗粒
    float seed = 0.0f;         // 图案偏移
    Point center{0.5f, 0.5f};  // 光心，相对 r 的 0..1
    Color tint{1.0f, 1.0f, 1.0f, 1.0f};   // 直通色（非预乘）；LUMEN 设计语言下保持白
    ShaderPalette palette{};   // count = 0：单色（tint）
    ShaderShape shape{};
};

// 窗口背景着色层（Window::BackdropShader）。独立 DirectComposition 层位于 UI 之下：
// 每帧只重绘这一层，控件树不重绘；按 resolution 缩小渲染，由合成器线性放大。
// 播放策略：窗口激活时按 max_fps 播放；失去激活、最小化、系统“关闭动画”时停在当前帧。
struct ShaderBackdrop {
    bool enabled = false;
    ShaderKind kind = ShaderKind::Flow;
    float intensity = 0.5f;     // 0..1；再乘 theme.glow_intensity
    float speed = 1.0f;         // 时间倍率
    float scale = 1.0f;
    float grain = 0.15f;
    float seed = 0.0f;
    Point center{0.5f, 0.15f};  // 光心，相对窗口 0..1
    float max_fps = 30.0f;      // 1..60
    float resolution = 0.5f;    // 渲染分辨率（相对窗口像素）0.25..1
    bool pause_inactive = true; // 窗口失去激活时暂停
    ShaderPalette palette{};    // count = 0：单色（accent 白光）；彩色或 Mesh/LiquidMetal 时强度封顶 0.35
    ShaderShape shape{};
};

} // namespace lumen
