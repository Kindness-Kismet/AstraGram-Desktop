/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "window/themes/window_theme_preview.h"

namespace Platform {

int PreviewTitleHeight();
void PreviewWindowFramePaint(QImage &preview, const style::palette &palette, QRect body, int outerWidth);

} // namespace Platform

// Platform dependent implementations.

#ifndef Q_OS_MAC

namespace Platform {

inline int PreviewTitleHeight() {
	return Window::Theme::DefaultPreviewTitleHeight();
}

inline void PreviewWindowFramePaint(QImage &preview, const style::palette &palette, QRect body, int outerWidth) {
	return Window::Theme::DefaultPreviewWindowFramePaint(preview, palette, body, outerWidth);
}

} // namespace Platform

#endif // !Q_OS_MAC
