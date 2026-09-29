# 用 LUMEN 写应用

视觉与布局遵守 [constraints.md](constraints.md) 对应章节。下面的路径相对 LUMEN 源码根目录；复制技能到应用工程后，从所接入的库中查找公共头与示例。

## 接入

```cmake
# 源码；也可通过 FetchContent 接入并固定发布标签
add_subdirectory(path/to/LUMENUI)
lumen_add_executable(myapp main.cpp app.rc)

# 或预编译 SDK（替代上面的 add_subdirectory）
# set(lumen_DIR "C:/libs/lumen-sdk-windows-x64/lib/cmake/lumen")
# find_package(lumen CONFIG REQUIRED)
# lumen_add_executable(myapp main.cpp app.rc)
```

`lumen_add_executable` 设置 WIN32、链接 `lumen::lumen` / `lumen::main`、拷贝启用的 LumaText 运行库并配置 `/utf-8`。自行创建 target 时使用 `lumen_copy_runtime(target)`；源码项目的 `lumen` 与 `lumen::lumen` 等价。模板见 `examples/template/`。

CMake 入口由 `lumen::main` 提供 `wWinMain` → `lumen_main`；非 CMake 可用 `<lumen/wmain.h>` + `LUMEN_MAIN()`，此时不再链接 `lumen::main`。按需 include 控件头；`lumen/lumen.h` 可用于演示，静态库按控件链接由编译单元边界保证。

```cpp
#include <lumen/Button.h>
#include <lumen/Main.h>
#include <lumen/Window.h>
int lumen_main(std::span<const std::wstring_view>) {
    return lumen::Run(L"演示", [](lumen::Window& w) {
        w.Root().Add<lumen::Button>(L"你好", lumen::ButtonKind::Primary);
    });
}
```

`Window(title)` 默认 Client 帧、`Backdrop::All`、960×640；三参构造不自动使用这些外观默认值。构造会 `App::Ensure()`，宿主模式见下文。

声明式嵌套用 `Children(...)`，`Ref(ptr)` 取得控件指针。布局、Grow、Grid 和 ScrollViewer 语义见 constraints.md，使用 `Density::Compact` / `Comfortable()` / `Dense()` 调整密度。

## 绑定、事件与提交

`BindText(Property&)`、`BindChecked`、`BindSelectedIndex`、`BindValue`、`BindEnabled`、`BindVisible` 是属性绑定，返回控件引用。事件的 `BindX(fn)` 返回 `Connection`，丢弃会立即断开；持久链式事件订阅用 `OnX(fn)`，手动管理生命周期则保存 Connection/ScopedConnection。绑定的 Property 和模型对象应活过使用它们的控件；`FilteredModel` / `SortedModel` 装饰源模型。

| 控件 | 程序赋值 | 用户操作与提交 |
| --- | --- | --- |
| TextBox | `Text(x)` 同值不打断 IME、光标或撤销；换值重置但不发 TextChanged，选区变化仍可能发 SelectionChanged | 打字/粘贴/IME 提交发 TextChanged；选区变化发 SelectionChanged；Enter 提交动作走 OnSubmit |
| NumberBox | `Value(x)` 钳制、格式化，不发 ValueChanged，非有限值忽略 | 步进从当前合法草稿出发，非法草稿用最近提交值；方向键/失焦/UIA 提交有效变化，同值不重复通知；UIA 非法文本拒绝且保留原值 |
| Switch / CheckBox / ToggleButton | `Checked(x)` 静默 | 用户切换发 Toggled |
| ComboBox / Segmented | 有效 `SelectedIndex(x)` 选择变化发事件；ComboBox 多选重设还会重建选集并通知 | 选择即提交，回调同步须防循环 |
| Slider | `Value(x)` 静默 | 拖动连续发 ValueChanged |
| Form / FormField | 程序改值后 `ValidateAll()` 重验；Child/Validate 配置阶段建立字段挂钩 | 输入和数字提交自动重验；提交前仍须 `ValidateAll()`，不只看 `Valid()` |

