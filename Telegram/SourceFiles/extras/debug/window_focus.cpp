#ifdef _DEBUG
#include "extras/debug/window_focus.h"

#include "settings.h"

#include <QVariant>
#include <QWidget>
#include <QWindow>

#include <QEventLoop>
#include <qpa/qwindowsysteminterface.h>

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

namespace ExtrasDebug::WindowFocus {
namespace {

Mode CurrentMode = Mode::Focus;
bool ModeInitialized = false;
int BackgroundDepth = 0;
bool LaunchConsumed = false;

[[nodiscard]] Mode currentMode() {
	if (!ModeInitialized) {
		ModeInitialized = true;
		CurrentMode = cDebugNoFocus() ? Mode::NoFocus : Mode::Focus;
	}
	return CurrentMode;
}

// Qt 只在创建原生窗口时读取 WA_ShowWithoutActivating，运行中还需要直接改
// QWindow 属性，让 show_sys 采用 SW_SHOWNOACTIVATE。
class ShowAttributeGuard final {
public:
	explicit ShowAttributeGuard(QWidget *window)
	: _window(window) {
		_previousAttribute = _window->testAttribute(Qt::WA_ShowWithoutActivating);
		_window->setAttribute(Qt::WA_ShowWithoutActivating, true);
		if (const auto handle = _window->windowHandle()) {
			_previousProperty = handle->property("_q_showWithoutActivating");
			handle->setProperty("_q_showWithoutActivating", true);
		}
	}
	~ShowAttributeGuard() {
		_window->setAttribute(Qt::WA_ShowWithoutActivating, _previousAttribute);
		if (const auto handle = _window->windowHandle()) {
			handle->setProperty("_q_showWithoutActivating", _previousProperty);
		}
	}

	ShowAttributeGuard(const ShowAttributeGuard &) = delete;
	ShowAttributeGuard &operator=(const ShowAttributeGuard &) = delete;

private:
	QWidget *_window = nullptr;
	bool _previousAttribute = false;
	QVariant _previousProperty;

};

class BackgroundDepthGuard final {
public:
	BackgroundDepthGuard() {
		++BackgroundDepth;
	}
	~BackgroundDepthGuard() {
		--BackgroundDepth;
	}

	BackgroundDepthGuard(const BackgroundDepthGuard &) = delete;
	BackgroundDepthGuard &operator=(const BackgroundDepthGuard &) = delete;

};

#ifdef Q_OS_WIN
[[nodiscard]] HWND handleOf(QWidget *window) {
	return window ? reinterpret_cast<HWND>(window->winId()) : nullptr;
}

[[nodiscard]] bool readWindowLong(HWND hwnd, int index, LONG_PTR *value) {
	SetLastError(ERROR_SUCCESS);
	const auto result = GetWindowLongPtrW(hwnd, index);
	if (!result && GetLastError() != ERROR_SUCCESS) {
		return false;
	}
	*value = result;
	return true;
}

[[nodiscard]] bool writeWindowLong(HWND hwnd, int index, LONG_PTR value) {
	SetLastError(ERROR_SUCCESS);
	const auto previous = SetWindowLongPtrW(hwnd, index, value);
	return previous != 0 || GetLastError() == ERROR_SUCCESS;
}

[[nodiscard]] HWND backgroundAnchor(HWND hwnd) {
	const auto foreground = GetForegroundWindow();
	if (!foreground || foreground == hwnd) {
		return HWND_BOTTOM;
	}
	return (GetWindowLongPtrW(foreground, GWL_EXSTYLE) & WS_EX_TOPMOST)
		? HWND_BOTTOM
		: foreground;
}

[[nodiscard]] bool maximizeHandleWithoutActivation(HWND hwnd) {
	if (IsIconic(hwnd)) {
		ShowWindow(hwnd, SW_SHOWNOACTIVATE);
	}
	auto original = LONG_PTR();
	if (!readWindowLong(hwnd, GWL_STYLE, &original)) {
		return false;
	}
	const auto typeMask = LONG_PTR(WS_CHILD | WS_POPUP);
	const auto applied = writeWindowLong(
		hwnd,
		GWL_STYLE,
		(original | WS_CHILD) & ~LONG_PTR(WS_POPUP));
	ShowWindow(hwnd, SW_MAXIMIZE);
	auto current = original;
	const auto read = readWindowLong(hwnd, GWL_STYLE, &current);
	const auto restored = writeWindowLong(
		hwnd,
		GWL_STYLE,
		(current & ~typeMask) | (original & typeMask));
	if (!applied || !read || !restored) {
		return false;
	}
	return SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
		SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOZORDER
		| SWP_NOMOVE | SWP_NOSIZE) != FALSE;
}
#endif // Q_OS_WIN

} // namespace

