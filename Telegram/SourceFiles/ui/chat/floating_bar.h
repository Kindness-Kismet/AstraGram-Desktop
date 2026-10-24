/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "extras/ui/components/floating_surface.h"
#include "ui/painter.h"
#include "base/assertion.h"
#include "base/basic_types.h"
#include "styles/palette.h"
#include "styles/style_window.h"

#include <QtGui/QRegion>
#include <QtWidgets/QWidget>

#include <algorithm>
#include <vector>

namespace Ui {

// 聊天顶部横条的外形：独立卡片自带表面，卡片行与胶囊由所在容器统一绘制。
enum class ChatBarShape {
	Card,
	Row,
	Capsule,
};

// 内容横条合并成一张卡片，行间留出分隔线；动作横条在卡片下方靠左排成胶囊，
// 放不下时换行。全部行先于胶囊加入，key 用于取回各横条的位置。
class ChatBarStack final {
public:
	explicit ChatBarStack(int width) : _width(std::max(width, 0)) {
	}

	void addRow(const void *key, int height) {
		Expects(_capsules.empty());

		const auto top = _cardBottom
			? (_cardBottom + st::lineWidth)
			: st::chatFloatingBarGap;
		if (height <= 0) {
			_items.push_back({ key, QRect(0, top, _width, 0) });
			return;
		}
		if (_cardBottom) {
			_dividers.emplace_back(
				st::chatBarDividerSkip,
				_cardBottom,
				std::max(_width - 2 * st::chatBarDividerSkip, 0),
				st::lineWidth);
		} else {
			_cardTop = top;
		}
		_cardBottom = top + height;
		_items.push_back({ key, QRect(0, top, _width, height) });
	}

	// 自然宽度超出容器时截到容器宽度。
	void addCapsule(const void *key, int naturalWidth, int height) {
		const auto width = std::clamp(naturalWidth, 0, _width);
		if (height <= 0 || !width) {
			const auto top = capsuleLineTop();
			_items.push_back({ key, QRect(_capsuleLeft, top, width, 0) });
			return;
		}
		if (_capsules.empty()) {
			_lineTop = capsuleLineTop();
		} else if (_capsuleLeft + width > _width) {
			_lineTop += _lineHeight + st::chatBarCapsuleGap;
			_capsuleLeft = 0;
			_lineHeight = 0;
		}
		const auto geometry = QRect(_capsuleLeft, _lineTop, width, height);
		_capsuleLeft += width + st::chatBarCapsuleGap;
		_lineHeight = std::max(_lineHeight, height);
		_capsules.push_back(geometry);
		_items.push_back({ key, geometry });
	}

	[[nodiscard]] QRect geometry(const void *key) const {
		for (const auto &item : _items) {
			if (item.key == key) {
				return item.geometry;
			}
		}
		Unexpected("Key in ChatBarStack::geometry.");
	}

	// 含与下方消息列表之间的间距。
	[[nodiscard]] int height() const {
		const auto bottom = _capsules.empty()
			? _cardBottom
			: (_lineTop + _lineHeight);
		return bottom ? (bottom + st::chatFloatingBarGap) : 0;
	}

	[[nodiscard]] QRect card() const {
		return _cardBottom
			? QRect(0, _cardTop, _width, _cardBottom - _cardTop)
			: QRect();
	}

	[[nodiscard]] const std::vector<QRect> &capsules() const {
		return _capsules;
	}

	[[nodiscard]] const std::vector<QRect> &dividers() const {
		return _dividers;
	}

	[[nodiscard]] QRegion region() const {
		auto result = QRegion(card());
		for (const auto &capsule : _capsules) {
			result += capsule;
		}
		return result;
	}

private:
	struct Item {
		const void *key = nullptr;
		QRect geometry;
	};

	[[nodiscard]] int capsuleLineTop() const {
		return _capsules.empty()
			? (_cardBottom + st::chatFloatingBarGap)
			: _lineTop;
	}

	int _width = 0;
	int _cardTop = 0;
	int _cardBottom = 0;
	int _capsuleLeft = 0;
	int _lineTop = 0;
	int _lineHeight = 0;
	std::vector<Item> _items;
	std::vector<QRect> _capsules;
	std::vector<QRect> _dividers;
};

inline void ApplyChatControlSurface(
		not_null<QWidget*> widget,
		int radius,
		bool outline = true,
		Fn<QColor()> background = nullptr) {
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
