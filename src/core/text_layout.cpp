#include "lumen/TextLayout.h"
#include "lumen/Painter.h"
#include "text_service.h"
#include "lumatext_bridge.h"
#include <d2d1_3.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>

namespace lumen {
namespace {
std::atomic_uint64_t next_paragraph{1};
float SafeWidth(float width) { return std::isfinite(width) ? Clamp(width, 0.5f, 100000.0f) : 100000.0f; }
}

struct TextLayout::Impl {
    std::wstring text;
    TextTypography style;
    float width = -1.0f;
    bool wrap = true;
    ComPtr<IDWriteTextLayout> layout;
    ComPtr<IDWriteTextLayout> pixels;
    float pixel_scale = 0.0f;
    uint64_t revision = 0;
    uint64_t pixel_revision = 0;
    Size size{};
    Rect ink{};
    std::vector<TextLine> lines;
    std::vector<TextSpanStyle> spans;
    mutable std::vector<DWRITE_HIT_TEST_METRICS> range;
    // 多色绘制的裁剪盒（布局版本 + 区间位置不变时复用）。
    struct ClipBox {
        Rect rect;
        uint32_t slot;   // colors 下标；kBaseSlot = foreground
    };
    static constexpr uint32_t kBaseSlot = 0x0FFFFFFFu;   // 高两位留给行首/行尾标记
    std::vector<ClipBox> clips;
    std::vector<size_t> clip_key;
    uint64_t clip_revision = 0;
    std::vector<Rect> scratch;
};

TextLayout::TextLayout() : impl_(std::make_unique<Impl>()) {}
TextLayout::~TextLayout() = default;
TextLayout::TextLayout(TextLayout&&) noexcept = default;
TextLayout& TextLayout::operator=(TextLayout&&) noexcept = default;

void TextLayout::Layout(std::wstring_view text, const TextTypography& input, float width, bool wrap) {
    Layout(text, input, std::span<const TextSpanStyle>{}, width, wrap);
}

void TextLayout::Layout(std::wstring_view text, const TextTypography& input,
                        std::span<const TextSpanStyle> spans, float width, bool wrap) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    auto style = input;
    style.size = std::isfinite(style.size) ? Clamp(style.size, 0.25f, 2048.0f) : 14.0f;
    style.weight = static_cast<uint16_t>(Clamp(static_cast<int>(style.weight), 1, 999));
    if (!std::isfinite(style.line_height) || style.line_height < 0.0f) style.line_height = 0.0f;
    width = SafeWidth(width);
    auto& s = *impl_;
    const bool same_spans = s.spans.size() == spans.size() && std::equal(spans.begin(), spans.end(), s.spans.begin());
    if (s.layout && s.text == text && s.style == style && s.width == width && s.wrap == wrap && same_spans) return;
    s.text.assign(text); s.style = std::move(style); s.width = width; s.wrap = wrap;
    if (!same_spans) s.spans.assign(spans.begin(), spans.end());
    s.layout.reset(); s.pixels.reset(); s.pixel_scale = 0.0f; s.lines.clear();
    s.revision = next_paragraph.fetch_add(1, std::memory_order_relaxed);
    s.size = {}; s.ink = {};
    s.layout.p = UiText().ParagraphLayout(s.text, s.style, width, wrap, 1.0f, s.spans);
    if (!s.layout) return;
    DWRITE_TEXT_METRICS metrics{};
    s.layout->GetMetrics(&metrics);
    DWRITE_OVERHANG_METRICS overhang{};
    s.layout->GetOverhangMetrics(&overhang);
    s.size = {std::max(0.0f, metrics.widthIncludingTrailingWhitespace), metrics.height};
    s.ink = {std::min(0.0f, -overhang.left), std::min(0.0f, -overhang.top),
             std::max(0.0f, metrics.widthIncludingTrailingWhitespace + std::max(0.0f, overhang.left) + std::max(0.0f, overhang.right)),
             std::max(0.0f, metrics.height + std::max(0.0f, overhang.top) + std::max(0.0f, overhang.bottom))};
    UINT32 count = 0;
    s.layout->GetLineMetrics(nullptr, 0, &count);
    std::vector<DWRITE_LINE_METRICS> native(count);
    if (count && SUCCEEDED(s.layout->GetLineMetrics(native.data(), count, &count))) {
        size_t start = 0; float y = 0.0f;
        s.lines.reserve(count);
        for (const auto& line : native) {
            s.lines.push_back({start, line.length, line.newlineLength, y, line.height, line.baseline});
            start += line.length; y += line.height;
        }
    }
}
Size TextLayout::ContentSize() const noexcept { return impl_ ? impl_->size : Size{}; }
Rect TextLayout::InkBounds() const noexcept { return impl_ ? impl_->ink : Rect{}; }
std::span<const TextLine> TextLayout::Lines() const noexcept { return impl_ ? std::span<const TextLine>(impl_->lines) : std::span<const TextLine>{}; }
const std::wstring& TextLayout::Text() const noexcept { return impl_->text; }
uint64_t TextLayout::Revision() const noexcept { return impl_ ? impl_->revision : 0; }

