/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ayu/ui/components/floating_surface.h"
#include "ui/painter.h"
#include "base/basic_types.h"
#include "styles/palette.h"
#include "styles/style_window.h"

#include <QtGui/QRegion>
#include <QtWidgets/QWidget>

#include <utility>
#include <vector>

namespace Ui {

class ChatBarStack final {
public:
	int add(int contentHeight) {
		if (!contentHeight) {
			return _height;
		}
		const auto top = _height + st::chatFloatingBarGap;
		_height = top + contentHeight;
		_cards.emplace_back(top, contentHeight);
		return top;
	}

	[[nodiscard]] int height() const {
		return _height ? (_height + st::chatFloatingBarGap) : 0;
	}

	[[nodiscard]] QRegion cardRegion(int width) const {
		auto result = QRegion();
		for (const auto &[top, height] : _cards) {
			result += QRect(0, top, width, height);
		}
		return result;
	}

private:
	int _height = 0;
	std::vector<std::pair<int, int>> _cards;
};

inline void ApplyChatControlSurface(
		not_null<QWidget*> widget,
		int radius,
		bool outline = true,
		Fn<QColor()> background = nullptr) {
	if (!background) {
		background = [] { return st::historyPinnedBg->c; };
	}
	auto border = Fn<QColor()>();
	if (outline) {
		border = [] { return st::windowDividerFg->c; };
	}
	AyuUi::FloatingSurface::attach(widget.get(), {
		.radius = radius,
		.background = std::move(background),
		.border = std::move(border),
		.borderWidth = st::lineWidth,
	});
}

// 独立控件保留底色；已处于公共表面的子控件只画交互反馈。
inline void PaintChatBar(
		QPainter &p,
		QWidget *widget,
		const QRect &rect,
		const QColor &fill,
		QColor hover = {}) {
	const auto surface = AyuUi::FloatingSurface::find(widget);
	if (!surface || !surface->hasBackdrop()) {
		p.fillRect(rect, hover.isValid() ? hover : fill);
	} else if (surface->widget() != widget && hover.isValid() && hover != fill) {
		hover.setAlphaF(hover.alphaF() * 0.16);
		p.fillRect(rect, hover);
	}
}

} // namespace Ui
