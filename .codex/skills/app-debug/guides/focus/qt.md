# Qt Debug 焦点

本项目使用 Qt Widgets 与自绘标题栏（`WS_POPUP` 普通顶层窗口）。Windows 的显示、
层级和状态规则见 [Windows 焦点](windows.md)；本文记录 Qt 侧的实际接入。

## 焦点策略

`extras/debug/window_focus.{h,cpp}` 保存当前实例的 `Mode::Focus / Mode::NoFocus`。
初值来自启动参数 `--no-focus`（`cDebugNoFocus()`），运行中由
`window.focus-mode` 或窗口动作的 `--focus-mode` 更新。模式只存在于内存，不写入普通设置。

`ExtrasDebug::WindowFocus::showWithoutActivating(window, maximized)` 是唯一的显示入口：

- 显示或还原使用 `show()` / `setWindowState(NoState)`，由 Qt 选择 `SW_SHOWNOACTIVATE`。
- 最大化使用原生顺序 `maximizeHandleWithoutActivation()`，不经过 `showMaximized()`。
- 整个操作期间 `BackgroundDepth > 0`，`WM_WINDOWPOSCHANGING` 按 Windows 规则调整层级。
- 用户手动激活窗口不经过该入口，仍按应用正常行为聚焦。

## 属性生效时机

Qt 只在创建原生窗口时把 `Qt::WA_ShowWithoutActivating` 映射为 QWindow 属性
`_q_showWithoutActivating`，`show_sys()` 每次显示都会读取它。运行中的窗口已经创建，
因此不能只改 widget 属性：

```cpp
if (const auto handle = window->windowHandle()) {
    handle->setProperty("_q_showWithoutActivating", true);
}
window->show();
```

`ShowAttributeGuard` 在操作结束后恢复 widget 属性与 QWindow 属性，避免影响用户后续的
托盘恢复或任务栏点击。

## 项目接入点

| 位置 | 作用 |
|---|---|
| `MainWindow::firstShow()` | 启动时构造 `LaunchGuard`，只在首次主窗口显示时生效 |
| `MainWindow::applyInitialWorkMode()` | 恢复最大化记录时走不激活最大化 |
| `MainWindow::activate()` | 后台操作期间显示窗口，跳过 `ActivateThisProcess`、`raise` 和 `activateWindow` |
| `MainWindow::handleVisibleChanged()` | 托盘恢复时重新最大化，后台模式下走原生入口 |
| `Platform::MainWindow::nativeEvent()` | 转发 `WM_WINDOWPOSCHANGING` 给 `adjustWindowPosition()` |
| `window_commands.cpp` | 解析 `--focus-mode`，按模式选择 `showMaximized()` 或原生入口 |

`window.tray show` 在 `no-focus` 下用 `BackgroundScope` 包住真实托盘流程，保留
`windowShown()`、未读计数和托盘菜单更新，只去掉系统激活。

## 其他操作与生命周期

- 在 UI 线程执行窗口操作；工作线程经 `QMetaObject::invokeMethod` 排队进入。
- HWND 每次从 `winId()` 获取；窗口 flags 或平台表面重建时沿用原有重绑定入口。
- 原生状态消息继续交给 Qt，保留 `WM_SIZE`、`windowState()`、`frameGeometry()` 与渲染通知。
- 最大化后核对 `isMaximized()`、`frameGeometry()` 和普通还原矩形，渲染完成条件采用项目真实帧信号。
- 原生样式临时修改必须恢复；错误只记录，不跨越 Win32 回调边界抛异常。

## 验收

- 其他应用保持前台时启动 `app.start --no-focus`，确认 `GetForegroundWindow` 不变。
- 依次执行 `window.maximize true`、`window.maximize false`、`window.resize`，检查前台、`IsZoomed`、
  `WINDOWPLACEMENT` 与 `WS_POPUP` / `WS_CHILD` 样式恢复。
- `window.tray hide` 后 `window.tray show --focus-mode no-focus`，确认窗口可见但不激活。
- `window.focus-mode` 切换本身不激活；`focus` 下的窗口动作按正常行为显示。
- 重复最大化、还原并覆盖最大化记录启动，确认不聚焦行为保持。

## 本次验收

2026-11-22 在 Windows 11 build 26200、显示缩放 150% 的 Debug 7.4.4 产物上核对：

- 不聚焦启动、最大化记录恢复、最大化/还原、调整尺寸和托盘恢复期间，前台始终是终端窗口，
  应用窗口紧贴在它后面（z-order 1 步），`WS_POPUP` 保持不变。
- `window.maximize` 与 `window.resize` 返回的 `maximized`、宽高在刷新窗口系统事件后与实际状态一致。
- `window.focus-mode` 与 `app.start --focus-mode` 只更新策略；运行中的 `app.start --no-focus` 不改变已有模式。
- 目标未运行时 `--focus-mode` 返回错误，未创建进程。
- 由 CLI 启动的 Debug 进程没有 Windows 前台权限，`focus` 模式的系统前台切换未能在自动化环境实测；
  该路径使用 `showMaximized()` / `showFromTray()` 的正常显示入口，切换本身不激活窗口。