TextHit TextLayout::HitTest(Point point) const {
    TextHit result;
    if (!impl_ || !impl_->layout) return result;
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS hit{};
    if (FAILED(impl_->layout->HitTestPoint(point.x, point.y, &trailing, &inside, &hit))) return result;
    result.cluster = hit.textPosition;
    result.index = std::min(impl_->text.size(), static_cast<size_t>(hit.textPosition) + (trailing ? hit.length : 0));
    result.trailing = trailing != FALSE; result.inside = inside != FALSE;
    result.rect = {hit.left, hit.top, hit.width, hit.height};
    return result;
}
Rect TextLayout::Caret(size_t index, bool trailing) const {
    if (!impl_ || !impl_->layout) return {};
    index = std::min(index, impl_->text.size());
    float x = 0.0f, y = 0.0f;
    DWRITE_HIT_TEST_METRICS hit{};
    if (FAILED(impl_->layout->HitTestTextPosition(static_cast<UINT32>(index), trailing ? TRUE : FALSE, &x, &y, &hit))) return {};
    return {x, y, 0.0f, hit.height > 0.0f ? hit.height : impl_->style.size * 1.2f};
}
void TextLayout::Selection(size_t start, size_t length, std::vector<Rect>& boxes) const {
    boxes.clear();
    if (!impl_ || !impl_->layout || start >= impl_->text.size() || length == 0) return;
    length = std::min(length, impl_->text.size() - start);
    UINT32 count = 0;
    impl_->layout->HitTestTextRange(static_cast<UINT32>(start), static_cast<UINT32>(length), 0, 0, nullptr, 0, &count);
    if (!count) return;
    impl_->range.resize(count);
    if (FAILED(impl_->layout->HitTestTextRange(static_cast<UINT32>(start), static_cast<UINT32>(length), 0, 0,
                                             impl_->range.data(), count, &count))) return;
    boxes.reserve(count);
    for (UINT32 i = 0; i < count; ++i) {
        const auto& r = impl_->range[i];
        boxes.push_back({r.left, r.top, std::max(1.0f, r.width), r.height});
    }
}

void TextLayout::Prepare(Painter& p, Point origin, Color foreground, Color backdrop) { Paint(p, origin, foreground, backdrop, true); }
void TextLayout::Draw(Painter& p, Point origin, Color foreground, Color backdrop) { Paint(p, origin, foreground, backdrop, false); }
void TextLayout::Prepare(Painter& p, Point origin, Color foreground, Color backdrop,
                         std::span<const TextColorSpan> colors) {
    PaintColored(p, origin, foreground, backdrop, true, colors);
}
void TextLayout::Draw(Painter& p, Point origin, Color foreground, Color backdrop,
                      std::span<const TextColorSpan> colors) {
    PaintColored(p, origin, foreground, backdrop, false, colors);
}

