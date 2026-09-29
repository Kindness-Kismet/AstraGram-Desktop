#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "core/application.h"
#include "window/window_controller.h"
#include "ui/widgets/popup_menu.h"

#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QtGui/QPainter>
#include <QtWidgets/QApplication>

namespace ExtrasDebug::Commands {
namespace {

using json = nlohmann::json;

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
	const auto target = QFileInfo(path).absoluteFilePath();
	QDir().mkpath(QFileInfo(target).absolutePath());
	if (!image.save(target, "PNG")) {
		return Result::Err(u"cannot write "_q + target);
	}
	return Result::Ok(Compact(json{
		{ "path", target.toStdString() },
		{ "width", image.width() },
		{ "height", image.height() },
		{ "count", int(shown.size()) },
	}));
}

[[nodiscard]] Result ScreenshotTake(const QStringList &args) {
	if (args.size() >= 2 && args[1] == u"notification"_q) {
		if (args.size() > 3) {
			return Result::Err(u"usage: screenshot.take <path> notification [#rrggbb]"_q);
		}
		return screenshotNotifications(args[0], args.value(2, u"#5b6b7f"_q));
	}
	if (args.size() != 1
		&& (args.size() != 2 || args[1] != u"popup"_q)) {
		return Result::Err(u"usage: screenshot.take <path> [popup|notification [#rrggbb]]"_q);
	}
	const auto popup = (args.size() == 2);
	const auto window = Core::App().activeWindow();
	const auto menu = Ui::PopupMenu::Active();
	const auto widget = popup
		? menu ? menu : QApplication::activePopupWidget()
		: window ? window->widget().get() : nullptr;
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
	const auto target = QFileInfo(args.front()).absoluteFilePath();
	QDir().mkpath(QFileInfo(target).absolutePath());
	if (!image.save(target, "JPG", 80)) {
		return Result::Err(u"cannot write "_q + target);
	}
	return Result::Ok(Compact(json{
		{ "path", target.toStdString() },
		{ "width", image.width() },
		{ "height", image.height() },
	}));
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
		{ u"screenshot.take"_q, &ScreenshotTake },
	};
	return result;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
