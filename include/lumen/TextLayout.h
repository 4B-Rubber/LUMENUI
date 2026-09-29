// lumen/TextLayout.h — shared paragraph layout for display, editing and hit testing.
// Events: none; Layout replaces the prepared paragraph only when inputs change.
// Keys: none; caret/selection geometry is shared with TextBox.
// Layout: font sizes, widths, metrics and hit-test coordinates are DIP.
#pragma once
#include "Core.h"
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lumen {
class Painter;

struct TextTypography {
    std::wstring family;
    float size = 14.0f;
    uint16_t weight = 400;
    bool italic = false;
    bool underline = false;
    Align alignment = Align::Leading;
    float line_height = 0.0f; // 0 = font's natural line metrics.
    bool operator==(const TextTypography&) const = default;
};

// 区间排版样式（参与换行与度量）。0 / 空 / false = 继承段落 TextTypography。
// 下标为 UTF-16 偏移；区间可重叠，后者覆盖前者。
struct TextSpanStyle {
    size_t start = 0;
    size_t length = 0;
    uint16_t weight = 0;
    float size = 0.0f;
    std::wstring family;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    bool operator==(const TextSpanStyle&) const = default;
};

// 区间文字颜色：绘制期参数，改色不重排（悬停变色等零分配）。区间须按 start 升序、互不重叠，
// 未覆盖的文字用 foreground。每个颜色区间按行拆成裁剪盒分别绘制，适合正文级短段落。
struct TextColorSpan {
    size_t start = 0;
    size_t length = 0;
    Color color{};
};

struct TextHit {
    size_t index = 0;       // UTF-16 insertion offset.
    size_t cluster = 0;     // Original cluster start, for trailing caret affinity.
    bool trailing = false;
    bool inside = false;
    Rect rect{};
};

struct TextLine {
    size_t start = 0;
    size_t length = 0;
    size_t newline_length = 0;
    float top = 0.0f;
    float height = 0.0f;
    float baseline = 0.0f;
};

// Uses one shaped paragraph for wrap, selection and caret geometry. Rendering is
// routed through the window's LumaText bridge, including arbitrary system fonts.
// No native edit control and no separate measurement-only text model are used.
class TextLayout {
public:
    TextLayout();
    ~TextLayout();
    TextLayout(TextLayout&&) noexcept;
    TextLayout& operator=(TextLayout&&) noexcept;
    TextLayout(const TextLayout&) = delete;
    TextLayout& operator=(const TextLayout&) = delete;

    void Layout(std::wstring_view text, const TextTypography& typography,
                float width, bool wrap = true);
    void Layout(std::wstring_view text, const TextTypography& typography,
                std::span<const TextSpanStyle> spans, float width, bool wrap = true);
    Size ContentSize() const noexcept;
    Rect InkBounds() const noexcept;
    std::span<const TextLine> Lines() const noexcept;
    TextHit HitTest(Point point) const;
    Rect Caret(size_t index, bool trailing = false) const;
    void Selection(size_t start, size_t length, std::vector<Rect>& boxes) const;
    const std::wstring& Text() const noexcept;
    uint64_t Revision() const noexcept;

    void Prepare(Painter& painter, Point origin, Color foreground, Color backdrop);
    void Draw(Painter& painter, Point origin, Color foreground, Color backdrop);
    // 多色绘制：Prepare 与 Draw 须传同一组区间（颜色可不同）。
    void Prepare(Painter& painter, Point origin, Color foreground, Color backdrop,
                 std::span<const TextColorSpan> colors);
    void Draw(Painter& painter, Point origin, Color foreground, Color backdrop,
              std::span<const TextColorSpan> colors);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void Paint(Painter&, Point, Color, Color, bool prepare);
    void PaintColored(Painter&, Point, Color, Color, bool prepare, std::span<const TextColorSpan>);
    void BuildClips(std::span<const TextColorSpan> colors);
};
} // namespace lumen
