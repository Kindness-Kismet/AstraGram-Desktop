#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "ui/widgets/popup_menu.h"
#include "core/application.h"
#include "core/core_screenshot_protection.h"

#include <QBackingStore>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QPixmap>
#include <QtGui/QPainter>
#include <QtGui/QScreen>
#include <QtWidgets/QApplication>

#ifdef Q_OS_WIN
#include <Windows.h>
#endif // Q_OS_WIN

namespace ExtrasDebug::Commands {
namespace {

using json = nlohmann::json;

[[nodiscard]] Result saveScreenshot(
		const QImage &image,
		const QString &path,
		json result = json::object()) {
	const auto file = QFileInfo(path);
	const auto format = file.suffix().toLower();
	if (format != u"webp"_q && format != u"png"_q) {
		return Result::Err(u"screenshot path must end in .webp or .png"_q);
	}
	const auto target = file.absoluteFilePath();
	QDir().mkpath(file.absolutePath());
	auto writer = QImageWriter(target, format.toLatin1());
	if (format == u"png"_q) {
		// Qt 以 0 至 100 表示压缩力度，最大无损压缩保留全部像素。
		writer.setCompression(100);
	} else {
		writer.setQuality(40);
	}
	if (!writer.write(image)) {
		return Result::Err(u"cannot write "_q + target);
	}
	result["path"] = target.toStdString();
	result["width"] = image.width();
	result["height"] = image.height();
	return Result::Ok(Compact(result));
}

#ifdef Q_OS_WIN
// 中心与四角内侧共五个点，任一点命中其他顶层窗口即视为被遮挡。
[[nodiscard]] bool screenCovered(not_null<QWidget*> window) {
	const auto handle = reinterpret_cast<HWND>(window->winId());
	auto bounds = RECT();
	if (!GetWindowRect(handle, &bounds)) {
		return true;
	}
	const auto dx = (bounds.right - bounds.left) / 8;
	const auto dy = (bounds.bottom - bounds.top) / 8;
	const auto points = {
		POINT{ (bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2 },
		POINT{ bounds.left + dx, bounds.top + dy },
		POINT{ bounds.right - dx, bounds.top + dy },
		POINT{ bounds.left + dx, bounds.bottom - dy },
		POINT{ bounds.right - dx, bounds.bottom - dy },
	};
	for (const auto &point : points) {
		const auto hit = WindowFromPoint(point);
		if (!hit || GetAncestor(hit, GA_ROOT) != handle) {
			return true;
		}
	}
	return false;
}
#endif // Q_OS_WIN

// 自绘通知是独立的透明顶层窗口，按屏幕位置合成到底色上，便于查看透明与阴影。
[[nodiscard]] Result screenshotNotifications(
		const QString &path,
		const QString &color) {
	const auto background = QColor(color);
	if (!background.isValid()) {
		return Result::Err(u"invalid color: "_q + color);
	}
	const auto shown = notificationWindows();
	if (shown.empty()) {
		return Result::Err(u"no notification windows"_q);
	}
	auto bounds = QRect();
	for (const auto widget : shown) {
		bounds |= widget->geometry();
	}
	const auto ratio = shown.front()->devicePixelRatioF();
	auto image = QImage(
		bounds.size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(background);
	{
		auto p = QPainter(&image);
		for (const auto widget : shown) {
			p.drawPixmap(
				widget->geometry().topLeft() - bounds.topLeft(),
				widget->grab());
		}
	}
	return saveScreenshot(image, path, json{
		{ "count", int(shown.size()) },
	});
}

// 从屏幕拷贝选定窗口的区域，包含系统材质等合成效果；窗口须可见且未被遮挡。
[[nodiscard]] Result screenshotScreen(const QString &path) {
	const auto window = controlWindow();
	if (!window || !window->isVisible()) {
		return Result::Err(u"no visible window"_q);
	}
	const auto screen = window->screen();
	if (!screen) {
		return Result::Err(u"window has no screen"_q);
	}
	const auto area = window->frameGeometry().translated(
		-screen->geometry().topLeft());
	const auto image = screen->grabWindow(
		0,
		area.x(),
		area.y(),
		area.width(),
		area.height());
	if (image.isNull()) {
		return Result::Err(u"screen grab returned an empty image"_q);
	}
	auto result = json{
		{ "active", window->isActiveWindow() },
	};
#ifdef Q_OS_WIN
	result["covered"] = screenCovered(window);
#endif // Q_OS_WIN
	return saveScreenshot(image.toImage(), path, std::move(result));
}

[[nodiscard]] Result screenshotTake(const QStringList &args) {
	if (args.size() == 2 && args[1] == u"buffer"_q) {
		const auto window = controlWindow();
		const auto store = window ? window->backingStore() : nullptr;
		const auto buffer = store ? dynamic_cast<const QImage*>(store->paintDevice()) : nullptr;
		if (!buffer) {
			return Result::Err(u"window has no readable raster backing store"_q);
		}
		return saveScreenshot(*buffer, args[0]);
	}
	if (args.size() == 2 && args[1] == u"animation"_q) {
		const auto window = controlWindow();
		if (!window || !window->isVisible()) {
			return Result::Err(u"no visible window"_q);
		}
		const auto image = Core::App().screenshotProtection().grabForAnimation(window);
		return saveScreenshot(image.toImage(), args[0], json{
			{ "protected", Core::App().screenshotProtection().active(window) },
		});
	}
	if (args.size() == 2 && args[1] == u"screen"_q) {
		return screenshotScreen(args[0]);
	}
	if (args.size() >= 2 && args[1] == u"notification"_q) {
		if (args.size() > 3) {
			return Result::Err(u"usage: screenshot.take <path> notification [#rrggbb]"_q);
		}
		return screenshotNotifications(args[0], args.value(2, u"#5b6b7f"_q));
	}
	if (args.size() != 1
		&& (args.size() != 2 || args[1] != u"popup"_q)) {
		return Result::Err(u"usage: screenshot.take <path.webp|path.png> [popup|screen|buffer|animation|notification [#rrggbb]]"_q);
	}
	const auto popup = (args.size() == 2);
	const auto window = controlWindow();
	const auto menu = Ui::PopupMenu::Active();
	const auto widget = popup
		? menu ? menu : QApplication::activePopupWidget()
		: window;
	if (!widget) {
		return Result::Err(popup ? u"no active popup"_q : u"no active window"_q);
	}
	const auto root = widget->window();
	const auto image = (root == widget)
		? widget->grab()
		: root->grab(QRect(widget->mapTo(root, QPoint()), widget->size()));
	if (image.isNull()) {
		return Result::Err(u"grab returned an empty image"_q);
	}
	return saveScreenshot(image.toImage(), args.front());
}

} // namespace

std::vector<not_null<QWidget*>> notificationWindows() {
	auto result = std::vector<not_null<QWidget*>>();
	for (const auto widget : QApplication::topLevelWidgets()) {
		if (widget->isVisible()
			&& widget->objectName() == u"notification"_q) {
			result.push_back(widget);
		}
	}
	return result;
}

const HandlerMap &ScreenshotHandlers() {
	static const auto result = HandlerMap{
		{ u"screenshot.take"_q, &screenshotTake },
	};
	return result;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
