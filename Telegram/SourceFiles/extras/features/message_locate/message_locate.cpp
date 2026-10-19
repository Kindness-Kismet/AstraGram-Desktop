#include "extras/features/message_locate/message_locate.h"

#include "extras/extras_settings.h"
#include "ui/chat/chat_style.h"
#include "ui/effects/animations.h"
#include "ui/painter.h"

#include <QtGui/QPainterPath>
#include <cmath>

namespace ExtrasFeatures::MessageLocate {
namespace {

void paintContour(
		QPainter &p,
		const Ui::ChatPaintContext &context,
		QRect geometry,
		Ui::BubbleRounding rounding,
		QColor color) {
	const auto &settings = ExtrasSettings::getInstance();
	const auto stroke = style::ConvertFloatScale(settings.messageLocateStroke());
	const auto inset = stroke / 2.;
	const auto body = QRectF(geometry).adjusted(inset, inset, -inset, -inset);
	const auto visible = QRectF(geometry.intersected(context.area))
		.adjusted(inset, inset, -inset, -inset);
	const auto &messageStyle = context.st->messageStyle(context.outbg, false);
	const auto tail = messageStyle.tailLeft.size();
	const auto outline = Ui::BubbleOutlinePath(body, rounding, tail);
	const auto reach = style::ConvertFloatScale(settings.messageLocateCorner());
	const auto horizontal = std::min(reach, body.width() * .35);
	const auto vertical = std::min(reach, visible.height() * .35);
	const auto extent = tail.width() + stroke;
	auto corners = QPainterPath();
	for (const auto left : { true, false }) {
		for (const auto top : { true, false }) {
			corners.addRect(QRectF(
				left ? body.left() - extent : body.right() - horizontal,
				top ? visible.top() - stroke : visible.bottom() - vertical,
				horizontal + extent,
				vertical + stroke));
		}
	}
	p.setClipPath(corners, Qt::IntersectClip);
	p.setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.setBrush(Qt::NoBrush);
	p.drawPath(outline);
}

void paintPin(
		QPainter &p,
		const Ui::ChatPaintContext &context,
		QRect geometry,
		int outerWidth,
		bool preferLeft,
		int leftLimit,
		QRect action,
		QColor color) {
	const auto &settings = ExtrasSettings::getInstance();
	const auto gap = style::ConvertFloatScale(5.);
	const auto requested = style::ConvertFloatScale(settings.messageLocatePinSize());
	const auto areaFor = [&](bool left) {
		const auto edge = geometry.x() + geometry.width() + gap;
		auto area = QRectF(
			left ? leftLimit : edge,
			geometry.y(),
			left ? geometry.left() - leftLimit - gap : outerWidth - edge,
			geometry.height());
		if (!action.isEmpty() && area.intersects(action)) {
			const auto above = action.top() - area.top();
			const auto below = area.bottom() - action.y() - action.height();
			if (above >= below) {
				area.setBottom(action.top());
			} else {
				area.setTop(action.y() + action.height());
			}
		}
		return area;
	};
	const auto sizeFor = [&](QRectF area) {
		return std::max(0., std::min({ requested, area.width(), area.height() / 1.35 }));
	};
	const auto preferred = areaFor(preferLeft);
	const auto opposite = areaFor(!preferLeft);
	const auto usePreferred = sizeFor(preferred) >= sizeFor(opposite);
	const auto left = usePreferred ? preferLeft : !preferLeft;
	const auto area = usePreferred ? preferred : opposite;
	// 窄窗口按留白等比缩小图标，保持五像素间隔，不覆盖消息。
	const auto size = sizeFor(area);
	if (size <= 0.) {
		return;
	}
	const auto height = size * 1.35;
	const auto progress = context.highlight.progress;
	const auto bounce = anim::Disabled() ? 0. : (progress < .38)
		? std::sin(std::clamp((progress - .08) / .3, 0., 1.) * M_PI)
		: .45 * std::sin(std::clamp((progress - .38) / .24, 0., 1.) * M_PI);
	const auto amplitude = std::min(
		style::ConvertFloatScale(settings.messageLocateBounce()),
		(area.height() - height) / 2.);
	const auto x = left
		? geometry.left() - gap - size
		: geometry.x() + geometry.width() + gap;
	const auto restingY = std::clamp(
		geometry.y() + (geometry.height() - height) / 2.,
		area.top() + amplitude,
		area.bottom() - height);
	const auto y = restingY - bounce * amplitude;
	p.translate(x, y);
	p.scale(size / 20., height / 27.);
	auto pin = QPainterPath();
	pin.moveTo(10., 26.);
	pin.cubicTo(8., 22., 0., 15., 0., 10.);
	pin.cubicTo(0., -3.3, 20., -3.3, 20., 10.);
	pin.cubicTo(20., 15., 12., 22., 10., 26.);
	pin.closeSubpath();
	pin.addEllipse(QPointF(10., 9.), 3.2, 3.2);
	pin.setFillRule(Qt::OddEvenFill);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawPath(pin);
}

} // namespace

void paint(
		QPainter &p,
		const Ui::ChatPaintContext &context,
		QRect geometry,
		Ui::BubbleRounding rounding,
		int outerWidth,
		bool preferLeft,
		int leftLimit,
		QRect action) {
	if (context.highlight.opacity <= 0. || geometry.isEmpty()) {
		return;
	}
	const auto visible = geometry.intersected(context.area);
	if (visible.isEmpty()) {
		return;
	}
	const auto &settings = ExtrasSettings::getInstance();
	const auto custom = settings.messageLocateColor();
	const auto color = custom.isEmpty()
		? context.messageStyle()->textPalette.linkFg->c
		: QColor(custom);
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.setOpacity(p.opacity() * context.highlight.opacity);
	if (settings.messageLocateStyle() == MessageLocateStyle::Pin) {
		geometry.setTop(visible.top());
		geometry.setBottom(visible.bottom());
		paintPin(p, context, geometry, outerWidth, preferLeft, leftLimit, action, color);
	} else {
		paintContour(p, context, geometry, rounding, color);
	}
	p.restore();
}

} // namespace ExtrasFeatures::MessageLocate
