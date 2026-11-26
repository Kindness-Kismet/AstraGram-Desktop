# 窗口操作

使用根 AGENTS 的调试入口。选中窗口后再执行动作；窗口和控件编号随当前界面重新查询。

## 选择与操作

- `control.list --all` 列出顶层窗口，`control.window [main|objectName|#序号]` 查询或选择目标；完整选择语义见 [控件指南](controls.md)。
- `window.resize`、`window.maximize`、`window.activation`、`window.close` 操作当前选定窗口，详细尺寸和返回值见 [运行控制](runtime.md#服务端指令)。
- 独立聊天与即时预览见 [独立窗口](runtime.md#独立窗口)；媒体查看器用 [媒体业务](business.md#消息成员与编号提及) 的 `media.open` 打开，再通过 `control.window media.viewer` 选择。关闭目标后重新确认当前窗口。
- 托盘显示与隐藏沿现有 `window.tray` 业务流程，关闭窗口保留原有确认行为。完整退出使用运行指南中的停止流程。

## 焦点与观察

[PC 焦点模式](runtime.md#pc-焦点模式) 统一定义 `--no-focus`、`--focus-mode` 和实例模式继承。`window.activation` 是内部 Qt 焦点模拟，不代表系统前台变化。

窗口尺寸、菜单层级或状态修改按本次任务核对；系统焦点、真实鼠标命中和系统材质需要对应的实际证据。视觉检查选择 [截图模式](screenshot.md)，维护显示实现时再阅读 [Windows](focus/windows.md) 与 [Qt](focus/qt.md) 的项目接入说明。
