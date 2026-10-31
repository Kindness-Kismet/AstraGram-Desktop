#include "extras/features/music_player/music_player_widgets.h"
#include "extras/ui/utils/extras_tooltip.h"

#include "styles/style_extras_styles.h"
#include "styles/style_chat.h"
#include "ui/painter.h"
#include "ui/effects/animations.h"
#include "ui/image/image_prepare.h"

#include <QtGui/QPainterPath>
#include <array>
#include <cmath>

namespace Extras::MusicPlayer {

QColor accentColor() {
	return st::windowBg->c.lightness() < 128
		? QColor(66, 165, 245) : QColor(30, 136, 229);
}

QColor softColor() {
	auto color = accentColor();
	color.setAlpha(24);
	return color;
}

PlayerButton::PlayerButton(QWidget *parent, PlayerIcon icon,
		const QString &name, const QString &label)
: AbstractButton(parent)
, _icon(icon) {
	setObjectName(name);
	setLabel(label);
	ExtrasUi::installTooltip(this, [=] { return accessibleName(); }, st::topBarTooltip);
	setFocusPolicy(Qt::StrongFocus);
	resize(style::ConvertScale(30), style::ConvertScale(30));
}

void PlayerButton::setIcon(PlayerIcon icon) {
	_icon = icon;
	update();
}

void PlayerButton::setText(QString text) {
	_text = std::move(text);
	update();
}

void PlayerButton::setActive(bool active) {
	_active = active;
	update();
}

void PlayerButton::setPrimary(bool primary) {
	_primary = primary;
	update();
}

void PlayerButton::setLabel(const QString &label) {
	setAccessibleName(label);
}

void PlayerButton::paintEvent(QPaintEvent *event) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	if (isDisabled()) {
		p.setOpacity(0.35);
	}
	p.translate(QRectF(rect()).center());
	if (isDown()) {
		p.scale(0.95, 0.95);
	}
	const auto bounds = QRectF(-width() / 2., -height() / 2., width(), height());
	p.setPen(Qt::NoPen);
	if (_primary) {
		p.setBrush(isOver() ? accentColor().darker(110) : accentColor());
		p.drawEllipse(bounds);
	} else if (isOver() || _active || hasFocus()) {
		p.setBrush(softColor());
		p.drawRoundedRect(bounds, style::ConvertScale(8), style::ConvertScale(8));
	}
	const auto foreground = _primary ? QColor(Qt::white)
		: (_active || isOver() || _icon == PlayerIcon::Previous || _icon == PlayerIcon::Next)
		? accentColor() : st::windowSubTextFg->c;
	p.setPen(foreground);
	if (!_text.isEmpty() && _icon == PlayerIcon::None) {
		p.setFont(st::extrasMusicCompactSmall);
		p.drawText(bounds, Qt::AlignCenter, _text);
		return;
	}
	if (!_text.isEmpty()) {
		p.setFont(st::extrasMusicCompactSmall);
		p.drawText(bounds.adjusted(width() / 2., 0, -style::ConvertScale(5), 0),
			Qt::AlignCenter, _text);
		p.translate(-style::ConvertScale(10), 0);
	}
	const auto side = _primary
		? style::ConvertScale(height() >= style::ConvertScale(56) ? 28 : 16)
		: style::ConvertScale(19);
	p.scale(side / 24., side / 24.);
	p.translate(-12, -12);
	p.setPen(QPen(foreground, 1.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.setBrush(Qt::NoBrush);
	auto path = QPainterPath();
	const auto line = [&](qreal x, qreal y, qreal x2, qreal y2) {
		path.moveTo(x, y);
		path.lineTo(x2, y2);
	};
	switch (_icon) {
	case PlayerIcon::Play:
		path.moveTo(8, 5); path.lineTo(19, 12); path.lineTo(8, 19); path.closeSubpath();
		p.fillPath(path, foreground);
		return;
	case PlayerIcon::Pause:
		p.setPen(Qt::NoPen); p.setBrush(foreground);
		p.drawRoundedRect(QRectF(6, 5, 4, 14), 1, 1);
		p.drawRoundedRect(QRectF(14, 5, 4, 14), 1, 1);
		return;
	case PlayerIcon::Previous:
	case PlayerIcon::Next:
		if (_icon == PlayerIcon::Next) { p.translate(24, 0); p.scale(-1, 1); }
		p.fillRect(QRectF(5, 6, 2, 12), foreground);
		path.moveTo(18, 6); path.lineTo(8.5, 12); path.lineTo(18, 18); path.closeSubpath();
		p.fillPath(path, foreground);
		return;
	case PlayerIcon::Collapse:
		path.moveTo(5, 9); path.lineTo(12, 16); path.lineTo(19, 9);
		break;
	case PlayerIcon::Minimize:
		line(5, 17, 19, 17);
		break;
	case PlayerIcon::Cancel:
	case PlayerIcon::Close:
		line(6, 6, 18, 18); line(18, 6, 6, 18);
		break;
	case PlayerIcon::Expand:
		path.moveTo(9, 4); path.lineTo(4, 4); path.lineTo(4, 9);
		path.moveTo(15, 20); path.lineTo(20, 20); path.lineTo(20, 15);
		line(4, 4, 10, 10); line(20, 20, 14, 14);
		path.moveTo(15, 4); path.lineTo(20, 4); path.lineTo(20, 9);
		path.moveTo(9, 20); path.lineTo(4, 20); path.lineTo(4, 15);
		break;
	case PlayerIcon::Download:
		p.drawEllipse(QRectF(3, 3, 18, 18));
		line(12, 7, 12, 16); path.moveTo(8, 12); path.lineTo(12, 16); path.lineTo(16, 12);
		break;
	case PlayerIcon::Repeat:
	case PlayerIcon::RepeatOne:
		path.moveTo(3, 10); path.lineTo(3, 8); path.quadTo(3, 5, 6, 5); path.lineTo(20, 5);
		path.moveTo(17, 2); path.lineTo(20, 5); path.lineTo(17, 8);
		path.moveTo(21, 14); path.lineTo(21, 16); path.quadTo(21, 19, 18, 19); path.lineTo(4, 19);
		path.moveTo(7, 16); path.lineTo(4, 19); path.lineTo(7, 22);
		if (_icon == PlayerIcon::RepeatOne) { line(11, 10, 12, 9); line(12, 9, 12, 15); }
		break;
	case PlayerIcon::Reverse:
		p.translate(24, 24); p.rotate(180);
		[[fallthrough]];
	case PlayerIcon::Order:
		line(4, 5, 12, 5); line(4, 10, 10, 10); line(4, 15, 8, 15);
		line(17, 4, 17, 20); path.moveTo(13, 16); path.lineTo(17, 20); path.lineTo(21, 16);
		break;
	case PlayerIcon::Shuffle:
		line(4, 20, 20, 4); line(4, 4, 9, 9); line(15, 15, 20, 20);
		path.moveTo(15, 4); path.lineTo(20, 4); path.lineTo(20, 9);
		path.moveTo(15, 20); path.lineTo(20, 20); path.lineTo(20, 15);
		break;
	case PlayerIcon::Volume:
	case PlayerIcon::Muted:
		path.moveTo(4, 9); path.lineTo(8, 9); path.lineTo(12, 5); path.lineTo(12, 19);
		path.lineTo(8, 15); path.lineTo(4, 15); path.closeSubpath();
		if (_icon == PlayerIcon::Muted) { line(16, 9, 22, 15); line(22, 9, 16, 15); }
		else { path.moveTo(16, 8); path.quadTo(20, 12, 16, 16);
			path.moveTo(19, 5); path.quadTo(25, 12, 19, 19); }
		break;
	case PlayerIcon::Queue:
		line(3, 5, 21, 5); line(3, 10, 21, 10); line(3, 15, 12, 15);
		path.moveTo(17, 14); path.lineTo(22, 17); path.lineTo(17, 20); path.closeSubpath();
		break;
	case PlayerIcon::None:
		return;
	}
	p.drawPath(path);
}

Waveform::Waveform(QWidget *parent)
: RpWidget(parent)
, _timer([=] { update(); }) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
}

void Waveform::setPlaying(bool playing) {
	_playing = playing;
	refreshAnimation();
	update();
}

void Waveform::refreshAnimation() {
	if (_playing && isVisible() && !anim::Disabled()) {
		if (!_timer.isActive()) { _timer.callEach(16); }
	} else {
		_timer.cancel();
	}
}

void Waveform::showEvent(QShowEvent *event) {
	RpWidget::showEvent(event);
	refreshAnimation();
}

void Waveform::hideEvent(QHideEvent *event) {
	_timer.cancel();
	RpWidget::hideEvent(event);
}

void Waveform::paintEvent(QPaintEvent *event) {
	auto p = QPainter(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(accentColor());
	const auto elapsed = _timer.isActive() ? crl::now() : crl::time(0);
	const auto heights = std::array<double, 3>{
		16. + 6. * std::cos(elapsed * 6.283185 / 1100.),
		16. + 8. * std::sin(elapsed * 6.283185 / 950.),
		14. - 5. * std::cos(elapsed * 6.283185 / 1250.),
	};
	const auto scale = height() / 28.;
	for (auto i = 0; i != 3; ++i) {
		const auto h = heights[i] * scale;
		const auto bar = QRectF(width() / 2. + (i - 1) * 9. * scale - 2. * scale,
			(height() - h) / 2., 4. * scale, h);
		p.drawRoundedRect(bar, 2. * scale, 2. * scale);
	}
}

PlayerProgress::PlayerProgress(QWidget *parent)
: MediaSlider(parent, st::extrasMusicProgress) {
}

void PlayerProgress::setMini(bool mini) {
	_mini = mini;
	update();
}

void PlayerProgress::paintEvent(QPaintEvent *event) {
	if (!_mini) {
		MediaSlider::paintEvent(event);
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	const auto radius = style::ConvertScale(10);
	if (_miniCorners[0].width() != radius * ratio) {
		_miniCorners = Images::CornersMask(radius);
	}
	auto buffer = QImage(size() * ratio, QImage::Format_ARGB32_Premultiplied);
	buffer.setDevicePixelRatio(ratio);
	buffer.fill(Qt::transparent);
	auto p = QPainter(&buffer);
	p.setRenderHint(QPainter::Antialiasing);
	const auto over = getCurrentOverFactor();
	const auto thickness = style::ConvertScale(5) + style::ConvertScale(2) * over;
	const auto track = QRectF(0, height() - thickness, width(), thickness);
	p.fillRect(track, st::windowBgOver);
	const auto progress = width() * getCurrentValue();
	p.fillRect(QRectF(0, track.y(), progress, track.height()), accentColor());
	if (over > 0.) {
		p.setOpacity(over);
		p.setPen(QPen(st::windowBg, style::ConvertScale(2)));
		p.setBrush(accentColor());
		const auto radius = style::ConvertScale(5);
		p.drawEllipse(QPointF(progress, track.center().y()), radius, radius);
	}
	p.end();
	buffer = Images::Round(std::move(buffer), _miniCorners,
		RectPart::BottomLeft | RectPart::BottomRight);
	QPainter(this).drawImage(QPoint(), buffer);
}

void CoverPainter::paint(QPainter &p, const QRect &rect, const QImage &image,
		bool video, bool voice, int radius, double zoom, bool dimmed, bool gradient) {
	const auto ratio = style::DevicePixelRatio();
	const auto size = rect.size() * ratio;
	const auto background = st::windowBgOver->c;
	if (_cache.size() != size || _cache.devicePixelRatio() != ratio
		|| _imageKey != image.cacheKey() || _background != background
		|| _radius != radius || _zoom != zoom || _video != video || _voice != voice
		|| _dimmed != dimmed || _gradient != gradient) {
		_imageKey = image.cacheKey();
		_background = background;
		_radius = radius;
		_zoom = zoom;
		_video = video;
		_voice = voice;
		_dimmed = dimmed;
		_gradient = gradient;
		_cache = QImage(size, QImage::Format_ARGB32_Premultiplied);
		_cache.setDevicePixelRatio(ratio);
		_cache.fill(background);
		{
			auto painter = QPainter(&_cache);
			auto hq = PainterHighQualityEnabler(painter);
			const auto bounds = QRect(QPoint(), rect.size());
			if (image.isNull()) {
				(video ? st::extrasMediaVideo : voice ? st::extrasMediaVoice
					: st::extrasMusicPlaceholder).paintInCenter(painter, bounds);
			} else {
				const auto fitted = QSizeF(image.size()).scaled(QSizeF(rect.size()),
					Qt::KeepAspectRatioByExpanding) * zoom;
				painter.setOpacity(dimmed ? 0.9 : 1.);
				painter.drawImage(QRectF(QRectF(bounds).center()
					- QPointF(fitted.width() / 2., fitted.height() / 2.), fitted), image);
				painter.setOpacity(1.);
			}
			if (gradient) {
				auto shade = QLinearGradient(0, 0, 0, rect.height());
				shade.setColorAt(0.45, Qt::transparent);
				shade.setColorAt(1., QColor(15, 23, 42, 72));
				painter.fillRect(bounds, shade);
			}
		}
		// 复用图片库的透明遮罩，让缩略图和大封面都保留平滑边缘。
		const auto masks = Images::CornersMask(style::ConvertScale(radius));
		_cache = Images::Round(std::move(_cache), masks);
	}
	p.drawImage(rect.topLeft(), _cache);
}

CoverButton::CoverButton(QWidget *parent) : AbstractButton(parent) {
	setObjectName(u"music.cover"_q);
	setFocusPolicy(Qt::StrongFocus);
	ExtrasUi::installTooltip(this, [=] { return accessibleName(); }, st::topBarTooltip);
}

void CoverButton::setImage(QImage image, bool video, bool voice) {
	_image = std::move(image);
	_video = video;
	_voice = voice;
	update();
}

void CoverButton::setPlayback(bool expanded, bool playing) {
	if (_expanded == expanded && _playing == playing) { return; }
	_expanded = expanded;
	_playing = playing;
	animate();
}

void CoverButton::setLabels(QString mode, QString format) {
	_mode = std::move(mode);
	_format = std::move(format);
	update();
}

void CoverButton::animate() {
	// 始终铺满圆角区域，暂停缩回原尺寸，避免露出图片内沿的直角。
	const auto to = _expanded ? (isOver() ? 1.06 : _playing ? 1.03 : 1.)
		: isOver() ? 1.05 : 1.;
	const auto from = _animation.value(_zoom);
	_zoom = to;
	_animation.start([=] { update(); }, from, to,
		anim::Disabled() ? 0 : 700, anim::sineInOut);
}

void CoverButton::paintEvent(QPaintEvent *event) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	_painter.paint(p, rect(), _image, _video, _voice, 10,
		_animation.value(_zoom), _expanded && !_playing, _expanded);
	if (!_expanded) { return; }
	p.setFont(st::extrasMusicCompactSmall);
	const auto badge = [&](const QString &text, bool right) {
		if (text.isEmpty()) { return; }
		const auto w = st::extrasMusicCompactSmall->width(text) + style::ConvertScale(16);
		const auto h = style::ConvertScale(22);
		const auto margin = style::ConvertScale(12);
		const auto bounds = QRect(right ? width() - w - margin : margin,
			height() - h - margin, w, h);
		p.setPen(Qt::NoPen);
		p.setBrush(right ? accentColor() : QColor(0, 0, 0, 102));
		p.drawRoundedRect(bounds, style::ConvertScale(6), style::ConvertScale(6));
		p.setPen(Qt::white);
		p.drawText(bounds, Qt::AlignCenter, text);
	};
	badge(_mode, false);
	badge(_format, true);
}

void CoverButton::enterEventHook(QEnterEvent *event) {
	AbstractButton::enterEventHook(event);
	animate();
}

void CoverButton::leaveEventHook(QEvent *event) {
	AbstractButton::leaveEventHook(event);
	animate();
}

} // namespace Extras::MusicPlayer