- Form 字段示例：`Field(L"名称").Validate(validate::Required()).Add<TextBox>().BindText(name)`，提交按钮可 `BindEnabled(form.Valid())`。`validate::Rule` 的独立 struct 支持 `|` 组合，不改成 std::function 别名。
- 焦点观察用 `OnFocused([](bool){})`；IME 组合态用 `TextBox::Composing()`，不为转发焦点单独派生控件。
- 严格 ASCII 字段可用 `TextBox::ImeEnabled(false)`；NumberBox 和 ColorPicker 的 hex 字段默认关闭组字。只临时解除本 LUMEN HWND 的输入法上下文，离开字段即恢复，不修改进程语言或宿主窗口。
- 可编辑 ComboBox 的 `CommitText()` 将精确匹配的文本关联到对应选择项；再次展开定位当前行。下拉可用空间不足时自动使用独立 LUMEN 弹窗，展开操作与 UIA Expand 不阻塞调用方；Tab 提交并进入下一个字段，Esc 只收起下拉。
- `Confirm` / `Prompt` 用回调；不在 UI 线程 `future.get()` 阻塞消息泵。
- `RunAsync` 返回 TaskHandle：Cancel 协作取消，Status/Error 查询终态，异常走 OnTaskFailed，窗口销毁后结果 Dropped。跨线程回 UI 用 `Window::Post`；返回 Closed/WakeFailed 表示拒绝且不会执行。

## Table 契约

先 `Bind(VectorModel<T>&)` 再 `Column(title, &T::mem)`。嵌套列类型为 `ColumnDef`；`SelectedIndex()` 是排序/展开后的视图行，数据行用 `SelectedDataIndex()`（ListView 同理）。

- 数字成员默认文本显示、数值排序；精度用 `ColumnPrecision`，自定义数字访问用 `BindNumber`。进度条必须显式 `.Progress(get)`。
- 三参 BindNumber 或类型化数字成员默认可编辑；两参 BindNumber 只读。`CellEditable(谓词)` 限定逐格编辑。Enter/Tab 严格解析：非法保持编辑器供修正，失焦非法回退不写回；Tab/Shift+Tab 按视图序跳到下一可编辑格。
- 排序激活时模型变动实时重排；单元格编辑期间延后到编辑结束，避免输入行移动。未排序的少量 `At(i,v)` 更新只失效可见受影响行与页脚；页脚数字聚合用数值访问快路径并遵守列精度，不固定 HUD dirty 数量。
- 类型化数字成员按目标类型校验：整数拒绝小数和越界值，浮点成员拒绝溢出；Enter/Tab 拒绝后保留草稿。当前数字通道仍基于 double，不承诺超过 2^53 的整数精确编辑。
- 基础模型先销毁时，FilteredModel/SortedModel 变为空模型并通知视图；Table 清除类型访问闭包、取消编辑并清空行。模型与视图的变更/销毁仍在 UI 线程进行。
- 布局查询用 `ColumnPixelWidth` / `ColumnsPixelWidth` / `NaturalHeight`，不复制表头、滚动条和弹性宽度常量。

## 浮层与宿主

Toast / Dialog / Drawer 已有亚克力与海拔，不叠加自制半透明黑遮罩。Primary 按钮白底，Danger 为红色实心警示；错误/警告/成功/信息等特殊状态用 Theme 状态色（见 constraints.md 设计语言），应用侧不自行硬编码 RGB；追光需按 constraints.md 显式开启。

色彩与光的应用侧入口：
- 图表：`Chart` 默认按 `Theme::chart_series` 上类别色；同页多图用 `PaletteOffset(n)` 错开主色，需要旧的白/灰阶外观时 `Monochrome(true)`。自绘数据系列取 `ChartSeriesColor(theme, i)`。
- 光色温：`window.LightTone(LightTone::Cool | Warm | Neutral)`，只影响辉光/聚光/镜面线；与 `GlowIntensity` 独立，可同时调节。
- 一次性结果反馈：`button.Flash(StatusTone::Success)`（Warning / Danger / Info 同理），并配合 `ShowToast(text, ToastKind::…)` 或文字说明结果；颜色只是附加提示。

无标题栏、非模态工具窗用 `WindowSpec.titleBar=false` 与 `owner`。`Show(false)` 显示但不抢原生键盘焦点，仍发送显示事件；`Show()` 保持原有激活行为。不要用长期阻塞的 `ShowPopup` 承载格式条。

