#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "core/application.h"
#include "window/window_controller.h"

namespace ExtrasDebug::Commands {
namespace {

using json = nlohmann::json;

// 尺寸取 Qt 逻辑像素，与 control.list 的几何在同一坐标系。
[[nodiscard]] json WindowState(not_null<::MainWindow*> window) {
	return json{
		{ "width", window->width() },
		{ "height", window->height() },
		{ "maximized", window->isMaximized() },
		{ "visible", window->isVisible() },
		{ "active", window->isActiveWindow() },
	};
}

[[nodiscard]] ::MainWindow *ActiveWindow() {
	const auto controller = Core::App().activeWindow();
	return controller ? controller->widget().get() : nullptr;
}

void showWithoutActivating(not_null<::MainWindow*> window, bool maximized) {
	const auto previous = window->testAttribute(Qt::WA_ShowWithoutActivating);
	window->setAttribute(Qt::WA_ShowWithoutActivating);
	if (maximized) {
		window->showMaximized();
	} else {
		window->showNormal();
	}
	window->setAttribute(Qt::WA_ShowWithoutActivating, previous);
}

// 无参报告当前尺寸，有参按逻辑像素调整。
[[nodiscard]] Result WindowSize(const QStringList &args) {
	if (args.size() != 0 && args.size() != 2) {
		return Result::Err(u"usage: window.resize [<width> <height>]"_q);
	}
	const auto window = ActiveWindow();
	if (!window) {
		return Result::Err(u"no active window"_q);
	}
	if (args.isEmpty()) {
		return Result::Ok(Compact(WindowState(window)));
	}
	auto ok = false;
	const auto width = args[0].toInt(&ok);
	if (!ok || width <= 0) {
		return Result::Err(u"width must be a positive integer"_q);
	}
	const auto height = args[1].toInt(&ok);
	if (!ok || height <= 0) {
		return Result::Err(u"height must be a positive integer"_q);
	}
	// 最大化状态下 resize 不生效，先还原。
	if (window->isMaximized()) {
		showWithoutActivating(window, false);
	}
	window->resize(width, height);
	return Result::Ok(Compact(WindowState(window)));
}

[[nodiscard]] Result WindowMaximize(const QStringList &args) {
	if (args.size() != 1) {
		return Result::Err(u"usage: window.maximize <true|false>"_q);
	}
	const auto text = args.front().trimmed();
	if (text != u"true"_q && text != u"false"_q) {
		return Result::Err(u"expected true or false"_q);
	}
	const auto window = ActiveWindow();
	if (!window) {
		return Result::Err(u"no active window"_q);
	}
	showWithoutActivating(window, text == u"true"_q);
	return Result::Ok(Compact(WindowState(window)));
}

} // namespace

const HandlerMap &WindowHandlers() {
	static const auto result = HandlerMap{
		{ u"window.resize"_q, &WindowSize },
		{ u"window.maximize"_q, &WindowMaximize },
	};
	return result;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
