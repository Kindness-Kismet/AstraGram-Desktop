/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "styles/palette.h"
#include "styles/style_widgets.h"
#include "ui/widgets/rp_window.h"

#include <QtWidgets/QWidget>

namespace Window {

[[nodiscard]] inline QColor ShellBackgroundColor(
		[[maybe_unused]] const QWidget *widget) {
#ifdef Q_OS_MAC
	const auto &title = st::defaultWindowTitle;
	return (widget->window()->isActiveWindow() ? title.bgActive : title.bg)->c;
#else
	return Ui::WindowShellBackground();
#endif
}

} // namespace Window
