// pointer_click.h — per-window double-click recognition for mouse-in-pointer.
#pragma once
#include "lumen/Control.h"
#include <cmath>
#include <cstdint>

namespace lumen {

// Physical coordinates and native time are supplied by the router. Keeping the
// recognizer independent makes drag, cancellation and lifetime cases testable
// without manufacturing a WM_LBUTTONDBLCLK event.
class PointerClick {
public:
    bool Down(Control* target, Point pixels, uint32_t tick, uint32_t source,
              Point double_extent, uint32_t double_time) {
        const bool twice = target && last_.Get() == target && last_source_ == source &&
            static_cast<uint32_t>(tick - last_tick_) <= double_time &&
            Near(pixels, last_pixels_, double_extent);
        last_.Reset();
        double_valid_ = twice;
        if (twice) { double_tick_ = tick; double_pixels_ = pixels; }
        pressed_ = target;
        press_pixels_ = pixels;
        press_tick_ = tick;
        press_source_ = source;
        consumed_ = twice;
        dragged_ = false;
        return twice;
    }

    void Move(Point pixels, Point drag_extent) {
        if (pressed_ && !Near(pixels, press_pixels_, drag_extent)) dragged_ = true;
    }

    void Up(Control* hit, Point pixels, Point drag_extent) {
        if (pressed_ && pressed_.Get() == hit && !dragged_ && !consumed_ &&
            Near(pixels, press_pixels_, drag_extent)) {
            last_ = pressed_.Get();
            last_pixels_ = press_pixels_;
            last_tick_ = press_tick_;
            last_source_ = press_source_;
        } else last_.Reset();
        pressed_.Reset();
    }

    bool PromotedDuplicate(Point pixels, uint32_t tick) const noexcept {
        return double_valid_ && double_tick_ == tick && Near(pixels, double_pixels_, {1.0f, 1.0f});
    }
    bool Pressed() const noexcept { return pressed_.Get() != nullptr; }
    void Cancel() noexcept { pressed_.Reset(); last_.Reset(); double_valid_ = false; }

private:
    static bool Near(Point a, Point b, Point extent) noexcept {
        return std::abs(a.x - b.x) <= extent.x && std::abs(a.y - b.y) <= extent.y;
    }
    WeakRef<Control> last_, pressed_;
    Point last_pixels_{}, press_pixels_{};
    uint32_t last_tick_ = 0, press_tick_ = 0;
    uint32_t last_source_ = 0, press_source_ = 0;
    bool consumed_ = false, dragged_ = false, double_valid_ = false;
    uint32_t double_tick_ = 0;
    Point double_pixels_{};
};
} // namespace lumen
