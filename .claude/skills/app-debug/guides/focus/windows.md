# Windows Debug 焦点

应用启动不带焦点参数时正常聚焦，包含 Debug 产物；启动时 `--no-focus` 使新实例不聚焦，
调试技能默认显式指定它。运行中使用 `--focus-mode focus|no-focus` 设置策略，
省略与重连沿用当前模式。调用方式与返回值见[运行指南](../runtime.md#pc-焦点模式)。

## 实际入口

| 操作 | 本项目入口 |
|---|---|
| 不聚焦启动 | `app.start --no-focus`，CLI 向应用追加 `--no-focus` |
| 运行中切换 | `window.focus-mode <focus\|no-focus>` |
| 单次动作切换 | `window.resize`、`window.maximize`、`window.tray` 的 `--focus-mode` |
| 状态查询 | `app.info`、窗口指令返回的 `focus_mode` |
| 实现位置 | `extras/debug/window_focus.{h,cpp}`、`mainwindow.cpp`、`window/main_window.cpp`、`platform/win/main_window_win.cpp` |

## 显示与层级

1. 主窗口以隐藏的普通窗口创建，首次显示前设置模式并接入原生消息；恢复最大化记录时使用下方最大化入口。
2. `no-focus` 的原生显示请求使用 `SWP_SHOWWINDOW | SWP_NOACTIVATE`。Qt 通过 `SW_SHOWNOACTIVATE` 完成首次显示与还原。
3. 位置请求生效前，在 `WM_WINDOWPOSCHANGING` 中把 `insertAfter` 设为当前前台窗口；前台不存在或是 topmost 时使用 `HWND_BOTTOM`，并清除 `SWP_NOZORDER`。
4. 调试移动和改尺寸在请求入口加 `SWP_NOACTIVATE`。层级适配只包围本次调试自动操作，用户手动激活窗口沿用正常交互。
5. `focus` 关闭该适配，模式切换本身只更新策略，不主动激活窗口。

`WM_WINDOWPOSCHANGING` 中追加的 `SWP_NOACTIVATE` 会被 Windows 忽略，必须在调用入口设置。
焦点和遮挡分别检查：后台窗口仍可能被提到终端前面，显示后再降层会产生短暂遮挡。

## 窗口状态

| 操作 | `no-focus` 原生入口 |
|---|---|
| 最小化 | `ShowWindow(hwnd, SW_SHOWMINNOACTIVE)` |
| 普通还原 | `ShowWindow(hwnd, SW_SHOWNOACTIVATE)` |
| 最大化 | 临时窗口类型适配后同步调用 `ShowWindow(hwnd, SW_MAXIMIZE)` |

最大化保持以下顺序：

1. 最小化窗口先用 `SW_SHOWNOACTIVATE` 恢复普通状态。
2. 保存 `GWL_STYLE`，临时设置 `WS_CHILD` 并清除 `WS_POPUP`，避开顶层窗口的激活路径。
3. 同步调用 `ShowWindow(SW_MAXIMIZE)`，让 Windows 更新最大化状态、尺寸和普通还原矩形。
4. 恢复原来的 `WS_CHILD / WS_POPUP` 位，保留本次产生的 `WS_MAXIMIZE` 等其他样式。
5. 使用 `SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE` 刷新外框。
6. 整组操作在窗口所属线程串行完成，并保持位置消息的层级适配；继续框架的 `WM_SIZE`、几何和渲染通知。

`ShowWindowAsync` 返回时请求可能尚未执行，临时样式不能立即恢复。`SetWindowPlacement`
最大化和单加 `WS_EX_NOACTIVATE` 均不足以维持焦点。`ShowWindow` 返回值表示此前是否可见，
不作为调用成功标志；`Get/SetWindowLongPtrW` 返回零时按 `GetLastError` 区分有效值与失败。

隐藏或关闭当前前台目标后，系统会选择其他窗口，按正常生命周期返回实际状态。
Debug 临时样式与模式不写入普通窗口配置。

## 框架接入

Qt 6.11.2 的实际接入、`WA_ShowWithoutActivating` 的生效范围和运行中属性设置见
[Qt 焦点](qt.md)。原生消息处理后继续 Qt 状态通知，避免回调再次激活、置前或重复请求窗口状态。

## 验收

- 其他应用保持前台时，检查不聚焦启动、最大化记录恢复、显示、尺寸、移动、托盘恢复和运行中最大化的系统前台。
- 查询 `GetForegroundWindow` 和 `GetGUIThreadInfo` 的键盘焦点，结合 `WM_ACTIVATE` 采样检查中间状态。
- 沿 `GetWindow` 的前序窗口链核对目标位于原前台后面，焦点保持和窗口无遮挡分别记录。
- 核对 `IsZoomed`、`IsIconic`、`WINDOWPLACEMENT`、实际外框、`WM_SIZE`、原窗口类型和普通还原矩形。
- 同一实例切换 `no-focus → focus → no-focus`，检查后续操作、模式返回值、重连继承和用户手动激活。

来源：[SetWindowPos](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos)、
[ShowWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-showwindow)、
[WM_WINDOWPOSCHANGING](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-windowposchanging)。
