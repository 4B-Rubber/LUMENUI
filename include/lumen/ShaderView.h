// lumen/ShaderView.h — GPU 程序化光效面板（云雾 / 光晕 / 点阵 / 光束 / 流体 / 网格渐变 / 液态金属），
// 默认单色、随光效强度缩放；Palette() 显式切换为彩色。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认宽度取可用宽、高度 Height()（默认 160）
//
// 播放策略遵守“持续动画仅在悬停、聚焦或显式播放时运行”：
//   ShaderPlay::Hover（默认）悬停时播放，离开后停在当前帧；
//   ShaderPlay::Always 显式常驻播放（窗口隐藏/最小化时由动画时钟自动停拍）；
//   ShaderPlay::Paused 只画静帧，零持续开销。
// MaxFps 限制重绘频率（默认 30），系统“关闭动画”时只画静帧，“减少动画”时半速。
#pragma once
#include "ControlOf.h"
#include "Painter.h"

namespace lumen {

enum class ShaderPlay : uint8_t { Hover, Always, Paused };

class ShaderView : public ControlOf<ShaderView> {
public:
    ShaderKind Kind() const noexcept { return params_.kind; }
    ShaderView& Kind(ShaderKind value);
    ShaderPlay Play() const noexcept { return play_; }
    ShaderView& Play(ShaderPlay value);
    float Speed() const noexcept { return speed_; }
    ShaderView& Speed(float value);              // 时间倍率，默认 1
    float Intensity() const noexcept { return intensity_; }
    ShaderView& Intensity(float value);          // 0..1，默认 0.6；再乘 theme.glow_intensity
    float PatternScale() const noexcept { return params_.scale; }
    ShaderView& PatternScale(float value);       // 图案尺度，默认 1
    float Grain() const noexcept { return params_.grain; }
    ShaderView& Grain(float value);              // 胶片颗粒 0..1，默认 0.25
    float Seed() const noexcept { return params_.seed; }
    ShaderView& Seed(float value);
    Point Center() const noexcept { return params_.center; }
    ShaderView& Center(Point relative);          // 光心，相对 0..1
    bool FollowPointer() const noexcept { return follow_pointer_; }
    ShaderView& FollowPointer(bool value);       // 悬停时光心跟随鼠标（Glow / Rays）
    float MaxFps() const noexcept { return max_fps_; }
    ShaderView& MaxFps(float value);             // 1..120，默认 30
    // 调色板（默认 count = 0 单色，随 accent）；MeshGradient 未设调色板时以 accent 明度阶梯绘制。
    const ShaderPalette& Palette() const noexcept { return params_.palette; }
    ShaderView& Palette(const ShaderPalette& value);
    // Paper 风格形态参数（MeshGradient 扭曲/涡旋；LiquidMetal 条纹/色散/边缘）。
    const ShaderShape& Shape() const noexcept { return params_.shape; }
    ShaderView& Shape(const ShaderShape& value);
    float Time() const noexcept { return params_.time; }
    ShaderView& Time(float seconds);             // 定位时间（静帧、截图与测试）
    float CornerRadius() const noexcept { return radius_; }
    ShaderView& CornerRadius(float value);
    float Height() const noexcept { return height_; }
    ShaderView& Height(float value);
    // GPU 不可用（如 9.x 特性级）时为 false，控件退化为静态径向光。
    bool GpuActive() const noexcept { return gpu_active_; }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool OnAnimate(float dt_seconds) override;
    void OnMouseEnter() override;
    void OnMouseLeave() override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    AutomationControlType AutomationType() const noexcept override {
        return AutomationControlType::Image;
    }

    bool Running() const noexcept;
    void RelayoutParent();

    ShaderParams params_{};
    ShaderPlay play_ = ShaderPlay::Hover;
    float speed_ = 1.0f;
    float intensity_ = 0.6f;
    float max_fps_ = 30.0f;
    float radius_ = 0.0f;
    float height_ = 160.0f;
    float frame_accum_ = 0.0f;
    Point pointer_{0.5f, 0.5f};   // 相对坐标，悬停时跟随
    Point center_now_{0.5f, 0.5f};
    bool follow_pointer_ = false;
    bool inside_ = false;
    bool gpu_active_ = true;
    bool ticking_ = false;        // 已向窗口申请动画时钟（构建期未挂窗口时由首帧补申请）
};

} // namespace lumen
