#include "lumen/RichLabel.h"
#include "lumen/Clipboard.h"
#include "lumen/Painter.h"
#include "../core/text_service.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cwctype>

namespace lumen {
namespace {

bool WordChar(wchar_t c) noexcept {
    return !std::iswspace(c) && !std::iswpunct(c);
}

} // namespace

RichLabel& RichLabel::Push(Run run) {
    if (run.text.empty()) return *this;
    runs_.push_back(std::move(run));
    content_dirty_ = true;
    RelayoutParent();
    return *this;
}

RichLabel& RichLabel::Add(std::wstring_view text) { return Push(Run{std::wstring(text), RunKind::Body}); }
RichLabel& RichLabel::Strong(std::wstring_view text) { return Push(Run{std::wstring(text), RunKind::Strong}); }
RichLabel& RichLabel::Italic(std::wstring_view text) { return Push(Run{std::wstring(text), RunKind::Italic}); }
RichLabel& RichLabel::Secondary(std::wstring_view text) { return Push(Run{std::wstring(text), RunKind::Dim}); }
RichLabel& RichLabel::Code(std::wstring_view text) { return Push(Run{std::wstring(text), RunKind::Code}); }

RichLabel& RichLabel::Colored(std::wstring_view text, Color color) {
    Run run{std::wstring(text), RunKind::Colored};
    run.color = color;
    return Push(std::move(run));
}

RichLabel& RichLabel::Tone(std::wstring_view text, StatusTone tone) {
    Run run{std::wstring(text), RunKind::Tone};
    run.tone = tone;
    return Push(std::move(run));
}

RichLabel& RichLabel::Link(std::wstring_view text, std::function<void()> on_click) {
    Run run{std::wstring(text), RunKind::Link};
    run.click = std::move(on_click);
    return Push(std::move(run));
}

RichLabel& RichLabel::Font(std::wstring_view text, std::wstring_view family) {
    Run run{std::wstring(text), RunKind::Body};
    run.family = std::wstring(family);
    return Push(std::move(run));
}

RichLabel& RichLabel::Markup(std::wstring_view m) {
    std::wstring plain;
    const auto flush = [&] {
        if (!plain.empty()) runs_.push_back(Run{std::move(plain), RunKind::Body});
        plain.clear();
    };
    const auto span = [&](RunKind kind, std::wstring_view body) {
        flush();
        if (!body.empty()) runs_.push_back(Run{std::wstring(body), kind});
    };
    size_t i = 0;
    while (i < m.size()) {
        const wchar_t c = m[i];
        if (c == L'\\' && i + 1 < m.size()) {
            plain += m[i + 1];
            i += 2;
            continue;
        }
        if (c == L'*' && i + 1 < m.size() && m[i + 1] == L'*') {
            const size_t end = m.find(L"**", i + 2);
            if (end != std::wstring_view::npos && end > i + 2) {
                span(RunKind::Strong, m.substr(i + 2, end - i - 2));
                i = end + 2;
                continue;
            }
        } else if (c == L'*') {
            const size_t end = m.find(L'*', i + 1);
            if (end != std::wstring_view::npos && end > i + 1) {
                span(RunKind::Italic, m.substr(i + 1, end - i - 1));
                i = end + 1;
                continue;
            }
        } else if (c == L'`') {
            const size_t end = m.find(L'`', i + 1);
            if (end != std::wstring_view::npos && end > i + 1) {
                span(RunKind::Code, m.substr(i + 1, end - i - 1));
                i = end + 1;
                continue;
            }
        } else if (c == L'[') {
            const size_t mid = m.find(L"](", i + 1);
            const size_t end = mid == std::wstring_view::npos ? mid : m.find(L')', mid + 2);
            if (end != std::wstring_view::npos && mid > i + 1) {
                flush();
                Run run{std::wstring(m.substr(i + 1, mid - i - 1)), RunKind::Link};
                run.target = std::wstring(m.substr(mid + 2, end - mid - 2));
                runs_.push_back(std::move(run));
                i = end + 1;
                continue;
            }
        }
        plain += c;
        ++i;
    }
    flush();
    content_dirty_ = true;
    RelayoutParent();
    return *this;
}

RichLabel& RichLabel::OnLink(std::function<void(std::wstring_view)> handler) {
    on_link_ = std::move(handler);
    return *this;
}

RichLabel& RichLabel::Clear() {
    runs_.clear();
    anchor_ = caret_ = 0;
    hover_run_ = -1;
    dragging_ = false;
    content_dirty_ = true;
    RelayoutParent();
    return *this;
}

RichLabel& RichLabel::Role(TextRole role) {
    if (role_ == role) return *this;
    role_ = role;
    content_dirty_ = true;
    RelayoutParent();
    return *this;
}

RichLabel& RichLabel::Alignment(Align value) {
    if (align_ == value) return *this;
    align_ = value;
    RelayoutParent();
    return *this;
}

RichLabel& RichLabel::Selectable(bool value) {
    if (selectable_ == value) return *this;
    selectable_ = value;
    if (!value) SetSelection(0, 0);
    Invalidate();
    return *this;
}

const std::wstring& RichLabel::Text() {
    EnsureContent();
    return text_;
}

std::wstring RichLabel::SelectedText() const {
    const size_t a = std::min(std::min(anchor_, caret_), text_.size());
    const size_t b = std::min(std::max(anchor_, caret_), text_.size());
    return text_.substr(a, b - a);
}

std::wstring RichLabel::AutomationName() const {
    if (!accessible_name_.empty()) return accessible_name_;
    std::wstring all;
    for (const Run& run : runs_) all += run.text;
    return all;
}

TextTypography RichLabel::BaseTypography() const {
    const TextRoleSpec spec = TextRoleStyle(role_);
    TextTypography t;
    t.size = spec.size;
    t.weight = spec.strong ? 600 : 400;
    t.alignment = align_;
    return t;
}

void RichLabel::EnsureContent() {
    if (!content_dirty_) return;
    content_dirty_ = false;
    selection_dirty_ = true;
    boxes_revision_ = 0;
    text_.clear();
    spans_.clear();
    const TextRoleSpec spec = TextRoleStyle(role_);
    for (Run& run : runs_) {
        run.start = text_.size();
        text_ += run.text;
        TextSpanStyle span;
        span.start = run.start;
        span.length = run.text.size();
        bool styled = true;
        switch (run.kind) {
        case RunKind::Strong: span.weight = spec.strong ? 700 : 600; break;
        case RunKind::Italic: span.italic = true; break;
        case RunKind::Code:
            span.family = UiText().CodeFamily();
            span.size = std::max(1.0f, spec.size - 1.0f);
            break;
        case RunKind::Link: span.underline = true; break;
        default: styled = false; break;
        }
        if (!run.family.empty()) {
            span.family = run.family;
            styled = true;
        }
        if (styled) spans_.push_back(std::move(span));
    }
    anchor_ = std::min(anchor_, text_.size());
    caret_ = std::min(caret_, text_.size());
}

Size RichLabel::Measure(Size available, const Theme&) {
    EnsureContent();
    const float min_h = std::ceil(TextRoleStyle(role_).size * 1.43f);
    if (text_.empty()) return {0.0f, min_h};
    const TextTypography base = BaseTypography();
    // 无约束测量返回单行自然宽（契约见 Core.h）；有约束且左对齐时收拢到内容宽。
    natural_.Layout(text_, base, spans_, kUnbounded, false);
    const float natural = std::ceil(natural_.ContentSize().w) + 1.0f;
    float width = natural;
    if (Bounded(available.w)) width = align_ == Align::Leading ? std::min(natural, available.w) : available.w;
    width = std::max(width, 1.0f);
    layout_.Layout(text_, base, spans_, width, true);
    return {width, std::max(min_h, std::ceil(layout_.ContentSize().h))};
}

void RichLabel::Arrange(const Rect& absolute) {
    Control::Arrange(absolute);
    EnsureContent();
    if (!text_.empty()) layout_.Layout(text_, BaseTypography(), spans_, std::max(1.0f, absolute_.w), true);
}

void RichLabel::ResolveColors(const Theme& theme) {
    colors_.clear();
    if (!enabled_) return;
    for (size_t i = 0; i < runs_.size(); ++i) {
        const Run& run = runs_[i];
        Color color{};
        switch (run.kind) {
        case RunKind::Dim: color = theme.text_secondary; break;
        case RunKind::Link:
            color = hover_run_ == static_cast<int>(i) ? theme.text : theme.text_secondary;
            break;
        case RunKind::Colored: color = run.color; break;
        case RunKind::Tone: color = StatusColor(theme, run.tone); break;
        default: continue;
        }
        colors_.push_back({run.start, run.text.size(), color});
    }
}

void RichLabel::RefreshBoxes() {
    const uint64_t revision = layout_.Revision();
    if (revision != boxes_revision_) {
        boxes_revision_ = revision;
        selection_dirty_ = true;
        code_boxes_.clear();
        for (const Run& run : runs_) {
            if (run.kind != RunKind::Code) continue;
            layout_.Selection(run.start, run.text.size(), scratch_);
            for (const Rect& r : scratch_) code_boxes_.push_back({r.x - 2.0f, r.y + 1.0f, r.w + 4.0f, r.h - 2.0f});
        }
    }
    if (selection_dirty_) {
        selection_dirty_ = false;
        selection_boxes_.clear();
        if (HasSelection()) {
            const size_t a = std::min(anchor_, caret_);
            layout_.Selection(a, std::max(anchor_, caret_) - a, selection_boxes_);
        }
    }
}

void RichLabel::Prepare(Painter& painter, const Theme& theme) {
    EnsureContent();
    if (text_.empty()) return;
    ResolveColors(theme);
    RefreshBoxes();
    const Color foreground = enabled_ ? theme.text : theme.text_disabled;
    layout_.Prepare(painter, {absolute_.x, absolute_.y}, foreground, painter.Backdrop(), colors_);
    painter.PrepareColor(theme.fill_hover);
    painter.PrepareColor(theme.fill_selected);
}

void RichLabel::Draw(Painter& painter, const Theme& theme) {
    if (text_.empty()) return;
    ResolveColors(theme);
    RefreshBoxes();
    const Point origin{absolute_.x, absolute_.y};
    for (const Rect& r : code_boxes_) painter.FillRoundedRect(r.Offset(origin.x, origin.y), 4.0f, theme.fill_hover);
    for (const Rect& r : selection_boxes_) painter.FillRect(r.Offset(origin.x, origin.y), theme.fill_selected);
    const Color foreground = enabled_ ? theme.text : theme.text_disabled;
    layout_.Draw(painter, origin, foreground, painter.Backdrop(), colors_);
}

int RichLabel::HitRun(Point local) const {
    if (text_.empty()) return -1;
    const TextHit hit = layout_.HitTest(local);
    if (!hit.inside) return -1;
    for (size_t i = 0; i < runs_.size(); ++i) {
        const Run& run = runs_[i];
        if (hit.cluster >= run.start && hit.cluster < run.start + run.text.size()) return static_cast<int>(i);
    }
    return -1;
}

size_t RichLabel::HitIndex(Point local) const {
    if (text_.empty()) return 0;
    const TextHit hit = layout_.HitTest(local);
    // 行尾换行符的后半命中仍停在本行末，不跳到下一行开头。
    if (hit.trailing && hit.cluster < text_.size() && (text_[hit.cluster] == L'\n' || text_[hit.cluster] == L'\r')) {
        return hit.cluster;
    }
    return std::min(hit.index, text_.size());
}

void RichLabel::SetSelection(size_t anchor, size_t caret) {
    anchor = std::min(anchor, text_.size());
    caret = std::min(caret, text_.size());
    if (anchor == anchor_ && caret == caret_) return;
    anchor_ = anchor;
    caret_ = caret;
    selection_dirty_ = true;
    Invalidate();
}

void RichLabel::OnMouseMove(Point local, uint32_t buttons) {
    if (dragging_ && (buttons & 0x0001)) {
        SetSelection(anchor_, HitIndex(local));
        return;
    }
    const int hit = HitRun(local);
    const int next = hit >= 0 && runs_[static_cast<size_t>(hit)].kind == RunKind::Link ? hit : -1;
    if (next != hover_run_) {
        hover_run_ = next;
        Invalidate();
    }
}

void RichLabel::OnMouseDown(Point local, uint32_t buttons) {
    if (!(buttons & 0x0001) || !selectable_) return;
    const size_t at = HitIndex(local);
    SetSelection(at, at);
    dragging_ = true;
}

void RichLabel::OnMouseUp(Point local, uint32_t buttons) {
    const bool selected = dragging_ && HasSelection();
    dragging_ = false;
    if (!(buttons & 0x0001) || selected) return;
    const int hit = HitRun(local);
    if (hit < 0) return;
    const Run& run = runs_[static_cast<size_t>(hit)];
    if (run.kind != RunKind::Link) return;
    if (run.click) {
        const auto click = run.click;   // 回调可能 Clear() 本控件
        click();
    } else if (on_link_ && !run.target.empty()) {
        const std::wstring target = run.target;
        on_link_(target);
    }
}

void RichLabel::OnMouseDoubleClick(Point local) {
    if (!selectable_ || text_.empty()) return;
    size_t a = std::min(layout_.HitTest(local).cluster, text_.size() - 1);
    if (!WordChar(text_[a])) {
        SetSelection(a, a + 1);
        return;
    }
    size_t b = a;
    while (a > 0 && WordChar(text_[a - 1])) --a;
    while (b < text_.size() && WordChar(text_[b])) ++b;
    SetSelection(a, b);
}

void RichLabel::OnMouseLeave() {
    Control::OnMouseLeave();
    if (hover_run_ != -1) {
        hover_run_ = -1;
        Invalidate();
    }
}

bool RichLabel::OnKey(uint32_t vk) {
    if (!selectable_) return false;
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (!ctrl) return false;
    if (vk == 'A') {
        SetSelection(0, text_.size());
        return true;
    }
    if (vk == 'C' || vk == VK_INSERT) {
        if (HasSelection()) {
            // 剪贴板文本约定 CRLF：段内换行 \n 展开为 \r\n。
            const std::wstring selected = SelectedText();
            std::wstring crlf;
            crlf.reserve(selected.size() + 8);
            for (size_t i = 0; i < selected.size(); ++i) {
                if (selected[i] == L'\n' && (i == 0 || selected[i - 1] != L'\r')) crlf += L'\r';
                crlf += selected[i];
            }
            clipboard::Text(crlf);
        }
        return true;
    }
    return false;
}

void RichLabel::OnFocusChanged(bool focused) {
    Control::OnFocusChanged(focused);
    if (!focused) {
        dragging_ = false;
        SetSelection(caret_, caret_);
    }
}

CursorShape RichLabel::CursorAt(Point local) const {
    const int hit = HitRun(local);
    if (hit >= 0 && runs_[static_cast<size_t>(hit)].kind == RunKind::Link) return CursorShape::Hand;
    return selectable_ ? CursorShape::IBeam : CursorShape::Arrow;
}

} // namespace lumen
