/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "extras/ui/components/floating_surface.h"
#include "ui/painter.h"
#include "ui/wrap/slide_wrap.h"
#include "base/basic_types.h"
#include "styles/palette.h"
#include "styles/style_window.h"

#include <QtGui/QRegion>
#include <QtWidgets/QWidget>

#include <algorithm>
#include <utility>
#include <vector>

namespace Ui {

class ChatBarStack final {
public:
	int add(int contentHeight) {
		if (!contentHeight) {
			return _height;
		}
		_bottomGap = std::min(contentHeight, st::chatFloatingBarGap);
		const auto top = _height + _bottomGap;
		_height = top + contentHeight;
		_cards.emplace_back(top, contentHeight);
		return top;
	}

	[[nodiscard]] int height() const {
		return _height + _bottomGap;
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
	int _bottomGap = 0;
	std::vector<std::pair<int, int>> _cards;
};

inline void ApplyChatControlSurface(
		not_null<QWidget*> widget,
		int radius,
		bool outline = true,
		Fn<QColor()> background = nullptr,
		Fn<qreal()> opacity = nullptr) {
	if (!background) {
		background = ExtrasUi::chatSurfaceBackground;
	}
	auto border = Fn<QColor()>();
	if (outline) {
		border = ExtrasUi::chatSurfaceBorder;
	}
	ExtrasUi::FloatingSurface::attach(widget.get(), {
		.radius = radius,
		.background = std::move(background),
		.border = std::move(border),
		.borderWidth = st::lineWidth,
		.windowMaterial = true,
		.opacity = std::move(opacity),
	});
}

// 透明度跟随展开高度，背景和文字共用一次表面合成。
template <typename Widget>
inline void ApplyChatBarSurface(SlideWrap<Widget> &wrap) {
	wrap.ease = anim::easeOutCubic;
	wrap.setDuration(220);
	ApplyChatControlSurface(wrap.entity(), st::windowCardRadius, true, nullptr, [&wrap] {
		const auto fullHeight = wrap.entity()->height();
		return fullHeight ? qreal(wrap.height()) / fullHeight : 0.;
	});
}

// 聊天磨砂或窗口材质由表面绘制，控件只叠加悬停层。
inline void PaintChatBar(
		QPainter &p,
		QWidget *widget,
		const QRect &rect,
		const QColor &fill,
		const QColor &hover = {}) {
	const auto surface = ExtrasUi::FloatingSurface::find(widget);
	if (!surface || (!surface->hasBackdrop() && !surface->usesWindowMaterial())) {
		p.fillRect(rect, hover.isValid() ? hover : fill);
	} else if (hover.isValid() && hover != fill) {
		p.fillRect(rect, ExtrasUi::chatSurfaceHover());
	}
}

// 输入候选面板共用底色、边框与圆角裁剪，聊天磨砂背景仍沿用输入框配色。
inline void ApplyAutocompleteSurface(
		not_null<QWidget*> widget,
		style::color background,
		Fn<qreal()> opacity = nullptr) {
	const auto frosted = [=] {
		const auto surface = ExtrasUi::FloatingSurface::find(widget.get());
		return surface && surface->hasBackdrop();
	};
	ExtrasUi::FloatingSurface::attach(widget.get(), {
		.radius = st::windowCardRadius,
		.background = [=] {
			return frosted() ? ExtrasUi::chatSurfaceBackground() : background->c;
		},
		.border = [=] {
			return frosted() ? ExtrasUi::chatSurfaceBorder() : st::windowDividerFg->c;
		},
		.borderWidth = st::lineWidth,
		.maskInput = true,
		.opacity = std::move(opacity),
	});
}

} // namespace Ui
