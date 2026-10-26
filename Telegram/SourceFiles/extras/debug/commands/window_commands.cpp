#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "extras/features/window_material/window_material.h"

#include "core/application.h"
#include "window/window_controller.h"

#include <QGuiApplication>
#include <QBackingStore>
#include <QImage>
#include <QWindow>
#include <qpa/qwindowsysteminterface.h>

#ifdef Q_OS_WIN
#include <Windows.h>
#include <dwmapi.h>
#endif

namespace ExtrasDebug::Commands {
namespace {

using json = nlohmann::json;

// 尺寸取 Qt 逻辑像素，与 control.list 的几何在同一坐标系。
[[nodiscard]] json WindowState(not_null<QWidget*> window) {
	return json{
		{ "width", window->width() },
		{ "height", window->height() },
		{ "maximized", window->isMaximized() },
		{ "visible", window->isVisible() },
		{ "active", window->isActiveWindow() },
		{ "windowMaterialActive", ExtrasFeatures::WindowMaterial::isActive(window) },
		{ "applicationActive",
			QGuiApplication::applicationState() == Qt::ApplicationActive },
	};
}

void showWithoutActivating(not_null<QWidget*> window, bool maximized) {
	const auto previous = window->testAttribute(Qt::WA_ShowWithoutActivating);
	window->setAttribute(Qt::WA_ShowWithoutActivating);
	if (maximized) {
		window->showMaximized();
	} else {
		window->showNormal();
	}
	window->setAttribute(Qt::WA_ShowWithoutActivating, previous);
}

// 直接查询已提交的绘制缓冲，不触发截图或重绘，保留动画当前帧。
[[nodiscard]] Result windowMaterial(const QStringList &args) {
	if (!args.empty() && args.size() != 2) {
		return Result::Err(u"usage: window.material [x y]"_q);
	}
	const auto window = controlWindow();
	if (!window) {
		return Result::Err(u"no selected window"_q);
	}
	const auto rgba = [](QColor color) {
		return json::array({ color.red(), color.green(), color.blue(), color.alpha() });
	};
	auto result = WindowState(window);
	result["mode"] = window->property("ExtrasWindowMaterialEffectiveMode").toInt();
	result["revision"] = window->property("ExtrasWindowMaterialRevision").toInt();
	result["rootTint"] = rgba(ExtrasFeatures::WindowMaterial::rootTintColor(window));
#ifdef Q_OS_WIN
	DWORD backdrop = 0;
	const auto handle = reinterpret_cast<HWND>(window->winId());
	if (SUCCEEDED(DwmGetWindowAttribute(handle, DWORD(38), &backdrop, sizeof(backdrop)))) {
		result["nativeBackdrop"] = backdrop;
	}
#endif
	if (args.empty()) {
		return Result::Ok(Compact(result));
	}
	auto xOk = false;
	auto yOk = false;
	const auto point = QPoint(args[0].toInt(&xOk), args[1].toInt(&yOk));
	if (!xOk || !yOk || !window->rect().contains(point)) {
		return Result::Err(u"sample point must be inside the selected window"_q);
	}
	const auto store = window->backingStore();
	const auto buffer = store ? dynamic_cast<const QImage*>(store->paintDevice()) : nullptr;
	if (!buffer) {
		return Result::Err(u"window has no readable raster backing store"_q);
	}
	const auto pixel = QPointF(point * buffer->devicePixelRatio()).toPoint();
	if (!buffer->rect().contains(pixel)) {
		return Result::Err(u"sample point is outside the backing store"_q);
	}
	result["position"] = json::array({ point.x(), point.y() });
	result["pixel"] = rgba(buffer->pixelColor(pixel));
	return Result::Ok(Compact(result));
}

// 无参报告当前尺寸，有参按逻辑像素调整。
[[nodiscard]] Result WindowSize(const QStringList &args) {
	if (args.size() != 0 && args.size() != 2) {
		return Result::Err(u"usage: window.resize [<width> <height>]"_q);
	}
	const auto window = controlWindow();
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
	const auto window = controlWindow();
	if (!window) {
		return Result::Err(u"no active window"_q);
	}
	showWithoutActivating(window, text == u"true"_q);
	return Result::Ok(Compact(WindowState(window)));
}

// 只改 Qt 内部的焦点窗口，系统前台不变，真实的焦点切换会覆盖它；
// Windows 不支持直接设置应用状态，应用状态随焦点窗口一起切换。
[[nodiscard]] Result windowActivation(const QStringList &args) {
	const auto text = args.size() == 1 ? args.front().trimmed() : QString();
	if (text != u"active"_q && text != u"inactive"_q) {
		return Result::Err(u"usage: window.activation <active|inactive>"_q);
	}
	const auto window = controlWindow();
	if (!window) {
		return Result::Err(u"no active window"_q);
	}
	using Interface = QWindowSystemInterface;
	Interface::handleFocusWindowChanged<Interface::SynchronousDelivery>(
		(text == u"active"_q) ? window->windowHandle() : nullptr);
	return Result::Ok(Compact(WindowState(window)));
}

[[nodiscard]] Result closeWindow(const QStringList &args) {
	if (!args.empty()) {
		return Result::Err(u"usage: window.close"_q);
	}
	const auto window = controlWindow();
	if (!window) {
		return Result::Err(u"no selected window"_q);
	}
	const auto accepted = window->close();
	return (accepted || !window->isVisible())
		? Result::Ok()
		: Result::Err(u"window close was rejected"_q);
}

[[nodiscard]] Result windowTray(const QStringList &args) {
	const auto action = args.size() == 1 ? args.front() : QString();
	if (action != u"hide"_q && action != u"show"_q) {
		return Result::Err(u"usage: window.tray <hide|show>"_q);
	}
	const auto controller = Core::App().activeWindow();
	if (!controller) {
		return Result::Err(u"no active window"_q);
	}
	const auto window = controller->widget();
	if (action == u"hide"_q) {
		if (!window->minimizeToTray()) {
			return Result::Err(u"window could not minimize to tray"_q);
		}
	} else {
		window->showFromTray();
	}
	return Result::Ok(Compact(WindowState(window)));
}

} // namespace

const HandlerMap &WindowHandlers() {
	static const auto result = HandlerMap{
		{ u"window.resize"_q, &WindowSize },
		{ u"window.material"_q, &windowMaterial },
		{ u"window.maximize"_q, &WindowMaximize },
		{ u"window.activation"_q, &windowActivation },
		{ u"window.close"_q, &closeWindow },
		{ u"window.tray"_q, &windowTray },
	};
	return result;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
