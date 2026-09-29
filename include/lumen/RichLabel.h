// lumen/RichLabel.h — 混排正文：普通 / 加粗 / 斜体 / 次要 / 状态色 / 自定义色 / 行内代码 / 链接 / 指定字体族，
// 按容器宽换行（含中日韩无空格文本），可选拖选复制。排版基于 TextLayout（与 TextBox 同一段落模型）。
// Events: Link(text, on_click) 的回调；Markup 链接走 OnLink(target)
// Keys: Selectable(true) 且获得焦点时 Ctrl+A 全选、Ctrl+C 复制
// Layout: Grow / FillCross / Margin 走 ControlOf；左对齐时期望宽度 = min(内容宽, 可用宽)，居中/右对齐占满
#pragma once
#include "ControlOf.h"
#include "TextLayout.h"
#include "Theme.h"
#include <functional>
#include <string>
#include <vector>

namespace lumen {

class RichLabel : public ControlOf<RichLabel> {
public:
    RichLabel& Add(std::wstring_view text);
    RichLabel& Strong(std::wstring_view text);
    RichLabel& Italic(std::wstring_view text);
    RichLabel& Secondary(std::wstring_view text);
    // 行内代码：等宽字体（Cascadia Mono → Consolas）+ 浅底。
    RichLabel& Code(std::wstring_view text);
    // 自定义文字色（非预乘）。强调请优先用 Strong；彩色只作附加提示。
    RichLabel& Colored(std::wstring_view text, Color color);
    // 状态色文字（danger / warning / success / info token），随主题变化。
    RichLabel& Tone(std::wstring_view text, StatusTone tone);
    RichLabel& Link(std::wstring_view text, std::function<void()> on_click);
    // 指定字体族的正文段（App::AddFont 的族名或系统字体）：符号字体 + 默认字体数字混排。
    RichLabel& Font(std::wstring_view text, std::wstring_view family);
    // 行内标记（追加）：**加粗**、*斜体*、`代码`、[文字](目标)；反斜杠转义。不支持嵌套。
    RichLabel& Markup(std::wstring_view markup);
    // Markup 链接点击：收到 [文字](目标) 中的目标串。
    RichLabel& OnLink(std::function<void(std::wstring_view target)> handler);
    RichLabel& Clear();

    // 基础字号 / 字重取自文字角色（默认 Body）。
    TextRole Role() const noexcept { return role_; }
    RichLabel& Role(TextRole role);
    Align Alignment() const noexcept { return align_; }
    RichLabel& Alignment(Align value);
    // 可选中：拖选（可跨行）、双击选词，获得焦点后 Ctrl+A / Ctrl+C。默认关闭（纯展示、不进 Tab 顺序）。
    // 选区限于本控件：需要整体选中的多段混排写进同一个 RichLabel，段间用 Add(L"\n") 硬换行。
    bool Selectable() const noexcept { return selectable_; }
    RichLabel& Selectable(bool value);

    // 全部文字（各段直接拼接）。
    const std::wstring& Text();
    std::wstring SelectedText() const;
    bool HasSelection() const noexcept { return anchor_ != caret_; }

    AutomationControlType AutomationType() const noexcept override { return AutomationControlType::Text; }
    std::wstring AutomationName() const override;

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;
    void Prepare(Painter& painter, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    void OnMouseDoubleClick(Point local) override;
    void OnMouseLeave() override;
    bool OnKey(uint32_t vk) override;
    void OnFocusChanged(bool focused) override;
    bool Focusable() const noexcept override { return selectable_ && enabled_; }
    bool PrefersDragOverPan() const noexcept override { return selectable_; }
    CursorShape CursorAt(Point local) const override;

    enum class RunKind { Body, Strong, Italic, Dim, Code, Colored, Tone, Link };
    struct Run {
        std::wstring text;
        RunKind kind = RunKind::Body;
        std::function<void()> click;
        std::wstring family;
        std::wstring target;
        Color color{};
        StatusTone tone = StatusTone::Info;
        size_t start = 0;
    };

    RichLabel& Push(Run run);
    void EnsureContent();
    TextTypography BaseTypography() const;
    void ResolveColors(const Theme& theme);
    void RefreshBoxes();
    int HitRun(Point local) const;
    size_t HitIndex(Point local) const;
    void SetSelection(size_t anchor, size_t caret);

    std::vector<Run> runs_;
    std::wstring text_;
    std::vector<TextSpanStyle> spans_;
    std::vector<TextColorSpan> colors_;
    std::vector<Rect> code_boxes_;
    std::vector<Rect> selection_boxes_;
    std::vector<Rect> scratch_;
    TextLayout layout_;
    TextLayout natural_;
    std::function<void(std::wstring_view)> on_link_;
    TextRole role_ = TextRole::Body;
    Align align_ = Align::Leading;
    uint64_t boxes_revision_ = 0;
    size_t anchor_ = 0;
    size_t caret_ = 0;
    int hover_run_ = -1;
    bool content_dirty_ = true;
    bool selection_dirty_ = true;
    bool selectable_ = false;
    bool dragging_ = false;
};

} // namespace lumen
