#include "ui/controls/media_button.h"

#include "ui/color_contrast.h"
#include "ui/painter.h"
#include "ui/style/style_core_font.h"
#include "styles/style_file_preview.h"

#include <QtGui/QPainterPath>
#include <QtGui/QPainterPathStroker>
#include <numbers>

namespace Ui {
namespace {

constexpr auto kProgressDuration = crl::time(160);
constexpr auto kNumberInterval = crl::time(240);
constexpr auto kCompleteNumberHold = crl::time(350);
constexpr auto kCheckDuration = crl::time(220);
constexpr auto kCompleteHold = crl::time(800);

[[nodiscard]] QColor WithAlpha(QColor color, int alpha) {
	color.setAlpha(alpha);
	return color;
}

[[nodiscard]] QColor ContrastingIconColor(QColor color, QColor background) {
	if (CountContrast(color, background) >= 4.5) {
		return color;
	}
	const auto black = QColor(Qt::black);
	const auto white = QColor(Qt::white);
	const auto target = CountContrast(white, background)
		> CountContrast(black, background) ? white : black;
	auto low = 0.;
	auto high = 1.;
	for (auto i = 0; i != 8; ++i) {
		const auto middle = (low + high) / 2.;
		if (CountContrast(anim::color(color, target, middle), background) < 4.5) {
			low = middle;
		} else {
			high = middle;
		}
	}
	return anim::color(color, target, high);
}

[[nodiscard]] QRectF BadgeRect(QRectF inner, float64 size = 20.) {
	const auto scale = inner.width() / 44.;
	const auto center = inner.topLeft() + QPointF(38. * scale, 38. * scale);
	const auto side = size * scale;
	return QRectF(center.x() - side / 2., center.y() - side / 2., side, side);
}

[[nodiscard]] int DisplayPercent(float64 progress) {
	const auto percent = int(std::floor(progress * 100.));
	return (percent <= 0 || percent >= 100)
		? std::clamp(percent, 0, 100)
		: ((percent % 2) ? percent : percent - 1);
}

// 填充面积对应实际进度，圆弓高度通过二分求解。
[[nodiscard]] float64 FillHeight(float64 progress) {
	if (progress <= 0. || progress >= 1.) {
		return progress;
	}
	auto low = -1.;
	auto high = 1.;
	for (auto i = 0; i != 18; ++i) {
		const auto y = (low + high) / 2.;
		const auto area = (std::acos(y) - y * std::sqrt(1. - y * y))
			/ std::numbers::pi;
		if (area > progress) {
			low = y;
		} else {
			high = y;
		}
	}
	return (1. - (low + high) / 2.) / 2.;
}

} // namespace

MediaButtonColors MediaButtonPalette(
		QColor foreground,
		QColor background,
		bool overImage) {
	const auto circle = overImage
		? background
		: ContrastingIconColor(foreground, QColor(Qt::white));
	const auto symbol = overImage ? foreground : QColor(Qt::white);
	return {
		.foreground = symbol,
		.background = circle,
		.badge = WithAlpha(background, 255),
		.fill = WithAlpha(symbol, overImage ? 56 : 48),
	};
}

void PaintMediaButtonSurface(
		QPainter &p,
		QRectF inner,
		const MediaButtonColors &colors,
		bool hovered) {
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(hovered
		? anim::color(colors.background, colors.foreground, 0.1)
		: colors.background);
	p.drawEllipse(inner);
}

void PaintMediaIcon(
		QPainter &p,
		QRectF inner,
		const style::icon &icon,
		QColor color) {
	p.save();
	auto hq = PainterHighQualityEnabler(p);
	p.translate(inner.topLeft());
	p.scale(inner.width() / icon.width(), inner.height() / icon.height());
	icon.paintInCenter(p, QRect(0, 0, icon.width(), icon.height()), color);
	p.restore();
}

void PaintMediaProgressArc(
		QPainter &p,
		QRectF inner,
		const RadialAnimation &animation,
		float64 thickness,
		QColor color) {
	const auto state = animation.computeState();
	p.save();
	auto hq = PainterHighQualityEnabler(p);
	p.setOpacity(p.opacity() * state.shown);
	p.setPen(QPen(color, thickness, Qt::SolidLine, Qt::RoundCap));
	p.setBrush(Qt::NoBrush);
	p.drawArc(inner, state.arcFrom, state.arcLength);
	p.restore();
}

void PaintMediaCheck(
		QPainter &p,
		QRectF inner,
		QColor color,
		float64 progress) {
	if (progress <= 0.) {
		return;
	}
	const auto scale = inner.width() / 44.;
	const auto center = inner.center();
	const auto a = center + QPointF(-8., 0.) * scale;
	const auto b = center + QPointF(-2., 6.) * scale;
	const auto c = center + QPointF(9., -6.) * scale;
	const auto first = QLineF(a, b).length();
	const auto second = QLineF(b, c).length();
	const auto drawn = progress * (first + second);
	auto visible = QPainterPath();
	visible.moveTo(a);
	visible.lineTo(a + (b - a) * std::min(drawn / first, 1.));
	if (drawn > first) {
		visible.lineTo(b + (c - b) * ((drawn - first) / second));
	}
	auto stroke = QPainterPathStroker();
	stroke.setWidth(4. * scale);
	stroke.setCapStyle(Qt::RoundCap);
	stroke.setJoinStyle(Qt::RoundJoin);
	p.save();
	p.setClipPath(stroke.createStroke(visible), Qt::IntersectClip);
	PaintMediaIcon(p, inner, st::mediaProgressCheckIcon, color);
	p.restore();
}

void PaintMediaCancelBadge(
		QPainter &p,
		QRectF inner,
		const MediaButtonColors &colors,
		bool hovered) {
	const auto scale = inner.width() / 44.;
	const auto badge = BadgeRect(inner);
	const auto foreground = ContrastingIconColor(
		CountContrast(colors.foreground, colors.badge)
			> CountContrast(colors.background, colors.badge)
			? colors.foreground : colors.background,
		colors.badge);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(colors.badge);
	p.drawEllipse(BadgeRect(inner, 24.));
	p.setPen(QPen(WithAlpha(foreground, hovered ? 255 : 96), scale));
	p.drawEllipse(badge);
	if (hovered) {
		p.setPen(Qt::NoPen);
		p.setBrush(WithAlpha(foreground, 24));
		p.drawEllipse(badge.adjusted(scale, scale, -scale, -scale));
	}
	PaintMediaIcon(p, badge, st::mediaCancelBadgeIcon, foreground);
}

bool MediaCancelBadgeContains(QRectF inner, QPoint point) {
	auto path = QPainterPath();
	path.addEllipse(BadgeRect(inner, 30.));
	return path.contains(point);
}

QRect MediaButtonPaintRect(QRect inner) {
	return inner.united(BadgeRect(inner, 26.).toAlignedRect());
}

void MediaProgressAnimation::start(float64 progress) {
	_durationMultiplier = anim::DurationMultiplier();
	_from = _target = std::clamp(progress, 0., 1.);
	_changedAt = _numberAt = crl::now();
	_completedAt = 0;
	_percent = DisplayPercent(_target);
	_radial.start(_target);
}

float64 MediaProgressAnimation::progressAt(crl::time now) const {
	const auto ratio = anim::Disabled()
		? 1.
		: std::clamp((now - _changedAt)
			/ (kProgressDuration * _durationMultiplier), 0., 1.);
	return _from + (_target - _from) * anim::easeOutCubic(1., ratio);
}

bool MediaProgressAnimation::update(
		float64 progress,
		bool finished,
		crl::time now,
		bool loaded) {
	progress = (finished && loaded) ? 1. : std::clamp(progress, 0., 1.);
	const auto changed = progress != _target;
	if (changed) {
		_from = progress < _target ? progress : progressAt(now);
		_target = progress;
		_changedAt = now;
	}
	if (!finished) {
		_completedAt = 0;
	} else if (loaded && !_completedAt) {
		_completedAt = now;
	}
	const auto percent = DisplayPercent(progress);
	if ((percent != _percent)
		&& (anim::Disabled() || percent == 0 || percent == 100
			|| now - _numberAt >= kNumberInterval)) {
		_percent = percent;
		_numberAt = now;
	}
	const auto hold = _completedAt
		&& !anim::Disabled()
		&& now - _completedAt < kCompleteHold * _durationMultiplier;
	return _radial.update(progress, finished && !hold, now) || changed;
}

bool MediaProgressAnimation::showingCheck() const {
	return _completedAt && crl::now() - _completedAt
		>= kCompleteNumberHold * _durationMultiplier;
}

void MediaProgressAnimation::draw(
		QPainter &p,
		QRectF inner,
		float64 thickness,
		style::color color) const {
	_radial.draw(p, inner, thickness, color);
}

void MediaProgressAnimation::paintFill(
		QPainter &p,
		QRectF inner,
		const MediaButtonColors &colors,
		bool hovered) const {
	p.save();
	auto hq = PainterHighQualityEnabler(p);
	PaintMediaButtonSurface(p, inner, colors, hovered);
	auto clip = QPainterPath();
	clip.addEllipse(inner);
	p.setClipPath(clip, Qt::IntersectClip);
	const auto filled = inner.height() * FillHeight(progressAt(crl::now()));
	p.fillRect(QRectF(
		inner.left(), inner.bottom() - filled, inner.width(), filled), colors.fill);
	p.restore();
}

void MediaProgressAnimation::paint(
		QPainter &p,
		QRectF inner,
		const MediaButtonColors &colors,
		bool cancellable,
		bool hovered,
		const style::icon *readyIcon) const {
	p.save();
	const auto baseOpacity = p.opacity();
	const auto progressOpacity = (readyIcon && !cancellable) ? opacity() : 1.;
	if (progressOpacity < 1.) {
		p.setOpacity(baseOpacity * (1. - progressOpacity));
		PaintMediaButtonSurface(p, inner, colors);
		readyIcon->paintInCenter(p, inner.toRect(), colors.foreground);
	}
	p.setOpacity(baseOpacity * progressOpacity);
	paintFill(p, inner, colors);
	const auto scale = inner.width() / 44.;
	if (showingCheck()) {
		const auto progress = anim::Disabled() ? 1. : std::clamp(
			((crl::now() - _completedAt) / _durationMultiplier - kCompleteNumberHold)
				/ kCheckDuration, 0., 1.);
		PaintMediaCheck(p, inner, colors.foreground, progress);
	} else {
		auto font = style::font(13, style::FontFlag::Bold, 0)->f;
		font.setWeight(QFont::DemiBold);
		font.setPixelSize(int(std::round(13. * scale)));
		p.setFont(font);
		p.setPen(colors.foreground);
		const auto number = QString::number(_percent);
		const auto metrics = QFontMetricsF(font);
		const auto numberWidth = metrics.horizontalAdvance(number);
		auto small = font;
		small.setPixelSize(int(std::round(6. * scale)));
		const auto smallWidth = QFontMetricsF(small).horizontalAdvance(u"%"_q);
		const auto left = inner.center().x() - (numberWidth + smallWidth + scale) / 2.;
		const auto baseline = inner.center().y() + (metrics.ascent() - metrics.descent()) / 2.;
		p.drawText(QPointF(left, baseline), number);
		p.setFont(small);
		p.drawText(QPointF(left + numberWidth + scale, baseline), u"%"_q);
	}
	if (cancellable && !_completedAt) {
		PaintMediaCancelBadge(p, inner, colors, hovered);
	}
	p.restore();
}

} // namespace Ui