`Window::ShowPopup(content, anchor, width, closed)` 借用未挂载的控件树，阻塞至收起；调用期间内容须存活。每个 UI 线程只允许一个会话，重入请求被忽略，不排队。锚点跟随主窗布局/移动，外点、Esc、`ClosePopup()`、锚点失效或主窗隐藏/销毁会收起。正常收起后在 UI 线程调用 `closed`；owner 销毁时不调用。返回后内容解除窗口绑定；超高内容的滚动和复杂编辑器 IME 不属于当前已验收能力。

AutoCAD .arx / 插件 DLL：

- 第一个窗口前 `App::HostMode(true)`，`WindowSpec.owner` 传宿主 HWND；原生嵌入设置 `WindowSpec.parent`，可选 `frameTarget` 将标题栏和 Resize 操作交给外壳；`NativeHandle()` 返回子窗。卸载前所有 Window 析构，确认 `App::CanShutdown()` 后 `App::Shutdown()`；UIA 引用或后台任务未结束时不得卸载模块。
- RunAsync 的结果状态不等于线程已经退出；`RunningTasks()` / `CanShutdown()` 会等待系统线程退出（含闭包和 TLS 析构）再回收。后台函数及其线程局部对象不得无限阻塞，不能绕过门禁直接卸载。
- 宿主自行泵消息，不创建 `App app` 或调用 `App::Run` / `lumen::Run`；跨线程使用 Window::Post。
- 窗口失焦自动清焦点并在重新聚焦时恢复；`Window::ClearFocus()` / `Control::Blur()` 显式清除后不恢复。原生消息用 OnNativeMessage/BindNativeMessage，回调内不泵消息。
- `App::AddFont(bytes | path)` 返回族名，供 `Label::FontFamily` / `RichLabel::Font` 使用。`App::LumaTextLibrary(path)` 在首窗创建前设置且显式路径优先，也可将 DLL 放在 .arx 旁。
- 宿主主窗的模态禁用与恢复由调用方管理。
- 原生外壳 WM_SIZE 将子窗填满客户区；WM_GETMINMAXINFO 转发子窗以应用 MinSize；WM_NCHITTEST 可转发子窗复用 Client 标题栏。外壳应用 WM_DPICHANGED 的建议位置后可转发给子窗，子窗仅更新 DPI 与布局。
- 嵌入子窗 Close 先执行 OnClosing，允许后异步发送外壳 WM_CLOSE；外壳 WM_CLOSE 执行自己的关闭协议，不能再转发子窗 WM_CLOSE。外壳取消操作可先转发子窗 WM_CLOSE 以执行否决逻辑；窗口析构必须由创建该 LUMEN 窗口的线程完成。
- MFC 外壳的 PreTranslateMessage 对 LUMEN 子窗及后代返回 FALSE，让 LUMEN 处理 Tab、Enter、Esc 和 IME；子窗提供 WM_GETDLGCODE 全键盘标志。外壳与子窗应避免相互同步等待；Resize 对外壳使用异步 SetWindowPos。
- HostMode 的帧呈现可退让，默认约 60Hz；无需增加渲染线程。菜单与独立弹出内容的原生 owner 为根外壳，保持不激活显示，输入和 IME 仍以 LUMEN 子窗为源。

## 验证与排障

应用代码改动须构建成功并验证受影响的界面/交互；窗口变化检查相关 DPI、Tab 焦点与已开启的聚光。纯文档改动仅核对链接和接口；不触发整库回归。不能自动验证的真实鼠标手感、IME 或宿主操作列为待人工确认。

用 `LUMEN_LOG=path` 或 `SetLogSink` 查看诊断；Gallery F12 可 `DumpTree`。文字质量异常时核对 LumaText 是否启用、DLL 路径/依赖及日志，不把所有问题都归因于漏拷 DLL；库允许回退 DirectWrite。

自绘编辑面的缩放/移动光标通过 `CursorAt()` 返回 `CursorShape::SizeNWSE`、`SizeNESW`、`SizeAll`、`Cross`；原有 Arrow/IBeam/Hand/SizeWE/SizeNS 数值不变，捕获期间仍按捕获控件查询，无需应用层 Win32 光标覆盖。