void TextLayout::BuildClips(std::span<const TextColorSpan> colors) {
    constexpr uint32_t kLeftEdge = 0x80000000u, kRightEdge = 0x40000000u;
    auto& s = *impl_;
    bool same = s.clip_revision == s.revision && s.clip_key.size() == colors.size() * 2;
    for (size_t i = 0; same && i < colors.size(); ++i) {
        same = s.clip_key[i * 2] == colors[i].start && s.clip_key[i * 2 + 1] == colors[i].length;
    }
    if (same) return;
    s.clip_revision = s.revision;
    s.clip_key.clear();
    s.clips.clear();
    const size_t n = s.text.size();
    auto add = [&](size_t start, size_t length, uint32_t slot) {
        if (length == 0) return;
        Selection(start, length, s.scratch);
        for (const Rect& r : s.scratch) s.clips.push_back({r, slot});
    };
    size_t pos = 0;
    for (size_t i = 0; i < colors.size(); ++i) {
        s.clip_key.push_back(colors[i].start);
        s.clip_key.push_back(colors[i].length);
        const size_t start = std::min(std::max(colors[i].start, pos), n);
        if (start > pos) add(pos, start - pos, Impl::kBaseSlot);
        const size_t end = std::min(n, colors[i].start + colors[i].length);
        if (end > start) add(start, end - start, static_cast<uint32_t>(i));
        pos = std::max(pos, end);
    }
    if (pos < n) add(pos, n - pos, Impl::kBaseSlot);
    // 行首/行尾与首/末行的盒向外放宽，容纳字形悬垂（斜体、下伸部），相邻盒之间严格按命中边界切分。
    float top = std::numeric_limits<float>::max(), bottom = -std::numeric_limits<float>::max();
    for (const auto& c : s.clips) {
        top = std::min(top, c.rect.y);
        bottom = std::max(bottom, c.rect.Bottom());
    }
    // 先在原始命中盒上判定行首/行尾，再统一外扩（边判定边改会让同行比较失效）。
    const size_t count = s.clips.size();
    for (size_t i = 0; i < count; ++i) {
        const Rect c = s.clips[i].rect;
        bool leftmost = true, rightmost = true;
        for (size_t j = 0; j < count; ++j) {
            const Rect o = s.clips[j].rect;
            if (j == i || std::abs(o.y - c.y) > 0.5f) continue;
            if (o.x < c.x - 0.1f) leftmost = false;
            if (o.Right() > c.Right() + 0.1f) rightmost = false;
        }
        s.clips[i].slot |= (leftmost ? kLeftEdge : 0u) | (rightmost ? kRightEdge : 0u);
    }
    for (auto& c : s.clips) {
        Rect r = c.rect;
        if (c.slot & kLeftEdge) { r.x -= 16.0f; r.w += 16.0f; }
        if (c.slot & kRightEdge) r.w += 16.0f;
        if (r.y <= top + 0.5f) { r.y -= 8.0f; r.h += 8.0f; }
        if (r.Bottom() >= bottom - 0.5f) r.h += 8.0f;
        c.rect = r;
        c.slot &= ~(kLeftEdge | kRightEdge);
    }
}

void TextLayout::PaintColored(Painter& p, Point origin, Color foreground, Color backdrop, bool prepare,
                              std::span<const TextColorSpan> colors) {
    if (colors.empty()) {
        Paint(p, origin, foreground, backdrop, prepare);
        return;
    }
    if (!impl_ || !impl_->layout || impl_->text.empty() || !p.dc_) return;
    BuildClips(colors);
    if (prepare) {
        // 每种颜色各自一份缓存表面；重复颜色命中缓存。
        Paint(p, origin, foreground, backdrop, true);
        for (const auto& c : colors) Paint(p, origin, c.color, backdrop, true);
        return;
    }
    for (const auto& box : impl_->clips) {
        const Color color = box.slot == Impl::kBaseSlot || box.slot >= colors.size()
                                ? foreground : colors[box.slot].color;
        if (color.a <= 0.0f) continue;
        p.PushClip(box.rect.Offset(origin.x, origin.y), false);
        Paint(p, origin, color, backdrop, false);
        p.PopClip();
    }
}
void TextLayout::Paint(Painter& p, Point origin, Color foreground, Color backdrop, bool prepare) {
    if (!impl_ || !impl_->layout || impl_->text.empty() || !p.dc_) return;
    auto& s = *impl_;
    D2D1_MATRIX_3X2_F transform;
    p.dc_->GetTransform(&transform);
    const float scale = transform._11;
    const bool uniform = scale > 0.0f && std::abs(transform._22 - scale) < 0.0001f &&
                         std::abs(transform._12) < 0.0001f && std::abs(transform._21) < 0.0001f;
    if (p.luma_ && p.luma_->Enabled() && uniform) {
        if (!s.pixels || s.pixel_scale != scale) {
            // Layout/font allocation belongs in Prepare, never in the draw pass.
            if (!prepare) return;
            s.pixels.reset();
            s.pixels.p = UiText().ParagraphLayout(s.text, s.style, s.width, s.wrap, scale, s.spans);
            s.pixel_scale = scale;
            s.pixel_revision = next_paragraph.fetch_add(1, std::memory_order_relaxed);
        }
        if (s.pixels) {
            const D2D1_POINT_2F point{origin.x * scale + transform._31, origin.y * scale + transform._32};
            const auto fg = D2D1::ColorF(foreground.r, foreground.g, foreground.b, foreground.a);
            const auto bg = D2D1::ColorF(backdrop.r, backdrop.g, backdrop.b, backdrop.a);
            p.dc_->SetTransform(D2D1::Matrix3x2F::Identity());
            const bool done = p.luma_->Paragraph(s.pixels.get(), s.pixel_revision, point, fg, bg, prepare);
            p.dc_->SetTransform(transform);
            if (done) return;
        }
    }
    if (!prepare) {
        if (p.luma_) p.luma_->RecordFallback();
        p.dc_->DrawTextLayout(D2D1::Point2F(origin.x, origin.y), s.layout.get(), p.Brush(foreground),
                             D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    }
}
} // namespace lumen
