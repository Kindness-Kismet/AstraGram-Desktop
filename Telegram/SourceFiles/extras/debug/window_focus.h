#pragma once

#ifdef _DEBUG

#include <QString>

#include <memory>

class QWidget;

namespace ExtrasDebug::WindowFocus {

enum class Mode {
	Focus,
	NoFocus,
};

// 当前实例持有的调试焦点策略，启动参数决定初值。
[[nodiscard]] Mode mode();
[[nodiscard]] QString modeName();
[[nodiscard]] QString modeName(Mode mode);
[[nodiscard]] bool noFocus();

// 只接受 focus 或 no-focus，非法取值返回 false。
[[nodiscard]] bool setModeFromText(const QString &text);

// 启动参数要求不聚焦，只用于首次显示主窗口。
[[nodiscard]] bool noFocusLaunch();

// 只覆盖首次主窗口显示，重复构造不会再次生效。
class LaunchGuard final {
public:
	explicit LaunchGuard(QWidget *window);
	LaunchGuard(const LaunchGuard &) = delete;
	LaunchGuard &operator=(const LaunchGuard &) = delete;
	~LaunchGuard();

private:
	struct State;

	std::unique_ptr<State> _state;

};

// 当前调试窗口操作需要维持后台层级。
[[nodiscard]] bool backgroundRequest();

class BackgroundScope final {
public:
	BackgroundScope();
	~BackgroundScope();
	BackgroundScope(const BackgroundScope &) = delete;
	BackgroundScope &operator=(const BackgroundScope &) = delete;

};

// 显示、还原或最大化时不取得系统焦点。
bool showWithoutActivating(QWidget *window, bool maximized);
bool maximizeWithoutActivating(QWidget *window);

// 在请求生效前把窗口留在前台窗口后面的层级。
void adjustWindowPosition(void *message);

} // namespace ExtrasDebug::WindowFocus

#endif // _DEBUG
