// visual — 视觉回归：离屏渲染 LUMEN 控件状态板 → PNG + 像素断言（单暗色主题）。
#include "lumen/lumen.h"
#include "lumen/UpdateScope.h"
#include "core/offscreen.h"
#include "core/lumatext_bridge.h"
#include "core/text_service.h"
#include "core/pointer_click.h"
#include "core/renderer.h"
#include "core/menu_window.h"
#include "control_usability.h"
#include <objbase.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <span>
#include <windows.h>
#include <oleauto.h>
#include <imm.h>
#include <UIAutomation.h>
#include <UIAutomationClient.h>

using namespace lumen;

namespace {

int g_failures = 0;

// 崩溃诊断：打印异常码与出错地址所在模块（RIP 相对模块基址偏移）。
LONG CALLBACK CrashReport(PEXCEPTION_POINTERS info) {
    if (info->ExceptionRecord->ExceptionCode == 0x406D1388) {
        return EXCEPTION_CONTINUE_SEARCH;   // Visual C++ 调试器线程命名通知，不是崩溃。
    }
    void* addr = info->ExceptionRecord->ExceptionAddress;
    HMODULE mod = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(addr), &mod) &&
        GetModuleFileNameA(mod, name, MAX_PATH)) {
        const uintptr_t base = reinterpret_cast<uintptr_t>(mod);
        std::fprintf(stderr, "[crash] code=0x%08lx addr=%p %s+0x%zx\n",
                     info->ExceptionRecord->ExceptionCode, addr, name,
                     reinterpret_cast<uintptr_t>(addr) - base);
    } else {
        std::fprintf(stderr, "[crash] code=0x%08lx addr=%p\n",
                     info->ExceptionRecord->ExceptionCode, addr);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void Check(bool condition, const char* name) {
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", name);
    if (!condition) ++g_failures;
}

bool CloseTo(Color a, Color b, float tol = 0.06f) {
    return std::fabs(a.r - b.r) <= tol && std::fabs(a.g - b.g) <= tol &&
           std::fabs(a.b - b.b) <= tol && std::fabs(a.a - b.a) <= tol;
}

bool CloseTo(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

Color Over(Color top, Color bottom) {
    const float a = top.a;
    return {top.r * a + bottom.r * (1.0f - a), top.g * a + bottom.g * (1.0f - a),
            top.b * a + bottom.b * (1.0f - a), 1.0f};
}

struct TestRoot : StackPanel {
    using StackPanel::Measure;
    using StackPanel::Arrange;
};

struct Scene {
    TestRoot root;
    Button* primary = nullptr;
    CheckBox* checked_box = nullptr;
    Switch* on_switch = nullptr;
    ListView* list = nullptr;
    StackPanel* spot_card = nullptr;

    void Build() {
        root.Padding(16.0f, 12.0f).Spacing(8.0f);
        auto& row1 = root.Add<StackPanel>(StackPanel::Orientation::Horizontal);
        row1.Spacing(8.0f);
        row1.Add<Button>(L"标准");
        row1.Add<Button>(L"主要", ButtonKind::Primary);
        primary = &row1.Add<Button>(L"危险", ButtonKind::Danger);
        row1.Add<Button>(L"透明", ButtonKind::Transparent);
        auto& disabled = row1.Add<Button>(L"禁用");
        disabled.Enabled(false);

        auto& row2 = root.Add<StackPanel>(StackPanel::Orientation::Horizontal);
        row2.Spacing(16.0f);
        row2.Add<CheckBox>(L"未选");
        checked_box = &row2.Add<CheckBox>(L"已选");
        checked_box->Checked(true);
        row2.Add<CheckBox>(L"混合").ThreeState(true).State(CheckState::Indeterminate);
        row2.Add<ToggleButton>(L"切换").Checked(true);
        row2.Add<RadioButton>(L"单选甲").Checked(true);
        row2.Add<RadioButton>(L"单选乙");
        on_switch = &row2.Add<Switch>(L"开关");
        on_switch->Checked(true);
        row2.Add<Switch>(L"关");

        auto& row3 = root.Add<StackPanel>(StackPanel::Orientation::Horizontal);
        row3.Spacing(12.0f);
        row3.Add<TextBox>(L"Emoji \xD83D\xDE00");
        auto& combo = row3.Add<ComboBox>();
        combo.AddItems({L"选项一", L"选项二"}).SelectedIndex(0);

        auto& row4 = root.Add<StackPanel>(StackPanel::Orientation::Horizontal);
        row4.Spacing(12.0f);
        row4.Add<Slider>().Value(0.4f);
        row4.Add<ProgressBar>().Value(0.7f);
        row4.Add<ProgressBar>().Indeterminate(true);

        list = &root.Add<ListView>();
        list->ItemCount(50);
        list->ItemText([](size_t i, std::wstring& s) { s = L"项目 " + std::to_wstring(i); });
        list->SelectedIndex(1);

        auto& tabs = root.Add<TabControl>();
        tabs.AddTab(L"标签一").Add<Label>(L"内容一");
        tabs.AddTab(L"标签二").Add<Label>(L"内容二");

        root.Add<Label>(L"静态文本 Body / 二级文本", TextRole::Body).Secondary(true);

        spot_card = &root.Add<StackPanel>();
        spot_card->Card(StackPanel::CardStyle::Lumen, 14.0f);
        spot_card->Padding(16.0f, 12.0f);
        spot_card->Add<Label>(L"BENTO 聚光卡 — 径向反射与边缘折射", TextRole::BodyStrong);
        spot_card->Add<Label>(L"鼠标跟随光斑 600px · 边缘折射光环 400px", TextRole::Caption)
            .Secondary(true);

        root.Add<Separator>();
        root.Add<HyperlinkButton>(L"文档链接");
        auto& bar = root.Add<InfoBar>(L"信息条");
        bar.Message(L"单色提示").Closable(false);
        root.Add<PasswordBox>(L"secret");
    }
};

bool Near(float a, float b, float eps = 0.05f) { return std::fabs(a - b) <= eps; }

void ClickDip(Window& window, Point dip) {
    window.DispatchMouseMove(dip);
    window.DispatchMouseDown(dip);
    window.DispatchMouseUp(dip);
}

void WheelDip(HWND hwnd, Point dip, int delta) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const float sx = rc.right > 0 ? static_cast<float>(rc.right) / 320.0f : 1.0f;
    const float sy = rc.bottom > 0 ? static_cast<float>(rc.bottom) / 360.0f : sx;
    POINT client{static_cast<int>(dip.x * sx + 0.5f), static_cast<int>(dip.y * sy + 0.5f)};
    ClientToScreen(hwnd, &client);
    SendMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, delta), MAKELPARAM(client.x, client.y));
}

// —— 交互逻辑：直接驱动控件输入入口（不经过窗口路由；修饰键走 buttons 标志）——
struct TestList : ListView {
    using ListView::OnMouseDown;
    using ListView::OnMouseMove;
    using ListView::OnMouseUp;
    using ListView::OnKey;
    using ListView::OnAnimate;
    using ListView::AutomationItemName;
    bool ReorderSettling() const noexcept { return float_row_ >= 0; }
};
struct TestDirtyControl : Button {
    using Button::Button;
    using Control::DirtyBounds;
    using Control::Invalidate;
};
struct TestSlider : Slider {
    using Slider::Measure;
    using Slider::OnKey;
};
struct TestRangeSlider : RangeSlider {
    using RangeSlider::Measure;
    using RangeSlider::OnKey;
};
struct TestTabs : TabControl {
    using TabControl::Measure;
    using TabControl::OnKey;
    using TabControl::OnMouseDown;
    using TabControl::OnMouseMove;
    using TabControl::OnMouseUp;
    using TabControl::OnWheel;
    using TabControl::Arrange;
    float Scroll() const { return scroll_x_; }
    bool Overflows() const { return StripOverflows(); }
};
struct TestComboBox : ComboBox {
    using ComboBox::AutomationExpand;
    using ComboBox::AutomationExpandState;
    using ComboBox::AutomationCollapse;
    using ComboBox::OnChar;
    using ComboBox::OnKey;
    using ComboBox::OnAnimate;
    using ComboBox::Measure;
    void Focus() { ComboBox::Focus(); }
};

struct TestTextBox : TextBox {
    using TextBox::OnChar;
    using TextBox::OnKey;
    using TextBox::OnImeCompose;
    using TextBox::OnImeCommit;
    using TextBox::OnImeEnd;
    using TextBox::ImeComposing;
    using TextBox::OnMouseDown;
    using TextBox::OnMouseDoubleClick;
    using TextBox::OnMouseUp;
    using TextBox::OnAnimate;
    using TextBox::Measure;
    using TextBox::Undo;
    using TextBox::SelectWordAt;
    using TextBox::SelectLineAt;
    using TextBox::WordLeft;
    using TextBox::EnsureEditMenu;
    using TextBox::CaretX;
    using TextBox::HitIndex;
    using Control::MeasureText;
    size_t CaretIndex() const noexcept { return caret_; }
    size_t AnchorIndex() const noexcept { return anchor_; }
};

struct TestAutoSuggestBox : AutoSuggestBox {
    using AutoSuggestBox::OnChar;
    using AutoSuggestBox::OnKey;
};

struct TestDatePicker : DatePicker {
    using DatePicker::OnKey;
};

struct TestCheckBox : CheckBox {
    using CheckBox::CheckBox;
    using CheckBox::OnMouseUp;
    using CheckBox::OnKey;
};
struct TestToggleButton : ToggleButton {
    using ToggleButton::ToggleButton;
    using ToggleButton::OnMouseUp;
    using ToggleButton::OnKey;
};


struct TestTimePicker : TimePicker {
    using TimePicker::OnKey;
};

struct TestCommandBar : CommandBar {
    using CommandBar::OnKey;
    using CommandBar::Measure;
    using CommandBar::Arrange;
};

struct TestNavigationView : NavigationView {
    using NavigationView::OnKey;
    using NavigationView::OnMouseDown;
    using NavigationView::Measure;
    using NavigationView::Arrange;
    using NavigationView::CursorAt;
};

struct TestScrollViewer : ScrollViewer {
    using ScrollViewer::OnWheel;
    using ScrollViewer::OnHWheel;
    using ScrollViewer::OnAnimate;
    using ScrollViewer::Measure;
    using ScrollViewer::Arrange;
};

struct TestBreadcrumb : Breadcrumb {
    using Breadcrumb::OnMouseDown;
    using Breadcrumb::Measure;
    using Breadcrumb::Arrange;
};

struct TestTable : Table {
    using Table::Measure;
    using Table::GroupHeaderContentRect;
    using Table::HeaderHeight;
    using Table::GroupBand;
    using Table::AutomationItemName;
    using Table::OnMouseDown;
    using Table::OnMouseMove;
    using Table::OnMouseUp;
    using Table::OnMouseLeave;
    using Table::OnMouseDoubleClick;
    using Table::CommitCellEdit;
    using Table::CancelCellEdit;
    using Table::ColumnAt;
    using Table::MaxHorizontalScroll;
    using Table::OnWheel;
    using Table::OnKey;
    using Table::ToolTipAnchor;
    using Table::Arrange;
    using Table::RowTop;
    using Table::ShowContextMenu;
    size_t SlotCount() const { return slots_.size(); }
    size_t SlotColumn(size_t i) const { return slots_[i].col; }
    Control* SlotControl(size_t i) { return i < slots_.size() ? slots_[i].control : nullptr; }
    void Commit() { CommitCellEdit(); }
    void Cancel() { CancelCellEdit(); }
    void BeginEdit(ptrdiff_t row, int col) { BeginCellEdit(row, col); }
    Table::CellEditor* Editor() { return cell_editor_; }
};
struct TestSplitView : SplitView {
    using SplitView::OnAnimate;
};
struct TestDialog : Dialog {
    using Dialog::OnAnimate;
    using Dialog::Measure;
    using Dialog::Arrange;
    using Dialog::OnKey;
};
struct TestColorPicker : ColorPicker {
    using ColorPicker::Measure;
    using ColorPicker::Arrange;
    using ColorPicker::OnKey;
    using ColorPicker::OnMouseDown;
};
struct TestCalendarView : CalendarView {
    using CalendarView::Measure;
    using CalendarView::Arrange;
    using CalendarView::OnKey;
    using CalendarView::OnMouseDown;
};
struct TestChip : Chip {
    using Chip::Chip;
    using Chip::Measure;
    using Chip::Arrange;
    using Chip::OnKey;
    using Chip::OnMouseDown;
    using Chip::OnMouseUp;
};
struct TestGridView : GridView {
    using GridView::Measure;
    using GridView::Arrange;
    using GridView::OnKey;
    using GridView::OnMouseDown;
};
struct TestPagination : Pagination {
    using Pagination::OnKey;
    using Pagination::OnMouseDown;
};
struct TestMenuBar : MenuBar {
    using MenuBar::OnMouseDown;
    using MenuBar::Measure;
    using MenuBar::Arrange;
};
struct TestCarousel : Carousel {
    using Carousel::OnKey;
    using Carousel::OnMouseDown;
    using Carousel::Measure;
    using Carousel::Arrange;
};
struct TestStepper : Stepper {
    using Stepper::OnMouseDown;
    using Stepper::Measure;
    using Stepper::Arrange;
};
struct TestSpotlightCard : StackPanel {
    void ForceSpotlight() {
        spotlight_t_ = 1.0f;
        spotlight_inside_ = true;
    }
};
struct TestButton : Button {
    using Button::OnMouseUp;
    using Button::OnKey;
    using Button::Measure;
    using Button::Arrange;
    using Button::Draw;
    using Button::OnAnimate;
};
struct TestPanel : Panel {
    using Panel::Measure;
};
struct TestSparkline : Sparkline {
    using Sparkline::Measure;
    using Sparkline::Arrange;
};
struct TestGauge : Gauge {
    using Gauge::Measure;
    using Gauge::Arrange;
};
struct TestChart : Chart {
    using Chart::Measure;
    using Chart::Arrange;
    using Chart::OnMouseMove;
    using Chart::OnMouseDown;
    using Chart::OnMouseUp;
    using Chart::OnMouseDoubleClick;
    using Chart::OnWheel;
    using Chart::OnAnimate;
};
struct TestLog : LogView {
    using LogView::OnKey;
    using LogView::Prepare;
    using LogView::Draw;
    using LogView::AutomationItemName;
    float Offset() const { return target_offset_; }
    using LogView::Measure;
    using LogView::Arrange;
    using LogView::OnWheel;
};
struct TestRich : RichLabel {
    using RichLabel::Measure;
    using RichLabel::Arrange;
    using RichLabel::OnMouseUp;
    using RichLabel::OnMouseDown;
    using RichLabel::OnMouseMove;
    using RichLabel::OnMouseDoubleClick;
    using RichLabel::Focusable;
    using RichLabel::layout_;
};

constexpr uint32_t kBtnL = 0x0001;
constexpr uint32_t kBtnShift = 0x0004;
constexpr uint32_t kBtnCtrl = 0x0008;
constexpr uint32_t kBtnM = 0x0010;

bool SameIndices(const std::vector<size_t>& got, std::initializer_list<size_t> want) {
    if (got.size() != want.size()) return false;
    size_t i = 0;
    for (size_t w : want) {
        if (got[i++] != w) return false;
    }
    return true;
}

void TestInteraction() {
    const Theme theme = MakeTheme();

    {
        TestSlider slider;
        int changed = 0;
        slider.Range(0.0f, 10.0f)
            .Step(2.0f)
            .Value(4.0f)
            .Orientation(SliderOrientation::Vertical)
            .OnValueChanged([&](float) { ++changed; });
        Check(slider.Measure({100.0f, 300.0f}, theme).h == 180.0f,
              "vertical slider measures tall");
        slider.OnKey(VK_UP);
        Check(slider.Value() == 6.0f && changed == 1, "vertical slider up increments");
        slider.OnKey(VK_DOWN);
        Check(slider.Value() == 4.0f && changed == 2, "vertical slider down decrements");
    }
    {
        TestRangeSlider slider;
        int changed = 0;
        slider.Range(0.0f, 100.0f)
            .Values(25.0f, 75.0f)
            .Step(5.0f)
            .OnValueChanged([&](float, float) { ++changed; });
        slider.OnKey(VK_END);
        Check(slider.LowerValue() == 75.0f && slider.UpperValue() == 75.0f,
              "range slider lower cannot cross upper");
        Check(slider.OnKey(VK_TAB), "range slider tab selects upper thumb");
        slider.OnKey(VK_RIGHT);
        Check(slider.UpperValue() == 80.0f && changed == 2,
              "range slider keyboard adjusts active thumb");
        slider.Values(90.0f, 10.0f);
        Check(slider.LowerValue() == 10.0f && slider.UpperValue() == 90.0f,
              "range slider programmatic values normalize");
    }
    {
        TestTabs tabs;
        auto& first = tabs.AddTab(L"Edit").Add<ScrollViewer>().Grow();
        first.Add<Column>().MinSize({200.0f, 320.0f});
        tabs.AddTab(L"Parameters").Add<Column>().MinSize({1100.0f, 900.0f});
        const auto initial = tabs.Measure({600.0f, 1.0e5f}, Theme{});
        Check(initial.w <= 600.0f && initial.h < 500.0f,
              "tabs natural measure honors viewport width and visible page");
        tabs.SelectedIndex(1);
        const auto other = tabs.Measure({1280.0f, 1.0e5f}, Theme{});
        Check(other.h > initial.h, "tabs measure selected page height");
        tabs.SelectedIndex(0);
        const auto returned = tabs.Measure({600.0f, 1.0e5f}, Theme{});
        Check(std::fabs(initial.w - returned.w) < 0.01f &&
              std::fabs(initial.h - returned.h) < 0.01f,
              "tabs initial and round-trip natural sizes match");
    }
    {
        TestTabs tabs;
        int closed = 0;
        int changed = 0;
        tabs.AddTab({L"home", L"Home", icon::kHome, false});
        tabs.AddTab({L"file", L"main.cpp", icon::kCode, true});
        tabs.AddTab({L"log", L"Build log", icon::kLayers, true});
        tabs.SelectedId(L"file")
            .OnSelectionChanged([&](ptrdiff_t, ptrdiff_t) { ++changed; })
            .OnTabClosing([](std::wstring_view id) { return id != L"log"; })
            .OnTabClosed([&](std::wstring_view) { ++closed; });
        Check(tabs.SelectedId() == L"file", "tabs select stable id");
        Check(!tabs.CloseTab(L"log") && closed == 0, "tabs close can be vetoed");
        Check(tabs.CloseTab(L"file") && closed == 1, "tabs close removes page");
        Check(tabs.SelectedId() == L"log" && changed == 1,
              "tabs close selects right neighbor");
        Check(!tabs.CloseTab(L"home"), "tabs nonclosable item stays open");
        tabs.OnKey(VK_HOME);
        Check(tabs.SelectedId() == L"home", "tabs home selects first");
        tabs.OnKey(VK_END);
        Check(tabs.SelectedId() == L"log", "tabs end selects last");
    }
    {
        TestTabs tabs;
        int moved = 0;
        tabs.AddTab({L"a", L"Alpha", L"", false});
        tabs.AddTab({L"b", L"Beta", L"", false});
        tabs.AddTab({L"c", L"Gamma", L"", false});
        tabs.OnReordered([&](size_t, size_t) { ++moved; });
        tabs.MoveTab(0, 2);
        Check(tabs.Tab(0).id == L"b" && tabs.Tab(2).id == L"a" && tabs.SelectedId() == L"a",
              "tabs MoveTab permutes and keeps selection");
        Check(moved == 1, "tabs MoveTab notifies");
        tabs.Arrange({0.0f, 0.0f, 480.0f, 180.0f});
        tabs.SelectedIndex(0);
        tabs.OnMouseDown({12.0f, 20.0f}, kBtnL);
        tabs.OnMouseMove({280.0f, 20.0f}, kBtnL);
        tabs.OnMouseUp({280.0f, 20.0f}, kBtnL);
        Check(tabs.Tab(0).id == L"c" && tabs.Tab(2).id == L"b" && moved == 2,
              "tabs drag reorder drops at slot");
    }
    {
        TestTabs tabs;
        int closed = 0;
        tabs.AddTab({L"pin", L"Pinned", L"", false});
        tabs.OnTabClosed([&](std::wstring_view) { ++closed; });
        tabs.Arrange({0.0f, 0.0f, 400.0f, 180.0f});
        tabs.OnMouseDown({20.0f, 20.0f}, kBtnM);
        Check(tabs.TabCount() == 1 && closed == 0, "tabs middle click skips nonclosable");
        TestTabs scratch;
        scratch.AddTab({L"tmp", L"Scratch", L"", true});
        scratch.OnTabClosed([&](std::wstring_view) { ++closed; });
        scratch.Arrange({0.0f, 0.0f, 400.0f, 180.0f});
        scratch.OnMouseDown({20.0f, 20.0f}, kBtnM);
        Check(closed == 1 && scratch.TabCount() == 0, "tabs middle click closes");
    }
    {
        TestTabs tabs;
        for (int i = 0; i < 8; ++i) {
            tabs.AddTab(L"Document " + std::to_wstring(i) + L" · long title");
        }
        tabs.Arrange({0.0f, 0.0f, 220.0f, 180.0f});
        Check(tabs.Overflows(), "tabs overflow when strip is narrow");
        tabs.OnMouseMove({40.0f, 16.0f}, 0);
        Check(tabs.OnWheel(-1.0f), "tabs wheel scrolls strip");
        Check(tabs.Scroll() > 0.0f, "tabs scroll offset increases");
        tabs.SelectedIndex(7);
        Check(tabs.Scroll() > 0.0f, "tabs selecting last keeps overflow scroll");
    }
    {
        TestList list;
        int expanded = 0;
        list.Groups({{L"today", L"Today", 3, true},
                        {L"older", L"Older", 100000, true}})
            .OnGroupExpandedChanged([&](std::wstring_view id, bool on) {
                if (id == L"older" && !on) ++expanded;
            });
        Check(list.ItemCount() == 100003, "grouped list exposes global item count");
        list.MultiSelect(true).SelectedIndices({1, 100002});
        list.GroupExpanded(L"older", false);
        Check(!list.GroupExpanded(L"older") && expanded == 1,
              "grouped list collapses section");
        Check(list.SelectionCount() == 2 && list.IsSelected(100002),
              "grouped list preserves hidden selection");
        list.ItemCount(12);
        Check(list.Groups().empty() && list.ItemCount() == 12,
              "plain item count exits grouped mode");
        list.Groups({{L"a", L"A", 3, true}, {L"b", L"B", 3, true}});
        Check(list.ItemCount() == 6, "grouped list rebuilds count");
        list.MoveItem(0, 2);
        Check(list.DataIndex(0) == 1 && list.DataIndex(1) == 2 && list.DataIndex(2) == 0,
              "grouped list reorders inside section");
        list.MoveItem(2, 4);
        Check(list.DataIndex(2) == 0 && list.DataIndex(4) == 4,
              "grouped list rejects cross-section move");
    }
    {
        TestRoot root;
        auto& list = root.Add<TestList>();
        list.ItemCount(4);
        list.MoveItem(3, 0);
        Check(list.DataIndex(0) == 3 && list.DataIndex(1) == 0 && list.DataIndex(3) == 2,
              "plain list MoveItem permutes view");
        list.SelectedIndex(0);
        Check(list.SelectedDataIndex() == 3, "selection follows moved row");
        size_t swiped = 99;
        list.SwipeTrailing({L"Del", L"", [&](size_t view) { swiped = view; }});
        root.Measure({300.0f, 200.0f}, theme);
        root.Arrange({0.0f, 0.0f, 300.0f, 200.0f});
        const float rh = theme.list_row_height;
        list.OnMouseDown({80.0f, 0.5f * rh}, kBtnL);
        list.OnMouseMove({20.0f, 0.5f * rh}, kBtnL);
        list.OnMouseUp({20.0f, 0.5f * rh}, 0);
        Check(swiped == 0, "trailing swipe invokes view row");
    }
    {
        // Drag reorder: lift past the slop, drop by row centre, Esc restores order.
        TestRoot root;
        auto& list = root.Add<TestList>();
        list.ItemCount(6, false).CanReorder(true);
        size_t moved_from = 99, moved_to = 99;
        list.OnReordered([&](size_t from, size_t to) { moved_from = from; moved_to = to; });
        root.Measure({300.0f, 400.0f}, theme);
        root.Arrange({0.0f, 0.0f, 300.0f, 400.0f});
        const float rh = theme.list_row_height;
        list.OnMouseDown({80.0f, 0.5f * rh}, kBtnL);
        list.OnMouseMove({80.0f, 0.5f * rh + 12.0f}, kBtnL);
        list.OnMouseMove({80.0f, 3.4f * rh}, kBtnL);
        list.OnAnimate(0.016f);
        list.OnMouseUp({80.0f, 3.4f * rh}, 0);
        Check(moved_from == 0 && moved_to == 3 && list.DataIndex(3) == 0 && list.DataIndex(0) == 1,
              "drag reorder drops at the row under the lifted centre");
        Check(list.ReorderSettling(), "dropped row springs into its slot");
        for (int i = 0; i < 90; ++i) list.OnAnimate(0.016f);
        Check(!list.ReorderSettling(), "drag reorder settle animation comes to rest");
        moved_from = 99;
        list.OnMouseDown({80.0f, 1.5f * rh}, kBtnL);
        list.OnMouseMove({80.0f, 1.5f * rh + 12.0f}, kBtnL);
        list.OnMouseMove({80.0f, 4.5f * rh}, kBtnL);
        list.OnKey(VK_ESCAPE);
        list.OnMouseUp({80.0f, 4.5f * rh}, 0);
        Check(moved_from == 99 && list.DataIndex(3) == 0 && list.DataIndex(1) == 2,
              "Esc cancels drag reorder without moving");
        list.OnMouseDown({80.0f, 0.5f * rh}, kBtnL);
        list.OnMouseMove({80.0f, 0.5f * rh + 12.0f}, kBtnL);
        list.OnMouseMove({80.0f, 5.0f * rh + 40.0f}, kBtnL);   // far below the last row
        list.OnMouseUp({80.0f, 5.0f * rh + 40.0f}, 0);
        Check(moved_to == 5, "drag reorder past the end clamps to the last slot");
    }
    {
        // Auto-scroll: holding the lifted row at the bottom edge scrolls and retargets.
        TestRoot root;
        auto& list = root.Add<TestList>();
        list.ItemCount(200, false).CanReorder(true);
        size_t moved_to = 0;
        list.OnReordered([&](size_t, size_t to) { moved_to = to; });
        root.Measure({300.0f, 200.0f}, theme);
        root.Arrange({0.0f, 0.0f, 300.0f, 200.0f});
        const float rh = theme.list_row_height;
        list.OnMouseDown({80.0f, 0.5f * rh}, kBtnL);
        list.OnMouseMove({80.0f, 0.5f * rh + 12.0f}, kBtnL);
        list.OnMouseMove({80.0f, 198.0f}, kBtnL);
        for (int i = 0; i < 30; ++i) list.OnAnimate(0.016f);
        list.OnMouseUp({80.0f, 198.0f}, 0);
        Check(moved_to > 10, "drag reorder auto-scrolls at the edge");
    }
    {
        TestRoot root;
        auto& list = root.Add<TestList>();
        list.EmptyTitle(L"None").EmptyHint(L"Hint");
        list.ItemCount(0);
        root.Measure({300.0f, 200.0f}, theme);
        root.Arrange({0.0f, 0.0f, 300.0f, 200.0f});
        Check(list.ChildCount() == 1 && list.Child(0).Visible(), "empty list hosts EmptyState");
        Check(list.Child(0).AbsoluteBounds().h > 8.0f, "empty state fills list");
        list.ItemCount(3);
        root.Measure({300.0f, 200.0f}, theme);
        root.Arrange({0.0f, 0.0f, 300.0f, 200.0f});
        Check(!list.Child(0).Visible(), "empty state hides when rows exist");
        int removed = 0;
        list.AnimateRemoved(1, [&] {
            ++removed;
            list.ItemCount(2);
        });
        list.OnAnimate(1.0f);
        Check(removed == 1 && list.ItemCount() == 2, "remove animation completes");
        list.ItemCount(3);
        list.AnimateInserted(0);
        list.OnAnimate(1.0f);
        Check(list.ItemCount() == 3, "insert animation leaves count");
    }
    {
        TestList list;
        VectorModel<std::wstring> model;
        model.Reset({L"Alpha", L"Bravo"});
        list.Bind(model);
        Check(list.ItemCount() == 2, "bind list takes model count");
        model.Insert(0, L"New");
        Check(list.ItemCount() == 3, "bind list insert updates count");
        Check(list.AutomationItemName(0) == L"New", "bind list insert at 0");
        Check(list.AutomationItemName(1) == L"Alpha", "bind list keeps following rows");
        model.RemoveAt(0);
        Check(list.ItemCount() == 2 && list.AutomationItemName(0) == L"Alpha",
              "bind list remove syncs count");

        VectorModel<std::wstring> names;
        names.Reset({L"Cedar", L"Aspen", L"Birch"});
        FilteredModel filtered(names, [](size_t, const ItemRow& row) {
            return row.text.find(L'e') != std::wstring::npos;
        });
        TestList flist;
        flist.Bind(filtered);
        Check(flist.ItemCount() == 2, "filtered model drops non-matching");
        Check(flist.AutomationItemName(0) == L"Cedar", "filtered keeps source order");
        filtered.Where([](size_t, const ItemRow& row) { return row.text == L"Birch"; });
        Check(flist.ItemCount() == 1 && flist.AutomationItemName(0) == L"Birch",
              "filtered Where rebuilds view");

        SortedModel sorted(names);
        sorted.OrderBy([&](size_t a, size_t b) {
            ItemRow ra, rb;
            names.Get(a, ra);
            names.Get(b, rb);
            return ra.text < rb.text;
        });
        TestList slist;
        slist.Bind(sorted);
        Check(slist.ItemCount() == 3 && slist.AutomationItemName(0) == L"Aspen",
              "sorted model orders A-Z");

        VectorModel<ItemData> table_model;
        table_model.Map([](const ItemData& d, ItemRow& row) {
            row.text = d.text;
            row.cells = {d.text, d.glyph};
        });
        table_model.Reset({{L"N", L"x"}, {L"Q", L"y"}});
        TestTable table;
        table.AddColumn(L"Name", 80.0f);
        table.AddColumn(L"G", 40.0f);
        table.Bind(table_model);
        Check(table.RowCount() == 2, "table bind follows model count");
        table_model.Push({L"R", L"z"});
        Check(table.RowCount() == 3 && table.AutomationItemName(2) == L"R",
              "table bind insert updates count");
    }
    {
        std::vector<std::wstring> items(10000);
        for (size_t i = 0; i < items.size(); ++i) items[i] = L"Option " + std::to_wstring(i);
        Window window(L"combo-virtual", {360.0f, 360.0f});
        auto& combo = window.Root().Add<TestComboBox>();
        combo.Items(std::move(items)).MaxDropDownRows(6);
        window.Show();
        HWND hwnd = static_cast<HWND>(window.NativeHandle());
        UpdateWindow(hwnd);
        combo.Focus();
        combo.OnKey(VK_RETURN);
        SendMessageW(hwnd, WM_KEYDOWN, VK_END, 0);
        SendMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0);
        Check(combo.SelectedIndex() == 9999, "combo virtual popup selects last of 10000");
        Check(combo.HasFocus(), "combo popup restores anchor focus");
        Check(combo.AutomationExpand() && combo.AutomationExpandState() == 1,
              "UIA expands combo popup");
        Check(combo.AutomationCollapse() && combo.AutomationExpandState() == 0,
              "UIA collapse actually closes combo popup");
        combo.Editable(false).SelectedIndex(-1).Editable(true);
        combo.OnChar(L'9');
        SendMessageW(hwnd, WM_CHAR, L'9', 0);
        SendMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0);
        Check(combo.SelectedIndex() == 99, "combo editable filter maps original index");
        window.Close();
    }
    {
        std::vector<std::wstring> items(2000);
        for (size_t i = 0; i < items.size(); ++i) items[i] = L"Option " + std::to_wstring(i);
        Window window(L"combo-scroll-drag", {360.0f, 420.0f});
        window.Root().Padding(12.0f, 8.0f);
        auto& combo = window.Root().Add<TestComboBox>();
        combo.Items(std::move(items)).MaxDropDownRows(8);
        window.Show();
        HWND hwnd = static_cast<HWND>(window.NativeHandle());
        UpdateWindow(hwnd);
        combo.Focus();
        combo.OnKey(VK_RETURN);
        UpdateWindow(hwnd);
        const Rect bounds = combo.AbsoluteBounds();
        const float scale = static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f;
        auto px = [scale](float dip) { return static_cast<int>(dip * scale + 0.5f); };
        // ShowTransient 把弹层 x 夹到 ≥8，与 ComboBox 左缘对齐（根已 pad 12）。
        const int bar_x = px(bounds.Right() - 3.0f);
        const int popup_y = px(bounds.Bottom() + 6.0f);
        const int thumb_y = popup_y + px(12.0f);
        const int drag_y = popup_y + px(240.0f);
        SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(bar_x, thumb_y));
        SendMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(bar_x, drag_y));
        SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(bar_x, drag_y));
        const int item_x = px(bounds.x + 40.0f);
        const int item_y = popup_y + px(20.0f);
        SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(item_x, item_y));
        SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(item_x, item_y));
        Check(combo.SelectedIndex() > 1000, "combo popup scrollbar drag scrolls list");
        window.Close();
    }
    {
        TestComboBox jump;
        jump.AddItems({L"Apple", L"Banana", L"Apricot", L"Cherry"});
        jump.OnChar(L'B');
        Check(jump.SelectedIndex() == 1, "combo typeahead jumps to Banana");
        jump.OnChar(L'a');
        Check(jump.SelectedIndex() == 1, "combo typeahead prefix stays Banana");
        jump.OnAnimate(1.5f);
        jump.OnChar(L'A');
        Check(jump.SelectedIndex() == 0, "combo typeahead Apple after timeout");
        jump.OnChar(L'A');
        Check(jump.SelectedIndex() == 2, "combo typeahead repeated letter cycles Apricot");
    }
    {
        TestComboBox grouped;
        grouped.Items({L"Apple", L"Pear", L"Carrot", L"Kale"});
        grouped.Groups({{L"f", L"Fruit", 2}, {L"v", L"Veg", 2}});
        Check(grouped.Groups().size() == 2, "combo groups stored");
        Window window(L"combo-grouped", {360.0f, 360.0f});
        auto& combo = window.Root().Add<TestComboBox>();
        combo.Items({L"Apple", L"Pear", L"Carrot", L"Kale"})
            .Groups({{L"f", L"Fruit", 2}, {L"v", L"Veg", 2}});
        window.Show();
        HWND hwnd = static_cast<HWND>(window.NativeHandle());
        UpdateWindow(hwnd);
        combo.Focus();
        combo.OnKey(VK_RETURN);
        SendMessageW(hwnd, WM_KEYDOWN, VK_END, 0);
        SendMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0);
        Check(combo.SelectedIndex() == 3, "combo grouped popup END selects last item");
        window.Close();
    }
    {
        TestComboBox multi;
        multi.AddItems({L"Glow", L"Spotlight", L"Specular", L"Ambient"});
        multi.MultiSelect(true);
        Check(!multi.Editable(), "combo multi disables editable");
        multi.Editable(true);
        Check(!multi.Editable(), "combo multi rejects editable");
        multi.SelectedIndices({0, 2});
        Check(multi.SelectionCount() == 2, "combo multi selection count");
        Check(multi.IsSelected(0) && multi.IsSelected(2) && !multi.IsSelected(1),
              "combo multi IsSelected");
        Check(multi.ChildCount() == 2, "combo multi hosts two chips");
        Check(multi.SelectedText() == L"Glow, Specular", "combo multi SelectedText joins");
        multi.ClearSelection();
        Check(multi.SelectionCount() == 0 && multi.ChildCount() == 0, "combo multi clear chips");
        Window window(L"combo-multi", {360.0f, 360.0f});
        auto& combo = window.Root().Add<TestComboBox>();
        combo.AddItems({L"Glow", L"Spotlight", L"Specular"}).MultiSelect(true);
        window.Show();
        HWND hwnd = static_cast<HWND>(window.NativeHandle());
        UpdateWindow(hwnd);
        combo.Focus();
        combo.OnKey(VK_RETURN);
        SendMessageW(hwnd, WM_KEYDOWN, VK_SPACE, 0);
        SendMessageW(hwnd, WM_KEYDOWN, VK_DOWN, 0);
        SendMessageW(hwnd, WM_KEYDOWN, VK_SPACE, 0);
        Check(combo.SelectionCount() == 2, "combo multi popup space toggles without closing");
        window.Close();
    }
    {
        Window window(L"combo-multi-open", {520.0f, 360.0f});
        window.Root().Padding(12.0f, 8.0f);
        auto& combo = window.Root().Add<TestComboBox>();
        combo.AddItems({L"Glow", L"Spotlight", L"Specular", L"Ambient", L"Carbon", L"Void"})
            .MultiSelect(true)
            .SelectedIndices({0, 2, 4});
        window.Show();
        HWND hwnd = static_cast<HWND>(window.NativeHandle());
        UpdateWindow(hwnd);
        combo.Focus();
        combo.OnKey(VK_RETURN);
        UpdateWindow(hwnd);
        const Rect bounds = combo.AbsoluteBounds();
        const float scale = static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f;
        auto px = [scale](float dip) { return static_cast<int>(dip * scale + 0.5f); };
        const int item_x = px(bounds.x + 40.0f);
        const int item_y = px(bounds.Bottom() + 6.0f + 20.0f);
        SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(item_x, item_y));
        SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(item_x, item_y));
        Check(combo.SelectionCount() == 2 && !combo.IsSelected(0),
              "combo multi popup opens on first item not scrolled to last");
        window.Close();
    }
    {
        TestComboBox chips;
        chips.AddItems({L"Glow", L"Spotlight", L"Specular", L"Ambient", L"Carbon", L"Void"})
            .MultiSelect(true)
            .SelectedIndices({0, 1, 2, 3, 4, 5});
        const Size unconstrained = chips.Measure({1.0e5f, 1.0e5f}, theme);
        Check(unconstrained.h <= theme.input_height + 0.5f,
              "combo unconstrained measure stays one chip row");
        TestRoot root;
        auto& row = root.Add<Row>();
        auto& combo = row.Add<TestComboBox>();
        combo.AddItems({L"Glow", L"Spotlight", L"Specular", L"Ambient", L"Carbon", L"Void"})
            .MultiSelect(true)
            .SelectedIndices({0, 1, 2, 3, 4, 5})
            .Grow();
        root.Measure({640.0f, 400.0f}, theme);
        root.Arrange({0.0f, 0.0f, 640.0f, 400.0f});
        Check(combo.Bounds().h <= theme.input_height + 0.5f,
              "combo multi chips one row when grow row is wide");
        Check(combo.Bounds().w > 400.0f, "combo multi grow fills wide row");
    }
    {
        TestCheckBox box(L"x");
        box.ThreeState(true);
        box.OnMouseUp({2.0f, 2.0f}, 0x0001);
        Check(box.State() == CheckState::Checked, "tri-state click checks");
        box.OnMouseUp({2.0f, 2.0f}, 0x0001);
        Check(box.State() == CheckState::Indeterminate, "tri-state click mixed");
        box.OnMouseUp({2.0f, 2.0f}, 0x0001);
        Check(box.State() == CheckState::Unchecked, "tri-state click clears");
        box.OnKey(VK_SPACE);
        Check(box.State() == CheckState::Checked, "tri-state space checks");
        TestCheckBox bin(L"y");
        bin.State(CheckState::Indeterminate);
        bin.OnMouseUp({2.0f, 2.0f}, 0x0001);
        Check(bin.State() == CheckState::Checked, "binary mixed click checks");
        int fires = 0;
        bin.OnToggled([&](bool) { ++fires; });
        bin.Checked(false);
        Check(!bin.Checked() && fires == 0, "checkbox Checked silent");
    }
    {
        TestToggleButton btn(L"Bold");
        int fires = 0;
        btn.OnToggled([&](bool) { ++fires; });
        btn.OnMouseUp({0.0f, 0.0f}, 0x0001);
        Check(btn.Checked() && fires == 1, "toggle button clicks on");
        btn.OnKey(VK_SPACE);
        Check(!btn.Checked() && fires == 2, "toggle button space off");
        btn.Checked(true);
        Check(btn.Checked() && fires == 2, "toggle Checked silent");
    }

    {
        using namespace std::chrono;
        TestDatePicker picker;
        picker.Range(year{2024} / January / day{10}, year{2024} / January / day{20});
        picker.Value(year{2024} / January / day{1});
        Check(picker.Value() && *picker.Value() == year{2024} / January / day{10},
              "date picker clamps minimum");
        picker.Value(year{2024} / February / day{29});
        Check(picker.Value() && *picker.Value() == year{2024} / January / day{20},
              "date picker clamps maximum");
        picker.Value(std::nullopt);
        Check(!picker.Value(), "date picker nullable value");
    }
    {
        using namespace std::chrono;
        TestTimePicker picker;
        picker.Range(hours{9}, hours{17}).MinuteIncrement(15);
        picker.Value(hours{8});
        Check(picker.Value() && picker.Value()->count() == 9 * 60,
              "time picker clamps minimum");
        picker.Value(hours{18});
        Check(picker.Value() && picker.Value()->count() == 17 * 60,
              "time picker clamps maximum");
        picker.MinuteIncrement(7);
        Check(picker.MinuteIncrement() == 7, "time picker preserves minute increment");
        picker.DisplayMode(TimeDisplayMode::TwelveHour);
        Check(picker.DisplayMode() == TimeDisplayMode::TwelveHour,
              "time picker supports explicit twelve-hour display");
        picker.DisplayMode(TimeDisplayMode::TwentyFourHour);
        Check(picker.DisplayMode() == TimeDisplayMode::TwentyFourHour,
              "time picker supports explicit twenty-four-hour display");
        TestTimePicker precise;
        Check(precise.MinuteIncrement() == 1, "time picker defaults to one-minute precision");
    }
    {
        using namespace std::chrono;
        Window window(L"time-picker-wheel", {320.0f, 360.0f});
        auto& picker = window.Root().Add<TimePicker>();
        picker.Value(hours{9} + minutes{45}).MinuteIncrement(15);
        window.Show();
        HWND hwnd = static_cast<HWND>(window.NativeHandle());
        UpdateWindow(hwnd);

        const auto click_picker = [&] {
            const Rect bounds = picker.AbsoluteBounds();
            ClickDip(window, {bounds.x + 24.0f, bounds.y + bounds.h * 0.5f});
        };

        click_picker();
        window.DispatchMouseMove({190.0f, 130.0f});
        WheelDip(hwnd, {190.0f, 130.0f}, -WHEEL_DELTA);
        window.DispatchKey(VK_RETURN);
        Check(picker.Value() && picker.Value()->count() == 9 * 60,
              "time minute wheel does not change hour");

        picker.Value(hours{23});
        click_picker();
        window.DispatchMouseMove({60.0f, 130.0f});
        WheelDip(hwnd, {60.0f, 130.0f}, -WHEEL_DELTA);
        window.DispatchKey(VK_RETURN);
        Check(picker.Value() && picker.Value()->count() == 0,
              "time hour wheel wraps 23 to 0");
        window.Close();
    }
    {
        using namespace std::chrono;
        Window window(L"time-picker-capture", {320.0f, 360.0f});
        auto& picker = window.Root().Add<TimePicker>();
        picker.Value(hours{9} + minutes{30});
        window.Show();
        UpdateWindow(static_cast<HWND>(window.NativeHandle()));

        const Rect bounds = picker.AbsoluteBounds();
        ClickDip(window, {bounds.x + 24.0f, bounds.y + bounds.h * 0.5f});
        ClickDip(window, {148.0f, bounds.Bottom() + 6.0f + 256.0f});
        Check(GetCapture() == nullptr, "time picker selection releases mouse capture");
        window.Close();
    }
    {
        using namespace std::chrono;
        Window window(L"date-picker-years", {340.0f, 380.0f});
        auto& picker = window.Root().Add<DatePicker>();
        picker.Value(year{2026} / August / day{31});
        window.Show();
        UpdateWindow(static_cast<HWND>(window.NativeHandle()));

        const Rect bounds = picker.AbsoluteBounds();
        ClickDip(window, {bounds.x + 24.0f, bounds.y + bounds.h * 0.5f});
        const float popup_y = bounds.Bottom() + 6.0f;
        ClickDip(window, {154.0f, popup_y + 28.0f});
        ClickDip(window, {154.0f, popup_y + 28.0f});
        ClickDip(window, {292.0f, popup_y + 28.0f});
        ClickDip(window, {52.0f, popup_y + 82.0f});
        window.DispatchKey(VK_RETURN);
        window.DispatchKey(VK_RETURN);
        Check(picker.Value() && picker.Value()->year() == year{2033},
              "date picker pages and selects later year");
        window.Close();
    }
    {
        using namespace std::chrono;
        Window window(L"date-picker-capture", {340.0f, 380.0f});
        auto& picker = window.Root().Add<DatePicker>();
        picker.Value(year{2026} / August / day{31});
        window.Show();
        UpdateWindow(static_cast<HWND>(window.NativeHandle()));

        const Rect bounds = picker.AbsoluteBounds();
        ClickDip(window, {bounds.x + 24.0f, bounds.y + bounds.h * 0.5f});
        // 2026-08-02 位于第二行第一列；点击日期格会在 OnMouseDown 内关闭弹层。
        ClickDip(window, {34.0f, bounds.Bottom() + 6.0f + 129.0f});
        Check(GetCapture() == nullptr, "date picker selection releases mouse capture");
        window.Close();
    }
    {
        TestCommandBar bar;
        int invoked = 0;
        bool checked = false;
        bar.Items({{L"run", L"Run", icon::kPlay, CommandBarItemType::Toggle},
                   {L"sep", L"", L"", CommandBarItemType::Separator},
                   {L"copy", L"Copy", icon::kCopy, CommandBarItemType::Action, true, false, true}})
            .OnInvoked([&](std::wstring_view id, bool value) {
                if (id == L"run") { ++invoked; checked = value; }
            });
        bar.Measure({160.0f, 40.0f}, theme);
        bar.Arrange({0.0f, 0.0f, 160.0f, 40.0f});
        bar.OnKey(VK_RETURN);
        Check(invoked == 1 && checked, "command bar toggle invokes");
        Check(bar.Items()[2].overflow_only, "command bar overflow-only retained");
        Check(bar.Measure({800.0f, 40.0f}, theme).w == 800.0f, "command bar fills available width");
    }
    {
        TestNavigationView nav;
        int changed = 0;
        nav.Items({{L"home", L"Home", icon::kLayers},
                   {L"disabled", L"Disabled", icon::kWarning, NavigationItemType::Item, false},
                   {L"files", L"Files", icon::kFolder}});
        nav.SelectedId(L"home")
            .OnSelectionChanged([&](std::wstring_view) { ++changed; });
        nav.Measure({680.0f, 300.0f}, theme);
        nav.Arrange({0.0f, 0.0f, 680.0f, 300.0f});
        Check(nav.SelectedId() == L"home", "navigation selected id");
        nav.OnKey(VK_DOWN);
        nav.OnKey(VK_RETURN);
        Check(nav.SelectedId() == L"files" && changed == 1,
              "navigation keyboard skips disabled item");
        nav.DisplayMode(NavigationDisplayMode::Compact);
        Check(nav.DisplayMode() == NavigationDisplayMode::Compact,
              "navigation compact mode");
        nav.Height(300.0f);
        Check(nav.Measure({620.0f, 1.0e5f}, theme).h == 300.0f,
              "navigation constrained height in scroll content");
        Check(nav.Measure({740.0f, 1.0e5f}, theme).w == 740.0f,
              "navigation width follows parent layout");
    }
    {
        TestNavigationView nav;
        nav.DisplayMode(NavigationDisplayMode::Expanded)
            .Items({{L"", L"Basics", L"", NavigationItemType::Header},
                    {L"home", L"Home", icon::kLayers},
                    {L"off", L"Off", icon::kWarning, NavigationItemType::Item, false},
                    {L"files", L"Files", icon::kFolder}});
        nav.Measure({400.0f, 360.0f}, theme);
        nav.Arrange({0.0f, 0.0f, 400.0f, 360.0f});
        Check(nav.CursorAt({20.0f, 28.0f}) == CursorShape::Hand, "navigation toggle cursor");
        Check(nav.CursorAt({24.0f, 62.0f}) == CursorShape::Arrow, "navigation header cursor");
        Check(nav.CursorAt({24.0f, 96.0f}) == CursorShape::Hand, "navigation item cursor");
        Check(nav.CursorAt({24.0f, 136.0f}) == CursorShape::Arrow, "navigation disabled cursor");
        Check(nav.CursorAt({24.0f, 280.0f}) == CursorShape::Arrow, "navigation empty pane cursor");
        Check(nav.CursorAt({300.0f, 96.0f}) == CursorShape::Arrow, "navigation content cursor");
    }
    {
        TestNavigationView nav;
        int expanded = 0;
        nav.Items({{L"root", L"Root", icon::kFolder, NavigationItemType::Item, true, L"", true},
                   {L"child", L"Child", icon::kFolder, NavigationItemType::Item, true, L"root", false},
                   {L"leaf", L"Leaf", icon::kCode, NavigationItemType::Item, true, L"child", false},
                   {L"orphan", L"Orphan", icon::kWarning, NavigationItemType::Item, true, L"missing", false}})
            .OnExpandedChanged([&](std::wstring_view id, bool on) {
                if (id == L"child" && on) ++expanded;
            });
        nav.Measure({800.0f, 360.0f}, theme);
        nav.Arrange({0.0f, 0.0f, 800.0f, 360.0f});
        Check(nav.ItemExpanded(L"root"), "navigation honors initial expanded state");
        nav.ItemExpanded(L"child", true);
        Check(nav.ItemExpanded(L"child") && expanded == 1, "navigation expands nested item");
        nav.RevealId(L"leaf");
        Check(nav.SelectedId() == L"leaf" && nav.ItemExpanded(L"root") && nav.ItemExpanded(L"child"),
              "navigation reveal expands ancestors");
        nav.SelectedId(L"orphan");
        Check(nav.SelectedId() == L"orphan", "navigation invalid parent becomes root");
    }
    {
        TestRoot root;
        auto& nav = root.Add<TestNavigationView>();
        nav.Items({{L"root", L"Root", icon::kFolder, NavigationItemType::Item, true, L"", true},
                   {L"one", L"One", icon::kCode, NavigationItemType::Item, true, L"root"},
                   {L"two", L"Two", icon::kCode, NavigationItemType::Item, true, L"root"},
                   {L"three", L"Three", icon::kCode, NavigationItemType::Item, true, L"root"},
                   {L"four", L"Four", icon::kCode, NavigationItemType::Item, true, L"root"}})
            .FooterItems({{L"settings", L"Settings", icon::kSettings}})
            .DisplayMode(NavigationDisplayMode::Expanded)
            .Height(236.0f);
        root.Measure({520.0f, 236.0f}, theme);
        root.Arrange({0.0f, 0.0f, 520.0f, 236.0f});
        nav.OnMouseDown({24.0f, 208.0f}, kBtnL);
        Check(nav.SelectedId() == L"settings", "navigation footer stays above scrolling tree");
    }
    {
        TestNavigationView nav;
        int searches = 0;
        nav.Items({{L"root", L"Root", icon::kFolder, NavigationItemType::Item, true, L"", true},
                   {L"leaf", L"UniqueLeaf", icon::kCode, NavigationItemType::Item, true, L"root"},
                   {L"other", L"Zzz", icon::kClock}})
            .OnSearch([&](std::wstring_view) { ++searches; })
            .SearchEnabled(true)
            .SelectedId(L"root");
        nav.RevealId(L"leaf");
        Check(nav.PathIds().size() == 2 && nav.PathIds()[0] == L"root" && nav.PathIds()[1] == L"leaf",
              "navigation path ids follow ancestors");
        Check(nav.PathTitles().size() == 2 && nav.PathTitles()[1] == L"UniqueLeaf",
              "navigation path titles follow ancestors");
        nav.SelectedId(L"root");
        nav.SearchQuery(L"unique");
        Check(searches == 1 && nav.SearchQuery() == L"unique", "navigation search query applied");
        nav.Measure({800.0f, 360.0f}, theme);
        nav.Arrange({0.0f, 0.0f, 800.0f, 360.0f});
        nav.OnKey(VK_DOWN);
        nav.OnKey(VK_RETURN);
        Check(nav.SelectedId() == L"leaf", "navigation search keeps matching lineage selectable");
        TestBreadcrumb crumb;
        nav.BindBreadcrumb(crumb);
        Check(crumb.Count() == 2, "navigation bind breadcrumb copies path");
        crumb.Measure({400.0f, 40.0f}, theme);
        crumb.Arrange({0.0f, 0.0f, 400.0f, 40.0f});
        crumb.OnMouseDown({12.0f, 20.0f}, kBtnL);
        Check(nav.SelectedId() == L"root", "navigation breadcrumb click selects ancestor");
        nav.SelectedId(L"other");
        Check(nav.PathIds().size() == 1 && nav.PathIds()[0] == L"other",
              "navigation root item path is one segment");
        nav.ShowBreadcrumb(true);
        Check(nav.BreadcrumbVisible(), "navigation owned breadcrumb enabled");
        nav.DisplayMode(NavigationDisplayMode::Compact);
        nav.Measure({800.0f, 360.0f}, theme);
        nav.Arrange({0.0f, 0.0f, 800.0f, 360.0f});
        Check(nav.SearchBox() && !nav.SearchBox()->Visible(),
              "navigation compact hides search box");
        nav.OnMouseDown({24.0f, 60.0f}, kBtnL);
        nav.Measure({800.0f, 360.0f}, theme);
        nav.Arrange({0.0f, 0.0f, 800.0f, 360.0f});
        Check(nav.SearchBox() && nav.SearchBox()->Visible(),
              "navigation compact search click expands pane");
    }
    {
        ImageView image;
        const std::array<std::byte, 4> invalid{};
        Check(image.Status() == ImageStatus::Empty, "image empty status");
        Check(!image.LoadMemory(invalid) && !image.HasImage() &&
                  image.Status() == ImageStatus::Failed,
              "image invalid memory rejected");
        image.CornerRadius(8.0f).Stretch(ImageStretch::UniformToFill);
        Check(image.NaturalPixelSize().w == 0.0f, "image empty natural size");
    }

    {
        TestTextBox box;
        Check(box.OnChar(L'\xD83D') && box.Text().empty(), "emoji high surrogate waits");
        Check(box.OnChar(L'\xDE00') && box.Text() == L"\xD83D\xDE00",
              "emoji surrogate pair inserts together");
        box.OnKey(VK_BACK);
        Check(box.Text().empty(), "emoji backspace removes surrogate pair");

        box.Text(L"A\xD83D\xDE00" L"B");
        box.OnKey(VK_LEFT);
        Check(box.CaretIndex() == 3, "emoji caret moves before trailing text");
        box.OnKey(VK_LEFT);
        Check(box.CaretIndex() == 1, "emoji caret skips surrogate pair");

        box.Text(L"\xD83D\xDC69\u200D\xD83D\xDCBB");
        box.OnKey(VK_BACK);
        Check(box.Text().empty(), "emoji backspace removes ZWJ sequence");

        box.Text(L"\xD83C\xDDE8\xD83C\xDDF3");
        box.OnKey(VK_BACK);
        Check(box.Text().empty(), "emoji backspace removes flag pair");

        box.OnChar(L'\xD83D');
        box.OnKey(VK_LEFT);
        box.OnChar(L'\xDE00');
        Check(box.Text().empty(), "interrupted surrogate input is discarded");
    }

    {
        Window window(L"ime-native-boundaries", {480.0f, 260.0f});
        auto& box = window.Root().Add<TestTextBox>();
        box.Multiline(true).Role(TextRole::Caption);
        window.Show();
        window.LayoutNow();
        box.Focus();
        const auto hwnd = static_cast<HWND>(window.NativeHandle());
        box.Text(L"地");
        SendMessageW(hwnd, WM_IME_STARTCOMPOSITION, 0, 0);
        SendMessageW(hwnd, WM_CHAR, L'd', 0);
        SendMessageW(hwnd, WM_CHAR, L'i', 0);
        Check(box.Text() == L"地", "ime native start blocks latin before first preedit");
        box.OnImeCompose(L"di", 2, {});
        box.Text(box.Text());
        Check(box.Composing(), "ime same-value page sync preserves preedit");
        box.OnImeCommit(L"地");
        SendMessageW(hwnd, WM_CHAR, L'i', 0);
        Check(box.Text() == L"地地", "ime native result blocks latin before native end");
        SendMessageW(hwnd, WM_IME_ENDCOMPOSITION, 0, 0);
        Check(!box.Composing() && box.Text() == L"地地", "ime native end leaves committed chinese only");
        SendMessageW(hwnd, WM_CHAR, L'A', 0);
        Check(box.Text() == L"地地A", "ime native end restores normal english input");

        box.Text(L"");
        SendMessageW(hwnd, WM_IME_STARTCOMPOSITION, 0, 0);
        box.OnImeCompose(L"ceshi", 5, {});
        SendMessageW(hwnd, WM_IME_COMPOSITION, 0, 0);
        Check(!box.Composing() && box.Text().empty(), "ime zero-flags cancellation clears preedit");
        SendMessageW(hwnd, WM_CHAR, L'B', 0);
        Check(box.Text() == L"B", "ime cancelled session restores english input");
        SendMessageW(hwnd, WM_IME_ENDCOMPOSITION, 0, 0);

        box.Text(L"");
        SendMessageW(hwnd, WM_IME_STARTCOMPOSITION, 0, 0);
        window.ClearFocus();
        box.Focus();
        SendMessageW(hwnd, WM_CHAR, L'C', 0);
        Check(box.Text() == L"C", "ime focus loss clears native input guard");
        window.Close();
    }

    {
        TestTextBox box;
        box.OnImeCompose(L"ceshi", 5, {});
        Check(box.Text().empty(), "ime compose does not commit latin");
        Check(box.ImeComposing(), "ime compose marks composing");
        box.OnKey(VK_BACK);
        Check(box.Text().empty(), "ime compose swallows backspace");
        box.OnImeEnd();
        Check(!box.ImeComposing() && box.Text().empty(), "ime cancel leaves text empty");

        box.Text(L"ab");
        box.OnImeCompose(L"ce", 2, {});
        box.OnImeCommit(L"\u6d4b\u8bd5");
        Check(box.Text() == L"ab\u6d4b\u8bd5", "ime commit inserts at caret");
        Check(!box.ImeComposing(), "ime commit ends composing");
        Check(box.CaretIndex() == 4, "ime commit moves caret after result");
    }

    {
        TestTextBox box;
        box.Text(L"hello world");
        box.SelectWordAt(1);
        Check(box.AnchorIndex() == 0 && box.CaretIndex() == 5, "double-click selects word");
        box.SelectLineAt(0);
        Check(box.AnchorIndex() == 0 && box.CaretIndex() == box.Text().size(),
              "triple-click selects line");
        box.Text(L"hello world");
        box.OnKey(VK_END);
        const size_t left = box.WordLeft(box.CaretIndex());
        Check(left == 6, "word left lands on world");
        // Ctrl 由 GetKeyState 读取，测试直接裁词：End 后删到词首。
        box.Text(L"hello world");
        box.OnKey(VK_END);
        const size_t start = box.WordLeft(box.Text().size());
        Check(box.Text().substr(start) == L"world", "word left range is last word");
    }

    {
        TestTextBox box;
        box.Text(L"top;bottom");
        box.Select(4, 10);
        Check(box.HasSelection() && box.SelectionStart() == 4 && box.SelectionEnd() == 10,
              "textbox public select sets range");
        box.Select(10, 4);
        Check(box.SelectionStart() == 4 && box.SelectionEnd() == 10,
              "textbox public select normalizes reverse range");
        box.Select(999, 999);
        Check(!box.HasSelection() && box.SelectionStart() == box.Text().size(),
              "textbox public select clamps range");
    }

    {
        TestTextBox box;
        box.MaxLength(3);
        box.OnChar(L'a');
        box.OnChar(L'b');
        box.OnChar(L'c');
        box.OnChar(L'd');
        Check(box.Text() == L"abc", "maxlength truncates insert");
    }

    {
        TestTextBox box;
        box.Mask(L"000-0000");
        for (wchar_t ch : std::wstring(L"1234567")) box.OnChar(ch);
        Check(box.Text() == L"123-4567", "mask inserts literals");
        box.OnChar(L'8');
        Check(box.Text() == L"123-4567", "mask rejects overflow");
    }

    {
        TestTextBox box;
        for (wchar_t ch : std::wstring(L"hello")) box.OnChar(ch);
        Check(box.Text() == L"hello", "typed word");
        Check(box.Undo() && box.Text().empty(), "undo groups a typed word");
        box.OnChar(L'a');
        box.OnChar(L' ');
        box.OnChar(L'b');
        Check(box.Undo() && box.Text() == L"a ", "space starts a new undo group");
        Check(box.Undo() && box.Text() == L"a", "space is its own group");
    }

    {
        TestTextBox box;
        box.Placeholder(L"Name").FloatingLabel(true);
        const Theme th = MakeTheme();
        const Size sz = box.Measure({200.0f, 80.0f}, th);
        Check(sz.h > th.input_height + 1.0f, "floating label grows height");
        box.EnsureEditMenu();
        Check(box.HasContextMenu(), "textbox installs edit menu");
    }

    {

        // 自定义字体不走 LumaText，命中必须复用完整 DirectWrite 行布局，不能累加单字宽。
        std::vector<std::wstring> families{L"Times New Roman"};
        wchar_t font_path[32768]{};
        const DWORD font_path_size = GetEnvironmentVariableW(L"LUMEN_TEST_FONT", font_path, 32768);
        if (font_path_size > 0 && font_path_size < 32768) {
            const auto family = UiText().AddFontFile(font_path);
            Check(!family.empty(), "optional custom test font loads");
            if (!family.empty()) families.push_back(family);
        }
        Window window(L"custom-font-caret", {560.0f, 140.0f});
        auto& box = window.Root().Add<TestTextBox>();
        window.Show();
        window.LayoutNow();
        for (const auto& family : families) {
            box.FontFamily(family);
            for (auto role : {TextRole::Body, TextRole::Caption}) {
                box.Role(role);
                bool caret_matches = true, hit_matches = true;
                const wchar_t* samples[] = {L"AVATAR 9\u03a625 4/5", L"111111111125 4 / 5",
                                           L"9D25 4/5", L"9C25 5/4", L"9D25 4 / 5 "};
                for (const auto* sample : samples) {
                    box.Text(sample);
                    FontFamilyScope family_scope(family);
                    auto* layout = UiText().LineLayout(box.Text(), UiText().Format(role),
                                                       1.0e4f, Align::Leading);
                    Check(layout != nullptr, "custom-font DirectWrite layout exists");
                    if (!layout) continue;
                    float max_delta = 0.0f;
                    size_t misses = 0;
                    for (size_t i = 0; i <= box.Text().size(); ++i) {
                        float x = 0.0f, y = 0.0f;
                        DWRITE_HIT_TEST_METRICS pos{};
                        if (FAILED(layout->HitTestTextPosition(static_cast<UINT32>(i), FALSE, &x, &y, &pos))) {
                            caret_matches = hit_matches = false;
                            continue;
                        }
                        max_delta = std::max(max_delta, std::fabs(box.CaretX(i) - x));
                        if (box.HitIndex({14.0f + x, 12.0f}) != i) ++misses;
                    }
                    std::printf("[font-layout] family=%ls role=%d text=%ls delta=%.3f misses=%zu\n",
                                family.c_str(), static_cast<int>(role), sample, max_delta, misses);
                    caret_matches &= max_delta < 0.05f;
                    hit_matches &= misses == 0;
                }
                Check(caret_matches, "custom-font caret follows full drawn layout");
                Check(hit_matches, "custom-font click follows full drawn layout");
            }
        }
        window.Close();
    }

    {
        Window window(L"cjk-caret", {560.0f, 140.0f});
        auto& box = window.Root().Add<TestTextBox>();
        const std::wstring cjk = L"输入中文，拼音应出现在框内";
        box.Text(cjk);
        window.Show();
        window.LayoutNow();
        const float caret = box.CaretX(cjk.size());
        const Size measured = box.MeasureText(cjk, TextRole::Body);
        Check(caret > 40.0f, "cjk caret has advance");
        Check(caret + 48.0f < box.AbsoluteBounds().w, "cjk caret is not at field edge");
        Check(caret < measured.w + 8.0f, "cjk caret not past measure");
        Check(caret > measured.w * 0.45f, "cjk caret tracks text width");
        window.Close();
    }

    {
        Window window(L"mixed-caret", {560.0f, 140.0f});
        auto& box = window.Root().Add<TestTextBox>();
        const std::wstring mixed = L"on this line.的";
        box.Text(mixed);
        window.Show();
        window.LayoutNow();
        const float caret = box.CaretX(mixed.size());
        const Size measured = box.MeasureText(mixed, TextRole::Body);
        Check(caret > 40.0f, "mixed caret has advance");
        Check(caret + 48.0f < box.AbsoluteBounds().w, "mixed caret is not at field edge");
        Check(caret < measured.w + 8.0f, "mixed caret not past measure");
        Check(caret > measured.w * 0.70f, "mixed caret sits after last glyph");
        window.Close();
    }

    {
        TestRoot root;
        auto& list = root.Add<TestList>();
        list.ItemCount(10);
        list.MultiSelect(true);
        root.Measure({300.0f, 400.0f}, theme);
        root.Arrange({0.0f, 0.0f, 300.0f, 400.0f});
        const float rh = theme.list_row_height;
        auto click = [&](ptrdiff_t row, uint32_t mods) {
            list.OnMouseDown({10.0f, (static_cast<float>(row) + 0.5f) * rh}, kBtnL | mods);
        };
        int changed = 0;
        list.OnSelectionChanged([&](ptrdiff_t, ptrdiff_t) { ++changed; });

        click(2, 0);
        Check(list.SelectedIndex() == 2 && SameIndices(list.SelectedIndices(), {2}),
              "multi plain click selects");
        click(5, kBtnCtrl);
        Check(SameIndices(list.SelectedIndices(), {2, 5}), "multi ctrl click adds");
        click(5, kBtnCtrl);
        Check(SameIndices(list.SelectedIndices(), {2}), "multi ctrl click toggles off");
        click(5, kBtnCtrl);
        click(7, kBtnShift);
        Check(SameIndices(list.SelectedIndices(), {2, 3, 4, 5, 6, 7}),
              "multi shift click range from anchor");
        click(1, 0);
        Check(SameIndices(list.SelectedIndices(), {1}), "multi plain click replaces");
        list.OnKey(VK_DOWN);
        Check(SameIndices(list.SelectedIndices(), {2}), "multi arrow moves selection");
        list.SelectedIndices({3, 1, 3, 99, -1, 4});
        Check(SameIndices(list.SelectedIndices(), {1, 3, 4}), "multi set indices sorted unique");
        list.OnKey(VK_ESCAPE);
        Check(list.SelectionCount() == 0 && list.SelectedIndex() == -1, "multi escape clears");
        Check(changed >= 8, "multi selection changed notified");
        list.SelectedIndices({8, 9});
        list.ItemCount(9);
        Check(SameIndices(list.SelectedIndices(), {8}), "multi shrink filters out-of-range");
        Check(list.SelectedIndex() >= 0 && list.SelectedIndex() < 9,
              "multi shrink clamps focus row");
    }
    {
        TestRoot root;
        auto& list = root.Add<TestList>();
        list.ItemCount(10);
        root.Measure({300.0f, 400.0f}, theme);
        root.Arrange({0.0f, 0.0f, 300.0f, 400.0f});
        list.SelectedIndex(4);
        Check(SameIndices(list.SelectedIndices(), {4}), "single SelectedIndices");
        list.SelectedIndices({6, 2});
        Check(SameIndices(list.SelectedIndices(), {6}), "single set indices takes last");
        const float rh = theme.list_row_height;
        list.OnMouseDown({10.0f, 0.5f * rh}, kBtnL);
        Check(SameIndices(list.SelectedIndices(), {0}), "single click selects row");
        list.SelectedIndex(9);
        list.ItemCount(10);
        list.SelectedIndex(9);
        list.ItemCount(5);
        Check(list.SelectedIndex() == 4, "single shrink clamps selected");
    }
    {
        TestRoot root;
        auto& table = root.Add<TestTable>();
        table.AddColumn(L"A", 100.0f);
        table.AddColumn(L"B");
        static const std::vector<int> kValues{9, 5, 7, 1};
        table.CellText([](size_t row, size_t col, std::wstring& out) {
            if (col == 0) out = std::to_wstring(kValues[row]);
            else out = L"b" + std::to_wstring(row);
        });
        table.Sortable(0, true);
        table.Sortable(1, true);
        table.RowCount(4);
        root.Measure({300.0f, 400.0f}, theme);
        root.Arrange({0.0f, 0.0f, 300.0f, 400.0f});

        // 升序 1,5,7,9 → 视图到数据的置换 3,1,2,0
        table.SortBy(0, 1);
        Check(table.SortedColumn() == 0 && table.SortDirection() == 1, "sort state asc");
        Check(table.DataRowAt(0) == 3 && table.DataRowAt(1) == 1 && table.DataRowAt(2) == 2 &&
                  table.DataRowAt(3) == 0,
              "sort asc permutation");
        table.SortBy(0, -1);
        Check(table.DataRowAt(0) == 0 && table.DataRowAt(3) == 3, "sort desc permutation");
        table.SortBy(0, 0);
        Check(table.SortedColumn() == -1 && table.DataRowAt(2) == 2, "sort clear identity");

        // 表头点击循环 无 → 升 → 降 → 无；换列直接升序
        auto header_click = [&](float x) {
            table.OnMouseDown({x, 16.0f}, kBtnL);
            table.OnMouseUp({x, 16.0f}, 0);
        };
        header_click(50.0f);
        Check(table.SortDirection() == 1, "header click asc");
        header_click(50.0f);
        Check(table.SortDirection() == -1, "header click desc");
        header_click(50.0f);
        Check(table.SortDirection() == 0, "header click clears");
        header_click(200.0f);
        Check(table.SortedColumn() == 1 && table.SortDirection() == 1, "header other column asc");
        table.SortBy(1, 0);

        // 选中跟随数据行：数据 0（"9"）升序后从视图 0 落到视图 3
        table.SelectedIndex(0);
        table.SortBy(0, 1);
        Check(table.SelectedIndex() == 3 && table.DataRowAt(3) == 0, "selection follows sort");
        table.SortBy(0, 0);

        // 列宽拖动：边界 ±4 DIP 命中，起点把弹性列固化为实宽
        auto drag = [&](float from, float to) {
            table.OnMouseDown({from, 16.0f}, kBtnL);
            table.OnMouseMove({to, 16.0f}, kBtnL);
            table.OnMouseUp({to, 16.0f}, 0);
        };
        drag(100.0f, 160.0f);
        Check(Near(table.ColumnWidth(0), 160.0f), "column drag widens");
        drag(160.0f, 10.0f);
        Check(Near(table.ColumnWidth(0), 40.0f), "column drag min clamp");
        drag(40.0f, 500.0f);
        Check(Near(table.ColumnWidth(0), 500.0f), "column drag creates horizontal overflow");
        Check(table.MaxHorizontalScroll() > 0.0f, "column drag exposes horizontal scroll");
    }
    {
        TestRoot root;
        auto& table = root.Add<TestTable>();
        table.AddColumn(L"A", 80.0f);
        table.AddColumn(L"B");
        table.AddColumn(L"C", 80.0f);
        table.RowCount(1);
        root.Measure({400.0f, 220.0f}, theme);
        root.Arrange({0.0f, 0.0f, 400.0f, 220.0f});
        table.OnMouseDown({80.0f, 16.0f}, kBtnL);
        const float snapped_b = table.ColumnWidth(1);
        Check(snapped_b > 90.0f, "resize snaps flex neighbor to pixels");
        table.OnMouseMove({140.0f, 16.0f}, kBtnL);
        table.OnMouseUp({140.0f, 16.0f}, 0);
        Check(Near(table.ColumnWidth(0), 140.0f), "resize only widens dragged column");
        Check(Near(table.ColumnWidth(1), snapped_b), "resize leaves neighbor width");
        Check(Near(table.ColumnWidth(2), 80.0f), "resize leaves trailing column");
    }
    {
        TestRoot root;
        auto& table = root.Add<TestTable>();
        table.AddColumn(L"Name", 90.0f).Frozen();
        table.AddColumn(L"Status", 100.0f);
        table.AddColumn(L"Owner", 80.0f).Frozen();
        table.AddColumn(L"Updated", 140.0f);
        table.RowCount(4);
        root.Measure({260.0f, 220.0f}, theme);
        root.Arrange({0.0f, 0.0f, 260.0f, 220.0f});

        Check(table.ColumnFrozen(0) && !table.ColumnFrozen(1) && table.ColumnFrozen(2),
              "table retains arbitrary frozen flags");
        Check(table.ColumnAt(20.0f) == 0 && table.ColumnAt(110.0f) == 2 &&
                  table.ColumnAt(180.0f) == 1,
              "frozen columns group left in declaration order");
        Check(table.MaxHorizontalScroll() > 0.0f, "table nonfrozen columns overflow");
        table.ScrollToX(10000.0f);
        Check(Near(table.HorizontalOffset(), table.MaxHorizontalScroll()),
              "table horizontal offset clamps");
        Check(table.ColumnAt(180.0f) == 3, "scrolled visual column maps to original index");

        table.CellText([](size_t row, size_t col, std::wstring& out) {
            out = std::to_wstring(row) + L":" + std::to_wstring(col);
        }).CellEditEnabled(true);
        table.OnMouseDoubleClick({180.0f, 46.0f});
        Check(table.Editor() != nullptr && table.Editor()->AbsoluteBounds().x >= 170.0f,
              "cell editor follows scrolled visual column");
    }
    {
        TestRoot root;
        auto& table = root.Add<TestTable>();
        table.AddColumn(L"Name", 90.0f);
        table.AddColumn(L"Status", 100.0f);
        table.RowCount(2);
        root.Measure({260.0f, 200.0f}, theme);
        root.Arrange({0.0f, 0.0f, 260.0f, 200.0f});
        Check(!table.ColumnFrozen(0), "header pin starts unfrozen");
        table.OnMouseDown({78.0f, 16.0f}, kBtnL);
        Check(table.ColumnFrozen(0) && !table.ColumnFrozen(1), "header pin freezes column");
        table.OnMouseDown({78.0f, 16.0f}, kBtnL);
        Check(!table.ColumnFrozen(0), "header pin unfreezes column");
    }
    {
        TestRoot root;
        auto& table = root.Add<TestTable>();
        table.AddColumn(L"A", 200.0f);
        table.AddColumn(L"B");
        table.RowCount(1);
        root.Measure({120.0f, 200.0f}, theme);
        root.Arrange({0.0f, 0.0f, 120.0f, 200.0f});
        table.Arrange({0.0f, 0.0f, 120.0f, 200.0f});
        Check(table.MaxHorizontalScroll() > 0.0f, "flex column keeps min width when overflow");
    }
    {
        TestRoot root;
        auto& table = root.Add<TestTable>();
        static std::vector<uint8_t> on(4, 0);
        table.AddColumn(L"#", 64.0f).Frozen();
        table.AddColumn(L"On", 56.0f).CheckBox(
            [](size_t row) { return row < on.size() && on[row] != 0; },
            [](size_t row, bool value) {
                if (row < on.size()) on[row] = value ? 1 : 0;
            });
        table.AddColumn(L"Note", 80.0f);
        table.AddColumn(L"Action", 108.0f).Button(L"Run", [](size_t) {});
        table.RowCount(4);
        root.Measure({400.0f, 220.0f}, theme);
        root.Arrange({0.0f, 0.0f, 400.0f, 220.0f});
        table.ColumnFrozen(1, true);
        Check(table.ColumnFrozen(0) && table.ColumnFrozen(1), "freeze on keeps hash frozen");
        bool kinds_ok = true;
        bool frozen_checkbox = false;
        bool frozen_button = false;
        for (size_t i = 0; i < table.SlotCount(); ++i) {
            Control* ctl = table.SlotControl(i);
            if (!ctl || !ctl->Visible()) continue;
            const size_t col = table.SlotColumn(i);
            if (col == 1 && dynamic_cast<CheckBox*>(ctl) == nullptr) kinds_ok = false;
            if (col == 3 && dynamic_cast<Button*>(ctl) == nullptr) kinds_ok = false;
            const float x = ctl->AbsoluteBounds().x;
            if (col == 1 && dynamic_cast<CheckBox*>(ctl) && x < 130.0f) frozen_checkbox = true;
            if (col == 3 && dynamic_cast<Button*>(ctl) && x < 130.0f) frozen_button = true;
        }
        Check(kinds_ok, "freeze on keeps checkbox/button on their columns");
        Check(frozen_checkbox && !frozen_button, "frozen band has on checkbox not action button");
    }
    {
        TestRoot root;
        auto& table = root.Add<TestTable>();
        table.AddColumn(L"#", 64.0f).Frozen();
        table.AddColumn(L"On", 56.0f).CheckBox([](size_t) { return false; }, [](size_t, bool) {});
        table.AddColumn(L"Note", 80.0f).TextBox([](size_t row) { return L"Note " + std::to_wstring(row); },
                                               [](size_t, std::wstring) {});
        table.AddColumn(L"Action", 108.0f).Button(L"Run", [](size_t) {});
        table.RowCount(8);
        root.Measure({420.0f, 280.0f}, theme);
        root.Arrange({0.0f, 0.0f, 420.0f, 280.0f});
        table.ColumnFrozen(2, true);
        table.ColumnFrozen(3, true);
        table.MoveColumn(3, 2);
        bool kinds_ok = true;
        float action_x = 1.0e9f;
        float note_x = 1.0e9f;
        for (size_t i = 0; i < table.SlotCount(); ++i) {
            Control* ctl = table.SlotControl(i);
            if (!ctl || !ctl->Visible()) continue;
            const size_t col = table.SlotColumn(i);
            if (col == 1 && dynamic_cast<CheckBox*>(ctl) == nullptr) kinds_ok = false;
            if (col == 2 && dynamic_cast<TextBox*>(ctl) == nullptr) kinds_ok = false;
            if (col == 3 && dynamic_cast<Button*>(ctl) == nullptr) kinds_ok = false;
            if (col == 2 && ctl->AbsoluteBounds().x < note_x) note_x = ctl->AbsoluteBounds().x;
            if (col == 3 && ctl->AbsoluteBounds().x < action_x) action_x = ctl->AbsoluteBounds().x;
        }
        Check(kinds_ok, "frozen reorder keeps widget kinds on data columns");
        Check(action_x < note_x, "frozen reorder puts action widgets left of note");
    }

    {
        TestRoot root;
        auto& table = root.Add<TestTable>();
        table.AddColumn(L"Name", 80.0f);
        table.AddColumn(L"Env", 80.0f);
        table.AddColumn(L"Load", 60.0f).Progress([](size_t row) {
            return row == 0 ? 0.5f : 1.0f;
        });
        table.CellText([](size_t row, size_t col, std::wstring& out) {
            if (col == 0) out = (row < 2) ? L"a" : L"b";
            else if (col == 1) out = (row % 2 == 0) ? L"Prod" : L"Stage";
            else out.clear();
        });
        table.RowCount(4);
        table.Sortable(0, true);
        table.Sortable(1, true);
        root.Measure({240.0f, 220.0f}, theme);
        root.Arrange({0.0f, 0.0f, 240.0f, 220.0f});

        table.CellEditEnabled(true);
        table.OnMouseDoubleClick({120.0f, 32.0f + 14.0f});
        Check(table.Editor() && table.Editor()->Visible(), "column visibility starts with editor");
        std::vector<std::pair<int, bool>> visibility_changes;
        int bound_visibility_changes = 0;
        table.OnColumnVisibilityChanged([&](int col, bool visible) {
            visibility_changes.emplace_back(col, visible);
            Check(table.ColumnVisible(col) == visible, "column visibility callback sees new state");
            Check(!table.Editor() || !table.Editor()->Visible(),
                  "column visibility callback sees no active editor");
        });
        auto visibility_connection = table.BindColumnVisibilityChanged([&](int, bool) {
            ++bound_visibility_changes;
        });
        table.ColumnVisible(1, false);
        table.ColumnVisible(2, false);
        Check(table.ColumnVisible(0) && !table.ColumnVisible(1) && !table.ColumnVisible(2),
              "column hide");
        table.ColumnVisible(0, false);
        Check(table.ColumnVisible(0), "cannot hide last remaining column");
        table.ColumnVisible(2, false);
        table.ColumnVisible(-1, false);
        table.ColumnVisible(3, true);
        Check(visibility_changes.size() == 2 && bound_visibility_changes == 2,
              "column visibility no-op and rejected changes emit nothing");
        table.ColumnVisible(1, true);
        table.ColumnVisible(2, true);
        table.ColumnVisible(2, true);
        Check(visibility_changes == std::vector<std::pair<int, bool>>{
                  {1, false}, {2, false}, {1, true}, {2, true}} &&
              bound_visibility_changes == 4, "column visibility events preserve hide and show order");
        visibility_connection.Disconnect();
        table.ColumnVisible(2, false);
        table.ColumnVisible(2, true);
        Check(visibility_changes.size() == 6 && bound_visibility_changes == 4,
              "column visibility binding disconnects independently");

        table.MoveColumn(1, 0);
        Check(table.ColumnAt(20.0f) == 1, "move column visual order");
        table.MoveColumn(1, 0);

        table.SortBy(0, 1);
        table.SortBy(1, -1, true);
        Check(table.SortKeys().size() == 2 && table.SortKeys()[1].col == 1,
              "multi-column sort keys");
        table.SortBy(0, 0);

        table.GroupBy(1);
        Check(table.GroupCount() == 2, "group by environment");
        Check(table.GroupExpanded(0), "group starts expanded");
        Check(table.RowTop(0) > 20.0f, "first grouped row sits below group header");
        table.GroupExpanded(0, false);
        Check(!table.GroupExpanded(0), "group collapse");
        table.GroupBy(-1);

        table.Footer(true).Aggregate(0, ColumnAggregate::Count);
        table.Aggregate(2, ColumnAggregate::Average);
        Check(table.Footer(), "footer enabled");
        Check(table.ColumnAggregateKind(0) == ColumnAggregate::Count, "footer aggregate");

        table.SelectedIndex(0);
        table.ActiveColumn(0);
        table.OnKey(VK_RIGHT);
        Check(table.ActiveColumn() == 1, "keyboard cell right");
        table.OnKey(VK_LEFT);
        Check(table.ActiveColumn() == 0, "keyboard cell left");
        Check(table.ShowContextMenu({12.0f, 10.0f}), "header context menu path");
        Check(!table.ShowContextMenu({12.0f, 80.0f}), "body has no column menu");
    }
}

void TestImageViewRendering() {
    OffscreenRenderer source;
    if (!source.Init(4, 2)) {
        Check(false, "image source renderer init");
        return;
    }
    ID2D1DeviceContext2* source_dc = source.BeginDraw();
    Painter source_painter;
    source_painter.BeginFrame(source_dc, &UiText(), 1.0f);
    source_painter.FillRect({0.0f, 0.0f, 2.0f, 2.0f}, Color{1.0f, 0.0f, 0.0f, 1.0f});
    source_painter.FillRect({2.0f, 0.0f, 2.0f, 2.0f}, Color{0.0f, 1.0f, 0.0f, 1.0f});
    source_painter.EndFrame();
    Check(source.EndDraw(), "image source enddraw");
    const wchar_t* path = L"lumen_image_view_source.png";
    Check(source.SavePNG(path), "image source save");
    source.Shutdown();

    ImageView image;
    image.SetBounds({0.0f, 0.0f, 80.0f, 80.0f});
    image.CornerRadius(10.0f).Stretch(ImageStretch::UniformToFill);
    Check(image.LoadFile(path) && image.HasImage(), "image file loads");
    Check(image.NaturalPixelSize().w == 4.0f && image.NaturalPixelSize().h == 2.0f,
          "image natural pixel size");
    const std::array<std::byte, 4> invalid{};
    Check(!image.LoadMemory(invalid) && image.HasImage() && image.Status() == ImageStatus::Ready,
          "image failure keeps previous source");

    TestRoot root;
    auto& shown = root.Add<ImageView>();
    Check(shown.LoadFile(path), "image render source loads");
    shown.SetBounds({0.0f, 0.0f, 80.0f, 80.0f});
    shown.Stretch(ImageStretch::UniformToFill).CornerRadius(10.0f);
    root.Measure({80.0f, 80.0f}, MakeTheme());
    root.Arrange({0.0f, 0.0f, 80.0f, 80.0f});
    OffscreenRenderer target;
    if (!target.Init(80, 80)) {
        Check(false, "image target renderer init");
        DeleteFileW(path);
        return;
    }
    ID2D1DeviceContext2* dc = target.BeginDraw();
    Painter painter;
    painter.BeginFrame(dc, &UiText(), 1.0f);
    painter.FillRect({0.0f, 0.0f, 80.0f, 80.0f}, Color{0.0f, 0.0f, 0.0f, 1.0f});
    DrawControlTree(painter, MakeTheme(), &root);
    painter.EndFrame();
    Check(target.EndDraw(), "image target enddraw");
    Color left{}, right{}, corner{};
    target.ReadPixel(20, 40, left);
    target.ReadPixel(60, 40, right);
    target.ReadPixel(1, 1, corner);
    Check(left.r > left.g + 0.25f && right.g > right.r + 0.25f,
          "image colors preserved");
    Check(corner.r < 0.05f && corner.g < 0.05f, "image rounded clip");
    target.Shutdown();
    DeleteFileW(path);
}

// —— 新控件（弹层/导航/轻量展示）交互逻辑断言 ——
struct TestNumberBox : NumberBox {
    using NumberBox::OnChar;
    using NumberBox::OnKey;
    using NumberBox::OnFocusChanged;
    using NumberBox::CursorAt;
    using NumberBox::OnMouseDown;
    using NumberBox::OnMouseUp;
    using NumberBox::OnAnimate;
};
struct TestRepeatButton : RepeatButton {
    using RepeatButton::RepeatButton;
    using RepeatButton::OnMouseDown;
    using RepeatButton::OnMouseUp;
    using RepeatButton::OnMouseMove;
    using RepeatButton::OnKey;
    using RepeatButton::OnAnimate;
};
struct TestRating : Rating {
    using Rating::OnMouseDown;
};
struct TestSkeleton : Skeleton {
    using Skeleton::OnAnimate;
    using Skeleton::Measure;
    using Skeleton::Arrange;
    using Skeleton::Draw;
};
struct TestTreeView : TreeView {
    using TreeView::OnKey;
    using TreeView::OnMouseDown;
};
struct TestTreeTable : TreeTable {
    using TreeTable::OnKey;
    using TreeTable::OnMouseDown;
    using TreeTable::Measure;
    using TreeTable::Arrange;
};
struct TestSplitButton : SplitButton {
    using SplitButton::SplitButton;
    using SplitButton::OnMouseUp;
    using SplitButton::Measure;
};
struct TestTitleBar : TitleBar {
    using TitleBar::Measure;
    using TitleBar::Arrange;
    using TitleBar::Draw;
    using TitleBar::ButtonSlot;
    using TitleBar::hover_;
    using TitleBar::min_glow_;
    using TitleBar::close_glow_;
};
struct TestDropDownButton : DropDownButton {
    using DropDownButton::DropDownButton;
    using DropDownButton::OnMouseDown;
    using DropDownButton::OnMouseUp;
    using DropDownButton::OnKey;
};
struct TestTeachingTip : TeachingTip {
    using TeachingTip::Measure;
    using TeachingTip::Arrange;
    using TeachingTip::OnMouseDown;
    using TeachingTip::OnMouseUp;
};
struct TestInfoBadge : InfoBadge {
    using InfoBadge::Measure;
};
struct TestFileDropZone : FileDropZone {
    using FileDropZone::Measure;
    using FileDropZone::OnFileDrag;
    using FileDropZone::OnFileDrop;
};
struct TestFormField : FormField {
    using FormField::FormField;
    using FormField::Measure;
    using FormField::Arrange;
};
struct TestStatusBar : StatusBar {
    using StatusBar::Measure;
    using StatusBar::Arrange;
    using StatusBar::OnMouseDown;
};
struct TestHotkeyBox : HotkeyBox {
    using HotkeyBox::HotkeyBox;
    using HotkeyBox::Measure;
    using HotkeyBox::OnKey;
    using HotkeyBox::OnChar;
    using HotkeyBox::OnMouseDown;
};
struct TestGroupBox : GroupBox {
    using GroupBox::GroupBox;
    using GroupBox::Measure;
    using GroupBox::Arrange;
};
struct TestViewbox : Viewbox {
    using Viewbox::Viewbox;
    using Viewbox::Measure;
    using Viewbox::Arrange;
    using Viewbox::MapToChildren;
};
struct TestPasswordBox : PasswordBox {
    using PasswordBox::PasswordBox;
    using PasswordBox::OnMouseDown;
    using PasswordBox::CursorAt;
};

bool Near2(double a, double b) { return std::fabs(a - b) <= 1e-6; }

template <typename T>
void root_measure_arrange2(T& control, const Theme& theme) {
    const Size desired = control.Measure({2000.0f, 2000.0f}, theme);
    control.Measure({desired.w + 40.0f, desired.h + 40.0f}, theme);
    control.Arrange({0.0f, 0.0f, desired.w + 40.0f, desired.h + 40.0f});
}

void TestExtras() {
    const Theme theme = MakeTheme();
    {
        Check(ToastKindGlyph(ToastKind::Default) == nullptr, "toast default uses dot");
        Check(std::wstring(ToastKindGlyph(ToastKind::Success)) == icon::kCheckMark,
              "toast success glyph");
        Check(std::wstring(ToastKindGlyph(ToastKind::Warning)) == icon::kWarning,
              "toast warning glyph");
        Check(std::wstring(ToastKindGlyph(ToastKind::Error)) == icon::kShield, "toast error glyph");
        Check(std::wstring(ToastKindGlyph(ToastKind::Info)) == icon::kInfo, "toast info glyph");
        ToastData data;
        Check(data.duration > 2.0f && data.kind == ToastKind::Default, "toast data defaults");
        data.title = L"Saved";
        data.text = L"saved";
        data.action = L"undo";
        data.duration = 0.0f;
        data.kind = ToastKind::Success;
        Check(data.title == L"Saved" && data.action == L"undo" && data.duration <= 0.0f,
              "toast data title, action and persist");
    }
    {
        auto tip = std::make_unique<ToolTip>();
        Check(tip->MaxWidth() > 200.0f, "tooltip default max width");
        Check(tip->Closable(), "tooltip closable by default");
        tip->Add<Label>(L"Title", TextRole::CaptionStrong);
        tip->Add<Label>(L"Body text that wraps").Wrap(true);
        Check(tip->ChildCount() == 2, "tooltip children");
        TestRoot host;
        auto& measured = host.Add<ToolTip>();
        measured.Add<Label>(L"Title", TextRole::CaptionStrong);
        measured.Add<Label>(L"Body text that wraps").Wrap(true);
        host.Measure({280.0f, 2000.0f}, theme);
        Check(measured.DesiredSize().w > 8.0f && measured.DesiredSize().h > 16.0f,
              "tooltip measure");
        Button btn(L"Host");
        btn.ToolTip(std::move(tip));
        Check(btn.ToolTipContent() != nullptr, "tooltip content owned");
        Check(btn.ToolTip().empty(), "string cleared by custom");
        Check(btn.HasToolTip(), "has custom tooltip");
        btn.ToolTip(L"plain");
        Check(btn.ToolTipContent() == nullptr, "string clears custom");
        Check(btn.ToolTip() == L"plain", "string tooltip");
        Check(btn.HasToolTip(), "has string tooltip");
        btn.ToolTip(L"");
        Check(!btn.HasToolTip(), "empty clears tooltip");
    }
    {
        TestNumberBox box;
        box.Range(0.0, 10.0);
        box.OnChar(L'1');
        box.OnChar(L'2');
        Check(box.Text() == L"12", "number accepts digits");
        box.OnChar(L'a');
        Check(box.Text() == L"12", "number rejects letter");
        box.OnFocusChanged(false);
        Check(Near2(box.Value(), 10.0) && box.Text() == L"10", "number clamp+format on blur");
        box.Value(7.0);
        box.OnKey(VK_UP);
        Check(Near2(box.Value(), 8.0), "number step up");
        box.OnKey(VK_DOWN);
        Check(Near2(box.Value(), 7.0), "number step down");
        box.Value(99.0);
        Check(Near2(box.Value(), 10.0), "number value clamps to range");
    }
    {
        // spin 区光标为箭头，文本区仍为 IBeam（需要 Arrange 后的绝对坐标）。
        TestRoot root;
        auto& box = root.Add<TestNumberBox>();
        box.Range(0.0, 100.0).Value(42.0);
        root.Measure({300.0f, 60.0f}, theme);
        root.Arrange({0.0f, 0.0f, 300.0f, 60.0f});
        const Rect b = box.AbsoluteBounds();
        Check(box.CursorAt({b.Right() - 4.0f, b.h * 0.5f}) == CursorShape::Arrow,
              "number spin zone cursor");
        Check(box.CursorAt({b.w * 0.4f, b.h * 0.5f}) == CursorShape::IBeam,
              "number text zone cursor");
        box.Value(10.0);
        box.OnMouseDown({b.w - 4.0f, 4.0f}, 0x0001);
        Check(Near2(box.Value(), 11.0), "number spin press steps once");
        box.OnAnimate(0.10f);
        Check(Near2(box.Value(), 11.0), "number spin waits delay");
        box.OnAnimate(0.35f);
        Check(box.Value() > 11.0 + 0.5, "number spin repeats after delay");
        box.OnMouseUp({b.w - 4.0f, 4.0f}, 0);
        const double held = box.Value();
        box.OnAnimate(1.0f);
        Check(Near2(box.Value(), held), "number spin stops on release");
    }
    {
        RepeatHold hold;
        hold.delay = 0.20f;
        hold.interval = 0.05f;
        hold.Press();
        Check(hold.Tick(0.10f, true) == 0, "repeat hold delay");
        Check(hold.Tick(0.11f, true) == 1, "repeat hold first extra");
        Check(hold.Tick(0.05f, true) == 1, "repeat hold interval");
        Check(hold.Tick(0.20f, false) == 0, "repeat hold pauses when inactive");
        hold.Release();
        Check(hold.Tick(1.0f, true) == 0, "repeat hold released");
    }
    {
        TestRoot root;
        auto& btn = root.Add<TestRepeatButton>(L"+");
        btn.Delay(0.20f).Interval(0.05f);
        int clicks = 0;
        btn.OnClick([&] { ++clicks; });
        root.Measure({200.0f, 80.0f}, theme);
        root.Arrange({0.0f, 0.0f, 200.0f, 80.0f});
        btn.OnMouseDown({8.0f, 8.0f}, 0x0001);
        Check(clicks == 1, "repeat button fires on press");
        btn.OnAnimate(0.10f);
        Check(clicks == 1, "repeat button waits delay");
        btn.OnAnimate(0.15f);
        Check(clicks >= 2, "repeat button starts after delay");
        btn.OnMouseMove({-10.0f, 8.0f}, 0x0001);
        const int paused = clicks;
        btn.OnAnimate(0.50f);
        Check(clicks == paused, "repeat button pauses outside");
        btn.OnMouseMove({8.0f, 8.0f}, 0x0001);
        btn.OnAnimate(0.20f);
        Check(clicks > paused, "repeat button resumes inside");
        btn.OnMouseUp({8.0f, 8.0f}, 0);
        const int released = clicks;
        btn.OnAnimate(1.0f);
        Check(clicks == released, "repeat button stops on release");
        btn.OnKey(VK_SPACE);
        Check(clicks == released + 1, "repeat button space fires");
    }
    {
        TestRoot root;
        auto& rating = root.Add<TestRating>();
        int rated = -1;
        rating.OnRated([&](int v) { rated = v; });
        root.Measure({400.0f, 60.0f}, theme);
        root.Arrange({0.0f, 0.0f, 400.0f, 60.0f});
        rating.Value(2.0);
        rating.OnMouseDown({3 * 24 + 10.0f, 10.0f}, 0x0001);
        Check(Near2(rating.Value(), 4.0) && rated == 4, "rating click sets star");
        rating.Value(2.0);
        rating.OnMouseDown({1 * 24 + 10.0f, 10.0f}, 0x0001);
        Check(Near2(rating.Value(), 1.0), "rating click same star cancels");
        rating.ReadOnly(true);
        rating.OnMouseDown({3 * 24 + 10.0f, 10.0f}, 0x0001);
        Check(Near2(rating.Value(), 1.0), "rating readonly ignores clicks");
    }
    {
        TestRoot root;
        auto& crumb = root.Add<TestBreadcrumb>();
        crumb.AddItem(L"库").AddItem(L"项目").AddItem(L"设置");
        size_t nav = 999;
        crumb.OnNavigate([&](size_t index) { nav = index; });
        root.Measure({400.0f, 40.0f}, theme);
        root.Arrange({0.0f, 0.0f, 400.0f, 40.0f});
        crumb.OnMouseDown({crumb.DesiredSize().w - 10.0f, 20.0f}, 0x0001);
        Check(crumb.SelectedIndex() == 2 && nav == 2, "breadcrumb click navigates");
        crumb.SelectedIndex(0);
        Check(crumb.SelectedIndex() == 0 && nav == 2, "breadcrumb programmatic silent");
    }
    {
        TestSkeleton skeleton;
        skeleton.Lines(3);
        Check(skeleton.OnAnimate(0.1f), "skeleton animates while active");
        skeleton.Active(false);
        Check(!skeleton.OnAnimate(0.1f), "skeleton stops when inactive");
    }
    {
        AutoSuggestBox box;
        box.Suggestions([](std::wstring_view query) {
            std::vector<std::wstring> all{L"Button", L"Badge", L"Breadcrumb", L"Slider"};
            std::vector<std::wstring> out;
            for (const std::wstring& item : all) {
                if (std::wstring_view(item).find(query) == 0) out.push_back(item);
            }
            return out;
        });
        box.MaxSuggestions(8);
        Check(box.QuerySuggestions().size() == 4, "suggest empty query all");
        box.Text(L"B");
        Check(box.QuerySuggestions().size() == 3, "suggest prefix filter");
        box.MaxSuggestions(2);
        Check(box.QuerySuggestions().size() == 2, "suggest limit");
        TestAutoSuggestBox typed;
        typed.OnChar(L'a');
        typed.OnChar(L'b');
        typed.OnChar(L'c');
        Check(typed.Text() == L"abc", "suggest types characters");
        typed.OnKey(VK_BACK);
        typed.OnKey(VK_BACK);
        Check(typed.Text() == L"a", "suggest backspace repeats without refocus");
    }
    {
        TestTreeView tree;
        tree.Roots(2);
        tree.ChildCount([](size_t id) { return id == 0 ? 2 : 0; });
        tree.ChildAt([](size_t id, size_t index) { (void)id; return 100 + index; });
        tree.ItemText([](size_t id, std::wstring& s) { s = L"n" + std::to_wstring(id); });
        Check(tree.VisibleCount() == 2, "tree roots flattened");
        tree.Expand(0);
        Check(tree.VisibleCount() == 4, "tree expand flattens children");
        Check(tree.VisibleIdAt(1) == 100 && tree.VisibleIdAt(2) == 101, "tree child order");
        tree.SelectedId(101);
        Check(tree.SelectedId() == 101, "tree select by id");
        tree.SelectedId(100);
        tree.OnKey(VK_DOWN);
        Check(tree.SelectedId() == 101, "tree arrow moves down");
        tree.Expand(0, false);
        Check(tree.VisibleCount() == 2 && tree.SelectedId() == 0,
              "tree collapse clamps selection to parent");
        tree.Expand(0);
        tree.SelectedId(1);
        tree.Expand(0, false);
        Check(tree.VisibleCount() == 2 && tree.SelectedId() == 1,
              "tree collapse keeps visible sibling root");
        tree.RevealId(101);
        Check(tree.VisibleCount() == 4, "tree reveal expands ancestors");
        int activated = -1;
        tree.OnActivate([&](size_t id) { activated = static_cast<int>(id); });
        tree.SelectedId(0);
        tree.OnKey(VK_RETURN);
        Check(activated == 0, "tree enter activates");
        // 点击命中：chevron 区折叠/展开（局部坐标），文本区仅选中。
        TestRoot click_root;
        auto& click_tree = click_root.Add<TestTreeView>();
        click_tree.Roots(2);
        click_tree.ChildCount([](size_t id) { return id == 0 ? 2 : 0; });
        click_tree.ChildAt([](size_t id, size_t index) { (void)id; return 100 + index; });
        click_tree.Expand(0);
        click_root.Measure({300.0f, 200.0f}, theme);
        click_root.Arrange({0.0f, 0.0f, 300.0f, 200.0f});
        click_tree.OnMouseDown({8.0f, 14.0f}, 0x0001);    // 行 0 chevron 区 x∈[4,20)
        Check(!click_tree.Expanded(0), "tree chevron click collapses");
        click_tree.OnMouseDown({8.0f, 14.0f}, 0x0001);
        Check(click_tree.Expanded(0), "tree chevron click expands");
        click_tree.OnMouseDown({60.0f, 14.0f}, 0x0001);   // 行 0 文本区
        Check(click_tree.SelectedId() == 0, "tree row click selects");
        click_tree.OnMouseDown({28.0f, 14.0f + 28.0f}, 0x0001);   // 行 1（子节点，无子 → 仅选中）
        Check(click_tree.SelectedId() == 100, "tree child row click selects");
    }
    {
        // 平铺数据入口与回调式等价；ExpandAll 全展开。
        TestTreeView flat;
        flat.SetFlatData({TreeView::kNone, TreeView::kNone, 0, 0, 1, 3, 3});
        flat.ItemText([](size_t id, std::wstring& s) { s = L"n" + std::to_wstring(id); });
        Check(flat.Roots() == 2 && flat.VisibleCount() == 2, "flat data roots");
        flat.ExpandAll();
        Check(flat.VisibleCount() == 7, "flat expand all");
        Check(flat.VisibleIdAt(1) == 2 && flat.VisibleIdAt(2) == 3, "flat child order");
        TestTreeView mirror;
        mirror.Roots(2);
        mirror.ChildCount([](size_t id) {
            return id == 0 ? 2 : (id == 1 ? 1 : (id == 3 ? 2 : 0));
        });
        mirror.ChildAt([](size_t id, size_t index) {
            return id == 0 ? (index == 0 ? 2 : 3) : (id == 1 ? 4 : (index == 0 ? 5 : 6));
        });
        mirror.ExpandAll();
        Check(mirror.VisibleCount() == 7 && mirror.VisibleIdAt(4) == 6,
              "provider model matches flat data");
        TestTreeView sparse;
        sparse.SetFlatData({TreeView::kNone, 0, TreeView::kNone});
        Check(sparse.Roots() == 2 && sparse.VisibleCount() == 2, "flat sparse roots");
        Check(sparse.VisibleIdAt(0) == 0 && sparse.VisibleIdAt(1) == 2, "flat sparse root ids");
        sparse.ExpandAll();
        Check(sparse.VisibleCount() == 3 && sparse.VisibleIdAt(1) == 1, "flat sparse expand");
    }
    {
        TestTreeTable table;
        table.AddColumn(L"Name", 180.0f).AddColumn(L"Kind", 80.0f);
        table.SetFlatData({TreeTable::kNone, TreeTable::kNone, 0, 0});
        table.ItemText([](size_t id, std::wstring& s) { s = L"n" + std::to_wstring(id); });
        Check(table.ColumnCount() == 2, "tree table columns");
        Check(table.Roots() == 2 && table.VisibleCount() == 2, "tree table roots");
        table.Expand(0);
        Check(table.VisibleCount() == 4, "tree table expand flattens children");
        Check(table.VisibleIdAt(1) == 2 && table.VisibleIdAt(2) == 3, "tree table child order");
        table.SelectedId(3);
        Check(table.SelectedId() == 3, "tree table select by id");
        table.OnKey(VK_UP);
        Check(table.SelectedId() == 2, "tree table arrow moves up");
        table.Expand(0, false);
        Check(table.VisibleCount() == 2 && table.SelectedId() == 0,
              "tree table collapse clamps selection to parent");
        table.RevealId(3);
        Check(table.VisibleCount() == 4, "tree table reveal expands");
        int activated = -1;
        table.OnActivate([&](size_t id) { activated = static_cast<int>(id); });
        table.SelectedId(1);
        table.OnKey(VK_RETURN);
        Check(activated == 1, "tree table enter activates");
        table.CollapseAll();
        table.ExpandAll();
        Check(table.VisibleCount() == 4 && table.VisibleIdAt(1) == 2,
              "tree table expand all");
        TestRoot click_root;
        auto& click = click_root.Add<TestTreeTable>();
        click.AddColumn(L"Name");
        click.SetFlatData({TreeTable::kNone, 0});
        click.Expand(0);
        click_root.Measure({400.0f, 200.0f}, theme);
        click_root.Arrange({0.0f, 0.0f, 400.0f, 200.0f});
        click.OnMouseDown({8.0f, 32.0f + 14.0f}, 0x0001);
        Check(!click.Expanded(0), "tree table chevron click collapses");
        click.OnMouseDown({8.0f, 32.0f + 14.0f}, 0x0001);
        Check(click.Expanded(0), "tree table chevron click expands");
        click.OnMouseDown({80.0f, 32.0f + 14.0f}, 0x0001);
        Check(click.SelectedId() == 0, "tree table row click selects");
    }
    {
        // 密码框揭示开关：切换掩码态不改真实文本。
        TestRoot root;
        auto& pwd = root.Add<TestPasswordBox>(L"secret");
        pwd.Revealable(true);
        root.Measure({220.0f, 40.0f}, theme);
        root.Arrange({0.0f, 0.0f, 220.0f, 40.0f});
        Check(pwd.Password() && !pwd.Revealed(), "password starts masked");
        pwd.OnMouseDown({pwd.AbsoluteBounds().w - 12.0f, 20.0f}, 0x0001);
        Check(pwd.Revealed() && !pwd.Password() && pwd.Text() == L"secret",
              "password reveal keeps text");
        pwd.OnMouseDown({pwd.AbsoluteBounds().w - 12.0f, 20.0f}, 0x0001);
        Check(!pwd.Revealed() && pwd.Password(), "password hide restores mask");
        Check(pwd.CursorAt({pwd.AbsoluteBounds().w - 8.0f, 20.0f}) == CursorShape::Hand,
              "password reveal zone cursor");
    }
    {
        // Table 单元格编辑：双击进入 → 提交回调（数据行换算）→ Esc 取消不回调。
        TestRoot root;
        auto& table = root.Add<TestTable>();
        table.AddColumn(L"A", 100.0f);
        table.AddColumn(L"B");
        static const std::vector<int> kCellValues{9, 5, 7, 1};
        table.CellText([](size_t row, size_t col, std::wstring& out) {
            if (col == 0) out = std::to_wstring(kCellValues[row]);
            else out = L"b" + std::to_wstring(row);
        });
        table.RowCount(4);
        table.CellEditEnabled(true);
        int edited_row = -1;
        std::wstring edited_text;
        table.OnCellEdited([&](size_t r, int c, std::wstring t) {
            edited_row = static_cast<int>(r);
            edited_text = t;
            (void)c;
        });
        root.Measure({300.0f, 400.0f}, theme);
        root.Arrange({0.0f, 0.0f, 300.0f, 400.0f});
        table.OnMouseDoubleClick({50.0f, 32.0f + 14.0f});
        Check(table.Editor() != nullptr && table.Editor()->Text() == L"9",
              "cell edit begins with cell text");
        table.Editor()->Text(L"99");
        table.Commit();
        Check(edited_row == 0 && edited_text == L"99", "cell edit commits data row");
        table.OnMouseDoubleClick({50.0f, 32.0f + 14.0f});
        table.Cancel();
        Check(edited_row == 0 && edited_text == L"99", "cell edit cancel skips callback");
    }
    {
        struct OriginPanel : Panel {
            using Panel::Measure;
            using Panel::Arrange;
        };
        OriginPanel root;
        auto& table = root.Add<TestTable>();
        table.AddColumn(L"A", 100.0f);
        table.AddColumn(L"B");
        static const std::vector<int> kOff{1, 2};
        table.CellText([](size_t row, size_t col, std::wstring& out) {
            if (col == 0) out = std::to_wstring(kOff[row]);
            else out = L"x";
        });
        table.RowCount(2);
        table.CellEditEnabled(true);
        table.SetBounds({80.0f, 0.0f, 220.0f, 200.0f});
        root.Measure({400.0f, 200.0f}, theme);
        root.Arrange({0.0f, 0.0f, 400.0f, 200.0f});
        table.OnMouseDoubleClick({50.0f, 32.0f + 14.0f});
        Check(table.Editor() != nullptr, "offset table editor exists");
        if (table.Editor()) {
            const Rect ed = table.Editor()->AbsoluteBounds();
            Check(ed.x >= 80.0f && ed.x < 160.0f, "cell editor parent-relative at x=80");
        }
    }
    {
        // SplitView：展开 220 / 折叠 Compact 48；侧栏子级不得溢到内容区。
        TestRoot root;
        auto& shell = root.Add<TestSplitView>();
        auto& nav = shell.Pane().Add<Button>(L"总览", ButtonKind::Transparent);
        nav.Glyph(icon::kLayers).SizeClass(ButtonSize::Small);
        shell.Content().Add<Label>(L"总览", TextRole::BodyStrong);
        root.Measure({600.0f, 200.0f}, theme);
        root.Arrange({0.0f, 0.0f, 600.0f, 200.0f});
        Check(Near(shell.Pane().AbsoluteBounds().w, 220.0f), "splitview pane length");
        shell.Collapse(true);
        for (int i = 0; i < 40; ++i) shell.OnAnimate(0.2f);   // 缓动收敛
        Check(Near(shell.Pane().AbsoluteBounds().w, 48.0f, 0.5f), "splitview collapse compact");
        const Rect pane = shell.Pane().AbsoluteBounds();
        Check(nav.AbsoluteBounds().Right() <= pane.Right() + 0.5f,
              "splitview compact pane children stay inside");
        Check(shell.Content().AbsoluteBounds().x + 0.5f >= pane.Right(),
              "splitview compact content starts after pane");
    }
    {
        TestRoot root;
        auto& wrap = root.Add<WrapPanel>().Gap(8.0f, 8.0f);
        auto& a = wrap.Add<Panel>().SetBounds({0.0f, 0.0f, 80.0f, 20.0f});
        auto& b = wrap.Add<Panel>().SetBounds({0.0f, 0.0f, 80.0f, 20.0f});
        auto& c = wrap.Add<Panel>().SetBounds({0.0f, 0.0f, 80.0f, 20.0f});
        root.Measure({200.0f, 80.0f}, theme);
        root.Arrange({0.0f, 0.0f, 200.0f, 80.0f});
        Check(Near(a.Bounds().x, 0.0f) && Near(b.Bounds().x, 88.0f), "wrap first row");
        Check(c.Bounds().y >= 20.0f, "wrap third item to next row");
    }
    {
        TestColorPicker picker;
        picker.Color(Color::Hex(0xFF0000));
        const Color red = picker.Color();
        Check(red.r > 0.9f && red.g < 0.15f && red.b < 0.15f, "colorpicker set red");
        picker.Measure({280.0f, 240.0f}, theme);
        picker.Arrange({0.0f, 0.0f, 280.0f, 240.0f});
        Check(picker.Measure({280.0f, 240.0f}, theme).w == 280.0f, "colorpicker fills available width");
        Check(picker.ChildCount() == 1, "colorpicker hosts text box");
        const auto& field = static_cast<const TextBox&>(picker.Child(0));
        Check(field.Text() == L"#FF0000", "colorpicker hex field tracks rgb");
        picker.OnKey(VK_DOWN);
        Check(picker.Color().r + picker.Color().g + picker.Color().b < red.r + red.g + red.b + 0.001f,
              "colorpicker value down");
        Check(picker.Hex(L"#00AA00") && picker.Hex() == L"#00AA00", "colorpicker set hex");
        Check(field.Text() == L"#00AA00", "colorpicker hex field after SetHex");
        picker.Color(Color::Hex(0xFF0000));
        Check(picker.Hex() == L"#FF0000", "colorpicker hex from rgb");
    }
    {
        using namespace std::chrono;
        TestCalendarView calendar;
        calendar.Value(year{2026} / August / day{31});
        calendar.Measure({308.0f, 320.0f}, theme);
        calendar.Arrange({0.0f, 0.0f, 308.0f, 320.0f});
        calendar.OnKey(VK_LEFT);
        calendar.OnKey(VK_RETURN);
        Check(calendar.Value() && calendar.Value()->day() == day{30}, "calendar key previous day");
    }
    {
        using namespace std::chrono;
        TestCalendarView calendar;
        calendar.Value(year{2026} / August / day{31});
        calendar.Measure({308.0f, 320.0f}, theme);
        calendar.Arrange({0.0f, 0.0f, 308.0f, 320.0f});
        calendar.OnMouseDown({154.0f, 28.0f}, 0x0001);
        calendar.OnMouseDown({52.0f, 82.0f}, 0x0001);
        calendar.OnKey(VK_RETURN);
        Check(calendar.Value() && calendar.Value()->month() == January,
              "calendar title opens month grid");
    }
    {
        using namespace std::chrono;
        TestCalendarView calendar;
        calendar.Value(year{2026} / August / day{31});
        calendar.Measure({308.0f, 320.0f}, theme);
        calendar.Arrange({0.0f, 0.0f, 308.0f, 320.0f});
        calendar.OnMouseDown({154.0f, 28.0f}, 0x0001);
        calendar.OnMouseDown({154.0f, 28.0f}, 0x0001);
        calendar.OnMouseDown({52.0f, 82.0f}, 0x0001);
        calendar.OnKey(VK_RETURN);
        calendar.OnKey(VK_RETURN);
        Check(calendar.Value() && calendar.Value()->year() == year{2021},
              "calendar year grid then month");
    }
    {
        TestChip chip(L"Filter");
        chip.Selectable(true);
        chip.Measure({80.0f, 28.0f}, theme);
        chip.Arrange({0.0f, 0.0f, 80.0f, 28.0f});
        chip.OnKey(VK_SPACE);
        Check(chip.Selected(), "chip space toggles");
        int closed = 0;
        TestChip tag(L"Draft");
        tag.Closable(true).OnClosed([&] { ++closed; });
        tag.Measure({80.0f, 28.0f}, theme);
        tag.Arrange({0.0f, 0.0f, 80.0f, 28.0f});
        tag.OnMouseDown({70.0f, 14.0f}, 0x0001);
        Check(closed == 1, "chip close dismisses");
    }
    {
        TestRoot root;
        auto& grid = root.Add<TestGridView>();
        grid.ItemCount(9).ItemSize({80.0f, 80.0f}).ItemGap(0.0f);
        root.Measure({250.0f, 260.0f}, theme);
        root.Arrange({0.0f, 0.0f, 250.0f, 260.0f});
        grid.OnMouseDown({40.0f, 40.0f}, 0x0001);
        Check(grid.SelectedIndex() == 0, "gridview click first tile");
        grid.OnKey(VK_RIGHT);
        Check(grid.SelectedIndex() == 1, "gridview key next tile");
    }
    {
        Button btn(L"ctx");
        Menu menu;
        menu.AddItem(L"Copy", nullptr);
        btn.ContextMenu(std::move(menu));
        Check(btn.HasContextMenu(), "control context menu stored");
        Check(!btn.ShowContextMenu({0.0f, 0.0f}), "context menu without window no-ops");
    }
    {
        // Pagination：键盘/点击翻页。
        TestRoot root;
        auto& pager = root.Add<TestPagination>();
        pager.PageCount(12).Current(5);
        root.Measure({500.0f, 40.0f}, theme);
        root.Arrange({0.0f, 0.0f, 500.0f, 40.0f});
        pager.OnKey(VK_RIGHT);
        Check(pager.Current() == 6, "pagination key next");
        pager.OnMouseDown({20.0f, 14.0f}, 0x0001);   // 上一页箭头
        Check(pager.Current() == 5, "pagination prev click");
    }
    {
        // MenuBar：装配与无窗口静默。
        TestMenuBar bar;
        Menu a;
        a.AddItem(L"x", nullptr);
        bar.AddMenu(L"File", std::move(a)).AddMenu(L"Edit", Menu{});
        Check(bar.Count() == 2, "menubar add menus");
        root_measure_arrange2(bar, theme);
        bar.OnMouseDown({20.0f, 16.0f}, 0x0001);   // 无窗口：静默
        Check(true, "menubar click headless safe");
    }
    {
        // Carousel：交互翻页回调；编程切换不回调。
        TestRoot root;
        auto& car = root.Add<TestCarousel>();
        car.AddPage<Label>(L"a");
        car.AddPage<Label>(L"b");
        car.AddPage<Label>(L"c");
        Check(car.PageCount() == 3, "carousel pages");
        size_t seen = 999;
        car.OnPageChanged([&](size_t page) { seen = page; });
        car.OnKey(VK_RIGHT);
        Check(car.Current() == 1 && seen == 1, "carousel key page + callback");
        car.Current(2);
        Check(car.Current() == 2 && seen == 1, "carousel programmatic silent");
        root_measure_arrange2(car, theme);
        // 圆点条按局部坐标命中：点第 0 格回第 0 页并回调。
        const Rect cb = car.AbsoluteBounds();
        car.OnMouseDown({cb.w * 0.5f - 14.0f, cb.h - 10.0f}, 0x0001);
        Check(car.Current() == 0 && seen == 0, "carousel dot click navigates");
        car.OnMouseDown({cb.w * 0.5f + 40.0f, cb.h - 10.0f}, 0x0001);   // 圆点条外：不翻页
        Check(car.Current() == 0, "carousel outside strip ignored");
    }
    {
        // Stepper：仅允许回跳，前进不触发。
        TestStepper steps;
        steps.AddStep(L"a").AddStep(L"b").AddStep(L"c");
        steps.Current(2);
        size_t stepped = 999;
        steps.OnStepChanged([&](size_t x) { stepped = x; });
        root_measure_arrange2(steps, theme);
        steps.OnMouseDown({10.0f, 13.0f}, 0x0001);
        Check(steps.Current() == 0 && stepped == 0, "stepper back navigate");
        steps.OnMouseDown({260.0f, 13.0f}, 0x0001);
        Check(steps.Current() == 0 && stepped == 0, "stepper forward blocked");
    }
    {
        // 弹出一行式：未入窗口树时静默（不弹、不崩），DropdownMenu 装配无副作用。
        TestRoot root;
        auto& split = root.Add<TestSplitButton>(L"demo");
        Menu menu;
        menu.AddItem(L"item", nullptr);
        split.DropdownMenu(std::move(menu));
        Check(Menu().PopupTo(split) == -1, "menu popup without window no-ops");
        Check(MenuLabel(L"&Copy") == L"Copy", "menu label strips mnemonic");
        Check(MenuAccessKey(L"&Copy") == L'C', "menu access key");
        Check(MenuLabel(L"Look && Feel") == L"Look & Feel", "menu escaped ampersand");
        Check(MenuAccessKey(L"Look && Feel") == 0, "menu escaped has no access key");
        Check(MenuAccessKey(L"C&omfortable") == L'O', "menu access key not first letter");
        const float p_adv = AdvanceUiText(L"P", TextRole::Body, nullptr);
        const float paste_adv = AdvanceUiText(L"Paste", TextRole::Body, nullptr);
        Check(p_adv > 2.0f && p_adv * 1.6f < paste_adv, "mnemonic letter narrower than word");
        Check(p_adv <= MeasureUiText(L"P", TextRole::Body).w, "advance excludes ink pad");
        Menu grouped;
        grouped.AddHeader(L"View");
        grouped.AddItem(L"Compact", nullptr).Radio().RadioGroup(L"density").Checked(true);
        grouped.AddItem(L"Comfortable", nullptr).Radio().RadioGroup(L"density");
        grouped.AddItem(L"Word wrap", nullptr).Checked(true);
        Check(grouped.Items()[0].header && grouped.Items()[0].disabled, "menu header not interactive");
        Check(grouped.Items()[1].radio && grouped.Items()[1].checked &&
                  grouped.Items()[1].radio_group == L"density",
              "menu radio group compact");
        Check(grouped.Items()[2].radio && !grouped.Items()[2].checked, "menu radio sibling off");
        Check(grouped.Items()[3].checkable && grouped.Items()[3].checked,
              "menu Checked() is checkable");
        root.Measure({200.0f, 44.0f}, theme);
        root.Arrange({0.0f, 0.0f, 200.0f, 44.0f});
        split.OnMouseUp({split.AbsoluteBounds().w - 6.0f, 22.0f}, 0x0001);   // 箭头区：无窗口静默
        Check(split.Text() == L"demo", "split dropdown headless safe");
        TestSplitButton packed(L"Auto deploy");
        packed.Toggle(true).Checked(true);
        const Size pack = packed.Measure({2000.0f, 2000.0f}, theme);
        const float text_w = UiText().MeasureText(L"Auto deploy", TextRole::Body).w;
        Check(pack.w + 0.5f >= 12.0f + 20.0f + text_w + 12.0f + 32.0f,
              "split button checked width includes check and text gap");
    }
    {
        TestTitleBar bar;
        bar.Title(L"LUMEN Gallery").Glyph(L"*").Status(L"86.6 FPS · 33.02 ms");
        bar.Measure({1280.0f, 40.0f}, theme);
        bar.Arrange({0.0f, 0.0f, 1280.0f, 40.0f});
        const float text_w = UiText().MeasureText(L"LUMEN Gallery", TextRole::CaptionStrong).w;
        Check(bar.Content().Bounds().x + 0.5f >= 16.0f + 24.0f + text_w + 12.0f,
              "title bar caption width includes full title");
    }
    {
        // 1x1 opaque PNG — layout must reserve the glyph slot once a bitmap icon loads.
        static constexpr unsigned char kPng1x1[] = {
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
            0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
            0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xDE, 0x00, 0x00, 0x00,
            0x0C, 0x49, 0x44, 0x41, 0x54, 0x08, 0xD7, 0x63, 0xF8, 0xCF, 0xC0, 0x00,
            0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0xFE, 0xD4, 0xEF, 0x00, 0x00,
            0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
        TestTitleBar bar;
        bar.Title(L"LUMEN Gallery");
        Check(bar.LoadIconMemory({reinterpret_cast<const std::byte*>(kPng1x1), sizeof(kPng1x1)}),
              "title bar LoadIconMemory accepts png");
        Check(bar.HasIcon(), "title bar reports bitmap icon");
        bar.Measure({1280.0f, 40.0f}, theme);
        bar.Arrange({0.0f, 0.0f, 1280.0f, 40.0f});
        const float text_w = UiText().MeasureText(L"LUMEN Gallery", TextRole::CaptionStrong).w;
        Check(bar.Content().Bounds().x + 0.5f >= 16.0f + 24.0f + text_w + 12.0f,
              "title bar bitmap icon reserves caption slot");
    }
    {
        TestRoot root;
        auto& dd = root.Add<TestDropDownButton>(L"Export");
        int opened = 0;
        dd.OnDropdown([&] { ++opened; });
        root.Measure({200.0f, 44.0f}, theme);
        root.Arrange({0.0f, 0.0f, 200.0f, 44.0f});
        dd.OnKey(VK_SPACE);
        Check(opened == 1, "dropdown space opens");
        dd.OnKey(VK_DOWN);
        Check(opened == 2, "dropdown down opens");
        dd.OnMouseDown({12.0f, 12.0f}, 0x0001);
        dd.OnMouseUp({12.0f, 12.0f}, 0x0001);
        Check(opened == 3, "dropdown click opens");
        dd.Enabled(false);
        dd.OnKey(VK_SPACE);
        dd.OnMouseDown({12.0f, 12.0f}, 0x0001);
        dd.OnMouseUp({12.0f, 12.0f}, 0x0001);
        Check(opened == 3, "dropdown disabled ignores");
        Menu menu;
        menu.AddItem(L"item", nullptr);
        dd.Enabled(true).DropdownMenu(std::move(menu));
        dd.Open();
        Check(dd.Text() == L"Export", "dropdown menu headless safe");
    }
    {
        TestRoot root;
        auto& box = root.Add<TokenBox>();
        Check(box.AddToken(L"  alpha  ") && box.Tokens().size() == 1 && box.Tokens()[0] == L"alpha",
              "token trim on add");
        Check(!box.AddToken(L"alpha"), "token rejects duplicate");
        box.AllowDuplicates(true);
        Check(box.AddToken(L"alpha") && box.Tokens().size() == 2, "token allows duplicate");
        box.AllowDuplicates(false);
        box.Tokens({L"Source", L"Glow"});
        Check(box.Tokens().size() == 2 && box.Tokens()[1] == L"Glow", "token replace set");
        box.Draft(L"beta");
        Check(box.CommitDraft() && box.Tokens().size() == 3 && box.Tokens()[2] == L"beta",
              "token commit draft");
        box.Draft(L"one, two；three");
        box.CommitDraft();
        Check(box.Tokens().size() == 6 && box.Tokens()[5] == L"three", "token split delimiters");
        Check(box.RemoveLast() && box.Tokens().size() == 5, "token remove last");
        box.MaxTokens(5);
        Check(!box.AddToken(L"overflow"), "token honors max");
        box.ClearTokens();
        Check(box.Tokens().empty(), "token clear");
        box.Tokens({L"Alpha", L"Beta", L"Gamma", L"Delta", L"Epsilon", L"Zeta"});
        root.Measure({220.0f, 400.0f}, theme);
        Check(box.DesiredSize().h > theme.input_height + 8.0f, "token wraps to extra row");
    }
    {
        TestTeachingTip tip;
        tip.Title(L"DropDownButton")
            .Message(L"整颗点击弹出菜单。")
            .Glyph(icon::kSparkle);
        const Size size = tip.Measure({280.0f, 400.0f}, theme);
        Check(size.w >= 160.0f && size.h >= 56.0f, "teaching tip measures card");
        tip.Arrange({20.0f, 40.0f, size.w, size.h});
        int closed = 0;
        tip.OnClosed([&] { ++closed; });
        const float close_x = size.w - 14.0f - 14.0f;
        tip.OnMouseDown({close_x, 16.0f}, 0x0001);
        tip.OnMouseUp({close_x, 16.0f}, 0x0001);
        Check(closed == 1 && !tip.Visible(), "teaching tip close dismisses");
    }
    {
        TestInfoBadge badge;
        Check(badge.Measure({100.0f, 100.0f}, theme).w == 0.0f, "info badge empty measures zero");
        badge.Dot();
        const Size dot = badge.Measure({100.0f, 100.0f}, theme);
        Check(dot.w == 8.0f && dot.h == 8.0f, "info badge dot size");
        badge.Count(7);
        const Size n = badge.Measure({100.0f, 100.0f}, theme);
        Check(n.h == 16.0f && n.w >= n.h, "info badge count pill");
        badge.Count(150);
        const Size overflow = badge.Measure({100.0f, 100.0f}, theme);
        Check(overflow.w > n.w, "info badge overflow wider than 7");
        TestTabs tabs;
        tabs.AddTab({L"inbox", L"Inbox", icon::kMail, false, InfoBadgeData::Count(12)});
        Check(tabs.TabBadge(L"inbox").kind == InfoBadgeData::Kind::Count &&
                  tabs.TabBadge(L"inbox").count == 12,
              "tab item badge retained");
        tabs.TabBadge(L"inbox", InfoBadgeData::Dot());
        Check(tabs.TabBadge(L"inbox").kind == InfoBadgeData::Kind::Dot, "tab set badge");
        TestNavigationView nav;
        nav.Items({{L"home", L"Home", icon::kLayers},
                   {L"files", L"Files", icon::kFolder}});
        nav.ItemBadge(L"files", InfoBadgeData::Count(3));
        Check(nav.ItemBadge(L"files").count == 3, "navigation set item badge");
        TestCommandBar bar;
        bar.Items({{L"run", L"Run", icon::kPlay}});
        bar.ItemBadge(L"run", InfoBadgeData::Dot());
        Check(bar.Items()[0].badge.kind == InfoBadgeData::Kind::Dot, "command bar item badge");
        IconView icon(icon::kBell);
        icon.Badge(InfoBadgeData::Count(8));
        Check(icon.Badge().count == 8, "icon view overlay badge");
    }
    {
        TestFileDropZone zone;
        const Size size = zone.Measure({400.0f, 200.0f}, theme);
        Check(size.w == 400.0f && size.h >= 72.0f, "file drop zone fills width");
        zone.Accept(L".png;.jpg");
        auto kept = zone.Filter({L"C:\\a.jpg", L"C:\\b.txt", L"C:\\c.PNG"});
        Check(kept.size() == 2 && kept[1] == L"C:\\c.PNG", "file drop filters extensions");
        zone.Multiple(false);
        kept = zone.Filter({L"C:\\a.png", L"C:\\b.png"});
        Check(kept.size() == 1 && kept[0] == L"C:\\a.png", "file drop single keeps first");
        int dropped = 0;
        zone.OnDrop([&](const std::vector<std::wstring>& paths) {
            dropped = static_cast<int>(paths.size());
        });
        zone.OnFileDrop({L"C:\\shot.png", L"C:\\notes.txt"});
        Check(dropped == 1 && zone.LastPaths().size() == 1 && !zone.Armed(),
              "file drop delivers filtered paths");
        zone.OnFileDrag(true);
        Check(zone.Armed(), "file drop arms on drag");
        zone.OnFileDrag(false);
        Check(!zone.Armed(), "file drop disarms on leave");
    }
    {
        TestRoot root;
        auto& field = root.Add<TestFormField>(L"Name");
        field.Required(true);
        auto& box = field.Add<TextBox>();
        box.Placeholder(L"Project").Text(L"Example");
        root.Measure({400.0f, 2000.0f}, theme);
        root.Arrange({0.0f, 0.0f, 400.0f, 200.0f});
        Check(field.Required() && field.Label() == L"Name", "form field required label");
        Check(box.AbsoluteBounds().y > field.AbsoluteBounds().y + 12.0f,
              "form field child sits below label");
        const Size idle = field.Measure({400.0f, 2000.0f}, theme);
        field.Error(L"Name is required");
        Check(field.HasError(), "form field reports error");
        const Size broken = field.Measure({400.0f, 2000.0f}, theme);
        Check(broken.h > idle.h, "form field error grows height");
        field.Error({});
        Check(!field.HasError() && field.Measure({400.0f, 2000.0f}, theme).h == idle.h,
              "form field clears error");
        field.Description(L"Shown on the build card.");
        Check(field.Measure({400.0f, 2000.0f}, theme).h > idle.h,
              "form field description grows height");
    }
    {
        TestRoot root;
        Button* ok = nullptr;
        root.Children(
            Column().Comfortable().Children(
                FormField(L"Name").Child(TextBox().Placeholder(L"Project")),
                Row().Children(Button(L"Cancel"), Button(L"OK", ButtonKind::Primary).Ref(ok))));
        Check(ok != nullptr && ok->Text() == L"OK", "children ref binds heap button");
        Check(root.ChildCount() == 1, "children added one column");
        root.Measure({400.0f, 2000.0f}, theme);
        root.Arrange({0.0f, 0.0f, 400.0f, 200.0f});
        Check(ok->DesiredSize().h == 44.0f, "children button medium height");
        Check(ok->AbsoluteBounds().y > 0.0f, "children nested row arranged");
    }
    {
        TestRoot root;
        Command save{L"Save", [] {}};
        bool dirty = false;
        save.CanExecute([&] { return dirty; });
        auto& btn = root.Add<Button>();
        btn.Bind(save);
        Check(!btn.Enabled() && btn.Text() == L"Save", "command canexecute disables button");
        dirty = true;
        save.RaiseCanExecuteChanged();
        Check(btn.Enabled(), "command raise enables button");
        int ran = 0;
        Command counted{L"Run", [&] { ++ran; }};
        counted.CanExecute([] { return false; });
        counted.Execute();
        Check(ran == 0, "command execute no-ops when disabled");
        Menu menu;
        menu.Add(save);
        Check(menu.Items().size() == 1 && menu.Items()[0].command == &save,
              "menu add command stores pointer");
    }
    {
        TestRoot root;
        auto& compact = root.Add<Column>();
        compact.Density(Density::Compact);
        auto& short_btn = compact.Add<Button>(L"Go");
        auto& normal = root.Add<Button>(L"Go");
        root.Measure({400.0f, 2000.0f}, theme);
        Check(short_btn.DesiredSize().h < normal.DesiredSize().h - 4.0f,
              "compact density shortens button");
        Check(normal.DesiredSize().h == 44.0f, "default density keeps medium height");
        const Theme compact_theme = short_btn.EffectiveTheme(theme);
        Check(compact_theme.button_height < theme.button_height, "effective theme scales button_height");
        auto& nested = compact.Add<Column>().Add<Column>().Add<Button>(L"Nested");
        root.Measure({400.0f, 2000.0f}, theme);
        Check(std::fabs(nested.DesiredSize().h - compact_theme.button_height) < 0.01f,
              "nested containers apply ancestor density only once");
        Check(std::fabs(short_btn.DesiredSize().h - compact_theme.button_height) < 0.01f,
              "density measurement matches drawing theme");
    }
    {
        TestRoot root;
        auto& bar = root.Add<InfoBar>(L"Update available");
        bar.Message(L"Install the artifact.").Glyph(icon::kPackage);
        int clicks = 0;
        bar.Action(L"Install", [&] { ++clicks; });
        root.Measure({480.0f, 200.0f}, theme);
        root.Arrange({0.0f, 0.0f, 480.0f, 200.0f});
        Check(bar.ChildCount() == 1, "info bar hosts action");
        Check(bar.Glyph() == icon::kPackage, "info bar custom glyph");
        const Rect bar_r = bar.AbsoluteBounds();
        const Rect btn = bar.Child(0).AbsoluteBounds();
        Check(btn.x > bar_r.x + 40.0f, "info bar action right of glyph");
        Check(btn.Right() <= bar_r.Right() - 14.0f - 28.0f + 0.5f,
              "info bar action left of close");
        Check(btn.y >= bar_r.y && btn.Bottom() <= bar_r.Bottom() + 0.5f,
              "info bar action inside bar");
        bar.Action({}, nullptr);
        Check(!bar.Child(0).Visible(), "info bar empty action hides button");
        bar.Action(L"Retry", [&] { ++clicks; });
        Check(bar.Child(0).Visible(), "info bar action restore shows button");
        (void)clicks;
    }
    {
        TestStatusBar bar;
        bar.Path(L"C:\\src\\lumen").CountText(L"3 files").Zoom(L"100%");
        Check(bar.ItemCount() == 3 && bar.Path() == L"C:\\src\\lumen" && bar.Zoom() == L"100%",
              "status bar named slots");
        const Size size = bar.Measure({400.0f, 80.0f}, theme);
        Check(size.w == 400.0f && size.h == StatusBar::kHeight, "status bar fills width");
        bar.ItemText(L"zoom", L"150%");
        Check(bar.Zoom() == L"150%", "status bar set text by id");
        Check(bar.RemoveItem(L"count") && bar.ItemCount() == 2 && bar.CountText().empty(),
              "status bar removes tally");
        int invoked = 0;
        std::wstring last;
        bar.OnInvoked([&](std::wstring_view id) {
            ++invoked;
            last = id;
        });
        bar.Arrange({0.0f, 0.0f, 400.0f, StatusBar::kHeight});
        bar.OnMouseDown({20.0f, 14.0f}, 0x0001);
        Check(invoked == 1 && last == L"path", "status bar click path");
        bar.OnMouseDown({380.0f, 14.0f}, 0x0001);
        Check(invoked == 2 && last == L"zoom", "status bar click zoom");
    }
    {
        TestHotkeyBox box(L"ctrl+k");
        Check(box.Chord() == L"Ctrl+K" && box.Vk() == 'K' && box.HasCtrl() && !box.HasShift(),
              "hotkey parses ctrl+k");
        box.Chord(L"Ctrl+Shift+F12");
        Check(box.Chord() == L"Ctrl+Shift+F12" && box.Vk() == VK_F12 && box.HasShift(),
              "hotkey parses function key");
        int changed = 0;
        box.OnChanged([&] { ++changed; });
        box.OnKey('K');
        Check(box.Chord() == L"Ctrl+Shift+F12" && changed == 0, "hotkey ignores bare letter");
        box.OnKey(VK_F5);
        Check(box.Chord() == L"F5" && box.Vk() == VK_F5 && changed == 1, "hotkey captures F5");
        box.OnKey(VK_BACK);
        Check(box.Empty() && changed == 2, "hotkey backspace clears");
        box.Chord(L"Alt+Enter");
        Check(box.Chord() == L"Alt+Enter" && box.HasAlt(), "hotkey parses alt+enter");
        const Size size = box.Measure({220.0f, 80.0f}, theme);
        Check(size.w == 220.0f && size.h == theme.input_height, "hotkey measures like input");
        box.OnChar(L'x');
        Check(box.Chord() == L"Alt+Enter", "hotkey swallows char");
        box.Clear();
        Check(box.Empty() && changed == 3, "hotkey clear notifies");
    }
    {
        TestRoot root;
        auto& group = root.Add<TestGroupBox>(L"Network");
        auto& inside = group.Add<Label>(L"inside");
        root.Measure({400.0f, 2000.0f}, theme);
        root.Arrange({0.0f, 0.0f, 400.0f, 200.0f});
        Check(group.Title() == L"Network", "group box title");
        Check(inside.AbsoluteBounds().y > group.AbsoluteBounds().y + 10.0f,
              "group box child sits below title");
        Check(group.DesiredSize().h > inside.DesiredSize().h + 16.0f,
              "group box taller than child");
        const float with_title = group.DesiredSize().h;
        group.Title({});
        root.Measure({400.0f, 2000.0f}, theme);
        Check(group.DesiredSize().h < with_title, "group box untitled is shorter");
    }
    {
        TestViewbox box;
        auto& inner = box.Add<Panel>();
        inner.SetBounds({0.0f, 0.0f, 80.0f, 40.0f});
        const Size uniform = box.Measure({160.0f, 80.0f}, theme);
        Check(uniform.w == 160.0f && uniform.h == 80.0f, "viewbox uniform scales to fit");
        box.Arrange({0.0f, 0.0f, 160.0f, 80.0f});
        Check(inner.AbsoluteBounds().w == 80.0f && inner.AbsoluteBounds().h == 40.0f,
              "viewbox child keeps natural size");
        const Point mapped = box.MapToChildren({40.0f, 20.0f});
        Check(mapped.x == 20.0f && mapped.y == 10.0f, "viewbox maps pointer into layout space");
        inner.SetBounds({0.0f, 0.0f, 80.0f, 20.0f});
        box.Stretch(ViewboxStretch::Fill);
        const Size fill = box.Measure({160.0f, 80.0f}, theme);
        Check(fill.w == 160.0f && fill.h == 80.0f, "viewbox fill eats available");
        box.Arrange({0.0f, 0.0f, 160.0f, 80.0f});
        const Point fill_mapped = box.MapToChildren({80.0f, 40.0f});
        Check(fill_mapped.x == 40.0f && fill_mapped.y == 10.0f, "viewbox fill maps non-uniform");
        inner.SetBounds({0.0f, 0.0f, 80.0f, 40.0f});
        box.Stretch(ViewboxStretch::None);
        const Size none = box.Measure({160.0f, 80.0f}, theme);
        Check(none.w == 80.0f && none.h == 40.0f, "viewbox none keeps natural");
        box.Arrange({0.0f, 0.0f, 160.0f, 80.0f});
        Check(inner.AbsoluteBounds().x == 40.0f && inner.AbsoluteBounds().y == 20.0f,
              "viewbox none centers natural child");
    }
    {
        OffscreenRenderer renderer;
        if (!renderer.Init(200, 140)) {
            Check(false, "viewbox fill clip renderer");
        } else {
            TestViewbox box;
            box.Stretch(ViewboxStretch::Fill);
            auto& inner = box.Add<Panel>();
            inner.Background(Color{1.0f, 1.0f, 1.0f, 1.0f});
            inner.SetBounds({0.0f, 0.0f, 40.0f, 80.0f});
            box.Measure({120.0f, 80.0f}, theme);
            box.Arrange({16.0f, 16.0f, 120.0f, 80.0f});
            ID2D1DeviceContext2* dc = renderer.BeginDraw();
            Painter painter;
            painter.BeginFrame(dc, &UiText(), 1.0f);
            painter.FillRect({0.0f, 0.0f, 200.0f, 140.0f}, theme.bg);
            DrawControlTree(painter, theme, &box, box.AbsoluteBounds());
            painter.EndFrame();
            Check(renderer.EndDraw(), "viewbox fill clip enddraw");
            Color inside{};
            Color outside{};
            Check(renderer.ReadPixel(76, 56, inside) && inside.r > 0.85f,
                  "viewbox fill paints stretched child");
            Check(renderer.ReadPixel(140, 56, outside) && CloseTo(outside, theme.bg),
                  "viewbox fill clips non-uniform overflow");
            renderer.Shutdown();
        }
    }
    {
        OffscreenRenderer renderer;
        if (!renderer.Init(200, 120)) {
            Check(false, "viewbox uniformtofill clip renderer");
        } else {
            TestViewbox box;
            box.Stretch(ViewboxStretch::UniformToFill);
            auto& inner = box.Add<Panel>();
            inner.Background(Color{1.0f, 1.0f, 1.0f, 1.0f});
            inner.SetBounds({0.0f, 0.0f, 40.0f, 80.0f});
            box.Measure({120.0f, 40.0f}, theme);
            box.Arrange({16.0f, 16.0f, 120.0f, 40.0f});
            ID2D1DeviceContext2* dc = renderer.BeginDraw();
            Painter painter;
            painter.BeginFrame(dc, &UiText(), 1.0f);
            painter.FillRect({0.0f, 0.0f, 200.0f, 120.0f}, theme.bg);
            DrawControlTree(painter, theme, &box, box.AbsoluteBounds());
            painter.EndFrame();
            Check(renderer.EndDraw(), "viewbox uniformtofill clip enddraw");
            Color inside{};
            Color outside{};
            Check(renderer.ReadPixel(76, 36, inside) && inside.r > 0.85f,
                  "viewbox uniformtofill paints cropped child");
            Check(renderer.ReadPixel(76, 64, outside) && CloseTo(outside, theme.bg),
                  "viewbox uniformtofill clips overflow");
            renderer.Shutdown();
        }
    }
    {
        // ShaderView：GPU 程序化光在控件局部坐标求值（窗口偏移 + 1.5x DPI 下仍以自身矩形为基准），
        // 单色、不越出边界；无窗口/未播放时不申请持续动画。
        OffscreenRenderer renderer;
        if (!renderer.Init(300, 210)) {
            Check(false, "shader view renderer");
        } else {
            struct TestShaderView : ShaderView {
                using ShaderView::Arrange;
                using ShaderView::Measure;
                using ShaderView::OnAnimate;
            };
            auto render = [&](TestShaderView& view) {
                ID2D1DeviceContext2* dc = renderer.BeginDraw();
                Painter painter;
                painter.BeginFrame(dc, &UiText(), 1.5f);
                painter.FillRect({0.0f, 0.0f, 200.0f, 140.0f}, theme.bg);
                DrawControlTree(painter, theme, &view);
                painter.EndFrame();
                return renderer.EndDraw();
            };
            TestShaderView view;
            view.Kind(ShaderKind::Glow).Grain(0.0f).Intensity(1.0f).Time(1.0f).Height(100.0f);
            view.Center({0.2f, 0.5f});
            Check(view.Measure({160.0f, 400.0f}, theme).h == 100.0f, "shader view measures height");
            view.Arrange({20.0f, 20.0f, 160.0f, 100.0f});   // 设备像素 (30,30)-(270,180)
            Check(render(view), "shader view enddraw");
            Check(view.GpuActive(), "shader view custom effect available");
            Color left{}, right{}, outside{};
            renderer.ReadPixel(30 + 48, 105, left);    // 光心：x = 0.2 * 240
            renderer.ReadPixel(30 + 216, 105, right);
            renderer.ReadPixel(20, 105, outside);
            Check(left.r > theme.bg.r + 0.35f, "shader glow lights its local center");
            Check(left.r > right.r + 0.2f, "shader evaluates in control-local coordinates");
            Check(std::fabs(left.r - left.g) < 0.02f && std::fabs(left.r - left.b) < 0.02f,
                  "shader output stays monochrome");
            Check(CloseTo(outside, theme.bg), "shader does not paint outside its bounds");

            view.Kind(ShaderKind::DotGrid).Center({0.5f, 0.5f}).CornerRadius(16.0f);
            Check(render(view), "shader dotgrid enddraw");
            float lo = 1.0f, hi = 0.0f;
            for (int x = 40; x < 100; ++x) {
                Color c{};
                renderer.ReadPixel(x, 40, c);   // 点行中心：局部 10.5px = 半个 21px 间距
                lo = std::min(lo, c.r);
                hi = std::max(hi, c.r);
            }
            Check(hi - lo > 0.05f, "shader dotgrid draws a lattice");
            Color corner{};
            renderer.ReadPixel(31, 31, corner);
            Check(CloseTo(corner, theme.bg), "shader view honors corner radius clip");

            // 无窗口 / 暂停：OnAnimate 不续订，不形成空闲动画循环。
            view.Play(ShaderPlay::Always);
            Check(!view.OnAnimate(0.016f), "shader view without window does not tick");
            view.Play(ShaderPlay::Paused);
            Check(!view.OnAnimate(0.016f), "paused shader view does not tick");

            // 可感知运动：每种效果 0.5 s 内的逐像素平均亮度变化（0..255），
            // 相对画面平均亮度归一。只看自主动画，不含鼠标跟随。
            view.Play(ShaderPlay::Paused).CornerRadius(0.0f).Grain(0.0f).Center({0.5f, 0.5f});
            const ShaderKind kinds[] = {ShaderKind::Mist, ShaderKind::Glow, ShaderKind::DotGrid,
                                        ShaderKind::Rays, ShaderKind::Flow, ShaderKind::Liquid};
            const char* kind_names[] = {"mist", "glow", "dotgrid", "rays", "flow", "liquid"};
            for (int k = 0; k < 6; ++k) {
                view.Center(k == 3 ? Point{0.5f, -0.15f} : Point{0.5f, 0.5f});
                std::vector<uint8_t> a, b;
                view.Kind(kinds[k]).Time(10.0f);
                const bool ok_a = render(view) && renderer.ReadBack(a);
                view.Time(10.5f);
                const bool ok_b = render(view) && renderer.ReadBack(b);
                double diff = 0.0, mean = 0.0;
                size_t n = 0;
                for (int y = 30; y < 180; ++y) {
                    for (int x = 30; x < 270; ++x) {
                        const size_t i = (static_cast<size_t>(y) * 300 + static_cast<size_t>(x)) * 4;
                        diff += std::fabs(static_cast<double>(a[i + 1]) - static_cast<double>(b[i + 1]));
                        mean += static_cast<double>(a[i + 1]) - theme.bg.g * 255.0;
                        ++n;
                    }
                }
                diff /= static_cast<double>(n);
                mean /= static_cast<double>(n);
                std::printf("[INFO] shader %-7s motion 0.5s: mean|d|=%.2f mean=%.2f ratio=%.3f\n",
                            kind_names[k], diff, mean, diff / std::max(1.0, mean));
                Check(ok_a && ok_b, "shader motion frames render");
                // 回归底线（改动前实测 mist 3.5 / dotgrid 0.4 / rays 3.0，用户反馈“要仔细看”）。
                // Glow 的自主运动刻意保持含蓄，其动感来自跟随指针，不设底线。
                const double floor_d[] = {8.0, 0.0, 3.0, 5.0, 4.0, 4.0};
                if (floor_d[k] > 0.0) {
                    char name[64];
                    std::snprintf(name, sizeof(name), "shader %s motion is perceptible", kind_names[k]);
                    Check(diff >= floor_d[k], name);
                }
            }

            // 调色板：MeshGradient / LiquidMetal 默认单色（中性灰），Palette() 显式切彩色。
            auto chroma = [](const std::vector<uint8_t>& px) {
                double sum = 0.0;
                size_t n = 0;
                for (int y = 30; y < 180; y += 3) {
                    for (int x = 30; x < 270; x += 3) {
                        const size_t i = (static_cast<size_t>(y) * 300 + static_cast<size_t>(x)) * 4;
                        const int hi = std::max({px[i], px[i + 1], px[i + 2]});
                        const int lo = std::min({px[i], px[i + 1], px[i + 2]});
                        sum += hi - lo;
                        ++n;
                    }
                }
                return n ? sum / static_cast<double>(n) : 0.0;
            };
            const ShaderKind color_kinds[] = {ShaderKind::MeshGradient, ShaderKind::LiquidMetal};
            const char* color_names[] = {"mesh", "liquidmetal"};
            for (int k = 0; k < 2; ++k) {
                std::vector<uint8_t> mono, colored;
                view.Kind(color_kinds[k]).Time(3.0f).Palette(ShaderPalette{});
                const bool ok_mono = render(view) && renderer.ReadBack(mono);
                view.Palette(ShaderPalette::Aurora());
                const bool ok_color = render(view) && renderer.ReadBack(colored);
                view.Palette(ShaderPalette{});
                char name[80];
                std::snprintf(name, sizeof(name), "shader %s palette frames render", color_names[k]);
                Check(ok_mono && ok_color, name);
                if (!ok_mono || !ok_color) continue;
                const double c_mono = chroma(mono), c_color = chroma(colored);
                std::printf("[INFO] shader %s chroma mono=%.2f aurora=%.2f\n", color_names[k], c_mono, c_color);
                std::snprintf(name, sizeof(name), "shader %s defaults to monochrome", color_names[k]);
                Check(c_mono < 4.0, name);
                std::snprintf(name, sizeof(name), "shader %s palette is colored", color_names[k]);
                Check(c_color > 12.0, name);
            }
            // 单色 Mist 套调色板后同样走色带。
            {
                std::vector<uint8_t> px;
                view.Kind(ShaderKind::Mist).Time(3.0f).Palette(ShaderPalette::Ember());
                const bool ok = render(view) && renderer.ReadBack(px);
                view.Palette(ShaderPalette{});
                Check(ok && chroma(px) > 4.0, "shader palette tints monochrome kinds");
            }
            renderer.Shutdown();
        }
    }
    {
        // RichLabel 多色：Colored 段绘成指定色，普通段保持中性。
        OffscreenRenderer renderer;
        if (!renderer.Init(300, 60)) {
            Check(false, "richlabel color renderer");
        } else {
            TestRich rich;
            rich.Add(L"MMMMMMMM").Colored(L"MMMMMMMM", Color{1.0f, 0.2f, 0.1f, 1.0f});
            const Size sz = rich.Measure({280.0f, 60.0f}, theme);
            rich.Arrange({10.0f, 10.0f, sz.w, sz.h});
            std::vector<Rect> boxes;
            rich.layout_.Selection(8, 8, boxes);
            const float split = boxes.empty() ? 150.0f : 10.0f + boxes[0].x;
            ID2D1DeviceContext2* dc = renderer.BeginDraw();
            Painter painter;
            painter.BeginFrame(dc, &UiText(), 1.0f);
            painter.FillRect({0.0f, 0.0f, 300.0f, 60.0f}, theme.bg);
            painter.SetBackdrop(theme.bg);
            DrawControlTree(painter, theme, &rich);
            painter.EndFrame();
            Check(renderer.EndDraw(), "richlabel color enddraw");
            std::vector<uint8_t> px;
            Check(renderer.ReadBack(px), "richlabel color readback");
            // 墨迹像素上的平均红色偏移（ClearType 彩边正负相抵，整段着色才显著偏正）。
            double plain_sum = 0.0, colored_sum = 0.0;
            int plain_n = 0, colored_n = 0;
            for (int y = 10; y < 10 + static_cast<int>(sz.h); ++y) {
                for (int x = 10; x < 10 + static_cast<int>(sz.w) && x < 300; ++x) {
                    const size_t i = (static_cast<size_t>(y) * 300 + static_cast<size_t>(x)) * 4;
                    if (i + 2 >= px.size()) continue;
                    const int b = px[i], g = px[i + 1], r = px[i + 2];   // BGRA
                    if (std::max({r, g, b}) < 80) continue;
                    const double excess = r - 0.5 * (g + b);
                    if (static_cast<float>(x) < split - 2.0f) {
                        plain_sum += excess;
                        ++plain_n;
                    } else if (static_cast<float>(x) > split + 2.0f) {
                        colored_sum += excess;
                        ++colored_n;
                    }
                }
            }
            const double plain_red = plain_n ? plain_sum / plain_n : 0.0;
            const double colored_red = colored_n ? colored_sum / colored_n : 0.0;
            std::printf("[INFO] richlabel color plain_red=%.1f (%d px) colored_red=%.1f (%d px)\n",
                        plain_red, plain_n, colored_red, colored_n);
            Check(plain_n > 20 && std::fabs(plain_red) < 8.0, "richlabel plain run stays neutral");
            Check(colored_n > 20 && colored_red > 60.0, "richlabel colored run paints its color");
            renderer.Shutdown();
        }
    }
    {
        // Dialog：额外子级排在换行正文之下、页脚按钮之上，不得与说明文字重叠。
        TestDialog dialog;
        const std::wstring message =
            L"A new luminescent primitive on the void. Spotlight, glow tokens "
            L"and monochrome contrast stay in lockstep.\n\nName this component:";
        dialog.Title(L"Add Component")
            .Message(message)
            .SecondaryButton(L"Cancel", [] {})
            .PrimaryButton(L"Add", [] {});
        auto& box = dialog.Add<TextBox>();
        box.Placeholder(L"Component name");
        const Size size = dialog.Measure({600.0f, 800.0f}, theme);
        dialog.Arrange({0.0f, 0.0f, size.w, size.h});
        const float inner = size.w - 48.0f;
        const float msg_h = MeasureWrappedHeight(message, TextRole::Body, inner);
        const float msg_bottom = 20.0f + 28.0f + 8.0f + msg_h;
        Check(box.AbsoluteBounds().y + 0.5f >= msg_bottom,
              "dialog extra child below message");
        Check(box.AbsoluteBounds().Bottom() + 0.5f <= size.h - 24.0f - 40.0f,
              "dialog extra child above footer");
        TestDialog compact;
        compact.Title(L"Delete").Message(L"Sure?").CardSize(DialogSize::Compact)
            .SecondaryButton(L"Cancel")
            .PrimaryButton(L"Delete");
        const Size compact_size = compact.Measure({800.0f, 800.0f}, theme);
        Check(std::fabs(compact_size.w - 320.0f) < 0.5f, "dialog compact width");
        TestDialog wide;
        wide.Title(L"Export").CardSize(DialogSize::Wide).PrimaryButton(L"Save");
        const Size wide_size = wide.Measure({800.0f, 800.0f}, theme);
        Check(std::fabs(wide_size.w - 560.0f) < 0.5f, "dialog wide width");
        int got = -1;
        TestDialog keyed;
        keyed.Title(L"x")
            .PrimaryButton(L"OK")
            .SecondaryButton(L"No")
            .DefaultButton(DialogCommand::Primary)
            .CancelButton(DialogCommand::Secondary)
            .OnResult([&](DialogResult r) { got = static_cast<int>(r); });
        Check(keyed.OnKey(VK_RETURN), "dialog enter default");
        Check(got == static_cast<int>(DialogResult::Primary), "dialog result primary");
        DialogResult esc_got = DialogResult::None;
        TestDialog esc;
        esc.Title(L"x")
            .CloseButton(L"Not now")
            .PrimaryButton(L"Save")
            .CancelButton(DialogCommand::Close)
            .OnResult([&](DialogResult r) { esc_got = r; });
        Check(esc.OnKey(VK_ESCAPE), "dialog esc cancel");
        Check(esc_got == DialogResult::Close, "dialog esc close command");
    }
    {
        Window window(L"overlay-lifetime", {240.0f, 160.0f});
        {
            Dialog dialog;
            dialog.Title(L"x").Message(L"y").DefaultClose();
            window.ShowDialog(dialog);
            Check(window.DialogActive(), "dialog shown");
        }
        Check(!window.DialogActive(), "dialog dtor unregisters");
        {
            Flyout flyout;
            flyout.Add<Label>(L"hi");
            window.ShowFlyout(flyout, nullptr);
            Check(window.FlyoutActive(), "flyout shown");
        }
        Check(!window.FlyoutActive(), "flyout dtor unregisters");
        {
            TeachingTip tip;
            tip.Title(L"Tip").Message(L"Body");
            window.ShowTeachingTip(tip, nullptr);
            Check(window.FlyoutActive(), "teaching tip shown");
        }
        Check(!window.FlyoutActive(), "teaching tip dtor unregisters");
    }
    {
        Check(clipboard::Text(L"lumen-clip") && clipboard::Text() == L"lumen-clip",
              "clipboard roundtrip");
        Button named(L"Save");
        named.AccessibleName(L"save document");
        Check(named.AccessibleName() == L"save document", "accessible name stored");
    }
    {
        TestLog log;
        log.Follow(true);
        const Size sz = log.Measure({240.0f, 80.0f}, theme);
        log.Arrange({0.0f, 0.0f, sz.w, sz.h});
        log.ItemCount(200);
        Check(log.Following(), "logview follows tail");
        log.OnWheel(4.0f);
        Check(!log.Following(), "logview pauses follow on scroll up");
    }
    {
        // RichLabel 基于 TextLayout：左对齐收拢到内容宽，居中占满；无约束 = 单行自然宽。
        TestRich rich;
        rich.Add(L"used ").Strong(L"85%").Secondary(L" of space");
        const Size sz = rich.Measure({200.0f, 400.0f}, theme);
        Check(sz.h >= 16.0f && sz.w > 40.0f && sz.w <= 200.0f, "richlabel measures within wrap width");
        const Size natural = rich.Measure({kUnbounded, kUnbounded}, theme);
        Check(std::fabs(natural.w - sz.w) < 1.0f, "richlabel short text keeps natural width");
        TestRich centered;
        centered.Add(L"used 85%").Alignment(Align::Center);
        Check(centered.Measure({200.0f, 400.0f}, theme).w == 200.0f, "richlabel centered fills width");
        TestRich empty;
        Check(empty.Measure({200.0f, 400.0f}, theme).w == 0.0f, "empty richlabel has no width");
    }
    {
        // 无空格中日韩文本必须在宽度内换行（旧实现按空格分词，整段溢出）。
        std::wstring cjk;
        for (int i = 0; i < 40; ++i) cjk += static_cast<wchar_t>(0x4E00 + i * 7);
        TestRich rich;
        rich.Add(cjk.substr(0, 20)).Strong(cjk.substr(20));
        const Size one = rich.Measure({kUnbounded, kUnbounded}, theme);
        const Size sz = rich.Measure({120.0f, 800.0f}, theme);
        Check(sz.w <= 120.0f && sz.h > one.h * 3.0f, "richlabel wraps CJK without spaces");
        rich.Arrange({0.0f, 0.0f, sz.w, sz.h});
        Check(rich.layout_.ContentSize().w <= 120.5f, "richlabel CJK lines stay inside width");
    }
    {
        // 行内标记 + 链接回调 + 双击选词。
        TestRich md;
        md.Markup(L"a **bold** *it* `code` [link](target) \\*x");
        Check(md.Text() == L"a bold it code link *x", "richlabel markup strips syntax");
        std::wstring target;
        md.OnLink([&target](std::wstring_view t) { target = std::wstring(t); });
        const Size sz = md.Measure({600.0f, 200.0f}, theme);
        md.Arrange({0.0f, 0.0f, sz.w, sz.h});
        std::vector<Rect> boxes;
        md.layout_.Selection(15, 4, boxes);   // "link"
        Check(!boxes.empty(), "richlabel link has hit boxes");
        if (!boxes.empty()) {
            md.OnMouseUp({boxes[0].x + boxes[0].w * 0.5f, boxes[0].y + boxes[0].h * 0.5f}, kBtnL);
        }
        Check(target == L"target", "richlabel markup link reports target");
        Check(!md.Focusable(), "richlabel not focusable unless selectable");
        md.Selectable(true);
        Check(md.Focusable(), "selectable richlabel is focusable");
        md.layout_.Selection(2, 4, boxes);    // "bold"
        if (!boxes.empty()) md.OnMouseDoubleClick({boxes[0].x + 2.0f, boxes[0].y + boxes[0].h * 0.5f});
        Check(md.SelectedText() == L"bold", "richlabel double click selects word");
        Check(md.AutomationName() == L"a bold it code link *x", "richlabel automation name is plain text");
    }
    {
        // 多段：\n 硬换行成多行，一次拖选可从末行拖到控件上方，跨行选中全部。
        TestRich multi;
        multi.Add(L"first line").Add(L"\n").Strong(L"second").Add(L"\n").Markup(L"third `code`");
        multi.Selectable(true);
        const Size sz = multi.Measure({400.0f, 400.0f}, theme);
        multi.Arrange({0.0f, 0.0f, sz.w, sz.h});
        Check(multi.layout_.Lines().size() == 3, "richlabel newline makes hard lines");
        std::vector<Rect> boxes;
        multi.layout_.Selection(multi.Text().size() - 1, 1, boxes);
        if (!boxes.empty()) {
            const Rect last = boxes.back();
            multi.OnMouseDown({last.Right() + 20.0f, last.y + last.h * 0.5f}, kBtnL);
            multi.OnMouseMove({sz.w * 0.5f, sz.h * 0.5f}, kBtnL);
            Check(multi.HasSelection() && multi.SelectedText().find(L"\n") != std::wstring::npos,
                  "richlabel drag selects across lines");
            multi.OnMouseMove({-10.0f, -30.0f}, kBtnL);   // 拖出控件上方：选到开头
            multi.OnMouseUp({-10.0f, -30.0f}, kBtnL);
        }
        Check(multi.SelectedText() == L"first line\nsecond\nthird code", "richlabel drag above selects to start");
    }
    {
        Window window(L"batch2-overlay", {240.0f, 160.0f});
        Check(window.IsUiThread(), "window ui thread");
        int posted = 0;
        window.Post([&posted] { posted = 1; });
        MSG msg{};
        while (PeekMessageW(&msg, static_cast<HWND>(window.NativeHandle()), 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Check(posted == 1, "window post drains on ui thread");
        window.ShowBusy(L"wait");
        Check(window.BusyActive(), "busy shown");
        window.CloseBusy();
        Check(!window.BusyActive(), "busy closed");
        {
            Drawer drawer;
            window.ShowDrawer(drawer, Edge::Right);
            Check(window.DrawerActive(), "drawer shown");
        }
        Check(!window.DrawerActive(), "drawer dtor unregisters");
        {
            struct AnimatedDrawer : Drawer { using Drawer::OnAnimate; } drawer;
            auto& combo = drawer.Add<TestComboBox>();
            combo.Items({L"A", L"B"});
            window.ShowDrawer(drawer, Edge::Right);
            Check(combo.AutomationExpand() && window.FlyoutActive(),
                  "drawer combo dropdown opens");
            window.CloseDrawer();
            for (int i = 0; i < 4 && window.DrawerActive(); ++i) drawer.OnAnimate(1.0f);
            Check(!window.DrawerActive() && !window.FlyoutActive(),
                  "closing drawer also closes anchored dropdown");
            Check(combo.AutomationExpandState() == 0,
                  "closing drawer resets combo expansion state");
            window.ShowDrawer(drawer, Edge::Right);
            Check(combo.AutomationExpand() && window.FlyoutActive(),
                  "drawer combo can reopen after drawer close");
            window.CloseFlyout();
        }
    }
    {
        OffscreenRenderer renderer;
        if (!renderer.Init(220, 80)) {
            Check(false, "sparkline renderer");
        } else {
            TestSparkline spark;
            spark.Count(8).Values([](size_t i) { return static_cast<float>(i); });
            spark.Measure({200.0f, 36.0f}, theme);
            spark.Arrange({8.0f, 16.0f, 200.0f, 36.0f});
            ID2D1DeviceContext2* dc = renderer.BeginDraw();
            Painter painter;
            painter.BeginFrame(dc, &UiText(), 1.0f);
            painter.FillRect({0.0f, 0.0f, 220.0f, 80.0f}, theme.bg);
            DrawControlTree(painter, theme, &spark);
            painter.EndFrame();
            Check(renderer.EndDraw(), "sparkline enddraw");
            Color ink{};
            int hits = 0;
            for (int x = 16; x < 200; ++x) {
                for (int y = 18; y < 50; ++y) {
                    renderer.ReadPixel(x, y, ink);
                    if (ink.r > 0.12f) ++hits;
                }
            }
            Check(hits > 8, "sparkline paints ink");
            renderer.Shutdown();
        }
    }
    {
        OffscreenRenderer renderer;
        if (!renderer.Init(140, 120)) {
            Check(false, "gauge renderer");
        } else {
            TestGauge gauge;
            gauge.Range(0.0f, 100.0f).Value(80.0f).Threshold(60.0f);
            gauge.Measure({120.0f, 110.0f}, theme);
            gauge.Arrange({10.0f, 4.0f, 120.0f, 110.0f});
            ID2D1DeviceContext2* dc = renderer.BeginDraw();
            Painter painter;
            painter.BeginFrame(dc, &UiText(), 1.0f);
            painter.FillRect({0.0f, 0.0f, 140.0f, 120.0f}, theme.bg);
            DrawControlTree(painter, theme, &gauge);
            painter.EndFrame();
            Check(renderer.EndDraw(), "gauge enddraw");
            Color ink{};
            int hits = 0;
            for (int y = 20; y < 100; ++y) {
                for (int x = 20; x < 120; ++x) {
                    renderer.ReadPixel(x, y, ink);
                    if (ink.r > 0.12f) ++hits;
                }
            }
            Check(hits > 20, "gauge paints arc ink");
            renderer.Shutdown();
        }
    }
    {
        OffscreenRenderer renderer;
        if (!renderer.Init(240, 180)) {
            Check(false, "chart area renderer");
        } else {
            TestChart chart;
            chart.Kind(ChartKind::Area)
                .Values({4.f, 8.f, 6.f, 12.f, 10.f, 16.f, 14.f, 18.f})
                .PreferredSize({220.0f, 140.0f});
            chart.Measure({220.0f, 140.0f}, theme);
            chart.Arrange({8.0f, 16.0f, 220.0f, 140.0f});
            ID2D1DeviceContext2* dc = renderer.BeginDraw();
            Painter painter;
            painter.BeginFrame(dc, &UiText(), 1.0f);
            painter.FillRect({0.0f, 0.0f, 240.0f, 180.0f}, theme.bg);
            DrawControlTree(painter, theme, &chart);
            painter.EndFrame();
            Check(renderer.EndDraw(), "chart area enddraw");
            Color ink{};
            int hits = 0;
            for (int x = 20; x < 210; ++x) {
                for (int y = 40; y < 150; ++y) {
                    renderer.ReadPixel(x, y, ink);
                    // 类别色系列（默认 cyan）按亮度判断，不只看红通道。
                    const float luma = 0.2126f * ink.r + 0.7152f * ink.g + 0.0722f * ink.b;
                    if (luma > theme.bg.r + 0.04f) ++hits;
                }
            }
            Check(hits > 40, "chart area fill brighter than backdrop");
            renderer.Shutdown();
        }
    }
    {
        OffscreenRenderer renderer;
        if (!renderer.Init(240, 180)) {
            Check(false, "chart donut renderer");
        } else {
            TestChart chart;
            chart.Kind(ChartKind::Donut)
                .Slices({{L"A", 0.5f}, {L"B", 0.3f}, {L"C", 0.2f}})
                .PreferredSize({220.0f, 160.0f});
            chart.Measure({220.0f, 160.0f}, theme);
            chart.Arrange({8.0f, 8.0f, 220.0f, 160.0f});
            ID2D1DeviceContext2* dc = renderer.BeginDraw();
            Painter painter;
            painter.BeginFrame(dc, &UiText(), 1.0f);
            painter.FillRect({0.0f, 0.0f, 240.0f, 180.0f}, theme.bg);
            DrawControlTree(painter, theme, &chart);
            painter.EndFrame();
            Check(renderer.EndDraw(), "chart donut enddraw");
            Color ink{};
            int hits = 0;
            for (int x = 20; x < 140; ++x) {
                for (int y = 20; y < 160; ++y) {
                    renderer.ReadPixel(x, y, ink);
                    if (std::max(ink.r, std::max(ink.g, ink.b)) > 0.12f) ++hits;
                }
            }
            Check(hits > 30, "chart donut paints ring");
            renderer.Shutdown();
        }
    }
    {
        OffscreenRenderer renderer;
        if (!renderer.Init(200, 80)) {
            Check(false, "chart heatmap renderer");
        } else {
            TestChart chart;
            chart.Kind(ChartKind::Heatmap).Grid(8, 4).Cell([](size_t x, size_t y) {
                return (x == 0 && y == 0) ? 1.0f : 0.0f;
            });
            chart.PreferredSize({180.0f, 64.0f});
            chart.Measure({180.0f, 64.0f}, theme);
            chart.Arrange({8.0f, 8.0f, 180.0f, 64.0f});
            ID2D1DeviceContext2* dc = renderer.BeginDraw();
            Painter painter;
            painter.BeginFrame(dc, &UiText(), 1.0f);
            painter.FillRect({0.0f, 0.0f, 200.0f, 80.0f}, theme.bg);
            DrawControlTree(painter, theme, &chart);
            painter.EndFrame();
            Check(renderer.EndDraw(), "chart heatmap enddraw");
            Color hi{}, lo{};
            renderer.ReadPixel(24, 22, hi);
            renderer.ReadPixel(52, 22, lo);
            const auto luma = [](Color c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; };
            Check(luma(hi) > luma(lo) + 0.05f, "chart heatmap high cell brighter than low");
            renderer.Shutdown();
        }
    }
    {
        OffscreenRenderer renderer;
        if (!renderer.Init(220, 80)) {
            Check(false, "chart sample-cap renderer");
        } else {
            TestChart chart;
            chart.Kind(ChartKind::Line).Count(10000).Values([](size_t i) {
                return static_cast<float>(i % 17);
            });
            chart.Measure({200.0f, 60.0f}, theme);
            chart.Arrange({8.0f, 8.0f, 200.0f, 60.0f});
            ID2D1DeviceContext2* dc = renderer.BeginDraw();
            Painter painter;
            painter.BeginFrame(dc, &UiText(), 1.0f);
            painter.FillRect({0.0f, 0.0f, 220.0f, 80.0f}, theme.bg);
            DrawControlTree(painter, theme, &chart);
            painter.EndFrame();
            Check(renderer.EndDraw(), "chart 10000-point sample cap");
            renderer.Shutdown();
        }
    }
    {
        TestChart chart;
        chart.Values({1.f, 2.f, 3.f});
        Check(std::fabs(chart.DisplayValue(0) - 1.f) < 0.01f, "chart first values snap");
        chart.Values({10.f, 20.f, 30.f});
        Check(chart.DisplayValue(0) < 4.f, "chart tween starts at previous");
        chart.OnAnimate(0.08f);
        Check(chart.DisplayValue(0) > 1.f && chart.DisplayValue(0) < 10.f, "chart tween mid");
        chart.OnAnimate(1.0f);
        Check(std::fabs(chart.DisplayValue(0) - 10.f) < 0.05f, "chart tween settles");
    }
    {
        TestChart chart;
        chart.Kind(ChartKind::Line)
            .Header(L"T", L"1")
            .Values({20.f, 32.f, 38.f, 45.f})
            .Baseline({18.f, 24.f, 29.f, 40.f})
            .SeriesName(L"Active")
            .BaselineName(L"Baseline");
        chart.Measure({400.0f, 200.0f}, theme);
        chart.Arrange({0.0f, 0.0f, 400.0f, 200.0f});
        Check(chart.LegendCount() == 2, "chart line legend has two series");
        Check(chart.SeriesVisible(1), "chart baseline visible by default");
        const Rect lb = chart.LegendBounds(1);
        Check(lb.w > 8.0f && lb.h > 8.0f, "chart legend bounds");
        chart.OnMouseDown({lb.x + 4.0f, lb.y + 4.0f}, kBtnL);
        Check(!chart.SeriesVisible(1), "chart legend click hides baseline");
        chart.OnMouseDown({lb.x + 4.0f, lb.y + 4.0f}, kBtnL);
        Check(chart.SeriesVisible(1), "chart legend click shows baseline");
    }
    {
        TestChart chart;
        chart.Kind(ChartKind::Line).Values({1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f, 10.f, 11.f, 12.f});
        chart.Measure({240.0f, 120.0f}, theme);
        chart.Arrange({0.0f, 0.0f, 240.0f, 120.0f});
        Check(chart.ViewStart() == 0.0f && chart.ViewEnd() == 1.0f, "chart view default");
        Check(chart.OnWheel(1.0f), "chart wheel zooms");
        Check(chart.ViewEnd() - chart.ViewStart() < 0.99f, "chart wheel shrinks window");
        chart.OnMouseDoubleClick({120.0f, 60.0f});
        Check(chart.ViewStart() == 0.0f && chart.ViewEnd() == 1.0f, "chart double-click resets view");
        Check(chart.OnWheel(1.0f), "chart wheel again");
        chart.ResetView();
        Check(chart.ViewStart() == 0.0f && chart.ViewEnd() == 1.0f, "chart ResetView");
    }
    {
        TestChart chart;
        chart.Kind(ChartKind::Bar).Values({1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f});
        chart.Measure({240.0f, 120.0f}, theme);
        chart.Arrange({0.0f, 0.0f, 240.0f, 120.0f});
        Check(chart.OnWheel(1.0f), "bar wheel zooms");
        const float a = chart.ViewStart() * 7.0f;
        const float b = chart.ViewEnd() * 7.0f;
        Check(std::fabs(a - std::floor(a + 0.5f)) < 0.02f, "bar view start snaps to category");
        Check(std::fabs(b - std::floor(b + 0.5f)) < 0.02f, "bar view end snaps to category");
        Check(b - a >= 1.5f, "bar view keeps at least two categories");
    }
    {
        const float data[] = {0.f, 1.f, 0.4f, 1.6f, 0.2f, 1.8f, 0.5f, 2.f};
        TestSparkline spark;
        spark.Values(std::span<const float>{data, 8});
        Check(spark.Count() == 8, "sparkline Values(span) stores count");
        spark.Measure({160.0f, 36.0f}, theme);
        spark.Arrange({0.0f, 0.0f, 160.0f, 36.0f});
    }
    {
        OffscreenRenderer renderer;
        if (!renderer.Init(220, 90)) {
            Check(false, "table progress renderer");
        } else {
            TestRoot host;
            auto& table = host.Add<Table>();
            table.AddColumn(L"Load", 160.0f).Progress([](size_t) { return 0.8f; });
            table.RowCount(1);
            host.Measure({200.0f, 80.0f}, theme);
            host.Arrange({8.0f, 8.0f, 200.0f, 72.0f});
            ID2D1DeviceContext2* dc = renderer.BeginDraw();
            Painter painter;
            painter.BeginFrame(dc, &UiText(), 1.0f);
            painter.FillRect({0.0f, 0.0f, 220.0f, 90.0f}, theme.bg);
            DrawControlTree(painter, theme, &host);
            painter.EndFrame();
            Check(renderer.EndDraw(), "table progress enddraw");
            Color ink{};
            const Rect body = table.AbsoluteBounds();
            int hits = 0;
            for (int y = static_cast<int>(body.y) + 34; y < static_cast<int>(body.Bottom()) - 2; ++y) {
                for (int x = static_cast<int>(body.x) + 16; x < static_cast<int>(body.x) + 140; ++x) {
                    renderer.ReadPixel(x, y, ink);
                    if (ink.r > 0.2f) ++hits;
                }
            }
            Check(hits > 8, "table progress bar ink");
            renderer.Shutdown();
        }
    }
    {
        OffscreenRenderer renderer;
        if (!renderer.Init(640, 280)) {
            Check(false, "table live reset renderer");
        } else {
            struct LiveRow {
                bool checked = true;
                std::wstring title;
                std::wstring paper;
                std::wstring out_name;
                std::wstring status;
                float progress = 0.0f;
            };
            VectorModel<LiveRow> model;
            std::vector<LiveRow> rows(11);
            for (size_t i = 0; i < rows.size(); ++i) {
                rows[i].title = L"S" + std::to_wstring(i + 1);
                rows[i].paper = L"A0";
                rows[i].out_name = std::to_wstring(i + 1) + L".pdf";
                rows[i].status = L"Q";
            }
            model.Map([](const LiveRow& item, ItemRow& out) {
                out.cells = {item.checked ? L"1" : L"0", item.title, item.paper, item.out_name, L"",
                             item.status};
            });
            model.Reset(rows);
            TestRoot host;
            auto& table = host.Add<Table>();
            table.Bind(model);
            table.AddColumn(L"On", 56.0f).CheckBox(
                [&rows](size_t i) { return i < rows.size() && rows[i].checked; },
                [&rows](size_t i, bool v) {
                    if (i < rows.size()) rows[i].checked = v;
                });
            table.AddColumn(L"Title");
            table.AddColumn(L"Paper", 80.0f).TextBox(
                [&rows](size_t i) { return i < rows.size() ? rows[i].paper : std::wstring{}; },
                [&rows](size_t i, std::wstring v) {
                    if (i < rows.size()) rows[i].paper = std::move(v);
                });
            table.AddColumn(L"File");
            table.AddColumn(L"Load", 120.0f).Progress(
                [&rows](size_t i) { return i < rows.size() ? rows[i].progress : 0.0f; });
            table.AddColumn(L"State", 80.0f);
            host.Measure({620.0f, 260.0f}, theme);
            host.Arrange({8.0f, 8.0f, 620.0f, 252.0f});
            bool draw_ok = true;
            for (int frame = 0; frame < 40; ++frame) {
                LiveRow& row = rows[static_cast<size_t>(frame % 11)];
                row.progress = std::min(1.0f, row.progress + 0.2f);
                row.status = L"P";
                model.Reset(rows);
                ID2D1DeviceContext2* dc = renderer.BeginDraw();
                Painter painter;
                painter.BeginFrame(dc, &UiText(), 1.0f);
                painter.FillRect({0.0f, 0.0f, 640.0f, 280.0f}, theme.bg);
                DrawControlTree(painter, theme, &host);
                painter.EndFrame();
                if (!renderer.EndDraw()) {
                    draw_ok = false;
                    break;
                }
            }
            Check(draw_ok, "table live reset enddraw");
            renderer.Shutdown();
        }
    }
}

void RenderListScene(const wchar_t* path) {
    OffscreenRenderer renderer;
    if (!renderer.Init(700, 500)) {
        Check(false, "renderer init (list scene)");
        return;
    }
    const Theme theme = MakeTheme();
    TestRoot root;
    root.Padding(8.0f, 8.0f).Spacing(8.0f);
    auto& list = root.Add<TestList>();
    list.ItemCount(8);
    list.ItemText([](size_t i, std::wstring& s) { s = L"行 " + std::to_wstring(i); });
    list.MultiSelect(true);
    list.SelectedIndices({0, 2});

    auto& table = root.Add<Table>();
    table.AddColumn(L"A", 120.0f);
    table.AddColumn(L"B");
    static const std::vector<int> kSceneValues{9, 5, 7, 1, 3};
    table.CellText([](size_t row, size_t col, std::wstring& out) {
        if (col == 0) out = std::to_wstring(kSceneValues[row]);
        else out = L"b" + std::to_wstring(row);
    });
    table.Sortable(0, true);
    table.RowCount(5);
    table.SelectedIndex(1);   // 数据 1（"5"），升序后应落到视图 2
    table.SortBy(0, 1);
    Check(table.SelectedIndex() == 2, "scene table selection follows sort");

    root.Measure({700.0f, 500.0f}, theme);
    root.Arrange({0.0f, 0.0f, 700.0f, 500.0f});

    ID2D1DeviceContext2* dc = renderer.BeginDraw();
    Painter painter;
    painter.BeginFrame(dc, &UiText(), 1.0f);
    painter.FillRect({0, 0, 700, 500}, theme.bg);
    DrawControlTree(painter, theme, &root);
    painter.EndFrame();
    Check(renderer.EndDraw(), "enddraw (list scene)");

    const float rh = theme.list_row_height;
    Color c{};
    const Rect lb = list.AbsoluteBounds();
    const Color plain_row = Over(theme.fill_input, theme.bg);
    const Color picked_row = Over(theme.fill_selected, Over(theme.fill_input, theme.bg));
    renderer.ReadPixel(static_cast<int>(lb.x + lb.w * 0.5f), static_cast<int>(lb.y + 0.5f * rh), c);
    Check(CloseTo(c, picked_row), "multi row0 selected fill");
    renderer.ReadPixel(static_cast<int>(lb.x + lb.w * 0.5f), static_cast<int>(lb.y + 1.5f * rh), c);
    Check(CloseTo(c, plain_row), "multi row1 unselected fill");
    renderer.ReadPixel(static_cast<int>(lb.x + lb.w * 0.5f), static_cast<int>(lb.y + 2.5f * rh), c);
    Check(CloseTo(c, picked_row), "multi row2 selected fill");

    const Rect tb = table.AbsoluteBounds();
    renderer.ReadPixel(static_cast<int>(tb.x + tb.w * 0.5f),
                       static_cast<int>(tb.y + 32.0f + 2.5f * rh), c);
    Check(CloseTo(c, picked_row), "table sorted view selected fill");

    Check(renderer.SavePNG(path), "save png (list scene)");
    renderer.Shutdown();
}

void TestSignal() {
    int n = 0;
    Signal<> sig;
    Connection a = sig.Connect([&] { ++n; });
    Connection b = sig.Connect([&] { n += 10; });
    sig.Emit();
    Check(n == 11, "signal two subscribers");
    a.Disconnect();
    sig.Emit();
    Check(n == 21, "signal disconnect");
    b.Disconnect();
    n = 0;
    {
        ScopedConnection scoped(sig.Connect([&] { n += 100; }));
        sig.Emit();
        Check(n == 100, "scoped connection fires");
    }
    sig.Emit();
    Check(n == 100, "scoped disconnects on dtor");

    TestButton btn;
    int clicks = 0;
    btn.OnClick([&] { ++clicks; });
    btn.OnClick([&] { clicks += 2; });
    Connection extra = btn.BindClick([&] { clicks += 4; });
    btn.OnMouseUp({0.0f, 0.0f}, 1);
    Check(clicks == 7, "button multicasts click");
    extra.Disconnect();
    btn.OnMouseUp({0.0f, 0.0f}, 1);
    Check(clicks == 10, "button bindclick disconnect");

    Property<int> count{1};
    int seen = -1;
    Connection watch = count.OnChanged([&](const int& v) { seen = v; });
    count = 3;
    Check(seen == 3 && count.Get() == 3, "property notifies");
    count = 3;
    Check(seen == 3, "property skips equal");
    watch.Disconnect();
    count = 9;
    Check(seen == 3, "property disconnect");
}

void TestLayout() {
    const Theme theme = MakeTheme();
    using Cross = StackPanel::CrossAlign;
    using Main = StackPanel::MainAlign;

    {
        TestRoot root;
        auto& row = root.Add<Row>().Spacing(4.0f);
        row.Add<Panel>().SetBounds({0.0f, 0.0f, 10.0f, 8.0f});
        auto& b = row.Add<Panel>().SetBounds({0.0f, 0.0f, 20.0f, 10.0f});
        const Size s = root.Measure({400.0f, 400.0f}, theme);
        Check(Near(s.w, 34.0f) && Near(s.h, 10.0f), "row pack size");
        root.Arrange({0.0f, 0.0f, s.w, s.h});
        Check(Near(b.Bounds().x, 14.0f), "row second x");
    }
    {
        TestRoot root;
        auto& row = root.Add<Row>();
        auto& a = row.Add<Panel>().SetBounds({0.0f, 0.0f, 10.0f, 10.0f});
        a.Grow();
        auto& b = row.Add<Panel>().SetBounds({0.0f, 0.0f, 10.0f, 10.0f});
        b.Grow();
        root.Measure({200.0f, 50.0f}, theme);
        root.Arrange({0.0f, 0.0f, 200.0f, 50.0f});
        Check(Near(a.Bounds().w, 100.0f) && Near(b.Bounds().w, 100.0f), "grow equal columns");
        Check(Near(b.Bounds().x, 100.0f), "grow second x");
    }
    {
        TestRoot root;
        auto& row = root.Add<Row>().Spacing(4.0f).AlignMain(Main::End);
        row.Add<Panel>().SetBounds({0.0f, 0.0f, 10.0f, 10.0f});
        auto& b = row.Add<Panel>().SetBounds({0.0f, 0.0f, 20.0f, 10.0f});
        root.Measure({200.0f, 50.0f}, theme);
        root.Arrange({0.0f, 0.0f, 200.0f, 50.0f});
        Check(Near(b.Bounds().x, 180.0f), "align main end");
    }
    {
        TestRoot root;
        auto& row = root.Add<Row>().AlignMain(Main::SpaceBetween);
        auto& a = row.Add<Panel>().SetBounds({0.0f, 0.0f, 10.0f, 10.0f});
        auto& b = row.Add<Panel>().SetBounds({0.0f, 0.0f, 10.0f, 10.0f});
        auto& c = row.Add<Panel>().SetBounds({0.0f, 0.0f, 10.0f, 10.0f});
        root.Measure({100.0f, 50.0f}, theme);
        root.Arrange({0.0f, 0.0f, 100.0f, 50.0f});
        Check(Near(a.Bounds().x, 0.0f) && Near(b.Bounds().x, 45.0f) && Near(c.Bounds().x, 90.0f),
              "align space-between");
    }
    {
        TestRoot root;
        root.AlignCross(Cross::End);
        auto& box = root.Add<Panel>().SetBounds({0.0f, 0.0f, 20.0f, 10.0f});
        root.Measure({100.0f, 50.0f}, theme);
        root.Arrange({0.0f, 0.0f, 100.0f, 50.0f});
        Check(Near(box.Bounds().x, 80.0f), "align cross end");
    }
    {
        TestRoot root;
        auto& grid = root.Add<Grid>(2).Gap(10.0f);
        auto& a = grid.Add<Panel>().SetBounds({0.0f, 0.0f, 10.0f, 20.0f});
        auto& b = grid.Add<Panel>().SetBounds({0.0f, 0.0f, 10.0f, 30.0f});
        root.Measure({210.0f, 100.0f}, theme);
        root.Arrange({0.0f, 0.0f, 210.0f, 50.0f});
        Check(Near(a.Bounds().w, 100.0f) && Near(b.Bounds().x, 110.0f), "grid equal columns");
        Check(Near(a.Bounds().h, 30.0f) && Near(b.Bounds().h, 30.0f), "grid stretch row height");
    }
    {
        TestRoot root;
        auto& grid = root.Add<Grid>(1, 0, 1);
        auto& lead = grid.Add<Panel>().SetBounds({0.0f, 0.0f, 40.0f, 10.0f});
        auto& mid = grid.Add<Panel>().SetBounds({0.0f, 0.0f, 80.0f, 10.0f});
        auto& trail = grid.Add<Panel>().SetBounds({0.0f, 0.0f, 20.0f, 10.0f});
        root.Measure({300.0f, 50.0f}, theme);
        root.Arrange({0.0f, 0.0f, 300.0f, 50.0f});
        Check(Near(lead.Bounds().w, 110.0f) && Near(mid.Bounds().x, 110.0f) && Near(mid.Bounds().w, 80.0f),
              "grid 1fr auto 1fr mid");
        Check(Near(trail.Bounds().x, 190.0f) && Near(trail.Bounds().w, 110.0f), "grid 1fr auto 1fr trail");
    }
    {
        TestRoot root;
        auto& row = root.Add<Row>();
        auto& a = row.Add<Panel>().SetBounds({0.0f, 0.0f, 10.0f, 10.0f});
        a.Grow();
        auto& b = row.Add<Panel>().SetBounds({0.0f, 0.0f, 10.0f, 10.0f});
        b.Grow();
        root.Measure({200.0f, 50.0f}, theme);
        root.Arrange({0.0f, 0.0f, 200.0f, 50.0f});
        root.Measure({300.0f, 80.0f}, theme);
        root.Arrange({0.0f, 0.0f, 300.0f, 80.0f});
        Check(Near(a.Bounds().w, 150.0f) && Near(b.Bounds().w, 150.0f), "grow relayout wider");
        Check(a.AbsoluteBounds().w > 1.0f && a.AbsoluteBounds().h > 1.0f, "grow relayout absolute");
    }
    {
        TestRoot root;
        root.Padding(10.0f, 10.0f);
        auto& grid = root.Add<Grid>(2).Gap(10.0f);
        auto& left = grid.Add<Column>().Spacing(8.0f);
        auto& field = left.Add<Row>();
        auto& box = field.Add<Panel>().SetBounds({0.0f, 0.0f, 40.0f, 24.0f});
        box.Grow();
        root.Measure({400.0f, 300.0f}, theme);
        root.Arrange({0.0f, 40.0f, 400.0f, 260.0f});
        const float w1 = box.AbsoluteBounds().w;
        Check(w1 > 50.0f && box.AbsoluteBounds().h > 0.5f && box.AbsoluteBounds().y >= 40.0f,
              "nested grow first size");
        root.Measure({600.0f, 400.0f}, theme);
        root.Arrange({0.0f, 40.0f, 600.0f, 360.0f});
        Check(box.AbsoluteBounds().w > w1 && box.AbsoluteBounds().y >= 40.0f,
              "nested grow after window resize");
    }
    {
        TestRoot root;
        auto& sv = root.Add<TestScrollViewer>();
        sv.Grow();
        auto& inner = sv.Add<Panel>().SetBounds({0.0f, 0.0f, 40.0f, 240.0f});
        root.Measure({120.0f, 80.0f}, theme);
        root.Arrange({0.0f, 0.0f, 120.0f, 80.0f});
        Check(Near(sv.AbsoluteBounds().h, 80.0f), "scroll viewport height");
        Check(Near(inner.AbsoluteBounds().h, 240.0f), "scroll content height");
        Check(Near(inner.AbsoluteBounds().y, 0.0f), "scroll offset 0");
        sv.ScrollToY(40.0f);
        Check(Near(inner.AbsoluteBounds().y, -40.0f), "scroll offset 40");
        Check(sv.ContentHeight() > sv.AbsoluteBounds().h + 1.0f, "scroll overflow");
        sv.OnWheel(-1.0f);
        Check(Near(sv.OffsetY(), 88.0f), "scroll wheel steps 48");
    }
    {
        TestRoot root;
        auto& sv = root.Add<TestScrollViewer>();
        sv.Grow();
        auto& a = sv.Add<Panel>().SetBounds({0.0f, 0.0f, 40.0f, 80.0f});
        auto& b = sv.Add<Panel>().SetBounds({0.0f, 0.0f, 40.0f, 40.0f});
        auto& c = sv.Add<Panel>().SetBounds({0.0f, 0.0f, 40.0f, 80.0f});
        (void)a;
        (void)c;
        root.Measure({120.0f, 80.0f}, theme);
        root.Arrange({0.0f, 0.0f, 120.0f, 80.0f});
        sv.ScrollIntoView(b);
        Check(Near(sv.OffsetY(), 40.0f), "scroll into view nearest mid item");
        sv.ScrollToY(0.0f);
        sv.ScrollIntoView(b, ScrollAlignment::Center);
        Check(Near(sv.OffsetY(), 60.0f), "scroll into view centers item");
        sv.ScrollToY(0.0f);
        sv.ScrollIntoView(b, ScrollAlignment::Start);
        Check(Near(sv.OffsetY(), 80.0f), "scroll into view start");
    }
    {
        TestRoot root;
        auto& sv = root.Add<TestScrollViewer>();
        sv.Grow();
        auto& top = sv.Add<Panel>().SetBounds({0.0f, 0.0f, 40.0f, 80.0f});
        auto& mid = sv.Add<Panel>().SetBounds({0.0f, 0.0f, 40.0f, 80.0f});
        sv.Add<Panel>().SetBounds({0.0f, 0.0f, 40.0f, 80.0f});
        root.Measure({120.0f, 80.0f}, theme);
        root.Arrange({0.0f, 0.0f, 120.0f, 80.0f});
        sv.ScrollToY(80.0f);
        Check(Near(mid.AbsoluteBounds().y, 0.0f), "scroll mid item at viewport top");
        top.SetBounds({0.0f, 0.0f, 40.0f, 120.0f});
        root.Measure({120.0f, 80.0f}, theme);
        root.Arrange({0.0f, 0.0f, 120.0f, 80.0f});
        Check(Near(sv.OffsetY(), 120.0f) && Near(mid.AbsoluteBounds().y, 0.0f),
              "scroll anchor keeps viewport item when content above grows");
        sv.AnchorEnabled(false);
        sv.ScrollToY(80.0f, false);
        top.SetBounds({0.0f, 0.0f, 40.0f, 160.0f});
        root.Measure({120.0f, 80.0f}, theme);
        root.Arrange({0.0f, 0.0f, 120.0f, 80.0f});
        Check(Near(sv.OffsetY(), 80.0f), "scroll anchor off leaves offset");
    }
    {
        TestRoot root;
        auto& row = root.Add<Row>();
        auto& a = row.Add<Panel>().SetBounds({0.0f, 0.0f, 20.0f, 10.0f});
        a.Margin(8.0f);
        auto& b = row.Add<Panel>().SetBounds({0.0f, 0.0f, 20.0f, 10.0f});
        root.Measure({400.0f, 50.0f}, theme);
        root.Arrange({0.0f, 0.0f, 400.0f, 50.0f});
        Check(Near(a.DesiredSize().w, 36.0f), "margin inflates desired");
        Check(Near(a.AbsoluteBounds().x, 8.0f) && Near(b.AbsoluteBounds().x, 36.0f),
              "margin insets arrange");
    }
    {
        TestRoot root;
        auto& stack = root.Add<ZStack>();
        auto& back = stack.Add<Panel>().SetBounds({0.0f, 0.0f, 80.0f, 40.0f});
        auto& front = stack.Add<Panel>().SetBounds({0.0f, 0.0f, 20.0f, 10.0f});
        root.Measure({200.0f, 100.0f}, theme);
        root.Arrange({0.0f, 0.0f, 200.0f, 100.0f});
        Check(Near(stack.DesiredSize().w, 80.0f) && Near(stack.DesiredSize().h, 40.0f),
              "zstack sizes to largest child");
        Check(Near(front.AbsoluteBounds().x, back.AbsoluteBounds().x + 30.0f),
              "zstack centers overlay");
    }
    {
        TestRoot root;
        auto& box = root.Add<TestPanel>();
        box.SetBounds({0.0f, 0.0f, 10.0f, 10.0f});
        box.MinSize({40.0f, 24.0f});
        const Size s = box.Measure({100.0f, 100.0f}, theme);
        Check(Near(s.w, 10.0f) && Near(s.h, 10.0f), "minsize applied in parent measure");
        root.Measure({100.0f, 100.0f}, theme);
        Check(Near(box.DesiredSize().w, 40.0f) && Near(box.DesiredSize().h, 24.0f),
              "minsize via MeasureChildAt");
    }
    {
        TestRoot root;
        auto& row = root.Add<Row>().AlignCross(Cross::Start);
        auto& box = row.Add<Panel>().SetBounds({0.0f, 0.0f, 100.0f, 80.0f});
        box.MaxSize({40.0f, 24.0f});
        root.Measure({400.0f, 100.0f}, theme);
        root.Arrange({0.0f, 0.0f, 400.0f, 100.0f});
        Check(Near(box.DesiredSize().w, 40.0f) && Near(box.DesiredSize().h, 24.0f),
              "maxsize clamps desired");
        Check(Near(box.AbsoluteBounds().w, 40.0f) && Near(box.AbsoluteBounds().h, 24.0f),
              "maxsize clamps arrange");
    }
    {
        TestRoot root;
        root.Padding(10.0f, 5.0f);
        auto& box = root.Add<Panel>().SetBounds({0.0f, 0.0f, 20.0f, 10.0f});
        const Size s = root.Measure({400.0f, 400.0f}, theme);
        Check(Near(s.w, 40.0f) && Near(s.h, 20.0f), "column padding size");
        root.Arrange({0.0f, 0.0f, s.w, s.h});
        Check(Near(box.AbsoluteBounds().x, 10.0f) && Near(box.AbsoluteBounds().y, 5.0f),
              "column padding origin");
    }
    {
        TestRoot root;
        auto& wrap = root.Add<WrapPanel>().Gap(8.0f);
        auto& a = wrap.Add<Panel>().SetBounds({0.0f, 0.0f, 40.0f, 20.0f});
        auto& b = wrap.Add<Panel>().SetBounds({0.0f, 0.0f, 40.0f, 20.0f});
        auto& c = wrap.Add<Panel>().SetBounds({0.0f, 0.0f, 40.0f, 20.0f});
        const Size s = root.Measure({90.0f, 400.0f}, theme);
        Check(Near(s.w, 88.0f) && Near(s.h, 48.0f), "wrap panel folds third item");
        root.Arrange({0.0f, 0.0f, 90.0f, 100.0f});
        Check(Near(a.AbsoluteBounds().x, 0.0f) && Near(a.AbsoluteBounds().y, 0.0f),
              "wrap first origin");
        Check(Near(b.AbsoluteBounds().x, 48.0f) && Near(b.AbsoluteBounds().y, 0.0f),
              "wrap second same row");
        Check(Near(c.AbsoluteBounds().x, 0.0f) && Near(c.AbsoluteBounds().y, 28.0f),
              "wrap third next row");
    }
}

void TestTypography() {
    const Size overline = UiText().MeasureText(L"SECTION", TextRole::Overline);
    const Size caption = UiText().MeasureText(L"SECTION", TextRole::Caption);
    Check(overline.w > 8.0f && overline.h > 8.0f, "overline measures");
    Check(overline.h <= caption.h + 0.05f, "overline not taller than caption");
    const Size subtitle = UiText().MeasureText(L"Subtitle", TextRole::Subtitle);
    const Size body = UiText().MeasureText(L"Subtitle", TextRole::Body);
    Check(subtitle.h > body.h + 0.4f, "subtitle taller than body");
    const float n0 = UiText().MeasureText(L"0000", TextRole::Numeric).w;
    const float n1 = UiText().MeasureText(L"1111", TextRole::Numeric).w;
    Check(Near(n0, n1, 0.4f), "numeric tabular figures");
    Check(UiText().MeasureText(L"界面", TextRole::Body).w > 12.0f, "cjk fallback measures");
    const float tracked = UiText().MeasureText(L"WWWW", TextRole::Caption).w;
    const float untracked = UiText().MeasureText(L"WWWW", TextRole::Numeric).w;
    // Caption 12px + 0.06em 字距，Numeric 14px 无字距；字距使 Caption 不至于明显窄于更大的 Numeric。
    Check(tracked + 4.0f > untracked * (12.0f / 14.0f), "caption tracking widens");
}

std::vector<std::byte> ReadAllBytes(const std::wstring& path) {
    std::vector<std::byte> bytes;
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file) return bytes;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size > 0) {
        bytes.resize(static_cast<size_t>(size));
        if (std::fread(bytes.data(), 1, bytes.size(), file) != bytes.size()) bytes.clear();
    }
    std::fclose(file);
    return bytes;
}

// 自定义字体：拿系统 Consolas 文件当"内嵌资源"，验证内存注册、族名回报、作用域覆盖与回落。
void TestCustomFonts() {
    wchar_t windows[MAX_PATH]{};
    GetWindowsDirectoryW(windows, MAX_PATH);
    const std::wstring path = std::wstring(windows) + L"\\Fonts\\consola.ttf";
    const std::vector<std::byte> bytes = ReadAllBytes(path);
    if (bytes.empty()) {
        std::printf("[SKIP] consola.ttf not found\n");
        return;
    }
    const std::wstring family = App::AddFont(std::span<const std::byte>(bytes));
    Check(family == L"Consolas", "AddFont memory returns family name");
    Check(App::AddFont(path) == L"Consolas", "AddFont file returns family name");

    const float body = UiText().MeasureText(L"iiii", TextRole::Body).w;
    float mono = 0.0f;
    {
        FontFamilyScope scope(family);
        mono = UiText().MeasureText(L"iiii", TextRole::Body).w;
    }
    Check(mono > body + 4.0f, "custom family changes measure (mono i wider)");
    Check(Near(UiText().MeasureText(L"iiii", TextRole::Body).w, body, 0.05f),
          "family scope pops back to role default");
    {
        FontFamilyScope scope(L"No Such Family 42");
        Check(Near(UiText().MeasureText(L"iiii", TextRole::Body).w, body, 0.05f),
              "unknown family falls back to role default");
    }
    {
        FontFamilyScope scope(L"Microsoft YaHei UI");
        Check(UiText().MeasureText(L"iiii", TextRole::Body).w > 1.0f, "system family accepted");
    }

    struct TestLabel : Label {
        using Label::Label;
        using Label::Measure;
    };
    const Theme theme = MakeTheme();
    TestLabel plain(L"iiii");
    TestLabel custom(L"iiii");
    custom.FontFamily(family);
    Check(custom.Measure({0.0f, 0.0f}, theme).w > plain.Measure({0.0f, 0.0f}, theme).w + 4.0f,
          "Label::FontFamily affects measure");
    Check(custom.FontFamily() == family, "Label::FontFamily reads back");

    struct TestRich : RichLabel {
        using RichLabel::Measure;
    };
    TestRich rich_plain;
    rich_plain.Add(L"iiii");
    TestRich rich_custom;
    rich_custom.Font(L"iiii", family);
    Check(rich_custom.Measure({300.0f, 0.0f}, theme).h >= rich_plain.Measure({300.0f, 0.0f}, theme).h,
          "RichLabel::Font run measures");
}

// Table::CellCharacterFont 运行回归：自定义字体分段绘制 + Reset/加列重建控件池后仍可绘制。
// 对应宿主（ShowBox 钢筋选配表）序列：布局 → 绘制 → 模型 Reset 换数据 → 加交互列重建 → 再绘制。
void TestTableCharacterFont() {
    wchar_t windows[MAX_PATH]{};
    GetWindowsDirectoryW(windows, MAX_PATH);
    const std::wstring path = std::wstring(windows) + L"\\Fonts\\consola.ttf";
    const std::vector<std::byte> bytes = ReadAllBytes(path);
    if (bytes.empty()) {
        std::printf("[SKIP] consola.ttf not found\n");
        return;
    }
    const std::wstring family = App::AddFont(std::span<const std::byte>(bytes));
    Check(!family.empty(), "table character font registered");

    const Theme theme = MakeTheme();
    TestRoot root;
    auto& table = root.Add<TestTable>();
    auto model = std::make_shared<VectorModel<std::wstring>>();
    model->Reset({L"C8@100", L"C10@150", L"C12@200"});
    table.Bind(*model);
    table.AddColumn(L"直径", 120.0f);
    table.CellCharacterFont(L"C", family);
    table.RowHeight(30.0f);
    root.Measure({160.0f, 200.0f}, theme);
    root.Arrange({0.0f, 0.0f, 160.0f, 200.0f});

    OffscreenRenderer target;
    if (!target.Init(200, 200)) {
        Check(false, "table font renderer init");
        return;
    }
    auto band_has_glyph = [&](int x0, int y0, int x1, int y1) {
        Color bg{};
        target.ReadPixel(100, 170, bg);   // 表体末行之下的空白区做底色
        const float base = bg.r + bg.g + bg.b;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                Color c{};
                target.ReadPixel(x, y, c);
                if (c.r + c.g + c.b > base + 0.15f) return true;
            }
        }
        return false;
    };

    // 悬停回调：进表体首行触发 enter，离开触发 leave；逐格提示与锚点跟随单元格。
    ptrdiff_t hover_row = -2;
    int hover_col = -2;
    bool hover_entered = false;
    int hover_hits = 0;
    table.OnCellHover([&](ptrdiff_t row, int col, bool entered) {
        ++hover_hits;
        hover_row = row;
        hover_col = col;
        hover_entered = entered;
    });
    table.CellToolTip([](size_t row, int col, std::wstring& out) {
        out = L"tip " + std::to_wstring(row) + L":" + std::to_wstring(col);
    });
    Check(table.ToolTipDelay() < 0.0f, "tooltip delay defaults to theme");
    table.ToolTipDelay(0.12f);
    Check(Near(table.ToolTipDelay(), 0.12f), "tooltip delay supports control override");
    Button delayed;
    delayed.ToolTipDelay(0.2f);
    Button moved(std::move(delayed));
    Check(Near(moved.ToolTipDelay(), 0.2f), "tooltip delay survives control move");
    bool wheel_consumed = false;
    table.OnWheelScroll([&](float d) {
        wheel_consumed = true;
        return d > 0.0f;
    });

    for (int round = 0; round < 3; ++round) {
        if (round == 1) {
            model->Reset({L"D8@100", L"D10@150"});
        } else if (round == 2) {
            const int del = table.AddColumn(L"删除", 60.0f);
            table.BindButton(del, L"删除", [](size_t) {});
            root.Measure({200.0f, 200.0f}, theme);
            root.Arrange({0.0f, 0.0f, 200.0f, 200.0f});
        }
        if (round == 0) {
            table.OnMouseMove({60.0f, 50.0f}, 0);
            Check(hover_hits == 1 && hover_row == 0 && hover_col == 0 && hover_entered,
                  "cell hover enter fires");
            Check(table.ToolTip() == L"tip 0:0", "cell tooltip provider feeds text");
            const Rect anchor = table.ToolTipAnchor();
            Check(anchor.y > 31.9f && anchor.y < 32.1f && Near(anchor.h, 30.0f),
                  "tooltip anchors to hovered cell");
            table.OnMouseMove({60.0f, 80.0f}, 0);
            Check(table.ToolTip() == L"tip 1:0", "cell tooltip refreshes after moving rows");
            const Rect moved_anchor = table.ToolTipAnchor();
            Check(moved_anchor.y > anchor.y + 29.9f,
                  "tooltip anchor refreshes after moving rows");
            table.OnMouseLeave();
            Check(hover_hits == 3 && hover_row == -1 && !hover_entered,
                  "cell hover leave fires");
            Check(table.ToolTip().empty(), "cell tooltip cleared on leave");
            Check(table.OnWheel(1.0f) && wheel_consumed, "wheel consumed by handler");
            Check(!table.OnWheel(-1.0f), "wheel falls through when handler declines");
        }
        ID2D1DeviceContext2* dc = target.BeginDraw();
        Painter painter;
        painter.BeginFrame(dc, &UiText(), 1.0f);
        painter.FillRect({0.0f, 0.0f, 200.0f, 200.0f}, Color{0.0f, 0.0f, 0.0f, 1.0f});
        DrawControlTree(painter, theme, &root);
        painter.EndFrame();
        Check(target.EndDraw(), "table font round enddraw");
        // 首行文本带（表头 32 高 + 行高 30）必须出现比底色亮的字形像素。
        Check(band_has_glyph(8, 34, 118, 92), "table font glyphs drawn");
    }
    target.Shutdown();
}

void TestWindowContentMeasure() {
    Window window(WindowSpec{.title = L"content-measure", .size = {640.0f, 480.0f}});
    auto& body = window.Root().Add<Column>();
    body.Grow().FillCross().Padding(10.0f).Spacing(8.0f);
    body.Add<Label>(L"Toolbar").MinSize({0.0f, 40.0f}).MaxSize({10000.0f, 40.0f});
    auto& tables = body.Add<Row>();
    tables.Grow().FillCross().Spacing(8.0f);
    auto& left = tables.Add<Column>();
    left.Grow().FillCross();
    auto& table = left.Add<Table>();
    table.FillCross().RowHeight(34.0f);
    for (int col = 0; col < 14; ++col) table.AddColumn(std::to_wstring(col));
    auto& side = tables.Add<Table>();
    side.FillCross().RowHeight(34.0f);
    side.AddColumn(L"Top");
    side.AddColumn(L"Bottom");
    body.Add<Button>(L"Confirm").MinSize({96.0f, 40.0f}).MaxSize({10000.0f, 40.0f});
    const HWND hwnd = static_cast<HWND>(window.NativeHandle());
    RECT before{};
    GetClientRect(hwnd, &before);
    const float caption = window.TitleBar() ? window.TitleBar()->Height() : 0.0f;
    for (const int rows : {1, 12, 40}) {
        const float table_height = 32.0f + rows * 34.0f;
        table.RowCount(rows).MinSize({0.0f, table_height}).MaxSize({10000.0f, table_height});
        side.RowCount(rows).MinSize({220.0f, table_height}).MaxSize({220.0f, table_height});
        const Size desired = window.MeasureContent(1592.0f);
        Check(Near(desired.h, caption + 20.0f + 80.0f + 16.0f + table_height),
              "window content measure includes nested grow rows and chrome");
        RECT after{};
        GetClientRect(hwnd, &after);
        Check(EqualRect(&before, &after) && !IsWindowVisible(hwnd),
              "window content measure does not resize or show window");
    }
    // 探测大尺寸不能污染下一次正常布局的测量缓存。
    const float table_height = 32.0f + 12.0f * 34.0f;
    table.RowCount(12).MinSize({0.0f, table_height}).MaxSize({10000.0f, table_height});
    side.RowCount(12).MinSize({220.0f, table_height}).MaxSize({220.0f, table_height});
    const auto desired = window.MeasureContent(1592.0f);
    window.Resize(desired);
    window.LayoutNow();
    Check(Near(table.AbsoluteBounds().h, table_height) && Near(side.AbsoluteBounds().h, table_height),
          "window measured size lays out complete table rows");
    window.Close();
}


void TestTableTypography() {
    OffscreenRenderer target;
    if (!target.Init(360, 140)) { Check(false, "table typography init"); return; }
    TestTable table;
    table.AddColumn(L"Value", 300.0f).Alignment(Align::Leading);
    double value = 31.75;
    table.RowCount(1).RowHeight(40.0f).CellEditEnabled(true)
        .BindNumber(0, [&](size_t) { return value; }, [&](size_t, double v) { value = v; })
        .ColumnPrecision(0, 3)
        .CellText([](size_t, size_t, std::wstring& out) { out.clear(); });
    table.Arrange({10, 10, 300, 110});
    const Theme theme = MakeTheme();
    auto render = [&] {
        auto* dc = target.BeginDraw();
        dc->Clear(D2D1::ColorF(D2D1::ColorF::Black));
        Painter painter;
        painter.BeginFrame(dc, &UiText(), 1.0f);
        DrawControlTree(painter, theme, &table);
        painter.EndFrame();
        Check(target.EndDraw(), "table typography draw");
        std::vector<uint8_t> pixels;
        Check(target.ReadBack(pixels), "table typography readback");
        return pixels;
    };
    auto ink = [&](const std::vector<uint8_t>& pixels) {
        int left = 360, right = -1;
        for (int y = 48; y < 78; ++y) for (int x = 20; x < 300; ++x) {
            if (pixels[(static_cast<size_t>(y) * 360 + x) * 4] > 180) {
                left = std::min(left, x); right = std::max(right, x);
            }
        }
        return std::pair{left, right};
    };
    const auto leading = render();
    target.SavePNG(L"lumen_visual_table_leading.png");
    const auto left = ink(leading);
    table.ColumnAlignment(0, std::nullopt);
    const auto automatic = render();
    const auto right = ink(automatic);
    Check(left.second >= left.first && right.second >= right.first && right.first > left.first + 100,
          "numeric column explicit leading overrides trailing default and refreshes cache");
    table.CellCharacterFont(L"3", L"Consolas").ColumnAlignment(0, Align::Leading);
    const auto mixed_left = ink(render());
    table.ColumnAlignment(0, Align::Trailing);
    const auto mixed_right = ink(render());
    Check(mixed_right.first > mixed_left.first + 100 && mixed_left.second >= mixed_left.first,
          "mixed-font numeric column respects alignment");
    table.CellCharacterFont(L"", L"").ColumnAlignment(0, Align::Leading).Role(TextRole::Title);
    const auto larger = render();
    target.SavePNG(L"lumen_visual_table_larger.png");
    Check(ink(larger).second > left.second, "table role changes rendered glyph size");
    table.BeginEdit(0, 0);
    Check(table.Editor() && table.Editor()->Role() == TextRole::Title,
          "new cell editor follows table content role");
    table.Role(TextRole::Caption);
    Check(table.Editor() && table.Editor()->Role() == TextRole::Caption,
          "active cell editor follows changed table role");
    if (!table.Editor()) { target.Shutdown(); return; }
    table.Editor()->Text(L"bad");
    table.Commit();
    Check(value == 31.75, "alignment and role retain invalid numeric rejection");
    table.Cancel();
    Check(target.SavePNG(L"lumen_visual_table_typography.png"), "save table typography");
    target.Shutdown();
}

void TestTablePaintStability() {
    OffscreenRenderer target;
    if (!target.Init(480, 180)) {
        Check(false, "table paint stability target init");
        return;
    }
    const Theme theme = MakeTheme();
    TestTable table;
    for (const auto* title : {L"First", L"Second", L"Last"}) table.AddColumn(title);
    table.RowCount(2).RowHeight(30.0f).CellCharacterFont(L"C", L"Consolas");
    table.CellText([](size_t, size_t, std::wstring& out) { out = L"2C28"; });
    table.Arrange({10.0f, 10.0f, 360.0f, 100.0f});
    Check(table.ColumnAt(119.0f) == 0 && table.ColumnAt(120.0f) == 1 &&
              table.ColumnAt(240.0f) == 2 && table.ColumnAt(359.0f) == 2 &&
              table.MaxHorizontalScroll() == 0.0f,
          "table flex columns fill viewport through last column");
    LumaTextBridge bridge;
#if defined(LUMEN_HAS_LUMATEXT)
    (void)UiText().Format(TextRole::Caption);
    auto* initial_dc = target.BeginDraw();
    const bool ready = bridge.Init(UiText().Factory(), initial_dc);
    Check(target.EndDraw() && ready, "table paint stability LumaText init");
#endif
    auto render = [&] {
        auto* dc = target.BeginDraw();
        dc->Clear(D2D1::ColorF(D2D1::ColorF::Black));
        Painter painter;
        painter.BeginFrame(dc, &UiText(), 1.0f);
        painter.SetLumaText(bridge.Enabled() ? &bridge : nullptr);
        DrawControlTree(painter, theme, &table);
        painter.EndFrame();
        Check(target.EndDraw(), "table paint stability enddraw");
        std::vector<uint8_t> pixels;
        Check(target.ReadBack(pixels), "table paint stability readback");
        return pixels;
    };
    const auto original = render();
    table.Arrange({90.0f, 50.0f, 360.0f, 100.0f});
    const auto moved = render();
    bool translated = original.size() == moved.size() && !original.empty();
    for (int y = 0; translated && y < 100; ++y) {
        for (int x = 0; x < 360; ++x) {
            const size_t old_at = (static_cast<size_t>(y + 10) * 480 + x + 10) * 4;
            const size_t new_at = (static_cast<size_t>(y + 50) * 480 + x + 90) * 4;
            if (std::memcmp(original.data() + old_at, moved.data() + new_at, 4) != 0) {
                translated = false;
                break;
            }
        }
    }
    Check(translated, "table same-size move refreshes cached drawing position");
    for (const float x : {40.0f, 160.0f, 359.0f}) {
        table.OnMouseMove({x, 45.0f}, 0);
        const auto hovered = render();
        bool same_ink = hovered.size() == moved.size() && !moved.empty();
        // 悬停底色影响 AA 边缘，逐单元格比较字形包围盒；禁止位置或末尾数字被裁。
        for (int col = 0; same_ink && col < 3; ++col) {
            std::array<int, 4> before{480, 180, -1, -1};
            std::array<int, 4> after{480, 180, -1, -1};
            for (int y = 84; y < 110; ++y) {
                for (int px = 102 + col * 120; px < 198 + col * 120; ++px) {
                    const size_t at = (static_cast<size_t>(y) * 480 + px) * 4;
                    for (int image = 0; image < 2; ++image) {
                        if ((image ? hovered : moved)[at] <= 180) continue;
                        auto& box = image ? after : before;
                        box[0] = std::min(box[0], px);
                        box[1] = std::min(box[1], y);
                        box[2] = std::max(box[2], px);
                        box[3] = std::max(box[3], y);
                    }
                }
            }
            same_ink = before[2] >= before[0] && after[2] >= after[0];
            for (size_t edge = 0; edge < before.size(); ++edge)
                same_ink = same_ink && std::abs(before[edge] - after[edge]) <= 1;
        }
        Check(same_ink, "table hover preserves mixed-font text positions");
        bool untouched = hovered.size() == moved.size() && !moved.empty();
        for (int y = 112; untouched && y < 142; ++y) {
            const size_t at = (static_cast<size_t>(y) * 480 + 90) * 4;
            untouched = std::memcmp(moved.data() + at, hovered.data() + at, 360 * 4) == 0;
        }
        Check(untouched, "table hover leaves other row pixels unchanged");
        Check(table.ColumnAt(119.0f) == 0 && table.ColumnAt(120.0f) == 1 &&
                  table.ColumnAt(240.0f) == 2 && table.ColumnAt(359.0f) == 2,
              "table hover preserves flex column boundaries");
    }
    Check(target.SavePNG(L"lumen_visual_table_stability.png"), "table stability save png");
    bridge.Shutdown();
    target.Shutdown();
}

void TestLumaTextAlignmentCache() {
#if defined(LUMEN_HAS_LUMATEXT)
    OffscreenRenderer target;
    if (!target.Init(144, 48)) {
        Check(false, "text alignment target init");
        return;
    }
    IDWriteTextFormat* format = UiText().Format(TextRole::Caption);
    const DWRITE_TEXT_ALIGNMENT alignments[] = {DWRITE_TEXT_ALIGNMENT_LEADING,
                                               DWRITE_TEXT_ALIGNMENT_CENTER,
                                               DWRITE_TEXT_ALIGNMENT_TRAILING};
    // 物理 bounds 完全相同，隔离 Painter 墨迹外扩，确保仅对齐方式不同。
    for (int reverse = 0; reverse < 2; ++reverse) {
        LumaTextBridge bridge;
        auto* dc = target.BeginDraw();
        const bool ready = format && bridge.Init(UiText().Factory(), dc);
        Check(target.EndDraw() && ready, "text alignment bridge init");
        if (!ready) break;
        for (int warm = 0; warm < 2; ++warm) {
            int left[3] = {144, 144, 144};
            for (int step = 0; step < 3; ++step) {
                const int index = reverse ? 2 - step : step;
                dc = target.BeginDraw();
                dc->Clear(D2D1::ColorF(D2D1::ColorF::Black));
                const bool drawn = bridge.Draw(L"28", format, {12.0f, 4.0f, 132.0f, 36.0f},
                                                 D2D1::ColorF(D2D1::ColorF::White),
                                                 D2D1::ColorF(D2D1::ColorF::Black), 1.0f,
                                                 alignments[index]);
                const bool ended = target.EndDraw();
                std::vector<uint8_t> pixels;
                const bool read = target.ReadBack(pixels);
                Check(drawn && ended && read, "text alignment draw/readback");
                if (!drawn || !ended || !read) continue;
                for (int y = 4; y < 36; ++y) {
                    for (int x = 12; x < 132; ++x) {
                        if (pixels[(static_cast<size_t>(y) * 144 + x) * 4] > 64)
                            left[index] = std::min(left[index], x - 12);
                    }
                }
            }
            Check(left[0] < 8 && left[1] > 40 && left[1] < 65 && left[2] > 90 &&
                      left[2] < 120,
                  reverse ? "text alignment reverse order preserves positions"
                          : "text alignment forward order preserves positions");
        }
        Check(bridge.Stats().surface_cache_hits >= 3, "text alignment warm cache reused");
        bridge.Shutdown();
    }
    target.Shutdown();
#endif
}

// 宿主模式循环：模拟 .arx 卸载/重载——Shutdown 后必须能重新注册窗口类、重建文本服务。
void TestNativeCallbacks() {
    Window window(WindowSpec{.title = L"callback-gate", .size = {240.0f, 120.0f}});
    const HWND hwnd = static_cast<HWND>(window.NativeHandle());
    Check(!App::HasActiveCallbacks(), "idle native window permits begin-cleanup");
    constexpr UINT outer = WM_APP + 701, inner = WM_APP + 702;
    bool nested = false, destroyed = false;
    auto connection = window.BindNativeMessage([&](std::uint32_t msg, std::uintptr_t, std::intptr_t) {
        if (msg == inner) {
            nested = App::HasActiveCallbacks() && !App::CanShutdown();
        } else if (msg == outer) {
            SendMessageW(hwnd, inner, 0, 0);
            Check(nested && App::HasActiveCallbacks(), "nested WndProc preserves outer callback gate");
            bool rejected = false;
            try { App::Shutdown(); } catch (const std::runtime_error&) { rejected = true; }
            Check(rejected && IsWindow(hwnd), "reentrant shutdown fails before destroying resources");
        } else if (msg == WM_NCDESTROY) {
            destroyed = App::HasActiveCallbacks();
        }
    });
    SendMessageW(hwnd, outer, 0, 0);
    Check(!App::HasActiveCallbacks(), "WndProc callback gate drains on return");
    // TIMERPROC 直接由模态消息泵派发，回调时没有 LUMEN WndProc 栈。
    auto probe = [](HWND, UINT, UINT_PTR id, DWORD) {
        KillTimer(nullptr, id);
        Check(App::HasActiveCallbacks() && !App::CanShutdown(), "native session blocks cleanup between WndProc calls");
        HWND popup = FindWindowW(L"lumen_popup", nullptr);
        if (!popup) popup = FindWindowW(L"lumen_menu", nullptr);
        Check(popup != nullptr, "native session timer finds popup");
        if (popup) PostMessageW(popup, WM_KEYDOWN, VK_ESCAPE, 0);
    };
    window.Show();
    std::vector<MenuItem> items(1);
    items[0].text = L"Callback gate";
    const UINT_PTR menu_timer = SetTimer(nullptr, 0, 30, probe);
    Check(menu_timer != 0, "menu gate timer created");
    if (menu_timer) {
        static const Theme theme{};
        MenuWindow::Show(hwnd, items, POINT{120, 120}, theme, 1.0f);
        KillTimer(nullptr, menu_timer);
        Check(!App::HasActiveCallbacks(), "menu session gate drains");
    }
    StackPanel content;
    content.Add<Label>(L"Callback gate");
    const UINT_PTR popup_timer = SetTimer(nullptr, 0, 30, probe);
    Check(popup_timer != 0, "popup gate timer created");
    if (popup_timer) {
        window.ShowPopup(content, nullptr, 240.0f);
        KillTimer(nullptr, popup_timer);
        Check(!App::HasActiveCallbacks(), "popup session gate drains");
    }
    DestroyWindow(hwnd);
    Check(destroyed && !App::HasActiveCallbacks(), "NCDESTROY stays gated until native return");
    Check(App::CanShutdown(), "OLE targets released after window destruction");
}

void TestHostCycle() {
    auto pump = [](HWND hwnd) {
        MSG msg{};
        while (PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    };
    // 之前的非宿主测试窗口关闭时已投过 WM_QUIT，先清空线程队列。
    for (MSG drain{}; PeekMessageW(&drain, nullptr, 0, 0, PM_REMOVE);) {}
    App::HostMode(true);
    Check(App::HostMode(), "host mode flag");
    TestNativeCallbacks();
    // 硬件设备若在循环中退化到 WARP，说明窗口开关泄漏了 D3D/DComp 设备链。
    int warp_falls = 0;
    SetLogSink([&warp_falls](LogLevel level, std::wstring_view text) {
        if (level == LogLevel::Warn || level == LogLevel::Error)
            std::printf("[host lifecycle] %.*ls\n", static_cast<int>(text.size()), text.data());
        if (text.find(L"WARP") != std::wstring_view::npos) ++warp_falls;
    });
    for (int i = 0; i < 12; ++i) {
        try { App::Shutdown(); }
        catch (const std::exception& ex) {
            std::printf("[FAIL] host shutdown: %s\n", ex.what());
            ++g_failures;
            SetLogSink(nullptr);
            App::HostMode(false);
            return;
        }
        WNDCLASSEXW retired{};
        retired.cbSize = sizeof(retired);
        Check(!GetClassInfoExW(GetModuleHandleW(nullptr), L"lumen_popup", &retired),
              "host shutdown unregisters popup class");
        // 独立测试进程没有其他字体库使用者，关闭后自有引用都应归还。
        Check(!GetModuleHandleW(L"lumatext.dll") && !GetModuleHandleW(L"lumatextd.dll"),
              "host shutdown releases LumaText module");
        Window window(WindowSpec{.title = L"host-cycle", .size = {240.0f, 120.0f}});
        auto& label = window.Root().Add<Label>(L"host");
        window.LayoutNow();
        HWND hwnd = static_cast<HWND>(window.NativeHandle());
        Check(hwnd != nullptr, "host cycle window created");
        Check(GetModuleHandleW(L"lumatext.dll") || GetModuleHandleW(L"lumatextd.dll"),
              "host cycle exercises LumaText load");
        Check(label.AbsoluteBounds().w > 0.0f, "host cycle window laid out");
        wchar_t cls[32]{};
        GetClassNameW(hwnd, cls, 32);
        Check(std::wcscmp(cls, L"lumen_window") == 0, "host cycle class name");
        window.Close();
        pump(hwnd);
        Check(window.Closed(), "host cycle window closed");
        Check(window.NativeHandle() == nullptr, "closed window forgets native handle");
        // 宿主模式下最后一个窗口关闭不得投 WM_QUIT（会把宿主消息泵带走）。
        MSG quit{};
        Check(!PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_NOREMOVE),
              "host mode never posts WM_QUIT");
    }
    {
        // owner：Z 序压在所有者之上。
        Window owner(WindowSpec{.title = L"host-owner", .size = {240.0f, 120.0f}});
        Window owned(WindowSpec{.title = L"host-owned", .size = {200.0f, 100.0f},
                                .owner = owner.NativeHandle()});
        Check(GetWindow(static_cast<HWND>(owned.NativeHandle()), GW_OWNER) ==
                  static_cast<HWND>(owner.NativeHandle()),
              "WindowSpec.owner sets owner");
        HWND owner_hwnd = static_cast<HWND>(owner.NativeHandle());
        owner.Close();
        pump(owner_hwnd);
        Check(owner.NativeHandle() == nullptr, "destroyed owner forgets native handle");
        Check(owned.NativeHandle() == nullptr, "owner destruction clears owned handle");
    }
    for (bool compose_to_frame : {false, true}) {
        HWND shell = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, L"STATIC", L"native-shell", WS_OVERLAPPEDWINDOW,
                                      0, 0, 400, 300, nullptr, nullptr, nullptr, nullptr);
        Check(shell != nullptr, "embedded native shell created");
        Window child(WindowSpec{.title = L"embedded", .size = {240.0f, 120.0f},
                                .parent = shell, .frameTarget = shell, .composeToFrame = compose_to_frame,
                                .cornerRadius = compose_to_frame ? 16.0f : 0.0f});
        child.Root().Add<Label>(L"Embedded host animation");
        HWND hwnd = static_cast<HWND>(child.NativeHandle());
        const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        if (compose_to_frame) {
            RECT input_rect{};
            GetWindowRect(hwnd, &input_rect);
            Check(SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(input_rect.left, input_rect.top)) == HTTRANSPARENT,
                  "custom rounded corner passes input through");
            Check(SendMessageW(hwnd, WM_NCHITTEST, 0,
                      MAKELPARAM((input_rect.left + input_rect.right) / 2, input_rect.top)) != HTTRANSPARENT,
                  "custom rounded straight edge retains input");
        }
        Check((style & WS_CHILD) != 0 && (style & WS_POPUP) == 0,
              "embedded window is created as a child");
        Check(GetParent(hwnd) == shell && child.TitleBar() != nullptr,
              "embedded window retains client title bar and parent");
        Check(AreDpiAwarenessContextsEqual(GetWindowDpiAwarenessContext(hwnd),
                                           GetWindowDpiAwarenessContext(shell)),
              "embedded window matches parent DPI context");
        Check((SendMessageW(hwnd, WM_GETDLGCODE, 0, 0) & DLGC_WANTALLKEYS) != 0,
              "embedded keyboard bypasses dialog defaults");
        child.MinSize({200.0f, 100.0f});
        MINMAXINFO limits{};
        SendMessageW(hwnd, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&limits));
        Check(limits.ptMinTrackSize.x >= 200 && limits.ptMinTrackSize.y >= 100,
              "embedded minimum dimensions available to shell");
        RECT before{}, after{};
        GetWindowRect(hwnd, &before);
        child.Resize({360.0f, 220.0f});
        RECT client{};
        GetClientRect(shell, &client);
        const float scale = GetDpiForWindow(hwnd) / 96.0f;
        Check(client.right == static_cast<LONG>(360.0f * scale) &&
              client.bottom == static_cast<LONG>(220.0f * scale),
              "embedded Resize targets native shell client area");
        GetWindowRect(hwnd, &after);
        Check(after.right - after.left == before.right - before.left,
              "embedded Resize leaves child layout to shell");
        RECT suggested{80, 90, 500, 400};
        const UINT dpi = GetDpiForWindow(hwnd);
        SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&suggested));
        GetWindowRect(hwnd, &after);
        Check(after.left == before.left && after.top == before.top,
              "embedded DPI change ignores top-level suggested position");
        ShowWindow(shell, SW_SHOWNOACTIVATE);
        child.Show();
        int frames = 0;
        bool run = true;
        auto animation = child.OnFrame([&](float) { ++frames; return run; });
        auto pump_for = [&](DWORD milliseconds) {
            const ULONGLONG end = GetTickCount64() + milliseconds;
            while (GetTickCount64() < end) { pump(nullptr); Sleep(1); }
        };
        pump_for(240);
        std::printf("[host frames] active=%d\n", frames);
        Check(frames > 1 && frames < 30, "host animation yields between coalesced frames");
        child.Hide();
        const int hidden_frames = frames;
        pump_for(80);
        Check(frames == hidden_frames, "hidden host animation stops");
        child.Show();
        pump_for(80);
        std::printf("[host frames] resumed=%d hidden=%d\n", frames, hidden_frames);
        Check(frames > hidden_frames, "host animation resumes after show");
        run = false;
        pump_for(80);
        const int settled_frames = frames;
        pump_for(80);
        Check(frames == settled_frames, "settled host animation stops scheduling");
        child.OnClosing([] { return false; });
        child.Close();
        pump(hwnd);
        MSG closing{};
        Check(!PeekMessageW(&closing, shell, WM_CLOSE, WM_CLOSE, PM_REMOVE),
              "embedded closing veto prevents shell close");
        child.OnClosing([] { return true; });
        child.Close();
        pump(hwnd);
        Check(PeekMessageW(&closing, shell, WM_CLOSE, WM_CLOSE, PM_REMOVE) != FALSE,
              "embedded close delegates to shell after approval");
        DestroyWindow(shell);
        Check(child.Closed(), "native shell destruction closes child");
    }
    for (int cycle = 0; cycle < 12; ++cycle) {
        HWND shell = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, L"STATIC", L"composition-shell",
                                     WS_POPUP, 0, 0, 240, 120, nullptr, nullptr, nullptr, nullptr);
        HWND input = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, L"STATIC", L"composition-input",
                                     WS_CHILD, 0, 0, 240, 120, shell, nullptr, nullptr, nullptr);
        {
            Renderer renderer;
            Check(renderer.Init(input, 240, 120, shell), "shell composition initializes");
            Check(renderer.SetCornerRadius(24.0f), "rounded clip uses 16 DIP at 150 percent");
            Renderer competing;
            Check(!competing.Init(shell, 240, 120), "composition occupies shell topmost slot");
            renderer.Resize(300, 180);
            Check(renderer.Recover(), "shell composition survives device recovery");
            Check(renderer.SetCornerRadius(32.0f), "rounded clip updates for 200 percent DPI");
            Check(renderer.SetCornerRadius(0.0f), "maximized frame removes rounded clip");
            Check(renderer.SetCornerRadius(24.0f), "restored frame restores rounded clip");
            Check(!competing.Init(shell, 240, 120), "recovery retains shell target");
            renderer.SetCompositionVisible(false);
            renderer.SetCompositionVisible(true);
            renderer.Shutdown();
            Check(competing.Init(shell, 240, 120), "shutdown releases shell composition slot");
            competing.Shutdown();
        }
        DestroyWindow(shell);
    }
    Check(warp_falls == 0, "host cycle keeps hardware D3D device");
    SetLogSink(nullptr);
    App::Shutdown();
    App::HostMode(false);
    MSG drain{};
    while (PeekMessageW(&drain, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
}

void RenderScene(const wchar_t* path) {
    OffscreenRenderer renderer;
    if (!renderer.Init(1000, 760)) {
        Check(false, "renderer init");
        return;
    }
    const Theme theme = MakeTheme();
    Scene scene;
    scene.Build();
    scene.root.Measure({1000.0f, 760.0f}, theme);
    scene.root.Arrange({0.0f, 0.0f, 1000.0f, 760.0f});

    ID2D1DeviceContext2* dc = renderer.BeginDraw();
    Painter painter;
    painter.BeginFrame(dc, &UiText(), 1.0f);
    painter.FillRect({0, 0, 1000, 760}, theme.bg);
    DrawControlTree(painter, theme, &scene.root);
    painter.EndFrame();
    Check(renderer.EndDraw(), "enddraw");

    Color corner{};
    Check(renderer.ReadPixel(3, 3, corner) && CloseTo(corner, theme.bg), "bg pixel = void black");

    if (scene.primary) {
        const Rect b = scene.primary->AbsoluteBounds();
        Color c{};
        // 采左上内边（避开居中 CJK 字墨），圆角半径内仍是 danger 填充。
        renderer.ReadPixel(static_cast<int>(b.x + 12.0f), static_cast<int>(b.y + 10.0f), c);
        Check(CloseTo(c, theme.danger), "danger button center");
        Check(c.r > c.g + 0.30f && c.r > c.b + 0.30f, "danger button uses red status color");
    }
    if (scene.checked_box) {
        const Rect b = scene.checked_box->AbsoluteBounds();
        const float box_y = b.y + (b.h - 20.0f) * 0.5f;
        Color c{};
        renderer.ReadPixel(static_cast<int>(b.x + 3.0f), static_cast<int>(box_y + 4.0f), c);
        Check(CloseTo(c, theme.accent), "checked box fill");
    }
    if (scene.on_switch) {
        const Rect b = scene.on_switch->AbsoluteBounds();
        Color c{};
        renderer.ReadPixel(static_cast<int>(b.x + 7.0f), static_cast<int>(b.y + b.h * 0.5f), c);
        Check(CloseTo(c, theme.accent), "switch on track");
    }
    if (scene.list) {
        const Rect b = scene.list->AbsoluteBounds();
        Color c{};
        renderer.ReadPixel(static_cast<int>(b.x + b.w - 40.0f), static_cast<int>(b.y + 42.0f), c);
        const Color expected = Over(theme.fill_selected, Over(theme.fill_input, theme.bg));
        Check(CloseTo(c, expected), "list selection composite");
    }
    if (scene.spot_card) {
        const Rect b = scene.spot_card->AbsoluteBounds();
        Color center{}, corner_px{};
        renderer.ReadPixel(static_cast<int>(b.x + b.w * 0.5f),
                           static_cast<int>(b.Bottom() - 5.0f), center);
        renderer.ReadPixel(static_cast<int>(b.x + 4.0f), static_cast<int>(b.y + 4.0f), corner_px);
        Check(center.r > corner_px.r + 0.02f && center.g > corner_px.g + 0.02f,
              "spotlight center brighter than corner");
        Color core{}, rim{};
        renderer.ReadPixel(static_cast<int>(b.x + b.w * 0.5f),
                           static_cast<int>(b.y + b.h * 0.5f), core);
        renderer.ReadPixel(static_cast<int>(b.x + 8.0f), static_cast<int>(b.y + b.h * 0.5f), rim);
        Check(core.r > rim.r + 0.015f, "spotlight hot core brighter than rim");
        float prev = 0.0f;
        float max_step = 0.0f;
        for (int i = 0; i < 48; ++i) {
            const float t = static_cast<float>(i) / 47.0f;
            Color sample{};
            renderer.ReadPixel(static_cast<int>(b.x + b.w * 0.5f),
                               static_cast<int>(b.y + 6.0f + t * (b.h - 12.0f)), sample);
            if (i > 0) max_step = std::max(max_step, std::fabs(sample.r - prev));
            prev = sample.r;
        }
        Check(max_step < 0.09f, "spotlight gradient no banding steps");
    }

    Check(renderer.SavePNG(path), "save png");
    renderer.Shutdown();
}

} // namespace

// 弹层/导航/轻量展示控件的像素板：骨架呼吸底色、星级填充差、头像圈+状态点、开关分割钮。
void RenderExtrasScene(const wchar_t* path) {
    OffscreenRenderer renderer;
    if (!renderer.Init(700, 1060)) {
        Check(false, "renderer init (extras)");
        return;
    }
    const Theme theme = MakeTheme();
    TestRoot root;
    root.Padding(16.0f, 16.0f).Spacing(18.0f);
    auto& row1 = root.Add<Row>().Spacing(28.0f).AlignCross(StackPanel::CrossAlign::Center);
    Skeleton* skeleton = &row1.Add<Skeleton>();
    skeleton->Lines(2);
    Avatar* avatar = &row1.Add<Avatar>();
    avatar->PresenceState(Avatar::Presence::Online);
    row1.Add<Avatar>(L"林").Diameter(40.0f).PresenceState(Avatar::Presence::Away);
    Rating* rating = &row1.Add<Rating>();
    rating->Value(2.0);

    auto& row2 = root.Add<Row>().Spacing(28.0f).AlignCross(StackPanel::CrossAlign::Center);
    NumberBox* number = &row2.Add<NumberBox>();
    number->Range(0.0, 100.0).Value(42.0);
    auto& crumb = row2.Add<Breadcrumb>();
    crumb.AddItem(L"库").AddItem(L"项目").AddItem(L"设置");
    crumb.SelectedIndex(0);
    SplitButton* toggle = &row2.Add<SplitButton>(L"自动部署").Toggle(true).Checked(true);

    // 图标字形核对行（kSparkle 码点有效性人工核对 PNG）。
    auto& row3 = root.Add<Row>().Spacing(16.0f);
    for (const wchar_t* glyph :
         {icon::kSparkle, icon::kCalendar, icon::kClock, icon::kPlay, icon::kPause}) {
        row3.Add<IconView>(glyph).Box(24.0f).IconSize(16.0f);
    }
    InfoBadge* info_dot = &row3.Add<InfoBadge>();
    info_dot->Dot();
    row3.Add<InfoBadge>(3);
    row3.Add<IconView>(icon::kBell).Box(24.0f).IconSize(16.0f).Badge(InfoBadgeData::Dot());

    // 步骤条：当前步为空心圆，核对连接线不伸入圆内。
    auto& row4 = root.Add<Row>();
    Stepper* steps = &row4.Add<Stepper>();
    steps->AddStep(L"配置").AddStep(L"构建").AddStep(L"发布").AddStep(L"验证");
    steps->Current(2);

    // 分页器：左垫 60 与原点拉开距离，核对墨迹只落在自身 AbsoluteBounds 内。
    auto& row5 = root.Add<Row>().Padding(60.0f, 0.0f);
    Pagination* pager = &row5.Add<Pagination>();
    pager->PageCount(12).Current(1);

    // 菜单栏：标题行盒须在 bar 内垂直居中（DrawText 顶对齐易贴顶）。
    auto& row6 = root.Add<Row>();
    MenuBar* bar = &row6.Add<MenuBar>();
    bar->AddMenu(L"File", Menu{}).AddMenu(L"Edit", Menu{});

    // 聚光卡内的虚拟化表格：缓冲行子控的避光垫不得露出表体（悬停剪影回归）。
    static std::vector<std::wstring> g_ghost_notes(24);
    auto& ghost_card = root.Add<TestSpotlightCard>();
    ghost_card.Card(Panel::CardStyle::Lumen, 16.0f);
    auto& ghost_table = ghost_card.Add<Table>();
    ghost_table.RowHeight(32.0f);
    ghost_table.AddColumn(L"A", 120.0f);
    ghost_table.AddColumn(L"Note");
    ghost_table.AddColumn(L"Run", 80.0f);
    ghost_table.CellText([](size_t row, size_t col, std::wstring& out) {
        if (col == 0) out = L"cell " + std::to_wstring(row);
        else out.clear();
    });
    ghost_table.BindTextBox(
        1, [](size_t row) { return row < g_ghost_notes.size() ? g_ghost_notes[row] : std::wstring{}; },
        [](size_t row, std::wstring value) {
            if (row < g_ghost_notes.size()) g_ghost_notes[row] = std::move(value);
        });
    ghost_table.BindButton(2, L"Run", [](size_t) {});
    ghost_table.RowCount(24);
    ghost_card.ForceSpotlight();

    // 轮播：页面必须实际渲染内容（回归：Measure 跳过子页导致整页空白）。
    auto& rider = root.Add<Carousel>();
    rider.Card(Panel::CardStyle::Input, 12.0f);
    auto& rider_page = rider.AddPage<StackPanel>();
    rider_page.Padding(16.0f, 12.0f)
        .Spacing(6.0f)
        .AlignCross(StackPanel::CrossAlign::Center)
        .AlignMain(StackPanel::MainAlign::Center);
    rider_page.Add<Label>(L"轮播页内容", TextRole::Body);
    rider_page.Add<Label>(L"第二行", TextRole::Caption).Secondary(true);

    // SplitView：两栏内容必须实际渲染（回归：Measure 跳过 + 绝对坐标双重偏移）。
    auto& shell = root.Add<SplitView>();
    shell.PaneLength(150.0f);
    shell.Pane().Padding(12.0f, 10.0f).Spacing(6.0f);
    shell.Pane().Add<Label>(L"总览", TextRole::Caption);
    shell.Content().Padding(16.0f, 12.0f);
    shell.Content().Add<Label>(L"主区内容", TextRole::Caption);
    StackPanel& shell_pane = shell.Pane();
    StackPanel& shell_content = shell.Content();

    root.Measure({700.0f, 1060.0f}, theme);
    root.Arrange({0.0f, 0.0f, 700.0f, 1060.0f});

    ID2D1DeviceContext2* dc = renderer.BeginDraw();
    Painter painter;
    painter.BeginFrame(dc, &UiText(), 1.0f);
    painter.FillRect({0, 0, 700, 1060}, theme.bg);
    DrawControlTree(painter, theme, &root);
    painter.EndFrame();
    Check(renderer.EndDraw(), "enddraw (extras)");

    std::vector<uint8_t> snapshot;
    Check(renderer.ReadBack(snapshot), "readback (extras)");
    const auto read_pixel = [&](int x, int y, Color& out) {
        if (x < 0 || y < 0 || x >= renderer.Width() || y >= renderer.Height() ||
            snapshot.size() != static_cast<size_t>(renderer.Width()) * renderer.Height() * 4) {
            return false;
        }
        const uint8_t* px = snapshot.data() +
                            (static_cast<size_t>(y) * renderer.Width() + x) * 4;
        out = {px[2] / 255.0f, px[1] / 255.0f, px[0] / 255.0f, px[3] / 255.0f};
        return true;
    };

    Color c{};
    // 骨架首行：呼吸相位 0 → fill_hover 与 fill_selected 的中点；扫光此时停在左侧界外。
    const Rect sk = skeleton->AbsoluteBounds();
    read_pixel(static_cast<int>(sk.x + sk.w * 0.5f), static_cast<int>(sk.y + 3.0f), c);
    Color expected = theme.fill_hover;
    expected.a = (theme.fill_hover.a + theme.fill_selected.a) * 0.5f;
    Check(CloseTo(c, Over(expected, theme.bg), 0.02f), "skeleton breathing fill");

    // 星级：填充星中心亮于未填充描边星中心
    const Rect rt = rating->AbsoluteBounds();
    Color filled{};
    Color dim{};
    read_pixel(static_cast<int>(rt.x + 10.0f), static_cast<int>(rt.y + 10.0f), filled);
    read_pixel(static_cast<int>(rt.x + 4 * 24 + 10.0f), static_cast<int>(rt.y + 10.0f), dim);
    Check(filled.r > dim.r + 0.10f && filled.g > dim.g + 0.10f, "rating filled vs dim star");

    // 头像：圈底合成 + Online 状态点亮度
    const Rect av = avatar->AbsoluteBounds();
    read_pixel(static_cast<int>(av.x + 10.0f), static_cast<int>(av.y + 8.0f), c);
    Check(CloseTo(c, Over(theme.fill_input_hover, theme.bg)), "avatar circle fill");
    read_pixel(static_cast<int>(av.x + 28.76f), static_cast<int>(av.y + 28.76f), c);
    Check(c.g > theme.fill_input_hover.g + 0.3f && c.g > c.r + 0.3f, "avatar online presence dot green");

    // InfoBadge 圆点：accent 实心（白），避免采到计数胶囊上的黑字。
    {
        const Rect ib = info_dot->AbsoluteBounds();
        read_pixel(static_cast<int>(ib.x + ib.w * 0.5f), static_cast<int>(ib.y + ib.h * 0.5f), c);
        Check(c.r > 0.70f && c.g > 0.70f && c.b > 0.70f, "info badge dot fill");
    }

    // 开关分割钮选中态：主区垫 fill_selected
    const Rect sp = toggle->AbsoluteBounds();
    read_pixel(static_cast<int>(sp.x + 10.0f), static_cast<int>(sp.y + 8.0f), c);
    Check(CloseTo(c, Over(theme.fill_selected, Over(theme.fill_input, theme.bg))),
          "toggle split checked wash");

    // 步骤条：末步空心圆内（距圆心 6px 处）必须是背景；线段中点必须是分隔线亮色。
    {
        const float tw = UiText().MeasureText(L"验证", TextRole::Caption).w;
        const float stride = 28.0f + tw + 16.0f + 24.0f;   // 内容宽(20圆+8距+文本) + 留白 + 净线长
        const Rect st = steps->AbsoluteBounds();
        const float cy = st.y + 13.0f;                     // kCircle*0.5 + 3
        const float cx3 = st.x + 8.0f + 3.0f * stride + 10.0f;   // “验证”圆心
        read_pixel(static_cast<int>(cx3 - 6.0f), static_cast<int>(cy), c);
        Check(CloseTo(c, theme.bg), "stepper line stays outside circle");
        // 线段 = 上一步文字末尾 + 8 留白起，净长 24；取中点。
        const float seg_x0 = st.x + 8.0f + 2.0f * stride + 28.0f + tw + 8.0f;
        Color line{};
        read_pixel(static_cast<int>(seg_x0 + 12.0f), static_cast<int>(cy), line);
        Check(line.r > theme.bg.r + 0.02f, "stepper connector visible");
        // 线不得穿过标题文字：文字末尾与线起点之间的空隙必须是背景。
        read_pixel(static_cast<int>(seg_x0 - 4.0f), static_cast<int>(cy), c);
        Check(CloseTo(c, theme.bg), "stepper line clear of label text");
    }

    // 分页器：绘制必须落在自身矩形内（回归：曾按局部坐标画到窗口原点、盖住别的控件）。
    {
        const Rect pg = pager->AbsoluteBounds();
        const int cy = static_cast<int>(pg.y + pg.h * 0.5f);
        int ink = 0;
        for (int dx = 0; dx < 48; ++dx) {
            read_pixel(static_cast<int>(pg.x) + dx, cy, c);
            if (c.r > 0.06f) ++ink;
        }
        Check(ink >= 4, "pagination renders inside its bounds");
        bool stray = false;
        for (int dx = 30; dx > 20; --dx) {
            read_pixel(static_cast<int>(pg.x) - dx, cy, c);
            if (c.r > 0.02f) stray = true;
        }
        Check(!stray, "pagination ink stays right of its bounds");
    }

    // 菜单栏：扫“File”标题墨迹的上下留白，须对称（垂直居中）。
    {
        const Rect mb = bar->AbsoluteBounds();
        int top = -1;
        int bottom = -1;
        for (int y = static_cast<int>(mb.y); y < static_cast<int>(mb.Bottom()); ++y) {
            for (int x = static_cast<int>(mb.x) + 10; x < static_cast<int>(mb.x) + 34; ++x) {
                read_pixel(x, y, c);
                if (c.r > 0.3f) {
                    if (top < 0) top = y;
                    bottom = y;
                    break;
                }
            }
        }
        const int gap_top = top - static_cast<int>(mb.y);
        const int gap_bottom = static_cast<int>(mb.Bottom()) - 1 - bottom;
        Check(top > 0 && gap_top >= 0 && gap_bottom >= 0 && (gap_top - gap_bottom <= 3) &&
                  (gap_bottom - gap_top <= 3),
              "menubar title vertically centered");

        // “File” 墨迹在自身 slot（x=+10, w=文本+20）内须水平居中。
        const float slot_x = mb.x + 10.0f;
        const float slot_right = slot_x + UiText().MeasureText(L"File", TextRole::Body).w + 20.0f;
        int ink_min = -1;
        int ink_max = -1;
        for (int x = static_cast<int>(mb.x); x < static_cast<int>(slot_right) + 2; ++x) {
            for (int y = top; y <= bottom; ++y) {
                read_pixel(x, y, c);
                if (c.r > 0.3f) {
                    if (ink_min < 0) ink_min = x;
                    ink_max = x;
                    break;
                }
            }
        }
        const int gap_left = ink_min - static_cast<int>(slot_x);
        const int gap_right = static_cast<int>(slot_right) - ink_max;
        Check(ink_min > 0 && gap_left >= 4 && gap_right >= 4 &&
                  (gap_left - gap_right <= 4) && (gap_right - gap_left <= 4),
              "menubar title horizontally centered");
    }

    // 聚光卡内表格：缓冲行的避光垫必须被表体视口裁掉（悬停时不再露出控件剪影）。
    {
        const Rect gt = ghost_table.AbsoluteBounds();
        const float ghost_x = gt.x + 120.0f + (gt.w - 200.0f) * 0.5f;   // Note 弹性列中心
        bool stray = false;
        for (int dy = 6; dy < 60; ++dy) {
            const int y = static_cast<int>(gt.Bottom()) + dy;
            Color px{};
            Color ref{};
            read_pixel(static_cast<int>(ghost_x), y, px);
            read_pixel(40, y, ref);
            if (std::fabs(px.r - ref.r) > 0.01f || std::fabs(px.g - ref.g) > 0.01f ||
                std::fabs(px.b - ref.b) > 0.01f) {
                stray = true;
            }
        }
        Check(!stray, "spotlight pads clipped to table body");
    }

    // 轮播页内容：卡内（避开边缘）必须有文字墨迹。
    {
        const Rect rc = rider.AbsoluteBounds();
        int ink = 0;
        for (int y = static_cast<int>(rc.y) + 12; y < static_cast<int>(rc.Bottom()) - 16; ++y) {
            for (int x = static_cast<int>(rc.x) + 20; x < static_cast<int>(rc.Right()) - 20; ++x) {
                read_pixel(x, y, c);
                if (c.r > 0.25f) ++ink;
            }
        }
        Check(ink > 20, "carousel page renders content");
    }

    // SplitView：侧栏与主区都须在各自矩形内渲染出文字墨迹。
    {
        auto any_ink = [&](const Rect& r) {
            for (int y = static_cast<int>(r.y) + 4; y < static_cast<int>(r.Bottom()) - 4; ++y) {
                for (int x = static_cast<int>(r.x) + 4; x < static_cast<int>(r.Right()) - 4; ++x) {
                    read_pixel(x, y, c);
                    if (c.r > 0.25f) return true;
                }
            }
            return false;
        };
        Check(any_ink(shell_pane.AbsoluteBounds().Inset(4.0f, 4.0f)),
              "splitview pane renders content");
        Check(any_ink(shell_content.AbsoluteBounds().Inset(4.0f, 4.0f)),
              "splitview content renders");
    }

    Check(renderer.SavePNG(path), "save png (extras)");
    renderer.Shutdown();
}

void TestGlowPrimitives() {
    OffscreenRenderer renderer;
    if (!renderer.Init(420, 240)) {
        Check(false, "glow renderer init");
        return;
    }
    const Theme theme = MakeTheme();
    TestRoot root;
    root.Padding(24.0f, 24.0f).Spacing(16.0f);
    auto& card = root.Add<StackPanel>();
    card.Card(Panel::CardStyle::Input, 12.0f);
    card.Padding(16.0f, 18.0f);
    card.Add<Label>(L" ");
    auto& row = root.Add<Row>().Spacing(12.0f);
    row.Add<Button>(L"标准");
    Button* primary = &row.Add<Button>(L"主要", ButtonKind::Primary);
    auto& lumen = root.Add<TestSpotlightCard>();
    lumen.Card(Panel::CardStyle::Lumen, 14.0f);
    lumen.Padding(16.0f, 20.0f);
    lumen.Add<Label>(L"SPOT");
    lumen.ForceSpotlight();
    root.Measure({420.0f, 240.0f}, theme);
    root.Arrange({0.0f, 0.0f, 420.0f, 240.0f});

    ID2D1DeviceContext2* dc = renderer.BeginDraw();
    Painter painter;
    painter.BeginFrame(dc, &UiText(), 1.0f);
    painter.FillRect({0.0f, 0.0f, 420.0f, 240.0f}, theme.bg);
    DrawControlTree(painter, theme, &root);
    painter.EndFrame();
    Check(renderer.EndDraw(), "glow enddraw");

    const Rect cb = card.AbsoluteBounds();
    Color top{}, bot{};
    renderer.ReadPixel(static_cast<int>(cb.x + cb.w * 0.5f), static_cast<int>(cb.y + 1.0f), top);
    renderer.ReadPixel(static_cast<int>(cb.x + cb.w * 0.5f), static_cast<int>(cb.Bottom() - 1.0f),
                       bot);
    Check(top.r > bot.r + 0.008f, "card top specular brighter than bottom shade");

    const Rect pb = primary->AbsoluteBounds();
    Color halo{};
    renderer.ReadPixel(static_cast<int>(pb.x - 4.0f), static_cast<int>(pb.y + pb.h * 0.5f), halo);
    Check(halo.r > theme.bg.r + 0.006f, "primary dual-layer glow");

    {
        ID2D1DeviceContext2* dc2 = renderer.BeginDraw();
        Painter p2;
        p2.BeginFrame(dc2, &UiText(), 1.0f);
        p2.FillRect({0.0f, 0.0f, 420.0f, 240.0f}, theme.bg);
        const Rect elevated{80.0f, 80.0f, 160.0f, 72.0f};
        DrawElevated(p2, theme, elevated, 12.0f, Elevation::Overlay, theme.bg);
        p2.EndFrame();
        Check(renderer.EndDraw(), "elevated glow enddraw");
        Color ear{};
        renderer.ReadPixel(static_cast<int>(elevated.x + 2), static_cast<int>(elevated.y + 2), ear);
        Check(ear.r > theme.bg.r + 0.004f, "elevated glow wraps corner");
    }

    renderer.Shutdown();
}

void TestAcrylic() {
    OffscreenRenderer renderer;
    if (!renderer.Init(240, 160)) {
        Check(false, "acrylic renderer init");
        return;
    }
    ID2D1DeviceContext2* dc = renderer.BeginDraw();
    Painter painter;
    painter.BeginFrame(dc, &UiText(), 1.0f);
    painter.FillRect({0.0f, 0.0f, 240.0f, 160.0f}, Color{0.0f, 0.0f, 0.0f, 1.0f});
    painter.FillRoundedRect({80.0f, 50.0f, 80.0f, 60.0f}, 8.0f, Color{1.0f, 1.0f, 1.0f, 1.0f});
    Check(painter.CaptureAcrylic(), "acrylic capture");
    painter.DrawAcrylic({0.0f, 0.0f, 240.0f, 160.0f}, 14.0f, 0.22f);
    painter.EndFrame();
    Check(renderer.EndDraw(), "acrylic enddraw");
    Color core{}, bleed{}, corner{};
    renderer.ReadPixel(120, 80, core);
    renderer.ReadPixel(68, 80, bleed);
    renderer.ReadPixel(6, 6, corner);
    Check(core.r > bleed.r + 0.04f, "acrylic core brighter than halo");
    Check(bleed.r > corner.r + 0.008f, "acrylic blur bleeds past edge");
    renderer.Shutdown();
}

void TestChoreography() {
    {
        TestRoot root;
        auto& host = root.Add<PageHost>();
        host.Page(L"a").Add<Panel>().SetBounds({0.0f, 0.0f, 12.0f, 10.0f});
        host.Page(L"b").Add<Panel>().SetBounds({0.0f, 0.0f, 12.0f, 20.0f});
        Check(host.Current() == L"a", "pagehost default current");
        Check(host.Child(0).Visible() && !host.Child(1).Visible(), "pagehost first visible");
        host.Show(L"b");
        Check(host.Current() == L"b", "pagehost show current");
        Check(host.Direction() == 1, "pagehost direction down");
        Check(!host.Child(0).Visible() && host.Child(1).Visible(), "pagehost snap hides old");
        host.Show(L"a");
        Check(host.Direction() == -1, "pagehost direction up");
        Check(host.Child(0).Visible() && !host.Child(1).Visible(), "pagehost snap back");
    }
}

void TestDefaultChrome() {
    Window window(L"default-chrome");
    Check(window.TitleBar() != nullptr, "Window(title) default Client frame");
    Check(window.Backdrop() == Backdrop::All, "Window(title) default Backdrop::All");
    window.LayoutNow();
    TitleBar* bar = window.TitleBar();
    Check(bar && bar->Title() == L"default-chrome", "default title bar caption");

    OffscreenRenderer renderer;
    constexpr int kW = 960;
    constexpr int kH = 200;
    if (!renderer.Init(kW, kH)) {
        Check(false, "default chrome renderer init");
        return;
    }
    const Theme& theme = window.VisualTheme();
    ID2D1DeviceContext2* dc = renderer.BeginDraw();
    Painter painter;
    painter.BeginFrame(dc, &UiText(), 1.0f);
    const Rect client{0.0f, 0.0f, static_cast<float>(kW), static_cast<float>(kH)};
    painter.FillRect(client, theme.bg);
    painter.FillRectRadial(client, {client.w * 0.5f, client.h * 0.15f}, client.w * 0.9f,
                           theme.ambient_flare,
                           Color{theme.ambient_flare.r, theme.ambient_flare.g,
                                 theme.ambient_flare.b, 0.0f},
                           0.7f);
    if (bar) DrawControlTree(painter, theme, bar);
    painter.EndFrame();
    Check(renderer.EndDraw(), "default chrome enddraw");

    Color flare{}, void_px{};
    renderer.ReadPixel(kW / 2, 80, flare);
    renderer.ReadPixel(8, kH - 8, void_px);
    Check(flare.r > void_px.r + 0.004f, "default chrome ambient flare at top");

    int ink = 0;
    for (int x = 16; x < kW - 16; ++x) {
        Color px{};
        renderer.ReadPixel(x, 20, px);
        if (px.r > 0.25f) ++ink;
    }
    Check(ink > 8, "default chrome title bar caption brightness");
    renderer.Shutdown();

    // CaptionVisible(false)：标题区收起为 0，Root 从客户区顶端开始；恢复后回到原高度。
    // 仅 TitleBar::Visible(false) 仍保留 40 DIP 拖动区（既有语义不变）。
    Check(window.CaptionVisible(), "caption visible by default");
    const float with_caption = window.MeasureContent(400.0f).h;
    const float caption_h = bar ? bar->Height() : 0.0f;
    window.CaptionVisible(false);
    window.LayoutNow();
    Check(!window.CaptionVisible() && bar && !bar->Visible() &&
              Near(window.MeasureContent(400.0f).h, with_caption - caption_h) &&
              Near(window.Root().AbsoluteBounds().y, 0.0f),
          "CaptionVisible(false) collapses caption to 0");
    window.CaptionVisible(true);
    window.LayoutNow();
    Check(window.CaptionVisible() && bar && bar->Visible() &&
              Near(window.MeasureContent(400.0f).h, with_caption) &&
              Near(window.Root().AbsoluteBounds().y, caption_h),
          "CaptionVisible(true) restores caption height");
    if (bar) bar->Visible(false);
    window.LayoutNow();
    Check(Near(window.MeasureContent(400.0f).h, with_caption),
          "hidden TitleBar alone keeps caption strip");
    if (bar) bar->Visible(true);

    Window system(L"system-chrome", {320.0f, 200.0f});
    Check(system.TitleBar() == nullptr, "three-arg Window keeps System frame");
    Check(system.Backdrop() == Backdrop::None, "three-arg Window keeps Backdrop::None");
}

void TestInjectedInput() {
    Window window(L"inject-input", {320.0f, 96.0f});
    auto& row = window.Root().Add<Row>().Spacing(8.0f);
    auto& first = row.Add<Button>(L"A");
    auto& second = row.Add<Button>(L"B");
    int clicks = 0;
    first.OnClick([&] { ++clicks; });
    window.LayoutNow();

    Check(window.DispatchKey(VK_TAB), "inject tab consumed");
    Check(window.Focused() == &first, "inject tab focuses first button");
    Check(window.DispatchKey(VK_TAB), "inject tab again consumed");
    Check(window.Focused() == &second, "inject tab focuses second button");

    const Rect a = first.AbsoluteBounds();
    Check(a.w > 1.0f && a.h > 1.0f, "inject layout sizes button");
    const Point center{a.x + a.w * 0.5f, a.y + a.h * 0.5f};
    window.DispatchMouseMove(center);
    Check(window.Hovered() == &first, "inject hover first button");
    window.DispatchMouseDown(center);
    window.DispatchMouseUp(center);
    Check(clicks == 1, "inject click fires");
    Check(window.Focused() == &first, "inject click focuses first button");
    window.ClearFocus();
    Check(window.Focused() == nullptr && !first.HasFocus(), "ClearFocus drops logical focus");
    first.Focus();
    Check(first.HasFocus(), "Focus restores after ClearFocus");
    first.Blur();
    Check(!first.HasFocus() && window.Focused() == nullptr, "Blur clears focused control");
}


void TestEscapeShortcut() {
    Window window(L"escape-shortcut", {400.0f, 240.0f});
    auto& text = window.Root().Add<TestTextBox>();
    auto& combo = window.Root().Add<ComboBox>();
    combo.Editable(true).Items({L"200", L"300"}).SelectedIndex(1);
    int cancelled = 0;
    int letter_shortcuts = 0;
    window.BindShortcut(L"Esc", [&] { ++cancelled; });
    window.BindShortcut(L"A", [&] { ++letter_shortcuts; });
    window.LayoutNow();
    text.Focus();
    window.DispatchKey(VK_ESCAPE);
    Check(cancelled == 1, "Escape shortcut runs from editable text focus");
    window.DispatchKey('A');
    Check(letter_shortcuts == 0, "unmodified character shortcut still yields to text input");
    text.OnImeCompose(L"ce", 2, {});
    window.DispatchKey(VK_ESCAPE);
    Check(cancelled == 1 && !text.ImeComposing(), "Escape cancels composition before window shortcut");
    window.DispatchKey(VK_ESCAPE);
    Check(cancelled == 2, "next Escape after composition runs window shortcut");
    combo.Editor().Focus();
    window.DispatchKey(VK_ESCAPE);
    Check(cancelled == 3 && combo.Text() == L"300", "Escape shortcut is not swallowed by combo editor");
    window.DispatchKey(VK_DOWN);
    Check(window.FlyoutActive(), "combo dropdown opens before Escape");
    window.DispatchKey(VK_ESCAPE);
    Check(cancelled == 3 && !window.FlyoutActive(), "Escape closes combo dropdown before window shortcut");
    TestDialog dialog;
    dialog.Title(L"Confirm").DefaultClose();
    Control* dialog_trigger = window.Focused();
    window.ShowDialog(dialog);
    window.DispatchKey(VK_ESCAPE);
    Check(cancelled == 3 && combo.Text() == L"300", "dialog Escape does not invoke window shortcut or cancel background editor");
    dialog.OnAnimate(1.0f);
    Check(!window.DialogActive(), "Escape dismisses dialog after its exit animation");
    Check(window.Focused() == dialog_trigger, "dialog Escape restores trigger focus");
    window.ShowBusy(L"Working");
    window.DispatchKey(VK_ESCAPE);
    Check(cancelled == 3, "busy overlay Escape does not invoke window shortcut");
    window.CloseBusy();
    combo.Editor().Focus();
    combo.CommitText();
    window.BindShortcut(L"Esc", {});
    combo.Text(L"200");
    window.DispatchKey(VK_ESCAPE);
    Check(cancelled == 3 && combo.Text() == L"300", "without Escape binding combo keeps local cancel behavior");
}

void TestTypedEditSafety() {
    struct Row { int quantity = 10; uint64_t serial = 7; float value = 2.0f; };
    auto model = std::make_unique<VectorModel<Row>>();
    model->Push({});
    TestTable table;
    table.Bind(*model).Column(L"Quantity", &Row::quantity, 100.0f)
        .Column(L"Serial", &Row::serial, 100.0f).Column(L"Value", &Row::value, 100.0f);
    table.CellEditEnabled(true);
    const Theme theme = MakeTheme();
    table.Measure({400.0f, 160.0f}, theme);
    table.Arrange({0.0f, 0.0f, 400.0f, 160.0f});
    table.BeginEdit(0, 0);
    Check(table.Editor() && table.Editor()->Visible(), "typed numeric editor opens");
    if (!table.Editor()) return;
    for (const auto* invalid : {L"1.5", L"2147483648", L"-2147483649"}) {
        table.Editor()->Text(invalid);
        table.Commit();
        Check(model->At(0).quantity == 10 && table.Editor()->Visible(),
              "integer edit rejects fraction and out-of-range values without closing");
    }
    table.BeginEdit(0, 1);
    Check(table.Editor()->Text() == L"-2147483649", "invalid draft survives attempted cell switch");
    table.Editor()->Text(L"2147483647");
    table.Commit();
    Check(model->At(0).quantity == 2147483647 && !table.Editor()->Visible(),
          "integer boundary commits successfully");
    table.BeginEdit(0, 1);
    for (const auto* invalid : {L"-1", L"18446744073709551616"}) {
        table.Editor()->Text(invalid);
        table.Commit();
        Check(model->At(0).serial == 7 && table.Editor()->Visible(),
              "uint64 rejects negative and exclusive upper bound before conversion");
    }
    table.Cancel();
    table.BeginEdit(0, 2);
    table.Editor()->Text(L"1e39");
    table.Commit();
    Check(model->At(0).value == 2.0f && table.Editor()->Visible(), "float member rejects overflow");
    model.reset();
    Check(table.RowCount() == 0 && !table.Editor()->Visible(), "model detach cancels active editor");
    table.RowCount(1);
    table.BeginEdit(0, 0);
    table.Editor()->Text(L"42");
    table.Commit();
    Check(table.RowCount() == 1, "detached typed callbacks do not access former source");
}

void TestHiddenAnimation() {
    struct Probe : Control {
        int ticks = 0;
        void Draw(Painter&, const Theme&) override {}
        void Start() { Animate(); }
        bool OnAnimate(float) override { ++ticks; return true; }
    };
    Window window(L"animation visibility", {240.0f, 120.0f}, Frame::System);
    auto& parent = window.Root().Add<Column>();
    auto& probe = parent.Add<Probe>();
    window.Show();
    const HWND hwnd = static_cast<HWND>(window.NativeHandle());
    auto paint = [&] { InvalidateRect(hwnd, nullptr, FALSE); UpdateWindow(hwnd); };
    probe.Start();
    paint();
    Check(probe.ticks > 0, "visible animation advances");
    parent.Visible(false);
    const int before = probe.ticks;
    paint();
    paint();
    Check(probe.ticks == before, "hidden ancestor suspends child animation");
    Check(!GetUpdateRect(hwnd, nullptr, FALSE), "hidden animation does not request another frame");
    parent.Visible(true);
    paint();
    Check(probe.ticks > before, "showing ancestor resumes suspended animation");
    ShowWindow(hwnd, SW_HIDE);
    const int hidden_window = probe.ticks;
    paint();
    Check(probe.ticks == hidden_window, "hidden window suspends control animation");
    window.Show();
    paint();
    Check(probe.ticks > hidden_window, "showing window resumes animation");
    DestroyWindow(hwnd);
    MSG quit{};
    while (PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
}

struct PopupWindows {
    HWND first = nullptr;
    int visible = 0;
};

PopupWindows FindPopupWindows() {
    PopupWindows result;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND hwnd, LPARAM data) -> BOOL {
        wchar_t name[64]{};
        GetClassNameW(hwnd, name, 64);
        if (std::wcscmp(name, L"lumen_popup") == 0 && IsWindowVisible(hwnd)) {
            auto& found = *reinterpret_cast<PopupWindows*>(data);
            if (!found.first) found.first = hwnd;
            ++found.visible;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}

void TestTableHeaderCheck() {
    TestTable table;
    std::array<bool, 3> rows{false, true, false};
    auto state = [&] {
        const auto count = std::count(rows.begin(), rows.end(), true);
        return count == 0 ? CheckState::Unchecked : count == 3 ? CheckState::Checked : CheckState::Indeterminate;
    };
    table.AddColumn(L"#", 64.0f).Frozen();
    table.AddColumn(L"On", 72.0f).CheckBox(
        [&](size_t row) { return rows[row]; }, [&](size_t row, bool value) { rows[row] = value; })
        .HeaderCheckBox(state, [&](bool value) { rows.fill(value); });
    table.RowCount(3);
    table.Arrange({10.0f, 10.0f, 300.0f, 200.0f});
    const Rect group = table.GroupHeaderContentRect(50.0f, 64.0f);
    Check(group.x == 74.0f && group.w == 236.0f, "group band content starts after frozen separator");
    OffscreenRenderer target;
    Check(target.Init(320, 220), "header checkbox render target");
    auto render_header = [&](const wchar_t* path) {
        auto* dc = target.BeginDraw();
        dc->Clear(D2D1::ColorF(D2D1::ColorF::Black));
        Painter painter;
        painter.BeginFrame(dc, &UiText(), 1.0f);
        DrawControlTree(painter, MakeTheme(), &table);
        painter.EndFrame();
        Check(target.EndDraw(), "header checkbox renders");
        std::vector<uint8_t> pixels, header;
        Check(target.ReadBack(pixels), "header checkbox readback");
        for (size_t y = 10; y < 40; ++y)
            for (size_t x = 100; x < 120; ++x)
                header.push_back(pixels[(y * 320 + x) * 4]);
        Check(target.SavePNG(path), "save header checkbox state");
        return header;
    };
    Check(state() == CheckState::Indeterminate, "header reports partial selection");
    const auto partial = render_header(L"lumen_visual_header_partial.png");
    table.OnMouseDown({84.0f, 16.0f}, 1);
    table.OnMouseUp({84.0f, 16.0f}, 1);
    Check(state() == CheckState::Checked, "partial header click checks every row");
    const auto all = render_header(L"lumen_visual_header_all.png");
    table.OnMouseDown({84.0f, 16.0f}, 1);
    table.OnMouseUp({84.0f, 16.0f}, 1);
    Check(state() == CheckState::Unchecked, "checked header click clears every row");
    const auto none = render_header(L"lumen_visual_header_none.png");
    Check(partial != all && all != none && partial != none, "header three states have distinct pixels");
    table.OnMouseDown({84.0f, 16.0f}, 1);
    table.OnMouseUp({84.0f, 100.0f}, 1);
    Check(state() == CheckState::Unchecked, "header release outside does not toggle rows");
    table.CellText([](size_t, size_t, std::wstring& out) { out = L"Production"; }).GroupBy(0);
    render_header(L"lumen_visual_group_divider.png");
    std::vector<uint8_t> grouped;
    Check(target.ReadBack(grouped), "group divider readback");
    const size_t band_y = static_cast<size_t>(10.0f + table.HeaderHeight() + 2.0f);
    const size_t row_y = static_cast<size_t>(10.0f + table.HeaderHeight() + table.GroupBand() + 2.0f);
    auto pixel = [&](size_t x, size_t y) { return grouped[(y * 320 + x) * 4]; };
    Check(pixel(73, band_y) == pixel(74, band_y), "frozen divider does not cross group header");
    Check(pixel(73, row_y) != pixel(74, row_y), "frozen divider remains between data columns");
}

// 特殊状态色板：成功/警告徽标、Critical 信息条、Busy 头像点须带各自色相，而不是灰阶。
void TestStatusColors() {
    OffscreenRenderer renderer;
    if (!renderer.Init(480, 200)) {
        Check(false, "renderer init (status colors)");
        return;
    }
    const Theme theme = MakeTheme();
    TestRoot root;
    root.Padding(16.0f, 16.0f).Spacing(16.0f);
    auto& row = root.Add<Row>().Spacing(16.0f).AlignCross(StackPanel::CrossAlign::Center);
    Badge* ok = &row.Add<Badge>(L"Stable", Badge::BadgeTone::Success);
    Badge* warn = &row.Add<Badge>(L"Beta", Badge::BadgeTone::Warning);
    Badge* neutral = &row.Add<Badge>(L"Draft", Badge::BadgeTone::Neutral);
    Avatar* busy = &row.Add<Avatar>();
    busy->PresenceState(Avatar::Presence::Busy);
    InfoBar* critical = &root.Add<InfoBar>(L"Critical");
    critical->Message(L"Status colors").Tone(InfoBar::InfoTone::Critical).Closable(false);
    root.Measure({480.0f, 200.0f}, theme);
    root.Arrange({0.0f, 0.0f, 480.0f, 200.0f});

    ID2D1DeviceContext2* dc = renderer.BeginDraw();
    Painter painter;
    painter.BeginFrame(dc, &UiText(), 1.0f);
    painter.FillRect({0, 0, 480, 200}, theme.bg);
    DrawControlTree(painter, theme, &root);
    painter.EndFrame();
    Check(renderer.EndDraw(), "enddraw (status colors)");
    Check(renderer.SavePNG(L"lumen_visual_status.png"), "save status color png");
    std::vector<uint8_t> snapshot;
    Check(renderer.ReadBack(snapshot), "readback (status colors)");
    const auto read_pixel = [&](float x, float y) {
        Color out{};
        const int ix = static_cast<int>(x);
        const int iy = static_cast<int>(y);
        if (ix < 0 || iy < 0 || ix >= renderer.Width() || iy >= renderer.Height() ||
            snapshot.size() != static_cast<size_t>(renderer.Width()) * renderer.Height() * 4) {
            return out;
        }
        const uint8_t* px = snapshot.data() + (static_cast<size_t>(iy) * renderer.Width() + ix) * 4;
        return Color{px[2] / 255.0f, px[1] / 255.0f, px[0] / 255.0f, px[3] / 255.0f};
    };
    // 徽标左内边（避开居中文字）：状态底为同色 14% 合成到黑底。
    const Rect okb = ok->AbsoluteBounds();
    Color c = read_pixel(okb.x + 5.0f, okb.y + okb.h * 0.5f);
    Check(CloseTo(c, Over(theme.success_subtle, theme.bg), 0.03f) && c.g > c.r + 0.04f,
          "success badge tinted green");
    const Rect wb = warn->AbsoluteBounds();
    c = read_pixel(wb.x + 5.0f, wb.y + wb.h * 0.5f);
    Check(CloseTo(c, Over(theme.warning_subtle, theme.bg), 0.03f) && c.r > c.b + 0.08f,
          "warning badge tinted amber");
    const Rect nb = neutral->AbsoluteBounds();
    c = read_pixel(nb.x + 5.0f, nb.y + nb.h * 0.5f);
    Check(std::fabs(c.r - c.g) < 0.01f && std::fabs(c.g - c.b) < 0.01f, "neutral badge stays gray");
    // 默认 32 DIP 头像：状态点中心与 extras 场景同一偏移。
    const Rect av = busy->AbsoluteBounds();
    c = read_pixel(av.x + 28.76f, av.y + 28.76f);
    Check(c.r > c.g + 0.3f && c.r > c.b + 0.3f, "avatar busy presence dot red");
    // Critical 字形井左上内角：danger_subtle 叠在碳底上，偏红。
    const Rect ib = critical->AbsoluteBounds();
    const float glyph_y = ib.y + (ib.h - 32.0f) * 0.5f;
    c = read_pixel(ib.x + 14.0f + 5.0f, glyph_y + 5.0f);
    Check(c.r > c.g + 0.05f && c.r > c.b + 0.05f, "critical info bar glyph well tinted red");

    // 标题栏：关闭钮悬停为 danger_pressed 深红底；最小化悬停保持中性灰。
    OffscreenRenderer caption;
    if (!caption.Init(480, 40)) {
        Check(false, "renderer init (title bar close hover)");
        return;
    }
    TestTitleBar bar;
    bar.Title(L"LUMEN");
    bar.Measure({480.0f, 40.0f}, theme);
    bar.Arrange({0.0f, 0.0f, 480.0f, 40.0f});
    bar.hover_ = TitleBar::Region::Close;
    bar.close_glow_ = 1.0f;
    bar.min_glow_ = 1.0f;
    ID2D1DeviceContext2* cdc = caption.BeginDraw();
    Painter caption_painter;
    caption_painter.BeginFrame(cdc, &UiText(), 1.0f);
    caption_painter.FillRect({0, 0, 480, 40}, theme.bg);
    bar.Draw(caption_painter, theme);
    caption_painter.EndFrame();
    Check(caption.EndDraw(), "enddraw (title bar close hover)");
    Check(caption.SavePNG(L"lumen_visual_caption.png"), "save title bar close hover png");
    std::vector<uint8_t> caption_px;
    Check(caption.ReadBack(caption_px), "readback (title bar close hover)");
    const auto caption_pixel = [&](const Rect& slot) {
        const int ix = static_cast<int>(slot.x + 4.0f);
        const int iy = static_cast<int>(slot.y + 4.0f);
        if (slot.IsEmpty() || caption_px.size() != 480u * 40u * 4u || ix < 0 || ix >= 480 || iy < 0 ||
            iy >= 40) {
            return Color{};
        }
        const uint8_t* px = caption_px.data() + (static_cast<size_t>(iy) * 480u + ix) * 4u;
        return Color{px[2] / 255.0f, px[1] / 255.0f, px[0] / 255.0f, px[3] / 255.0f};
    };
    c = caption_pixel(bar.ButtonSlot(TitleBar::Region::Close));
    Check(CloseTo(c, theme.danger_pressed, 0.03f), "title bar close hover uses danger fill");
    c = caption_pixel(bar.ButtonSlot(TitleBar::Region::Min));
    Check(CloseTo(c, Over(theme.fill_hover, theme.bg), 0.02f), "title bar minimize hover stays neutral");
}

// 骨架动画须肉眼可见：呼吸相位间底色有可辨亮度差，扫光经过处明显亮于两端。
void TestSkeletonMotion() {
    OffscreenRenderer renderer;
    if (!renderer.Init(200, 24)) {
        Check(false, "renderer init (skeleton motion)");
        return;
    }
    const Theme theme = MakeTheme();
    TestSkeleton skeleton;
    skeleton.Measure({200.0f, 24.0f}, theme);
    skeleton.Arrange({20.0f, 6.0f, 160.0f, 12.0f});
    const auto render = [&](float& edge, float& center) {
        ID2D1DeviceContext2* dc = renderer.BeginDraw();
        Painter painter;
        painter.BeginFrame(dc, &UiText(), 1.0f);
        painter.FillRect({0, 0, 200, 24}, theme.bg);
        skeleton.Draw(painter, theme);
        painter.EndFrame();
        Check(renderer.EndDraw(), "enddraw (skeleton motion)");
        std::vector<uint8_t> px;
        Check(renderer.ReadBack(px), "readback (skeleton motion)");
        const auto at = [&](int x) { return px.size() == 200u * 24u * 4u ? px[(12u * 200u + x) * 4u + 1u] / 255.0f : 0.0f; };
        edge = at(30);
        center = at(100);
    };
    float edge0 = 0.0f, center0 = 0.0f, edge1 = 0.0f, center1 = 0.0f;
    render(edge0, center0);
    // 1.4 rad/s：约 1.122 s 到呼吸峰值（sin=1），此时扫光正好在横向中点。
    skeleton.OnAnimate(1.1220f);
    render(edge1, center1);
    Check(edge1 > edge0 + 0.02f, "skeleton breathing changes visibly");
    Check(center1 > edge1 + 0.04f, "skeleton light sweep brightens the passing band");
}

// 布局契约：无约束测量返回自然尺寸、Row 收缩换行文字、Grid/ScrollViewer/WrapPanel 不溢出。
struct LayoutRow : StackPanel {
    LayoutRow() : StackPanel(Orientation::Horizontal) {}
    using StackPanel::Measure;
    using StackPanel::Arrange;
};
struct LayoutGrid : Grid {
    LayoutGrid() : Grid(2) {}
    using Grid::Measure;
    using Grid::Arrange;
};
struct LayoutWrap : WrapPanel {
    using WrapPanel::Measure;
    using WrapPanel::Arrange;
};
struct LayoutLabel : Label {
    using Label::Label;
    using Label::Measure;
};
struct FixedBlock : Control {
    Size size;
    explicit FixedBlock(Size s) : size(s) {}
    Size Measure(Size, const Theme&) override { return size; }
    void Draw(Painter&, const Theme&) override {}
};

void TestLayoutContract() {
    const Theme theme = MakeTheme();
    const wchar_t* kLong =
        L"Glow intensity scales every accent halo in the window. Lower it for dense tool panels, "
        L"raise it for showcase pages and splash screens where the light should carry the mood.";
    {   // 1. 无约束测量契约：Row 首测给的 kUnbounded 不得被原样当期望宽度返回。
        LayoutRow row;
        row.Add<Label>(kLong).Wrap(true);
        row.Add<Expander>(L"Advanced options").Add<Button>(L"Inner");
        row.Add<InfoBar>(L"Saved").Message(kLong);
        row.Add<TextBox>();
        row.Add<Button>(L"OK");
        const Size d = row.Measure({kUnbounded, kUnbounded}, theme);
        bool all_natural = true;
        for (size_t i = 0; i < row.ChildCount(); ++i) {
            if (row.Child(i).DesiredSize().w >= 1.0e4f) all_natural = false;
        }
        Check(all_natural, "layout: controls return natural width on unbounded axis");
        Check(d.w < 1.0e4f, "layout: row of greedy controls stays finite when unbounded");
    }
    {   // 2. Row 中换行 Label + 按钮：文字在剩余宽度内折行，按钮保持自然宽且不溢出。
        TestRoot root;
        auto& row = root.Add<Row>();
        row.Spacing(8.0f);
        auto& text = row.Add<Label>(kLong);
        text.Wrap(true);
        auto& button = row.Add<Button>(L"Apply");
        LayoutRow probe;
        auto& lone = probe.Add<Button>(L"Apply");
        probe.Measure({kUnbounded, kUnbounded}, theme);
        const float button_natural = lone.DesiredSize().w;
        root.Measure({420.0f, kUnbounded}, theme);
        root.Arrange({0.0f, 0.0f, 420.0f, 600.0f});
        const Rect t = text.AbsoluteBounds();
        const Rect b = button.AbsoluteBounds();
        Check(root.DesiredSize().w <= 420.5f, "layout: row with wrap label does not widen parent");
        Check(t.Right() <= b.x + 0.5f && b.Right() <= 420.5f, "layout: wrap label and button share row width");
        Check(CloseTo(b.w, button_natural, 0.5f), "layout: button keeps natural width in shrunk row");
        Check(t.h > 30.0f, "layout: wrap label in row wraps to multiple lines");
    }
    {   // 3. 换行 Label 期望宽度：左对齐取内容宽，居中占满可用宽。
        LayoutLabel leading(L"Short text");
        leading.Wrap(true);
        LayoutLabel centered(L"Short text");
        centered.Wrap(true).Alignment(Align::Center);
        Check(leading.Measure({400.0f, kUnbounded}, theme).w < 200.0f, "layout: leading wrap label hugs content");
        Check(CloseTo(centered.Measure({400.0f, kUnbounded}, theme).w, 400.0f, 0.5f),
              "layout: centered wrap label keeps full width");
    }
    {   // 4. Grid 在无约束宽度下 Arrange：fr 列退回内容宽，单元格不重叠。
        LayoutGrid grid;
        auto& a = grid.Add<Button>(L"Left cell");
        auto& c = grid.Add<Button>(L"Right cell");
        grid.Measure({kUnbounded, kUnbounded}, theme);
        grid.Arrange({0.0f, 0.0f, kUnbounded, 200.0f});
        Check(a.AbsoluteBounds().w > 20.0f && c.AbsoluteBounds().w > 20.0f,
              "layout: grid fr columns non-zero when unbounded");
        Check(c.AbsoluteBounds().x >= a.AbsoluteBounds().Right() - 0.5f,
              "layout: grid cells do not overlap when unbounded");
    }
    {   // 5. 纵向 ScrollViewer：内容宽恒等于视口宽；原始 bug 场景（Row+换行文字+Grid）。
        TestScrollViewer viewer;
        auto& page = viewer.Add<Column>();
        page.Add<FixedBlock>(Size{2000.0f, 40.0f});
        auto& row = page.Add<Row>();
        row.Add<Label>(kLong).Wrap(true);
        row.Add<Button>(L"Go");
        auto& grid = page.Add<Grid>(2);
        auto& left = grid.Add<Button>(L"A");
        auto& right = grid.Add<Button>(L"B");
        viewer.Measure({600.0f, 400.0f}, theme);
        viewer.Arrange({0.0f, 0.0f, 600.0f, 400.0f});
        Check(page.AbsoluteBounds().w <= 600.5f, "layout: vertical scroll viewer clamps content width");
        Check(left.AbsoluteBounds().w > 200.0f && right.AbsoluteBounds().x > 250.0f &&
                  right.AbsoluteBounds().Right() <= 600.5f,
              "layout: grid below wrap-label row splits viewport width");
    }
    {   // 6. WrapPanel：超长项按行宽约束，不撑破容器；短项保持自然宽。
        LayoutWrap wrap;
        auto& chip = wrap.Add<Button>(L"Chip");
        auto& text = wrap.Add<Label>(kLong);
        text.Wrap(true);
        const Size d = wrap.Measure({300.0f, kUnbounded}, theme);
        wrap.Arrange({0.0f, 0.0f, 300.0f, d.h});
        Check(text.AbsoluteBounds().Right() <= 300.5f && text.AbsoluteBounds().h > 30.0f,
              "layout: wrap panel constrains long item to line width");
        Check(chip.AbsoluteBounds().w < 150.0f, "layout: wrap panel keeps short item natural");
    }
    {   // 7. StackPanel 非 Stretch 交叉轴不超出容器。
        TestRoot root;
        root.AlignCross(CrossAlign::Center);
        auto& wide = root.Add<FixedBlock>(Size{2000.0f, 40.0f});
        root.Measure({300.0f, kUnbounded}, theme);
        root.Arrange({0.0f, 0.0f, 300.0f, 200.0f});
        Check(wide.AbsoluteBounds().x >= -0.5f && wide.AbsoluteBounds().Right() <= 300.5f,
              "layout: non-stretch cross extent clamped to container");
    }
}

// 图表类别色 / 光色温 / 语义瞬时光（Button::Flash、FormField 错误柔光）。
void TestColorLight() {
    {
        const Theme neutral = MakeTheme(1.0f);
        const Theme cool = MakeTheme(1.0f, LightTone::Cool);
        const Theme warm = MakeTheme(1.0f, LightTone::Warm);
        Check(neutral.glow_sm.r == 1.0f && neutral.glow_sm.b == 1.0f, "neutral light tone stays white");
        Check(cool.glow_sm.b > cool.glow_sm.r + 0.08f && cool.spotlight_fill.b > cool.spotlight_fill.r,
              "cool light tone tints glow tokens blue");
        Check(warm.glow_sm.r > warm.glow_sm.b + 0.08f && warm.specular_line.r > warm.specular_line.b,
              "warm light tone tints glow tokens amber");
        Check(CloseTo(cool.glow_sm.a, neutral.glow_sm.a, 0.001f), "light tone keeps glow alpha");
        Check(CloseTo(cool.text.b, neutral.text.b, 0.001f) && CloseTo(warm.accent.b, neutral.accent.b, 0.001f),
              "light tone leaves text and accent untouched");
        Check(cool.light_tone == LightTone::Cool, "theme records light tone");
        const Color s0 = ChartSeriesColor(neutral, 0);
        const Color s6 = ChartSeriesColor(neutral, kChartSeriesCount);
        Check(CloseTo(s6.g, s0.g * 0.72f, 0.01f), "chart palette cycles darker after six series");
        Check(StatusColor(neutral, StatusTone::Danger).r == neutral.danger.r &&
                  StatusColor(neutral, StatusTone::Success).g == neutral.success.g,
              "status tone maps to theme status colours");
    }
    const Theme theme = MakeTheme();
    const auto chroma = [](Color c) {
        return std::max(c.r, std::max(c.g, c.b)) - std::min(c.r, std::min(c.g, c.b));
    };
    {
        OffscreenRenderer renderer;
        if (!renderer.Init(240, 180)) {
            Check(false, "chart palette renderer");
        } else {
            TestChart chart;
            chart.Kind(ChartKind::Donut)
                .Slices({{L"A", 0.5f}, {L"B", 0.5f}})
                .PreferredSize({220.0f, 160.0f});
            chart.Measure({220.0f, 160.0f}, theme);
            chart.Arrange({8.0f, 8.0f, 220.0f, 160.0f});
            const auto count_chromatic = [&]() {
                ID2D1DeviceContext2* dc = renderer.BeginDraw();
                Painter painter;
                painter.BeginFrame(dc, &UiText(), 1.0f);
                painter.FillRect({0.0f, 0.0f, 240.0f, 180.0f}, theme.bg);
                DrawControlTree(painter, theme, &chart);
                painter.EndFrame();
                Check(renderer.EndDraw(), "chart palette enddraw");
                int hits = 0;
                Color ink{};
                for (int x = 10; x < 230; x += 2) {
                    for (int y = 10; y < 170; y += 2) {
                        renderer.ReadPixel(x, y, ink);
                        if (chroma(ink) > 0.25f) ++hits;
                    }
                }
                return hits;
            };
            const int colored = count_chromatic();
            chart.Monochrome(true);
            const int mono = count_chromatic();
            Check(colored > 60, "chart donut slices use category colours");
            Check(mono * 10 < colored, "chart Monochrome returns to grey ramp");
            renderer.Shutdown();
        }
    }
    {
        OffscreenRenderer renderer;
        if (!renderer.Init(160, 80)) {
            Check(false, "button flash renderer");
        } else {
            TestButton button;
            button.Text(L"Save");
            button.Measure({160.0f, 80.0f}, theme);
            button.Arrange({30.0f, 20.0f, 100.0f, 40.0f});
            const auto edge = [&]() {
                ID2D1DeviceContext2* dc = renderer.BeginDraw();
                Painter painter;
                painter.BeginFrame(dc, &UiText(), 1.0f);
                painter.FillRect({0.0f, 0.0f, 160.0f, 80.0f}, theme.bg);
                button.Draw(painter, theme);
                painter.EndFrame();
                Check(renderer.EndDraw(), "button flash enddraw");
                Color ink{};
                renderer.ReadPixel(25, 40, ink);
                return ink;
            };
            const Color rest = edge();
            button.Flash(StatusTone::Success);
            Check(CloseTo(button.FlashLevel(), 1.0f, 0.001f), "button flash starts at full");
            const Color lit = edge();
            Check(lit.g > lit.r + 0.05f && lit.g > rest.g + 0.05f, "button flash glows in success green");
            button.OnAnimate(0.5f);
            Check(button.FlashLevel() > 0.1f && button.FlashLevel() < 0.5f, "button flash decays");
            for (int i = 0; i < 20; ++i) button.OnAnimate(0.1f);
            Check(button.FlashLevel() == 0.0f, "button flash settles to zero");
            const Color done = edge();
            Check(CloseTo(done.g, rest.g, 0.01f), "button flash leaves no residue");
            renderer.Shutdown();
        }
    }
    {
        OffscreenRenderer renderer;
        if (!renderer.Init(320, 140)) {
            Check(false, "form error glow renderer");
        } else {
            TestRoot root;
            auto& field = root.Add<TestFormField>(L"Name");
            auto& box = field.Add<TextBox>();
            box.Text(L"x");
            const auto redness = [&]() {
                root.Measure({260.0f, 2000.0f}, theme);
                root.Arrange({30.0f, 10.0f, 260.0f, 120.0f});
                ID2D1DeviceContext2* dc = renderer.BeginDraw();
                Painter painter;
                painter.BeginFrame(dc, &UiText(), 1.0f);
                painter.FillRect({0.0f, 0.0f, 320.0f, 140.0f}, theme.bg);
                DrawControlTree(painter, theme, &root);
                painter.EndFrame();
                Check(renderer.EndDraw(), "form error glow enddraw");
                const Rect in = box.AbsoluteBounds();
                Color ink{};
                renderer.ReadPixel(static_cast<int>(in.x) - 3, static_cast<int>(in.y + in.h * 0.5f), ink);
                return ink.r - ink.g;
            };
            const float calm = redness();
            field.Error(L"Name is required");
            const float broken = redness();
            Check(calm < 0.01f, "form field without error has no red halo");
            Check(broken > 0.03f, "form field error adds danger glow around input");
            renderer.Shutdown();
        }
    }
}

void TestStructuredLogView() {
    std::vector<LogEntry> rows{
        {L"17:00:00.165", LogLevel::Info, L"worker", L"Order matched id=2913 px=341.50", L"trace=alpha"},
        {L"17:00:01.421", LogLevel::Error, L"orderbook", L"Retrying upstream call attempt=1151", L"trace=beta"},
        {L"17:00:03.331", LogLevel::Debug, L"auth", L"Slow query detected duration=240ms", L""},
        {L"17:00:04.427", LogLevel::Warn, L"api", L"Disk usage 87% on /data", L""},
    };
    TestLog log;
    size_t reads = 0;
    int following_events = 0;
    log.Entry([&](size_t index, LogEntry& out) { ++reads; out = rows.at(index); })
        .ItemCount(rows.size());
    log.OnFollowingChanged([&](bool) { ++following_events; });
    log.Arrange({0, 0, 1000, 208});
    Check(log.VisibleCount() == 4 && log.LevelCount(LogLevel::Error) == 1, "log level counts include source entries");
    log.Query(L"ALPHA");
    Check(log.VisibleCount() == 1 && log.DataIndex(0) == 0, "log search matches trace without case sensitivity");
    log.Query(L"orderbook");
    Check(log.VisibleCount() == 1 && log.DataIndex(0) == 1, "log search matches source");
    log.SelectedIndex(0);
    Check(log.CopySelection(), "log copy selection succeeds");
    const std::wstring copied = clipboard::Text();
    Check(copied == L"17:00:01.421 ERROR orderbook Retrying upstream call attempt=1151 trace=beta",
          "log copies full filtered source entry including trace");
    Check(log.AutomationItemName(0) ==
              L"17:00:01.421 ERROR orderbook Retrying upstream call attempt=1151 trace=beta",
          "log accessibility reads complete filtered entry");
    log.Query(L"").LevelEnabled(LogLevel::Info, false).LevelEnabled(LogLevel::Debug, false);
    Check(log.VisibleCount() == 2 && log.DataIndex(0) == 1 && log.DataIndex(1) == 3,
          "log combines independent level switches");
    Check(log.SelectedIndex() == 0 && log.LevelCount(LogLevel::Info) == 1,
          "log preserves selected source and disabled-level counts");
    log.Query(L"no match");
    Check(log.VisibleCount() == 0 && log.SelectedIndex() == -1 && !log.OnKey(VK_NEXT),
          "empty filtered log safely handles keyboard navigation");
    log.Query(L"").LevelEnabled(LogLevel::Info, true).LevelEnabled(LogLevel::Debug, true);
    OffscreenRenderer target;
    Check(target.Init(1000, 208), "structured log render target");
    auto render = [&](const wchar_t* path) {
        auto* dc = target.BeginDraw();
        dc->Clear(D2D1::ColorF(D2D1::ColorF::Black));
        Painter painter;
        painter.BeginFrame(dc, &UiText(), 1.0f);
        log.Prepare(painter, MakeTheme());
        const size_t before = reads;
        log.Draw(painter, MakeTheme());
        Check(reads == before, "log draw never invokes data providers");
        painter.EndFrame();
        Check(target.EndDraw() && target.SavePNG(path), "save structured log scene");
        std::vector<uint8_t> pixels;
        Check(target.ReadBack(pixels), "structured log pixels");
        // BGRA：ERROR 标记/级别为 danger 红，WARN 为 warning 琥珀，不再退化成灰阶。
        bool has_error_red = false;
        bool has_warn_amber = false;
        for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
            const int b = pixels[i], g = pixels[i + 1], r = pixels[i + 2];
            if (r > 150 && r > g + 70 && r > b + 70) has_error_red = true;
            if (r > 150 && g > 110 && r > b + 110 && g > b + 80) has_warn_amber = true;
        }
        Check(has_error_red, "log error severity uses danger color");
        Check(has_warn_amber, "log warn severity uses warning color");
    };
    render(L"lumen_visual_logs.png");
    log.Arrange({0, 0, 400, 208});
    render(L"lumen_visual_logs_narrow.png");
    while (rows.size() < 100) rows.push_back(rows.front());
    log.ItemCount(rows.size()).Follow(true);
    Check(log.Following() && log.Offset() > 0, "log follows appended tail");
    log.OnWheel(4);
    const float paused_offset = log.Offset();
    Check(!log.Following() && following_events == 1, "scrolling history pauses follow and notifies toolbar");
    rows.push_back(rows.front()); reads = 0;
    log.ItemCount(rows.size());
    Check(reads == 1 && log.Offset() == paused_offset, "append filters only new entries and preserves paused position");
    log.Follow(false);
    log.OnKey(VK_END);
    Check(log.Following() && log.Offset() > paused_offset, "End resumes explicitly paused follow");
    log.Follow(false);
    rows.push_back(rows.front()); log.ItemCount(rows.size());
    Check(!log.Following(), "append does not override explicit pause");
    log.Follow(true);
    Check(log.Following(), "follow button immediately resumes");
    log.ItemCount(0);
    Check(log.VisibleCount() == 0 && log.LevelCount(LogLevel::Info) == 0, "clearing logs clears counts");
}


void TestEditableComboChrome() {
    bool ok = true;
    int delta = 0;
    for (float scale : {1.0f, 1.5f, 2.0f}) {
        const int width = static_cast<int>(300.0f * scale);
        OffscreenRenderer renderer;
        if (!renderer.Init(width, static_cast<int>(64.0f * scale))) { ok = false; continue; }
        struct ChromeCombo : ComboBox { using ComboBox::Measure; using ComboBox::Arrange; } combo;
        const Theme theme = MakeTheme();
        std::vector<uint8_t> pixels[2];
        for (int pass = 0; pass < 2; ++pass) {
            combo.Editable(pass == 1);
            combo.Measure({280.0f, 40.0f}, theme);
            combo.Arrange({10.0f, 10.0f, 280.0f, 40.0f});
            Painter painter;
            painter.BeginFrame(renderer.BeginDraw(), &UiText(), scale);
            painter.FillRect({0.0f, 0.0f, 300.0f, 64.0f}, theme.bg);
            DrawControlTree(painter, theme, &combo);
            painter.EndFrame();
            ok = renderer.EndDraw() && renderer.ReadBack(pixels[pass]) && ok;
        }
        if (pixels[0].empty() || pixels[0].size() != pixels[1].size()) { ok = false; continue; }
        // The inner editor must not add another bottom edge/shadow inside the field.
        for (int y = static_cast<int>(47.0f * scale); y <= static_cast<int>(48.0f * scale); ++y) {
            for (int x = static_cast<int>(40.0f * scale); x < static_cast<int>(200.0f * scale); ++x) {
                const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
                for (size_t channel = 0; channel < 4; ++channel) {
                    delta = std::max(delta, std::abs(static_cast<int>(pixels[0][offset + channel]) -
                                                   pixels[1][offset + channel]));
                }
            }
        }
        if (scale == 1.5f) ok = renderer.SavePNG(L"lumen_visual_editable_combo.png") && ok;
    }
    Check(ok && delta == 0, "editable combo shares one field chrome at 100/150/200 percent");
}

void TestParagraphEditing() {
    struct Field : TextBox {
        using TextBox::Measure; using TextBox::Arrange; using TextBox::OnKey;
        using TextBox::OnChar; using TextBox::OnImeCompose; using TextBox::OnImeCommit;
        using TextBox::OnImeEnd; using TextBox::ImeCaret; using TextBox::Undo; using TextBox::Redo;
    };
    const Theme theme = MakeTheme();
    TextTypography style; style.family = L"Arial"; style.size = 16.0f; style.line_height = 19.2f;
    const std::wstring content = L"Clear text \u6c34\u7535\u8d39 wraps without changing the font size.\nSecond paragraph.";
    Field field;
    field.Multiline(true).Typography(style).WordWrap(true).ContentPadding(2).Chrome(false).Text(content);
    field.Measure({182.0f, 170.0f}, theme); field.Arrange({14.0f, 14.0f, 182.0f, 170.0f});
    Check(field.VisualLineCount() >= 3, "paragraph word wrapping uses visual lines");
    field.PlaceCaret({10000.0f, 8.0f});
    Check(field.CaretBounds().y < 22.0f, "clicking a wrapped line end keeps trailing caret on that line");
    const auto first = field.CaretBounds();
    field.OnKey(VK_DOWN);
    Check(field.CaretBounds().y > first.y + 10.0f, "Down moves through wrapped visual lines");
    field.OnKey(VK_HOME);
    Check(field.CaretBounds().x < 3.0f, "Home goes to visual line start");
    field.Select(2, 5);
    field.OnImeCompose(L"\u4e2d\u6587", 1, {});
    Check(field.Text() == content && field.Composing(), "IME preedit does not delete the selected original text");
    Point candidate{}; float candidate_h = 0;
    Check(field.ImeCaret(candidate, candidate_h) && candidate_h > 10.0f && candidate.y >= 14.0f,
          "paragraph IME candidate uses the visible shaped caret");
    field.OnImeEnd();
    Check(field.Text() == content && field.SelectionStart() == 2 && field.SelectionEnd() == 5,
          "cancelling IME restores the original selection and text");
    field.OnImeCompose(L"\u4e2d\u6587", 2, {}); field.OnImeCommit(L"\u4e2d\u6587");
    const auto committed = content.substr(0, 2) + L"\u4e2d\u6587" + content.substr(5);
    Check(field.Text() == committed, "IME result replaces the selection exactly once");
    Check(field.Undo() && field.Text() == content, "IME replacement is one text undo operation");
    Check(field.Redo() && field.Text() == committed, "IME replacement redo restores committed Unicode");

    TextLayout layout;
    layout.Layout(content, style, 178.0f, true);
    std::vector<Rect> boxes;
    layout.Selection(0, content.size(), boxes);
    Check(boxes.size() >= 3, "shared selection geometry spans wrapped lines");
    bool roundtrip = true;
    for (size_t i = 0; i < content.size(); ++i) {
        if (content[i] == L'\n' || content[i] == L' ') continue;
        const auto caret = layout.Caret(i);
        const auto hit = layout.HitTest({caret.x + .05f, caret.y + caret.h * .5f});
        if (hit.index != i) roundtrip = false;
    }
    Check(roundtrip, "paragraph caret and pointer hit tests share one layout");

    for (float scale : {1.0f, 1.5f, 2.0f}) {
        OffscreenRenderer target;
        const int width = static_cast<int>(420.0f * scale), height = static_cast<int>(220.0f * scale);
        if (!target.Init(width, height)) { Check(false, "paragraph offscreen renderer"); continue; }
        auto* dc = target.BeginDraw();
        LumaTextBridge luma;
        Check(luma.Init(UiText().Factory(), dc) && luma.Enabled(), "paragraph LumaText bridge is active");
        target.EndDraw();
        field.ResetDocument(content);field.Select(0,0);field.ReadOnly(true);
        field.Foreground(Color::Hex(0x202020)).TextBackdrop(Color::Hex(0xffffff));
        std::vector<uint8_t> pixels[2];
        bool ok = true;
        for (int pass = 0; pass < 2; ++pass) {
            Painter painter;
            painter.BeginFrame(target.BeginDraw(), &UiText(), scale);painter.SetLumaText(&luma);
            painter.FillRect({0,0,420,220}, Color::Hex(0xffffff));
            if (pass == 0) {
                layout.Prepare(painter, {16,16}, Color::Hex(0x202020), Color::Hex(0xffffff));
                layout.Draw(painter, {16,16}, Color::Hex(0x202020), Color::Hex(0xffffff));
            } else DrawControlTree(painter, theme, &field);
            painter.EndFrame();ok = target.EndDraw() && target.ReadBack(pixels[pass]) && ok;
        }
        Check(ok && pixels[0] == pixels[1], "display and editor glyph pixels match at the same DPI");
        Check(luma.Stats().freetype_glyphs > 0 && luma.Stats().fallback_draws == 0,
              "paragraph is really rasterized by LumaText, not a renamed DirectWrite fallback");
        const auto glyphs = luma.Stats().freetype_glyphs;
        auto styled = style;styled.family = L"Times New Roman";styled.weight = 700;styled.italic = true;styled.underline = true;
        layout.Layout(L"Bold italic underline 12pt", styled, 380, true);
        Painter painter;painter.BeginFrame(target.BeginDraw(), &UiText(), scale);painter.SetLumaText(&luma);
        layout.Prepare(painter,{16,140},Color::Hex(0x202020),Color::Hex(0xffffff));
        layout.Draw(painter,{16,140},Color::Hex(0x202020),Color::Hex(0xffffff));
        painter.EndFrame();Check(target.EndDraw(), "styled paragraph end draw");
        Check(luma.Stats().freetype_glyphs > glyphs && luma.Stats().fallback_draws == 0,
              "arbitrary family and italic remain on the LumaText renderer");
        wchar_t path[80]{};swprintf_s(path,L"lumen_visual_paragraph_%d.png",static_cast<int>(scale*100));
        Check(target.SavePNG(path), "save paragraph DPI/state scene");
        luma.Shutdown();
        // Restore the shared layout after the style sample for the next DPI.
        layout.Layout(content,style,178.0f,true);
    }
}

void TestCompactToolWindow() {
    Window owner(L"tool owner", {640.0f, 420.0f}, Frame::System);
    owner.Root().Add<TextBox>(L"Keep selection and focus");
    owner.Show();
    const HWND owner_hwnd = static_cast<HWND>(owner.NativeHandle());
    SetFocus(owner_hwnd);
    WindowSpec spec;
    spec.title = L"compact tool";
    spec.titleBar = false;
    spec.size = {380.0f, 148.0f};
    spec.owner = owner_hwnd;
    spec.backdrop = Backdrop::None;
    Window tool(spec);
    auto& row = tool.Root().Add<Row>();
    auto& combo = row.Add<TestComboBox>();
    combo.Items({L"Arial", L"Calibri", L"Courier New", L"Georgia", L"Segoe UI",
                 L"Tahoma", L"Times New Roman", L"Verdana"}).Editable(true).SelectedIndex(0);
    combo.MinSize({240.0f, 40.0f}).MaxSize({240.0f, 40.0f});
    combo.Text(L"Georgia"); combo.CommitText();
    Check(combo.SelectedIndex() == 3, "typed exact font commits matching selection");
    combo.SelectedIndex(0);
    auto& size = row.Add<NumberBox>(12);
    size.MinSize({100.0f, 40.0f}).MaxSize({100.0f, 40.0f});
    int showing = 0, shown = 0;
    tool.OnShowing([&] { ++showing; });
    tool.OnShown([&] { ++shown; });
    tool.Show(false);
    tool.LayoutNow();
    Check(GetFocus() == owner_hwnd, "inactive tool window preserves owner keyboard focus");
    Check(showing == 1 && shown == 1 && tool.Visible(), "inactive show retains visibility events");
    const HWND hwnd = static_cast<HWND>(tool.NativeHandle());
    Check(GetWindow(hwnd, GW_OWNER) == owner_hwnd, "compact tool keeps native owner");
    auto pump = [&] {
        MSG message{};
        const ULONGLONG deadline = GetTickCount64() + 500;
        // A focused TextBox intentionally animates its caret. Do not require its
        // WM_PAINT queue to become empty on a slow/cold renderer.
        while (GetTickCount64() < deadline && PeekMessageW(&message, hwnd, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
    };
    const UINT_PTR timer = SetTimer(hwnd, 0, 3000, [](HWND h, UINT, UINT_PTR id, DWORD) {
        (void)h; (void)id;
        Check(false, "compact combo watchdog");
        if (auto popup = FindPopupWindows(); popup.first) PostMessageW(popup.first, WM_KEYDOWN, VK_ESCAPE, 0);
    });
    Check(combo.AutomationExpand(), "compact combo UIA expand returns without blocking");
    bool outside = false;
    tool.SetTimeout(.03f, [&] {
        const auto popup = FindPopupWindows();
        RECT host{}, drop{}; GetWindowRect(hwnd, &host);
        if (popup.first) GetWindowRect(popup.first, &drop);
        outside = popup.first && (drop.bottom > host.bottom || drop.top < host.top);
        if (popup.first) {
            SendMessageW(popup.first, WM_KEYDOWN, VK_DOWN, 0);
            SendMessageW(popup.first, WM_KEYDOWN, VK_RETURN, 0);
        } else tool.ClosePopup();
    });
    pump();
    Check(outside && combo.SelectedIndex() == 1 && !tool.PopupActive(),
          "compact font list escapes host clipping and Enter commits highlighted row");
    combo.Editor().Focus();
    Check(combo.AutomationExpand(), "compact combo reopens");
    tool.SetTimeout(.03f, [&] {
        const auto popup = FindPopupWindows();
        std::printf("[INFO] compact Tab popup count=%d expanded=%d\n", popup.visible, combo.AutomationExpandState());
        if (popup.first) PostMessageW(popup.first, WM_KEYDOWN, VK_TAB, 0);
        else tool.ClosePopup();
    });
    pump();
    Check(size.HasFocus() && !tool.PopupActive(), "Tab leaves detached combo for next field");
    size.Focus();
    Check(!size.ImeEnabled() && ImmGetContext(hwnd) == nullptr,
          "numeric field suspends only its window IME context");
    combo.Editor().Focus();
    const HIMC restored = ImmGetContext(hwnd);
    Check(restored != nullptr && combo.Editor().ImeEnabled(), "font input restores IME after numeric field");
    if (restored) ImmReleaseContext(hwnd, restored);
    Check(combo.AutomationExpand() && combo.AutomationCollapse(), "pending native dropdown can be collapsed");
    pump();
    Check(!tool.PopupActive() && combo.AutomationExpandState() == 0, "cancelled posted dropdown never reappears");
    KillTimer(hwnd, timer);

    struct CursorProbe : Control {
        CursorShape shape = CursorShape::Arrow;
        void Draw(Painter&, const Theme&) override {}
        CursorShape CursorAt(Point) const override { return shape; }
    };
    auto& probe = tool.Root().Add<CursorProbe>();
    probe.MinSize({0.0f, 32.0f}).MaxSize({100000.0f, 32.0f});
    tool.Resize({320.0f, 180.0f});
    tool.LayoutNow();
    const auto bounds = probe.AbsoluteBounds();
    const float scale = GetDpiForWindow(hwnd) / 96.0f;
    const Point local{bounds.x + 20.0f, bounds.y + 12.0f};
    tool.DispatchMouseMove(local);
    POINT screen{static_cast<LONG>(local.x * scale), static_cast<LONG>(local.y * scale)};
    ClientToScreen(hwnd, &screen); SetCursorPos(screen.x, screen.y);
    const CursorShape shapes[]{CursorShape::SizeNWSE, CursorShape::SizeNESW, CursorShape::SizeAll, CursorShape::Cross};
    const LPCWSTR cursors[]{IDC_SIZENWSE, IDC_SIZENESW, IDC_SIZEALL, IDC_CROSS};
    for (size_t i = 0; i < std::size(shapes); ++i) {
        probe.shape = shapes[i];
        SendMessageW(hwnd, WM_SETCURSOR, reinterpret_cast<WPARAM>(hwnd), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
        Check(GetCursor() == LoadCursorW(nullptr, cursors[i]), "extended LUMEN cursor maps to system cursor");
    }
    tool.Hide(); owner.Close();
}

void TestNestedComboFlyout() {
    // The preceding owned-window lifetime test closes its last native HWND.
    // A new popup session must not inherit that test's process-level WM_QUIT.
    MSG stale_quit{};
    while (PeekMessageW(&stale_quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
    Window window(L"nested combo", {480.0f, 400.0f}, Frame::System);
    auto& trigger = window.Root().Add<Button>(L"Filters");
    Flyout filters;
    filters.FlyoutWidth(300.0f);
    auto& combo = filters.Add<ComboBox>();
    combo.Items({L"Any", L"Open", L"Closed"}).SelectedIndex(0);
    window.Show();
    window.LayoutNow();
    window.ShowFlyout(filters, &trigger);
    int closed = 0;
    filters.OnClosed([&] { ++closed; });
    const HWND owner = static_cast<HWND>(window.NativeHandle());
    const UINT_PTR timer = SetTimer(owner, 0, 3000, [](HWND hwnd, UINT, UINT_PTR id, DWORD) {
        Check(false, "nested combo watchdog");
        if (auto popup = FindPopupWindows(); popup.first) PostMessageW(popup.first, WM_KEYDOWN, VK_ESCAPE, 0);
        KillTimer(hwnd, id);
    });
    combo.Focus();
    window.DispatchKey(VK_SPACE);
    window.SetTimeout(.03f, [&] {
        std::printf("[INFO] nested flyout=%d popup=%d closed=%d bound=%d\n",
                    window.FlyoutActive(), window.PopupActive(), closed, combo.WindowOf() == &window);
        Check(window.FlyoutActive() && window.PopupActive() && closed == 0,
              "dropdown preserves parent filters");
        const auto popup = FindPopupWindows();
        if (!popup.first) { window.ClosePopup(); return; }
        SendMessageW(popup.first, WM_KEYDOWN, VK_DOWN, 0);
        SendMessageW(popup.first, WM_KEYDOWN, VK_RETURN, 0);
    });
    MSG pending{};
    while (PeekMessageW(&pending, owner, 0, 0, PM_REMOVE)) {
        TranslateMessage(&pending); DispatchMessageW(&pending);
    }
    KillTimer(owner, timer);
    Check(combo.SelectedIndex() == 1 && !window.PopupActive(), "nested combo commits selected item");
    Check(window.FlyoutActive() && closed == 0 && combo.WindowOf() == &window,
          "filters remains bound after selection");
    combo.Focus();
    window.DispatchKey(VK_SPACE);
    window.SetTimeout(.03f, [&] { window.ClosePopup(); });
    while (PeekMessageW(&pending, owner, 0, 0, PM_REMOVE)) {
        TranslateMessage(&pending); DispatchMessageW(&pending);
    }
    Check(window.FlyoutActive() && closed == 0, "nested combo can reopen and cancel");
    window.CloseFlyout();
    Check(closed == 1, "parent filters closes once when requested");
}

void TestPopupWindow() {
    Window window(L"popup regression", {480.0f, 320.0f}, Frame::System);
    auto& trigger = window.Root().Add<Button>(L"Open");
    int owner_clicks = 0;
    trigger.OnClick([&] { ++owner_clicks; });
    window.Show();
    window.LayoutNow();
    const HWND owner = static_cast<HWND>(window.NativeHandle());
    SetWindowPos(owner, nullptr, 120, 120, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    MSG stale{};
    while (PeekMessageW(&stale, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {}

    struct PopupContent : StackPanel {
        float wheel = 0.0f, horizontal = 0.0f;
        bool OnWheel(float delta) override { wheel = delta; return true; }
        bool OnHWheel(float delta) override { horizontal = delta; return true; }
    } content;
    auto& pick = content.Add<Button>(L"Pick");
    int picked = 0, closed = 0;
    pick.OnClick([&] { ++picked; window.ClosePopup(); });
    auto run = [&](const Control* anchor, auto action) {
        const UINT_PTR timer = SetTimer(owner, 0, 3000, [](HWND timer_owner, UINT, UINT_PTR id, DWORD) {
            Check(false, "popup test watchdog: session must terminate");
            const auto popup = FindPopupWindows();
            if (popup.first) PostMessageW(popup.first, WM_KEYDOWN, VK_ESCAPE, 0);
            KillTimer(timer_owner, id);
        });
        window.Post(std::move(action));
        window.ShowPopup(content, anchor, 240.0f, [&] { ++closed; });
        KillTimer(owner, timer);
        Check(!window.PopupActive() && FindPopupWindows().visible == 0, "popup session closes exactly one native window");
        Check(!Renderer::FlyoutOpen(), "popup balances renderer flyout depth");
        Check(content.WindowOf() == nullptr && pick.WindowOf() == nullptr, "popup detaches borrowed content tree");
    };

    run(&trigger, [&] {
        const auto popup = FindPopupWindows();
        Check(popup.visible == 1, "popup opens one native window");
        if (!popup.first) { window.ClosePopup(); return; }
        RECT before{}, after{}, main{};
        GetWindowRect(popup.first, &before);
        GetWindowRect(owner, &main);
        SetWindowPos(owner, nullptr, main.left + 43, main.top + 31, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        GetWindowRect(popup.first, &after);
        Check(after.left - before.left == 43 && after.top - before.top == 31,
              "popup follows owner movement synchronously");
        const float scale = static_cast<float>(GetDpiForWindow(popup.first)) / 96.0f;
        const Rect r = pick.AbsoluteBounds();
        const LPARAM point = MAKELPARAM(static_cast<int>((r.x + r.w * 0.5f) * scale),
                                        static_cast<int>((r.y + r.h * 0.5f) * scale));
        POINT screen{static_cast<short>(LOWORD(point)), static_cast<short>(HIWORD(point))};
        ClientToScreen(popup.first, &screen);
        const LPARAM wheel_point = MAKELPARAM(screen.x, screen.y);
        SendMessageW(popup.first, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), wheel_point);
        Check(content.wheel == 1.0f, "popup normalizes a wheel notch to one unit");
        SendMessageW(popup.first, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA / 4), wheel_point);
        Check(content.wheel == 0.25f, "popup preserves fractional wheel input");
        SendMessageW(popup.first, WM_MOUSEHWHEEL, MAKEWPARAM(0, WHEEL_DELTA), wheel_point);
        Check(content.horizontal == 1.0f, "popup routes horizontal wheel separately");
        SendMessageW(popup.first, WM_MOUSEMOVE, 0, point);
        SendMessageW(popup.first, WM_LBUTTONDOWN, MK_LBUTTON, point);
        SendMessageW(popup.first, WM_LBUTTONUP, 0, point);
        Check(picked == 1, "popup client coordinates hit the intended button");
        Check(!IsWindowVisible(popup.first), "ClosePopup hides immediately before the callback unwinds");
        window.ClosePopup();
    });
    Check(closed == 1, "popup closed callback fires once");

    run(&trigger, [&] {
        StackPanel second;
        second.Add<Label>(L"Second");
        int rejected_closed = 0;
        const HWND original = FindPopupWindows().first;
        window.ShowPopup(second, &trigger, 220.0f, [&] { ++rejected_closed; });
        window.ShowPopup(content, &trigger, 240.0f);
        Check(FindPopupWindows().visible == 1 && FindPopupWindows().first == original,
              "reentrant ShowPopup cannot stack native windows");
        Check(rejected_closed == 0 && second.WindowOf() == nullptr,
              "rejected popup does not replace callbacks or borrow content");
        window.ClosePopup();
    });
    Check(closed == 2, "reentrant popup preserves the original closed callback");

    auto click_owner = [&] {
        const Rect r = trigger.AbsoluteBounds();
        const float scale = static_cast<float>(GetDpiForWindow(owner)) / 96.0f;
        const LPARAM point = MAKELPARAM(static_cast<int>((r.x + r.w * 0.5f) * scale),
                                        static_cast<int>((r.y + r.h * 0.5f) * scale));
        SendMessageW(owner, WM_LBUTTONDOWN, MK_LBUTTON, point);
        SendMessageW(owner, WM_LBUTTONUP, 0, point);
    };
    run(&trigger, [&] { click_owner(); });
    Check(owner_clicks == 0, "outside click dismisses without activating owner content");
    click_owner();
    Check(owner_clicks == 1, "owner input works normally after popup closes");

    run(&trigger, [&] { PostMessageW(owner, WM_KEYDOWN, VK_ESCAPE, 0); });
    Check(closed == 4, "Escape addressed to owner dismisses popup");

    run(&trigger, [&] {
        const HWND popup = FindPopupWindows().first;
        const Rect r = pick.AbsoluteBounds();
        const float scale = static_cast<float>(GetDpiForWindow(popup)) / 96.0f;
        const LPARAM point = MAKELPARAM(static_cast<int>((r.x + 4.0f) * scale),
                                        static_cast<int>((r.y + 4.0f) * scale));
        SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, point);
        SetCapture(owner);
    });
    Check(GetCapture() == owner, "popup cleanup does not release another window's capture");
    ReleaseCapture();
    Check(picked == 1, "capture loss cancels the pressed button without invoking it");

    run(&trigger, [&] { ShowWindow(owner, SW_HIDE); });
    window.Show();
    window.LayoutNow();
    run(&trigger, [&] { SendMessageW(owner, WM_ACTIVATEAPP, FALSE, 0); });
    run(&trigger, [&] { PostQuitMessage(42); });
    MSG quit{};
    Check(PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE) && quit.wParam == 42,
          "popup preserves WM_QUIT and its exit code");

    run(&trigger, [&] { window.Root().Clear(); });
    Check(closed == 9, "anchor deletion closes popup and calls closed once");

    window.Post([&] { DestroyWindow(owner); });
    window.ShowPopup(content, nullptr, 240.0f, [&] { ++closed; });
    Check(window.Closed() && !window.PopupActive(), "owner destruction ends popup session");
    Check(content.WindowOf() == nullptr && !Renderer::FlyoutOpen(),
          "owner destruction detaches popup content and balances renderer state");
    Check(closed == 9, "owner destruction suppresses callbacks capturing the dead owner");
    while (PeekMessageW(&stale, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
}

void TestHwndFocus() {
    Window window(L"hwnd-focus", {320.0f, 96.0f});
    auto& first = window.Root().Add<Button>(L"A");
    window.LayoutNow();
    first.Focus();
    Check(first.HasFocus(), "hwnd-focus starts focused");
    HWND hwnd = static_cast<HWND>(window.NativeHandle());
    int kill = 0;
    int set = 0;
    auto hook = window.BindNativeMessage([&](std::uint32_t msg, std::uintptr_t, std::intptr_t) {
        if (msg == WM_KILLFOCUS) ++kill;
        if (msg == WM_SETFOCUS) ++set;
    });
    SendMessageW(hwnd, WM_KILLFOCUS, 0, 0);
    Check(kill == 1, "native hook sees WM_KILLFOCUS");
    Check(!first.HasFocus() && window.Focused() == nullptr, "WM_KILLFOCUS clears HasFocus");
    SendMessageW(hwnd, WM_SETFOCUS, 0, 0);
    Check(set == 1, "native hook sees WM_SETFOCUS");
    Check(window.Focused() == &first && first.HasFocus(), "WM_SETFOCUS restores last focus");
    window.ClearFocus();
    SendMessageW(hwnd, WM_SETFOCUS, 0, 0);
    Check(window.Focused() == nullptr, "explicit ClearFocus is not restored on SETFOCUS");
    hook.Disconnect();
    window.Close();
}

void TestPointerDoubleClickRecognition() {
    struct Target : Control { void Draw(Painter&, const Theme&) override {} } one, two;
    PointerClick clicks;
    const Point position{24.0f, 18.0f}, extent{3.0f, 3.0f}, drag{3.0f, 3.0f};
    Check(!clicks.Down(&one, position, 100, PT_MOUSE, extent, 500), "pointer first press is not a double click");
    clicks.Up(&one, position, drag);
    Check(clicks.Down(&one, position, 180, PT_MOUSE, extent, 500), "pointer completed mouse click pair emits double click");
    Check(clicks.PromotedDuplicate(position, 180), "same pointer double click is recognized for legacy deduplication");
    Check(!clicks.PromotedDuplicate(position, 181), "independent legacy double click is not discarded by stale pointer state");
    Check(!clicks.PromotedDuplicate({40.0f,18.0f}, 180), "different legacy double click location is not deduplicated");
    clicks.Up(&one, position, drag);
    Check(!clicks.Down(&one, position, 260, PT_MOUSE, extent, 500), "pointer third press does not duplicate the second double click");
    clicks.Up(&one, position, drag);
    Check(clicks.Down(&one, position, 340, PT_MOUSE, extent, 500), "pointer fourth press starts the next double click pair");
    clicks.Cancel();

    clicks.Down(&one, position, 1000, PT_MOUSE, extent, 500);clicks.Up(&one, position, drag);
    Check(!clicks.Down(&two, position, 1080, PT_MOUSE, extent, 500), "pointer double click never crosses controls");
    clicks.Cancel();
    clicks.Down(&one, position, 2000, PT_MOUSE, extent, 500);clicks.Up(&one, position, drag);
    Check(!clicks.Down(&one, position, 2600, PT_MOUSE, extent, 500), "pointer pair respects the system double click timeout");
    clicks.Cancel();
    clicks.Down(&one, position, 3000, PT_MOUSE, extent, 500);clicks.Up(&one, position, drag);
    Check(!clicks.Down(&one, {32.0f,18.0f}, 3080, PT_MOUSE, extent, 500), "pointer pair respects physical double click bounds");
    clicks.Cancel();
    clicks.Down(&one, position, 4000, PT_MOUSE, extent, 500);clicks.Move({40.0f,18.0f}, drag);clicks.Move(position, drag);clicks.Up(&one, position, drag);
    Check(!clicks.Down(&one, position, 4080, PT_MOUSE, extent, 500), "dragging out and back cannot arm a double click");
    clicks.Cancel();
    clicks.Down(&one, position, 5000, PT_MOUSE, extent, 500);clicks.Up(&one, {40.0f,18.0f}, drag);
    Check(!clicks.Down(&one, position, 5080, PT_MOUSE, extent, 500), "distant release without a move message is not a click");
    clicks.Cancel();
    clicks.Down(&one, position, 6000, PT_MOUSE, extent, 500);clicks.Up(&two, position, drag);
    Check(!clicks.Down(&one, position, 6080, PT_MOUSE, extent, 500), "release on a different target is not a completed click");
    clicks.Cancel();
    clicks.Down(&one, position, 7000, PT_MOUSE, extent, 500);clicks.Cancel();
    Check(!clicks.Down(&one, position, 7080, PT_MOUSE, extent, 500), "capture cancellation clears the click sequence");
    clicks.Cancel();
    clicks.Down(&one, position, 8000, PT_MOUSE, extent, 500);clicks.Up(&one, position, drag);
    Check(!clicks.Down(&one, position, 8080, PT_PEN, extent, 500), "mouse and pen clicks are not combined");
    clicks.Cancel();
    clicks.Down(&one, position, 0xfffffff0u, PT_MOUSE, extent, 500);clicks.Up(&one, position, drag);
    Check(clicks.Down(&one, position, 32, PT_MOUSE, extent, 500), "native message timestamp wrap preserves a valid click pair");
    clicks.Cancel();
    auto temporary = std::make_unique<Target>();
    clicks.Down(temporary.get(), position, 9000, PT_MOUSE, extent, 500);clicks.Up(temporary.get(), position, drag);temporary.reset();
    Check(!clicks.Down(&one, position, 9080, PT_MOUSE, extent, 500), "deleted controls cannot receive a deferred double click");
    clicks.Cancel();
    Check(!clicks.Down(nullptr, position, 10000, PT_MOUSE, extent, 500) && !clicks.Pressed(),
          "dismissed overlays and empty hits cannot arm pointer clicks");
}

void TestListSecondaryText() {
    struct List : ListView {
        using ListView::Measure;using ListView::Arrange;using ListView::OnMouseDown;using ListView::OnMouseUp;
        using ListView::AutomationItemName;
    };
    const Theme theme=MakeTheme();
    List list;
    std::wstring state=L"Queued";
    list.ItemCount(3,false).ItemText([](size_t i,std::wstring& out){out=i==0?L"Document.pdf":i==1?L"Report.docx":L"Notes.txt";});
    list.ItemSecondaryText([&](size_t i,std::wstring& out){out=(i==0?L"PDF | ":i==1?L"Word | ":L"Text | ")+state;});
    list.Measure({360,180},theme);list.Arrange({0,0,360,180});
    list.OnMouseDown({50,60},0x0001);list.OnMouseUp({50,60},0);
    Check(list.SelectedIndex()==1,"two-line list hit testing uses expanded row height");
    Check(list.AutomationItemName(1).find(L"Word | Queued")!=std::wstring::npos,"secondary status is available to accessibility");
    OffscreenRenderer target;Check(target.Init(360,180),"secondary list renderer");
    std::vector<uint8_t> before,after;
    auto paint=[&](std::vector<uint8_t>& pixels){
        Painter painter;painter.BeginFrame(target.BeginDraw(),&UiText(),1);
        painter.FillRect({0,0,360,180},theme.bg);DrawControlTree(painter,theme,&list);
        painter.EndFrame();return target.EndDraw()&&target.ReadBack(pixels);
    };
    Check(paint(before),"paint secondary list initial status");
    state=L"Completed 24 pages";list.RefreshItems();
    Check(list.SelectedIndex()==1&&list.ItemCount()==3,"refreshing row data does not reset selection or count");
    Check(paint(after)&&before!=after,"refreshing secondary provider invalidates cached row pixels");
    Check(target.SavePNG(L"lumen_visual_list_secondary.png"),"save secondary list state scene");
    list.CanReorder(true).MoveItem(0,2);
    Check(list.AutomationItemName(2).find(L"Document.pdf")!=std::wstring::npos&&list.AutomationItemName(2).find(L"PDF | Completed")!=std::wstring::npos,
          "secondary provider follows reordered data indices");
    list.ItemSecondaryText({});list.Measure({360,180},theme);list.Arrange({0,0,360,180});
    list.OnMouseDown({50,60},0x0001);list.OnMouseUp({50,60},0);
    Check(list.SelectedIndex()==2,"removing secondary provider restores single-line row hit testing");
    Check(list.AutomationItemName(0).find(L"|")==std::wstring::npos,"single-line accessibility contract is unchanged");
}

void TestPointer() {
    {
        Window window(L"touch-slop", {320.0f, 96.0f});
        window.Root().Padding(16.0f);
        auto& btn = window.Root().Add<Button>(L"Hit");
        int clicks = 0;
        btn.OnClick([&] { ++clicks; });
        window.LayoutNow();
        const Rect a = btn.AbsoluteBounds();
        Check(a.x > 4.0f, "touch slop button inset");
        const Point outside{a.x - 4.0f, a.y + a.h * 0.5f};
        const Point center{a.x + a.w * 0.5f, a.y + a.h * 0.5f};
        window.DispatchTouchDown(outside);
        Check(window.Hovered() == &btn, "touch slop hits button");
        window.DispatchTouchUp(center);
        Check(clicks == 1, "touch slop press then release clicks");
        window.Close();
    }
    {
        Window window(L"mouse-no-slop", {320.0f, 96.0f});
        window.Root().Padding(16.0f);
        auto& btn = window.Root().Add<Button>(L"Hit");
        int clicks = 0;
        btn.OnClick([&] { ++clicks; });
        window.LayoutNow();
        const Rect a = btn.AbsoluteBounds();
        const Point outside{a.x - 4.0f, a.y + a.h * 0.5f};
        window.DispatchMouseDown(outside);
        window.DispatchMouseUp(outside);
        Check(clicks == 0 && window.Hovered() != &btn, "mouse has no hit slop");
        window.Close();
    }
    {
        Window window(L"touch-pan", {240.0f, 160.0f});
        auto& sv = window.Root().Add<ScrollViewer>().Grow();
        sv.Add<Panel>().SetBounds({0.0f, 0.0f, 40.0f, 800.0f});
        window.LayoutNow();
        Check(sv.ContentHeight() > sv.AbsoluteBounds().h + 1.0f, "touch pan overflow");
        const Rect r = sv.AbsoluteBounds();
        const Point start{r.x + r.w * 0.5f, r.y + 48.0f};
        window.DispatchTouchDown(start);
        window.DispatchTouchMove({start.x, start.y - 24.0f});
        window.DispatchTouchMove({start.x, start.y - 72.0f});
        window.DispatchTouchUp({start.x, start.y - 72.0f});
        Check(sv.OffsetY() > 20.0f, "touch pan increases offset");
        window.Close();
    }
    {
        Window window(L"touch-slider", {280.0f, 80.0f});
        auto& slider = window.Root().Add<Slider>();
        slider.Range(0.0f, 100.0f).Value(0.0f);
        window.LayoutNow();
        const Rect r = slider.AbsoluteBounds();
        const Point at{r.x + r.w * 0.85f, r.y + r.h * 0.5f};
        window.DispatchTouchDown(at);
        Check(slider.Value() > 50.0f, "touch slider tracks");
        window.DispatchTouchUp(at);
        window.Close();
    }
}

void TestDirtyRects() {
    const Rect a{10.0f, 20.0f, 30.0f, 40.0f};
    const Rect b{25.0f, 30.0f, 30.0f, 40.0f};
    const Rect u = UnionRect(a, b);
    Check(u.x == 10.0f && u.y == 20.0f, "union origin");
    Check(u.Right() == 55.0f && u.Bottom() == 70.0f, "union extent");
    const Rect from_empty = UnionRect({}, a);
    Check(from_empty.x == a.x && from_empty.w == a.w && from_empty.h == a.h, "union empty lhs");
    const Rect distant{200.0f, 200.0f, 10.0f, 10.0f};
    const Rect box = UnionRect(a, distant);
    Check(box.x == 10.0f && box.Right() == 210.0f && box.Bottom() == 210.0f, "union disjoint");

    Window window(L"dirty-host", {400.0f, 280.0f});
    auto& col = window.Root().Add<Column>().Spacing(8.0f);
    auto& btn = col.Add<TestDirtyControl>(L"Pad");
    auto& spot = col.Add<TestDirtyControl>(L"Spot");
    spot.Spotlight(true);
    window.Show();
    window.LayoutNow();

    const Rect abs = btn.AbsoluteBounds();
    const Rect dirty = btn.DirtyBounds();
    Check(!abs.IsEmpty(), "dirty button laid out");
    Check(std::fabs(dirty.x - (abs.x - kDirtyPadDip)) < 0.01f, "dirty pad x");
    Check(std::fabs(dirty.y - (abs.y - kDirtyPadDip)) < 0.01f, "dirty pad y");
    Check(std::fabs(dirty.w - (abs.w + kDirtyPadDip * 2.0f)) < 0.01f, "dirty pad w");
    Check(std::fabs(dirty.h - (abs.h + kDirtyPadDip * 2.0f)) < 0.01f, "dirty pad h");

    const Rect spot_abs = spot.AbsoluteBounds();
    const Rect spot_dirty = spot.DirtyBounds();
    Check(!spot_abs.IsEmpty(), "spotlight button laid out");
    Check(spot_dirty.x == spot_abs.x && spot_dirty.y == spot_abs.y &&
              spot_dirty.w == spot_abs.w && spot_dirty.h == spot_abs.h,
          "spotlight dirty is whole control");

    btn.Invalidate();
    window.Invalidate();
    window.Close();
}

void TestUia() {
    Window window(L"uia-host", {420.0f, 220.0f});
    auto& col = window.Root().Add<Column>().Spacing(8.0f);
    int clicks = 0;
    auto& btn = col.Add<Button>(L"UiaInvoke");
    btn.OnClick([&] {
        ++clicks;
        Check(App::HasActiveCallbacks() && !App::CanShutdown(), "UIA invoke blocks reentrant shutdown");
    });
    auto& box = col.Add<CheckBox>(L"UiaCheck");
    auto& slider = col.Add<Slider>();
    slider.Range(0.0f, 100.0f).Value(25.0f);
    std::wstring grid_value = L"12";
    auto& grid = col.Add<Table>().AccessibleName(L"UiaGrid").MinSize({200.0f, 100.0f});
    grid.AddColumn(L"Hidden", 50.0f);
    grid.AddColumn(L"Amount", 100.0f);
    grid.ColumnVisible(0, false).RowCount(2).CellEditEnabled();
    grid.CellText([&](size_t row, size_t, std::wstring& out) { out = row == 0 ? grid_value : L"24"; });
    grid.ValidateCell([](const CellEdit& edit) { return edit.after == L"bad" ? L"Invalid" : L""; });
    grid.OnCellEdited([&](size_t row, int column, std::wstring value) {
        if (row == 0 && column == 1) grid_value = std::move(value);
    });
    window.Show();
    window.LayoutNow();

    IUIAutomation* uia = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IUIAutomation, reinterpret_cast<void**>(&uia));
    Check(SUCCEEDED(hr) && uia, "uia CoCreateInstance");
    if (!uia) {
        window.Close();
        return;
    }

    IUIAutomationElement* root = nullptr;
    hr = uia->ElementFromHandle(static_cast<UIA_HWND>(window.NativeHandle()), &root);
    Check(SUCCEEDED(hr) && root, "uia ElementFromHandle");
    if (!root) {
        uia->Release();
        window.Close();
        return;
    }

    auto find_named = [&](const wchar_t* label) -> IUIAutomationElement* {
        VARIANT name;
        VariantInit(&name);
        name.vt = VT_BSTR;
        name.bstrVal = SysAllocString(label);
        IUIAutomationCondition* cond = nullptr;
        uia->CreatePropertyCondition(UIA_NamePropertyId, name, &cond);
        VariantClear(&name);
        IUIAutomationElement* found = nullptr;
        if (cond) {
            root->FindFirst(TreeScope_Descendants, cond, &found);
            cond->Release();
        }
        return found;
    };

    IUIAutomationElement* found = find_named(L"UiaInvoke");
    Check(found != nullptr, "uia find invoke button");
    if (found) {
        IUIAutomationInvokePattern* invoke = nullptr;
        found->GetCurrentPatternAs(UIA_InvokePatternId, IID_IUIAutomationInvokePattern,
                                   reinterpret_cast<void**>(&invoke));
        Check(invoke != nullptr, "uia invoke pattern");
        if (invoke) {
            hr = invoke->Invoke();
            Check(SUCCEEDED(hr) && clicks == 1, "uia invoke click");
            invoke->Release();
        }
        found->Release();
    }

    found = find_named(L"UiaCheck");
    Check(found != nullptr, "uia find checkbox");
    if (found) {
        IUIAutomationTogglePattern* toggle = nullptr;
        found->GetCurrentPatternAs(UIA_TogglePatternId, IID_IUIAutomationTogglePattern,
                                   reinterpret_cast<void**>(&toggle));
        Check(toggle != nullptr, "uia toggle pattern");
        if (toggle) {
            toggle->Toggle();
            Check(box.Checked(), "uia toggle checks");
            toggle->Release();
        }
        found->Release();
    }

    VARIANT type;
    VariantInit(&type);
    type.vt = VT_I4;
    type.lVal = UIA_SliderControlTypeId;
    IUIAutomationCondition* slider_cond = nullptr;
    uia->CreatePropertyCondition(UIA_ControlTypePropertyId, type, &slider_cond);
    VariantClear(&type);
    IUIAutomationElement* slider_el = nullptr;
    if (slider_cond) {
        root->FindFirst(TreeScope_Descendants, slider_cond, &slider_el);
        slider_cond->Release();
    }
    Check(slider_el != nullptr, "uia find slider");
    if (slider_el) {
        IUIAutomationRangeValuePattern* range = nullptr;
        slider_el->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_IUIAutomationRangeValuePattern,
                                       reinterpret_cast<void**>(&range));
        Check(range != nullptr, "uia range pattern");
        if (range) {
            range->SetValue(80.0);
            Check(slider.Value() > 79.0f && slider.Value() < 81.0f, "uia range set");
            range->Release();
        }
        slider_el->Release();
    }

    found = find_named(L"UiaGrid");
    Check(found != nullptr, "uia finds grid");
    IUIAutomationValuePattern* retained_cell = nullptr;
    if (found) {
        IUIAutomationGridPattern* pattern = nullptr;
        found->GetCurrentPatternAs(UIA_GridPatternId, IID_IUIAutomationGridPattern,
            reinterpret_cast<void**>(&pattern));
        Check(pattern != nullptr, "uia grid pattern");
        if (pattern) {
            int rows = 0, columns = 0;
            pattern->get_CurrentRowCount(&rows); pattern->get_CurrentColumnCount(&columns);
            Check(rows == 2 && columns == 1, "uia grid counts omit hidden columns");
            IUIAutomationElement* cell = nullptr;
            Check(SUCCEEDED(pattern->GetItem(0, 0, &cell)) && cell, "uia obtains virtual cell");
            if (cell) {
                IUIAutomationGridItemPattern* item = nullptr;
                cell->GetCurrentPatternAs(UIA_GridItemPatternId, IID_IUIAutomationGridItemPattern,
                    reinterpret_cast<void**>(&item));
                Check(item != nullptr, "uia grid item coordinates");
                if (item) {
                    int row = -1, column = -1, span = 0;
                    item->get_CurrentRow(&row); item->get_CurrentColumn(&column); item->get_CurrentColumnSpan(&span);
                    Check(row == 0 && column == 0 && span == 1, "uia visible cell coordinates");
                    item->Release();
                }
                cell->GetCurrentPatternAs(UIA_ValuePatternId, IID_IUIAutomationValuePattern,
                    reinterpret_cast<void**>(&retained_cell));
                Check(retained_cell != nullptr, "uia cell value pattern");
                if (retained_cell) {
                    BSTR initial = nullptr;
                    retained_cell->get_CurrentValue(&initial);
                    Check(initial && std::wstring_view(initial) == L"12", "uia cell reads current value");
                    SysFreeString(initial);
                    BSTR valid = SysAllocString(L"36"), invalid = SysAllocString(L"bad");
                    Check(SUCCEEDED(retained_cell->SetValue(valid)) && grid_value == L"36", "uia cell commits mapped data column");
                    Check(FAILED(retained_cell->SetValue(invalid)) && grid_value == L"36", "uia cell rejects invalid edit");
                    SysFreeString(valid); SysFreeString(invalid);
                }
                cell->Release();
            }
            pattern->Release();
        }
        found->Release();
    }
    Check(!App::HasActiveCallbacks(), "idle UIA providers allow host to begin cleanup");
    Check(!App::CanShutdown(), "live UIA providers block shutdown");
    HWND closing_hwnd = static_cast<HWND>(window.NativeHandle());
    window.Close();
    MSG close_msg{};
    while (PeekMessageW(&close_msg, closing_hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&close_msg);
        DispatchMessageW(&close_msg);
    }
    Check(window.Closed(), "UIA window closed with client retained");
    if (retained_cell) {
        BSTR value = SysAllocString(L"48");
        Check(FAILED(retained_cell->SetValue(value)) && grid_value == L"36", "closed grid cell provider refuses mutation");
        SysFreeString(value);
        retained_cell->Release();
    }
    BSTR stale_name = nullptr;
    hr = root->get_CurrentName(&stale_name);
    SysFreeString(stale_name);
    Check(FAILED(hr), "closed UIA provider is unavailable");
    root->Release();
    uia->Release();
}

void TestDebugChecks() {
    Control::SetDebugHandler([](const wchar_t*) { throw std::runtime_error("lumen-debug"); });
    int hits = 0;
    auto trap = [&](auto&& fn) {
        try {
            fn();
        } catch (const std::runtime_error& e) {
            if (std::strcmp(e.what(), "lumen-debug") == 0) ++hits;
        }
    };

    {
        TestRoot root;
        root.Add<Label>(L"a");
        trap([&] { root.Child(999); });
    }
    {
        struct Probe : Label {
            void FakeParent(Panel* p) { parent_ = p; }
        };
        TestRoot root;
        auto owned = std::make_unique<Probe>();
        owned->FakeParent(&root);
        trap([&] { root.Add(std::unique_ptr<Control>(std::move(owned))); });
    }
    {
        Window window(L"debug-check", {240.0f, 120.0f});
        auto& label = window.Root().Add<Label>(L"x");
        std::thread worker([&] { trap([&] { (void)label.Text(); }); });
        worker.join();
        window.Close();
    }

    Control::SetDebugHandler(nullptr);
    Check(hits == 3, "debug checks child/add/thread");
}

void TestDarkGradient() {
    OffscreenRenderer renderer;
    if (!renderer.Init(1024, 512)) {
        Check(false, "dark gradient renderer init");
        return;
    }
    Painter painter;
    painter.BeginFrame(renderer.BeginDraw(), &UiText(), 1.0f);
    painter.FillRect({0, 0, 1024, 512}, Color{0, 0, 0, 1});
    painter.FillRoundedRect({8, 8, 1008, 496}, 24, Color{0.035f, 0.035f, 0.035f, 1});
    painter.FillRoundedRectRadial({8, 8, 1008, 496}, 24, {512, 256}, 600,
                                 Color{1, 1, 1, 0.08f}, Color{1, 1, 1, 0});
    painter.EndFrame();
    Check(renderer.EndDraw(), "dark gradient end draw");
    Check(renderer.SavePNG(L"lumen_visual_gradient.png"), "dark gradient save png");
    std::vector<uint8_t> pixels;
    Check(renderer.ReadBack(pixels), "dark gradient readback");
    if (pixels.size() != 1024u * 512u * 4u) return;
    bool monochrome = true;
    for (size_t i = 0; i < pixels.size(); i += 4) {
        monochrome &= pixels[i] == pixels[i + 1] && pixels[i] == pixels[i + 2];
    }
    Check(monochrome, "dark gradient stays monochrome");
    // Average across the noise tile so grain cannot hide a stepped light ramp.
    // Compare displayed intensity, not the number of stops in the implementation.
    float max_error = 0.0f;
    for (int x = 520; x < 775; ++x) {
        float actual = 0.0f, expected = 0.0f;
        for (int y = 224; y < 288; ++y) {
            actual += pixels[(static_cast<size_t>(y) * 1024u + x) * 4u];
            const float dx = static_cast<float>(x) + 0.5f - 512.0f;
            const float dy = static_cast<float>(y) + 0.5f - 256.0f;
            const float t = std::min(1.0f, std::sqrt(dx * dx + dy * dy) / 270.0f);
            const float alpha = 0.08f * (1.0f - t * t * (3.0f - 2.0f * t));
            // The existing 0..2/255 white noise has mean alpha 1/255.
            expected += (9.0f + 246.0f * alpha) * (254.0f / 255.0f) + 1.0f;
        }
        max_error = std::max(max_error, std::fabs(actual - expected) / 64.0f);
    }
    std::printf("dark gradient max averaged error: %.3f / 255\n", max_error);
    Check(max_error < 1.2f, "dark gradient follows smooth falloff within quantization tolerance");
}

template<class T>
struct PolishProbe : T {
    using T::Arrange;
    using T::Draw;
    void KeyboardFocus(bool value) { this->focused_ = value; }
};

bool ReferenceTriangle(ID2D1DeviceContext2* dc, Point a, Point b, Point c, Color ink) {
    ComPtr<ID2D1Factory> factory;
    dc->GetFactory(&factory);
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    ComPtr<ID2D1SolidColorBrush> brush;
    if (!factory || FAILED(factory->CreatePathGeometry(&path)) ||
        FAILED(path->Open(&sink)) ||
        FAILED(dc->CreateSolidColorBrush({ink.r, ink.g, ink.b, ink.a}, &brush))) return false;
    sink->BeginFigure({a.x, a.y}, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine({b.x, b.y});
    sink->AddLine({c.x, c.y});
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) return false;
    dc->FillGeometry(path.get(), brush.get());
    return true;
}

void TestTriangleTransforms() {
    OffscreenRenderer target;
    if (!target.Init(384, 320)) { Check(false, "triangle renderer init"); return; }
    Painter painter;
    const Point triangles[][3] = {
        {{12.25f, 16.5f}, {60.75f, 20.25f}, {34.5f, 58.75f}},
        {{70, 12}, {72, 65}, {112, 35}}, // reversed winding
        {{18, 72}, {96, 72.125f}, {54, 72.25f}}, // thin
        {{18, 86}, {45, 86}, {72, 86}}, // collinear
        {{24, 100}, {24, 100}, {24, 100}}, // coincident
        {{32, 28}, {90, 50}, {34, 62}}, // translucent overlap
    };
    bool restored = true, reference_ok = true, readback_ok = true;
    int max_delta = 0;
    for (float scale : {1.0f, 1.25f, 1.5f, 2.0f}) {
        for (int mode = 0; mode < 5; ++mode) {
            std::vector<uint8_t> pixels[2];
            for (int pass = 0; pass < 2; ++pass) {
                auto* dc = target.BeginDraw();
                painter.BeginFrame(dc, &UiText(), scale);
                dc->Clear({0, 0, 0, 0});
                // Keep the clip in caller space while triangle transforms change.
                painter.PushClip({8, 8, 105, 103});
                if (mode == 1) painter.PushTranslate(4.25f, 6.5f);
                if (mode == 2) painter.PushRotate({64, 56}, 17.0f);
                if (mode == 3) painter.PushScale({64, 56}, -0.85f, 0.65f);
                if (mode == 4) {
                    painter.PrepareRoundedClip({10, 10, 100, 98}, 12);
                    painter.PushRoundedClip({10, 10, 100, 98}, 12);
                    painter.PushOpacity(0.65f);
                }
                D2D1_MATRIX_3X2_F before;
                dc->GetTransform(&before);
                for (const auto& points : triangles) {
                    const Color ink{1, 1, 1, mode % 2 == 0 ? 0.375f : 1.0f};
                    if (pass == 0) {
                        reference_ok = ReferenceTriangle(dc, points[0], points[1], points[2], ink) && reference_ok;
                    } else {
                        painter.FillTriangle(points[0], points[1], points[2], ink);
                        // Transparent early-return must leave state alone too.
                        painter.FillTriangle(points[2], points[1], points[0], {1, 1, 1, 0});
                    }
                    D2D1_MATRIX_3X2_F after;
                    dc->GetTransform(&after);
                    restored = restored && std::memcmp(&before, &after, sizeof(before)) == 0;
                }
                painter.FillRect({92, 90, 6, 6}, {1, 1, 1, 1});
                if (mode >= 1 && mode <= 3) painter.PopTransform();
                if (mode == 4) { painter.PopOpacity(); painter.PopRoundedClip(); }
                painter.PopClip();
                painter.EndFrame();
                readback_ok = target.EndDraw() && readback_ok;
                readback_ok = target.ReadBack(pixels[pass]) && readback_ok;
            }
            if (pixels[0].empty() || pixels[0].size() != pixels[1].size()) {
                readback_ok = false;
                continue;
            }
            for (size_t i = 0; i < pixels[0].size(); ++i) {
                max_delta = std::max(max_delta, std::abs(static_cast<int>(pixels[0][i]) - pixels[1][i]));
            }
        }
    }
    Check(reference_ok && readback_ok, "triangle reference render/readback");
    Check(restored, "triangle fill restores caller transform including degenerate/transparent cases");
    std::printf("triangle reference max channel delta=%d/255\n", max_delta);
    Check(max_delta <= 2, "triangle pixels match reference at 100/125/150/200 percent, clips and opacity");
}

void TestPolylineBatches() {
    OffscreenRenderer target;
    if (!target.Init(384, 320)) { Check(false, "polyline renderer init"); return; }
    Painter painter;
    std::array<Point, 2049> points;
    bool ok = true, restored = true;
    int max_delta = 0;
    for (int count : {2, 3, 256, 257, 258, 513, 2049}) {
        for (int i = 0; i < count; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(count - 1);
            points[i] = {12.25f + 145.0f * t, 60.5f + 24.0f * std::sin(t * 18.0f)};
        }
        if (count > 257) {
            points[255].y = 100.0f;
            points[256] = points[255];
            points[257].y = 30.0f;
        }
        if (count == 513) points[count - 1] = points[0];
        for (float scale : {1.0f, 1.25f, 1.5f, 2.0f}) {
            for (bool dashed : {false, true}) {
                std::vector<uint8_t> pixels[2];
                for (int pass = 0; pass < 2; ++pass) {
                    auto* dc = target.BeginDraw();
                    painter.BeginFrame(dc, &UiText(), scale);
                    dc->Clear({0, 0, 0, 0});
                    painter.PushClip({10, 8, 150, 115});
                    if (dashed) {
                        painter.PushRotate({80, 60}, 9.0f);
                        painter.PushScale({80, 60}, 0.85f, 1.15f);
                    }
                    D2D1_MATRIX_3X2_F before;
                    dc->GetTransform(&before);
                    const Color ink{1, 1, 1, 0.5f};
                    const float width = dashed ? 3.25f : 1.4f;
                    if (pass == 0) {
                        ComPtr<ID2D1Factory> factory;
                        ComPtr<ID2D1PathGeometry> path;
                        ComPtr<ID2D1GeometrySink> sink;
                        ComPtr<ID2D1SolidColorBrush> brush;
                        ComPtr<ID2D1StrokeStyle> style;
                        dc->GetFactory(&factory);
                        D2D1_STROKE_STYLE_PROPERTIES props{};
                        props.startCap = props.endCap = props.dashCap = D2D1_CAP_STYLE_ROUND;
                        props.lineJoin = D2D1_LINE_JOIN_ROUND;
                        props.miterLimit = 10.0f;
                        props.dashStyle = dashed ? D2D1_DASH_STYLE_CUSTOM : D2D1_DASH_STYLE_SOLID;
                        const FLOAT dashes[] = {4.0f, 3.0f};
                        const bool ready = factory && SUCCEEDED(factory->CreatePathGeometry(&path)) &&
                            SUCCEEDED(path->Open(&sink)) &&
                            SUCCEEDED(dc->CreateSolidColorBrush({1, 1, 1, 0.5f}, &brush)) &&
                            SUCCEEDED(factory->CreateStrokeStyle(props, dashed ? dashes : nullptr,
                                                                  dashed ? 2u : 0u, &style));
                        ok = ready && ok;
                        if (ready) {
                            sink->BeginFigure({points[0].x, points[0].y}, D2D1_FIGURE_BEGIN_HOLLOW);
                            for (int i = 1; i < count; ++i) sink->AddLine({points[i].x, points[i].y});
                            sink->EndFigure(D2D1_FIGURE_END_OPEN);
                            ok = SUCCEEDED(sink->Close()) && ok;
                            dc->DrawGeometry(path.get(), brush.get(), width, style.get());
                        }
                    } else {
                        painter.StrokeOpenPolyline(points.data(), count, ink, width, dashed);
                        painter.StrokeOpenPolyline(nullptr, count, ink, width, dashed);
                        for (int invalid : {-1, 0, 1})
                            painter.StrokeOpenPolyline(points.data(), invalid, ink, width, dashed);
                        painter.StrokeOpenPolyline(points.data(), count, ink, 0, dashed);
                        painter.StrokeOpenPolyline(points.data(), count, {1, 1, 1, 0}, width, dashed);
                    }
                    D2D1_MATRIX_3X2_F after;
                    dc->GetTransform(&after);
                    restored = std::memcmp(&before, &after, sizeof(before)) == 0 && restored;
                    if (dashed) { painter.PopTransform(); painter.PopTransform(); }
                    painter.PopClip();
                    painter.EndFrame();
                    ok = target.EndDraw() && ok;
                    ok = target.ReadBack(pixels[pass]) && ok;
                }
                if (pixels[0].empty() || pixels[0].size() != pixels[1].size()) { ok = false; continue; }
                for (size_t i = 0; i < pixels[0].size(); ++i)
                    max_delta = std::max(max_delta, std::abs(static_cast<int>(pixels[0][i]) - pixels[1][i]));
            }
        }
    }
    Check(ok && restored, "polyline reference render/readback and caller state");
    std::printf("polyline batch reference max channel delta=%d/255\n", max_delta);
    Check(max_delta == 0, "polyline batches preserve joins/dash phase at boundaries and 100/125/150/200 percent");
}

void TestQuietStates() {
    Panel card;
    card.Card(Panel::CardStyle::Subtle, 16.0f);
    Check(!card.Spotlight(), "subtle card is static by default");
    card.Card(Panel::CardStyle::Lumen, 16.0f);
    Check(card.Spotlight(), "lumen card explicitly enables spotlight");
    card.Card(Panel::CardStyle::Subtle, 16.0f);
    Check(!card.Spotlight(), "changing to subtle clears previous spotlight");
    card.Spotlight(true);
    Check(card.Spotlight(), "static card supports explicit spotlight opt-in");

    OffscreenRenderer target;
    if (!target.Init(480, 180)) { Check(false, "quiet states renderer"); return; }
    for (float intensity : {0.0f, 0.5f, 1.0f}) {
        const Theme theme = MakeTheme(intensity);
        PolishProbe<Button> standard;
        PolishProbe<Button> primary;
        PolishProbe<CheckBox> checkbox;
        PolishProbe<ComboBox> combo;
        standard.Arrange({20.0f, 20.0f, 120.0f, 40.0f});
        primary.Kind(ButtonKind::Primary);
        primary.Arrange({170.0f, 20.0f, 120.0f, 40.0f});
        primary.KeyboardFocus(true);
        checkbox.Arrange({320.0f, 20.0f, 120.0f, 40.0f});
        combo.Arrange({20.0f, 100.0f, 120.0f, 40.0f});
        Painter painter;
        painter.BeginFrame(target.BeginDraw(), &UiText(), 1.0f);
        painter.FillRect({0.0f, 0.0f, 480.0f, 180.0f}, theme.bg);
        standard.Draw(painter, theme);
        primary.Draw(painter, theme);
        checkbox.Draw(painter, theme);
        combo.Draw(painter, theme);
        painter.EndFrame();
        Check(target.EndDraw(), "quiet states enddraw");
        Color pixel{};
        Check(target.ReadPixel(20, 40, pixel) && pixel.r > 0.06f,
              "standard button boundary survives glow settings");
        Check(target.ReadPixel(320, 40, pixel) && pixel.r > 0.06f,
              "unchecked checkbox boundary survives glow settings");
        Check(target.ReadPixel(20, 120, pixel) && pixel.r > 0.06f,
              "combo boundary survives glow settings");
        Check(target.ReadPixel(220, 18, pixel) && pixel.r > 0.5f,
              "primary keyboard focus survives glow settings");
        if (intensity == 0.0f) Check(target.SavePNG(L"lumen_visual_quiet_states.png"), "save quiet states");
    }
    TestDialog short_title;
    short_title.Title(L"Confirm").Message(L"Message").PrimaryButton(L"Continue");
    TestDialog long_title;
    long_title.Title(L"Confirm the project settings before replacing the current configuration")
        .Message(L"Message").PrimaryButton(L"Continue");
    const Theme theme = MakeTheme(0.0f);
    const Size short_size = short_title.Measure({320.0f, 800.0f}, theme);
    const Size long_size = long_title.Measure({320.0f, 800.0f}, theme);
    Check(long_size.h > short_size.h + 20.0f, "wrapped dialog title reserves extra height");
}

struct ProbeTextBox : TextBox {
    using TextBox::TextBox;
    using TextBox::AutomationValue;
    using TextBox::AutomationIsPassword;
};
struct ProbePasswordBox : PasswordBox {
    using PasswordBox::PasswordBox;
    using TextBox::AutomationValue;
    using TextBox::AutomationIsPassword;
};

void TestReviewFixes() {
    {
        ProbePasswordBox pwd(L"review-secret");
        const std::wstring shown = pwd.AutomationValue();
        Check(pwd.AutomationIsPassword() && shown.size() == 13 &&
                  shown.find(L"review-secret") == std::wstring::npos,
              "password UIA value is masked");
        ProbeTextBox plain(L"hello");
        Check(plain.AutomationValue() == L"hello", "text box UIA value stays plain");
    }
    {
        Property<bool> flag{true};
        Button original;
        original.BindEnabled(flag);
        WeakRef<Button> watch(&original);
        Button moved(std::move(original));
        Check(watch.Get() == &moved, "weak ref follows control move construction");
        flag = false;
        Check(!moved.Enabled() && original.Enabled(), "enabled binding follows move construction");
        Button target;
        WeakRef<Button> watch_target(&target);
        target = std::move(moved);
        Check(watch.Get() == &target && watch_target.Get() == &target,
              "weak refs converge after move assignment");
        flag = true;
        Check(target.Enabled() && !moved.Enabled(), "rebound binding acts on move target");
    }
    {
        Property<int> base{2};
        Computed<int> doubled([&] { return base.Get() * 2; }, base);
        Check(doubled.Get() == 4, "computed evaluates eagerly");
        base = 5;
        Check(doubled.Get() == 10, "computed tracks dependency change");
        Computed<int> moved(std::move(doubled));
        base = 6;
        Check(moved.Get() == 12 && doubled.Get() == 12,
              "computed move shares value and transfers subscriptions");
    }
    {
        Window window(L"mcp-review-fixes", {320.0f, 200.0f});
        auto& parent = window.Root().Add<Column>();
        auto& button = parent.Add<Button>(L"Action");
        int clicks = 0;
        button.OnClick([&] { ++clicks; });
        window.Show();
        window.LayoutNow();
        button.Focus();
        button.Visible(false);
        window.DispatchKey(VK_RETURN);
        Check(clicks == 0, "hidden focused control ignores keys");
        button.Visible(true);
        button.Focus();
        parent.Enabled(false);
        window.DispatchKey(VK_RETURN);
        Check(clicks == 0, "disabled ancestor blocks focused key");
        parent.Enabled(true);
        window.DispatchKey(VK_RETURN);
        Check(clicks == 1, "usable focus still dispatches");
        window.Close();
    }
    {
        VectorModel<int> model;
        model.Reset({1, 2, 3});
        Table table;
        table.AddColumn(L"V");
        table.Bind(model);
        {
            UpdateScope scope;
            model.RemoveAt(0);
        }
        Check(table.RowCount() == 2, "removal inside update scope keeps table rows fresh");
    }
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // 崩溃时也要能看到已通过的断言
    AddVectoredExceptionHandler(1, CrashReport);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        std::printf("[FAIL] CoInitializeEx\n");
        return 1;
    }
    if (argc > 1 && std::strcmp(argv[1], "--paragraph") == 0) {
        TestParagraphEditing();
        CoUninitialize();
        std::printf("%s\n", g_failures == 0 ? "ALL PASS" : "FAILURES PRESENT");
        return g_failures == 0 ? 0 : 1;
    }
    TestParagraphEditing();
    TestSignal();
    TestReviewFixes();
    TestLayout();
    TestTypography();
    TestTriangleTransforms();
    TestPolylineBatches();
    TestQuietStates();
    control_usability::Run(Check);
    TestInteraction();
    TestImageViewRendering();
    TestExtras();
    TestGlowPrimitives();
    TestDarkGradient();
    TestAcrylic();
    TestChoreography();
    TestDefaultChrome();
    TestInjectedInput();
    TestEscapeShortcut();
    TestHwndFocus();
    TestPopupWindow();
    TestEditableComboChrome();
    TestCompactToolWindow();
    TestNestedComboFlyout();
    TestTableHeaderCheck();
    TestStructuredLogView();
    TestStatusColors();
    TestSkeletonMotion();
    TestColorLight();
    TestLayoutContract();
    TestTypedEditSafety();
    TestHiddenAnimation();
    TestPointerDoubleClickRecognition();
    TestListSecondaryText();
    TestPointer();
    TestDirtyRects();
    TestUia();
    TestDebugChecks();
    TestCustomFonts();
    TestTableCharacterFont();
    TestLumaTextAlignmentCache();
    TestTablePaintStability();
    TestTableTypography();
    TestWindowContentMeasure();
    TestHostCycle();
    RenderScene(L"lumen_visual_dark.png");
    RenderListScene(L"lumen_visual_lists.png");
    RenderExtrasScene(L"lumen_visual_extras.png");
    CoUninitialize();
    std::printf("%s\n", g_failures == 0 ? "ALL PASS" : "FAILURES PRESENT");
    return g_failures == 0 ? 0 : 1;
}