struct LaunchGuard::State {
	explicit State(QWidget *window)
	: attribute(window) {
	}

	ShowAttributeGuard attribute;
	BackgroundDepthGuard background;

};

Mode mode() {
	return currentMode();
}

QString modeName(Mode mode) {
	return (mode == Mode::NoFocus) ? u"no-focus"_q : u"focus"_q;
}

QString modeName() {
	return modeName(currentMode());
}

bool noFocus() {
	return currentMode() == Mode::NoFocus;
}

bool setModeFromText(const QString &text) {
	const auto value = text.trimmed();
	if (value == u"focus"_q) {
		CurrentMode = Mode::Focus;
	} else if (value == u"no-focus"_q) {
		CurrentMode = Mode::NoFocus;
	} else {
		return false;
	}
	ModeInitialized = true;
	return true;
}

bool noFocusLaunch() {
	return cDebugNoFocus();
}

LaunchGuard::LaunchGuard(QWidget *window) {
	if (LaunchConsumed || !noFocusLaunch()) {
		return;
	}
	LaunchConsumed = true;
	_state = std::make_unique<State>(window);
}

LaunchGuard::~LaunchGuard() = default;

bool backgroundRequest() {
	return BackgroundDepth > 0;
}

BackgroundScope::BackgroundScope() {
	++BackgroundDepth;
}

BackgroundScope::~BackgroundScope() {
	--BackgroundDepth;
}

bool showWithoutActivating(QWidget *window, bool maximized) {
	Expects(window != nullptr);

	const auto attributes = ShowAttributeGuard(window);
	const auto background = BackgroundDepthGuard();
#ifdef Q_OS_WIN
	const auto hwnd = handleOf(window);
	if (!hwnd) {
		return false;
	}
	auto ok = true;
	if (!window->isVisible()) {
		auto state = window->windowState() & ~Qt::WindowMinimized;
		state = maximized
			? (state | Qt::WindowMaximized)
			: (state & ~(Qt::WindowMaximized | Qt::WindowFullScreen));
		window->setWindowState(state);
		window->show();
	} else {
		if (window->isMinimized()) {
			ShowWindow(hwnd, SW_SHOWNOACTIVATE);
		}
		if (maximized) {
			ok = window->isMaximized() || maximizeHandleWithoutActivation(hwnd);
		} else if (window->isMaximized() || window->isFullScreen()) {
			window->setWindowState(
				window->windowState()
					& ~(Qt::WindowMaximized | Qt::WindowFullScreen));
		}
	}
	// 原生状态消息默认排入事件队列，先送达再返回实际状态。
	QWindowSystemInterface::flushWindowSystemEvents(
		QEventLoop::ExcludeUserInputEvents);
	return ok;
#else
	if (!window->isVisible()) {
		auto state = window->windowState() & ~Qt::WindowMinimized;
		state = maximized
			? (state | Qt::WindowMaximized)
			: (state & ~(Qt::WindowMaximized | Qt::WindowFullScreen));
		window->setWindowState(state);
		window->show();
	} else if (maximized) {
		window->showMaximized();
	} else if (window->isMinimized()
		|| window->isMaximized()
		|| window->isFullScreen()) {
		window->showNormal();
	}
	return true;
#endif
}

bool maximizeWithoutActivating(QWidget *window) {
	Expects(window != nullptr);

	const auto background = BackgroundDepthGuard();
#ifdef Q_OS_WIN
	const auto hwnd = handleOf(window);
	if (!hwnd) {
		return false;
	}
	return window->isMaximized() || maximizeHandleWithoutActivation(hwnd);
#else
	const auto attributes = ShowAttributeGuard(window);
	window->showMaximized();
	return true;
#endif
}

void adjustWindowPosition(void *message) {
#ifdef Q_OS_WIN
	if (!BackgroundDepth || !message) {
		return;
	}
	const auto msg = static_cast<MSG*>(message);
	if (msg->message != WM_WINDOWPOSCHANGING || !msg->lParam) {
		return;
	}
	if (GetForegroundWindow() == msg->hwnd) {
		return;
	}
	auto position = reinterpret_cast<WINDOWPOS*>(msg->lParam);
	position->hwndInsertAfter = backgroundAnchor(msg->hwnd);
	position->flags &= ~SWP_NOZORDER;
#endif
}

} // namespace ExtrasDebug::WindowFocus
#endif // _DEBUG
