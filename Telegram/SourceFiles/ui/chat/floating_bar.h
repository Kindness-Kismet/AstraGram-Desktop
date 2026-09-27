/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/painter.h"
#include "base/basic_types.h"
#include "styles/palette.h"
#include "styles/style_window.h"

#include <QtGui/QRegion>
#include <QtWidgets/QGraphicsEffect>

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

class ChatControlSurface final : public QGraphicsEffect {
public:
	using BackgroundPainter = Fn<void(QPainter&, QRect, QColor)>;

	ChatControlSurface(int radius, bool outline)
	: _radius(radius)
	, _outline(outline) {
	}

	void setBackgroundPainter(BackgroundPainter painter) {
		_backgroundPainter = std::move(painter);
		update();
	}

	[[nodiscard]] const BackgroundPainter &backgroundPainter() const {
		return _backgroundPainter;
	}

protected:
	void draw(QPainter *p) override {
		auto offset = QPoint();
		const auto source = sourcePixmap(Qt::LogicalCoordinates, &offset, NoPad);
		if (source.isNull()) {
			return;
		}
		auto surface = QPixmap(source.size());
		surface.setDevicePixelRatio(source.devicePixelRatio());
		surface.fill(Qt::transparent);
		const auto rect = QRectF(QPointF(),
			QSizeF(source.size()) / source.devicePixelRatio());
		const auto radius = std::min(qreal(_radius), rect.height() / 2.);
		{
			auto painter = QPainter(&surface);
			painter.setRenderHint(QPainter::Antialiasing);
			painter.setPen(Qt::NoPen);
			painter.setBrush(Qt::white);
			painter.drawRoundedRect(rect, radius, radius);
			painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
			painter.drawPixmap(0, 0, source);
			if (_outline) {
				painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
				const auto halfStroke = st::lineWidth / 2.;
				painter.setPen(QPen(st::windowDividerFg->c, st::lineWidth));
				painter.setBrush(Qt::NoBrush);
				painter.drawRoundedRect(rect.adjusted(
					halfStroke, halfStroke, -halfStroke, -halfStroke),
					radius - halfStroke, radius - halfStroke);
			}
		}
		p->drawPixmap(offset, surface);
	}

private:
	const int _radius;
	const bool _outline;
	BackgroundPainter _backgroundPainter;
};

inline void ApplyChatControlSurface(
		not_null<QWidget*> widget,
		int radius,
		bool outline = true) {
	widget->setGraphicsEffect(new ChatControlSurface(radius, outline));
}

// 背景和子控件统一由圆角表面裁切。
inline void PaintChatBar(
		QPainter &p,
		const QRect &rect,
		const QColor &fill) {
	if (rect.isEmpty()) {
		return;
	}
	p.fillRect(rect, fill);
}

inline void PaintChatBar(
		QPainter &p,
		QWidget *widget,
		const QRect &rect,
		const QColor &fill) {
	for (auto parent = widget; parent; parent = parent->parentWidget()) {
		const auto surface = dynamic_cast<ChatControlSurface*>(
			parent->graphicsEffect());
		if (!surface || !surface->backgroundPainter()) {
			continue;
		}
		// 子按钮按所属悬浮栏的坐标取背景，保留各自的文字和交互反馈。
		const auto offset = widget->mapTo(parent, QPoint());
		p.save();
		p.translate(-offset);
		surface->backgroundPainter()(p, rect.translated(offset), fill);
		p.restore();
		return;
	}
	PaintChatBar(p, rect, fill);
}

} // namespace Ui