## 自定义段落与页内编辑

需要自动换行、明确字号/字体、对齐或文档式编辑时，使用 TextBox 的显式段落模式：

```cpp
TextTypography font;
font.family = L"Arial";
font.size = 16.0f; // DIP, not a PDF point value
font.line_height = 19.2f;
editor.Multiline(true).Typography(font).WordWrap(true);
```

`TextTypography` 包含字体族、DIP 字号、字重、斜体、下划线、对齐和可选行高。`WordWrap` 为视觉软换行，不在 Text() 中插入额外换行符。既有单行/硬换行字段默认保持原行为。文档内容可使用 `Chrome(false)` / `ContentPadding(...)`；`Foreground` 与 `SelectionFill` 表示内容颜色，`TextBackdrop` 仅提供文字合成底色提示，不填充背景，也不新增亮色 UI 主题。

混排正文用 `RichLabel`：`Add/Strong/Italic/Secondary/Code/Colored/Tone/Link/Font` 逐段追加，或 `Markup(L"**粗** *斜* `代码` [文字](目标)")` 配合 `OnLink(target)`；`Role(TextRole)` 定基础字号，`Alignment` 控制对齐。中日韩无空格文本按宽折行；`Selectable(true)` 后可拖选（可跨行）、双击选词、Ctrl+A/C。选区限于单个控件：需要整体选中的多段混排写进同一个 RichLabel，段间用 `Add(L"\n")`。左对齐时期望宽度为 `min(内容宽, 可用宽)`，需要占满时用 Stretch 容器或 `Grow()`。RichLabel 只读，不提供富文本编辑。

`TextLayout::Layout(text, typography, spans, width, wrap)` 接受 `TextSpanStyle` 区间（字重、斜体、字号、字体族、下划线、删除线，参与换行与度量）；`Prepare/Draw` 的 `TextColorSpan` 重载按区间换色而不重排，区间须升序且不重叠。

`TextLayout` 共享排版、Lines、HitTest、Caret 与 Selection 几何；它通过现有字体服务排版，再交给 LumaText 字形绘制。普通字体、任意系统字体及斜体不必在进入编辑时切换渲染器。自绘内容先调用 `Prepare`，再调用同帧 `Draw`；几何/字形准备和缓存更新不放在 Draw 中。对象的尺寸和命中位置都是 DIP，DPI 只在 Painter 中转换。

TextBox 的 `ContentSize(width)` 返回不含内边距的内容尺寸；`CaretBounds()` 和 `PlaceCaret(point)` 使用控件局部 DIP；`VisualLineCount()` 统计软/硬换行后的视觉行。IME 组合串参与同一布局；组字取消不删除原选区，提交后才进行一次可撤销替换。程序设置 Text() 仍遵守既有静默/文档重置约定，格式设置不吞掉文字撤销栈。

## 双行列表与外部状态刷新

`ListView::ItemSecondaryText(provider)` 为同一数据行提供副文本（类型、页码、状态等）。与 ItemText 一样，provider 接收数据行下标；重排后仍按 DataIndex 对应原数据。开启副文本后行高至少 52 DIP，测量、绘制、点击命中与滚动使用同一高度；不提供该回调时保持既有单行布局。

外部数据变化后调用 `RefreshItems()`：清除行绘制缓存但不清选中项、滚动或重排状态。替换 provider 或 ItemCount 重设同样会刷新缓存，避免同数量数据更新后继续显示旧内容。读屏/自动化的行名称同时包含主文本与副文本。

```cpp
list.ItemText([&](size_t row, std::wstring& text) { text = files[row].name; })
    .ItemSecondaryText([&](size_t row, std::wstring& text) { text = files[row].status; });
// On the UI thread after updating the external status:
list.RefreshItems();
```

副文本不是业务状态存储；转换/合并进度、线程通信与文件事务仍由应用管理。使用 `ProgressBar::Indeterminate(true)` 表示无法准确量化的阶段；只有已知真实分母时设置 Value，不能用动画假装百分比。
