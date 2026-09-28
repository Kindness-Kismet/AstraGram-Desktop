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
		background = AyuUi::ChatSurfaceBackground;
	}
	auto border = Fn<QColor()>();
	if (outline) {
		border = AyuUi::ChatSurfaceBorder;
	}
	AyuUi::FloatingSurface::attach(widget.get(), {
		.radius = radius,
		.background = std::move(background),
		.border = std::move(border),
		.borderWidth = st::lineWidth,
	});
}

// 没有磨砂背景时保留原底色；磨砂表面的底色由表面绘制，控件只叠加悬停层。
inline void PaintChatBar(
		QPainter &p,
		QWidget *widget,
		const QRect &rect,
		const QColor &fill,
		const QColor &hover = {}) {
	const auto surface = AyuUi::FloatingSurface::find(widget);
	if (!surface || !surface->hasBackdrop()) {
		p.fillRect(rect, hover.isValid() ? hover : fill);
	} else if (hover.isValid() && hover != fill) {
		p.fillRect(rect, AyuUi::ChatSurfaceHover());
	}
}

} // namespace Ui
