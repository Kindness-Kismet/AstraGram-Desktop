/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_slide_animation.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "styles/style_basic.h"


namespace Window {

void ShowCrossFadeAnimation(
		not_null<QWidget*> parent,
		const QRect &geometry,
		QPixmap oldContentCache,
		QPixmap newContentCache,
		SlideDirection direction,
		CrossFadeArgs args) {
	const auto overlay = Ui::CreateChild<Ui::RpWidget>(parent);
	overlay->setObjectName(u"section-slide-overlay"_q);
	overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
	overlay->setGeometry(geometry);
	const auto animation = overlay->lifetime().make_state<SlideAnimation>();
	animation->setDirection(direction);
	animation->setCrossFade(std::move(args));
	animation->setPixmaps(
		std::move(oldContentCache),
		std::move(newContentCache));
	animation->setRepaintCallback([=] { overlay->update(); });
	animation->setFinishedCallback([=] {
		overlay->hide();
		overlay->deleteLater();
	});
	overlay->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(overlay);
		animation->paintContents(p);
	}, overlay->lifetime());
	overlay->show();
	overlay->raise();
	animation->start();
}

not_null<Ui::RpWidget*> ShowFadeOutAnimation(
		not_null<QWidget*> parent,
		const QRect &geometry,
		QPixmap cache,
		crl::time duration) {
	const auto overlay = Ui::CreateChild<Ui::RpWidget>(parent);
	overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
	overlay->setGeometry(geometry);
	const auto animation
		= overlay->lifetime().make_state<Ui::Animations::Simple>();
	overlay->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(overlay);
		// Source 模式带不透明度 o 时结果为 o * 旧画面 + (1 - o) * 底下已绘制的内容，
		// 材质下的透明像素同样按比例插值。
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setOpacity(animation->value(0.));
		p.drawPixmap(0, 0, cache);
	}, overlay->lifetime());
	overlay->show();
	overlay->raise();
	animation->start([=] {
		overlay->update();
		if (animation->animating()) {
			return;
		}
		overlay->hide();
		overlay->deleteLater();
	}, 1., 0., duration, anim::easeOutCubic);
	return overlay;
}

void SlideAnimation::paintContents(QPainter &p) const {
	if (_crossFade) {
		paintCrossFade(p);
		return;
	}
	const auto retina = style::DevicePixelRatio();

	const auto slideLeft = (_direction == SlideDirection::FromLeft);
	const auto progress = _animation.value(slideLeft ? 0. : 1.);
	if (_withFade) {
		const auto dt = slideLeft
			? (1. - progress)
			: progress;
		const auto easeOut = anim::easeOutCirc(1., dt);
		const auto easeIn = anim::easeInCirc(1., dt);
		const auto arrivingAlpha = easeIn;
		const auto departingAlpha = 1. - easeOut;
		const auto leftWidthFull = _cacheUnder.width() / retina;
		const auto rightWidthFull = _cacheOver.width() / retina;
		const auto leftCoord = slideLeft
			? anim::interpolate(-leftWidthFull, 0, easeOut)
			: anim::interpolate(0, -leftWidthFull, easeIn);
		const auto leftAlpha = (slideLeft ? arrivingAlpha : departingAlpha);
		const auto rightCoord = slideLeft
			? anim::interpolate(0, rightWidthFull, easeIn)
			: anim::interpolate(rightWidthFull, 0, easeOut);
		const auto rightAlpha = (slideLeft ? departingAlpha : arrivingAlpha);

		const auto leftWidth = (leftWidthFull + leftCoord);
		const auto rightWidth = rightWidthFull - rightCoord;

		if (!_mask.isNull()) {
			auto frame = QImage(
				_mask.size(),
				QImage::Format_ARGB32_Premultiplied);
			frame.setDevicePixelRatio(_mask.devicePixelRatio());
			frame.fill(Qt::transparent);
			QPainter q(&frame);

			if (leftWidth > 0) {
				q.setOpacity(leftAlpha);
				q.drawPixmap(
					0,
					0,
					_cacheUnder,
					_cacheUnder.width()
						- leftWidth * style::DevicePixelRatio(),
					0,
					leftWidth * style::DevicePixelRatio(),
					_topSkip * retina);
			}

			if (rightWidth > 0) {
				q.setOpacity(rightAlpha);
				q.drawPixmap(
					rightCoord,
					0,
					_cacheOver,
					0,
					0,
					rightWidth * style::DevicePixelRatio(),
					_topSkip * retina);
			}

			q.setOpacity(1.);
			q.setCompositionMode(QPainter::CompositionMode_DestinationIn);
			q.drawPixmap(0, 0, _mask);

			p.drawImage(0, 0, frame);
		}

		if (leftWidth > 0) {
			p.setOpacity(leftAlpha);
			p.drawPixmap(
				0,
				_topSkip,
				_cacheUnder,
				(_cacheUnder.width() - leftWidth * retina),
				_topSkip * retina,
				leftWidth * retina,
				_cacheUnder.height() - _topSkip * retina);
		}
		if (rightWidth > 0) {
			p.setOpacity(rightAlpha);
			p.drawPixmap(
				rightCoord,
				_topSkip,
				_cacheOver,
				0,
				_topSkip * retina,
				rightWidth * retina,
				_cacheOver.height() - _topSkip * retina);
		}
	} else if (_direction == SlideDirection::FromBottom) {
		const auto height = _cacheOver.height() / retina;
		const auto offset = anim::interpolate(0, height, progress);
		p.drawPixmap(
			QRect(QPoint(0, -offset), _cacheUnder.size() / retina),
			_cacheUnder,
			QRect(QPoint(), _cacheUnder.size()));
		p.drawPixmap(
			QRect(QPoint(0, height - offset), _cacheOver.size() / retina),
			_cacheOver,
			QRect(QPoint(), _cacheOver.size()));
	} else {
		const auto coordUnder = anim::interpolate(
			0,
			-st::slideShift,
			progress);
		const auto coordOver = anim::interpolate(
			_cacheOver.width() / retina,
			0,
			progress);
		if (coordOver) {
			p.drawPixmap(
				QRect(0, 0, coordOver, _cacheUnder.height() / retina),
				_cacheUnder,
				QRect(
					-coordUnder * retina,
					0,
					coordOver * retina,
					_cacheUnder.height()));
			p.setOpacity(progress);
			p.fillRect(
				0,
				0,
				coordOver, _cacheUnder.height() / retina,
				st::slideFadeOutBg);
			p.setOpacity(1);
		}
		p.drawPixmap(
			QRect(QPoint(coordOver, 0), _cacheOver.size() / retina),
			_cacheOver,
			QRect(QPoint(), _cacheOver.size()));
		p.setOpacity(progress);
		st::slideShadow.fill(
			p,
			QRect(
				coordOver - st::slideShadow.width(),
				0,
				st::slideShadow.width(),
				_cacheOver.height() / retina));
	}
}

void SlideAnimation::paintCrossFade(QPainter &p) const {
	const auto &args = *_crossFade;
	const auto slideLeft = (_direction == SlideDirection::FromLeft);
	const auto &was = slideLeft ? _cacheOver : _cacheUnder;
	const auto &now = slideLeft ? _cacheUnder : _cacheOver;
	const auto shown = progress();
	const auto shift = slideLeft ? args.shift : -args.shift;
	const auto ratio = style::DevicePixelRatio();
	if (_crossFadeFrame.size() != was.size()) {
		_crossFadeFrame = QImage(
			was.size(),
			QImage::Format_ARGB32_Premultiplied);
		_crossFadeFrame.setDevicePixelRatio(ratio);
	}
	auto q = QPainter(&_crossFadeFrame);
	// Source 模式带不透明度 o 时结果为 o * 本层 + (1 - o) * 底，透明像素同样成立。
	q.setCompositionMode(QPainter::CompositionMode_Source);
	if (args.overlay) {
		// 平移露出的部分不绘制上层，直接透出下层。
		q.drawPixmap(0, 0, slideLeft ? now : was);
		q.setOpacity(slideLeft ? (1. - shown) : shown);
		q.drawPixmap(
			slideLeft
				? anim::interpolate(0, shift, shown)
				: anim::interpolate(-shift, 0, shown),
			0,
			slideLeft ? was : now);
		q.end();
		paintCrossFadeFrame(p);
		return;
	}
	// 旧内容前半程淡出、新内容稍后淡入，余量由 bg 补足。
	constexpr auto kWasPart = 0.5;
	constexpr auto kNowDelay = 0.2;
	const auto wasAlpha = std::max(1. - shown / kWasPart, 0.);
	const auto nowAlpha = std::clamp(
		(shown - kNowDelay) / (1. - kNowDelay),
		0.,
		1.);
	// 平移露出的边缘按 bg 计入同一层；纯色填充不随不透明度插值，改用图片。
	auto edge = QImage();
	if (shift) {
		edge = QImage(
			QSize(
				std::abs(shift) * ratio,
				std::max(was.height(), now.height())),
			QImage::Format_ARGB32_Premultiplied);
		edge.fill(args.bg);
	}
	const auto layer = [&](const QPixmap &cache, int x, float64 opacity) {
		if (opacity <= 0.) {
			return;
		}
		const auto width = cache.width() / ratio;
		const auto height = cache.height() / ratio;
		q.setOpacity(opacity);
		q.drawPixmap(x, 0, cache);
		if (x) {
			q.drawImage(
				QRect((x > 0) ? 0 : (width + x), 0, std::abs(x), height),
				edge,
				QRect(0, 0, std::abs(x) * ratio, cache.height()));
		}
	};
	// 依次插值后各层权重：bg 为 1 - a - b，旧内容为 a，新内容为 b。
	q.fillRect(QRect(QPoint(), was.size() / ratio), args.bg);
	layer(
		was,
		anim::interpolate(0, shift, shown),
		(wasAlpha > 0.) ? (wasAlpha / (1. - nowAlpha)) : 0.);
	layer(now, anim::interpolate(-shift, 0, shown), nowAlpha);
	q.end();
	paintCrossFadeFrame(p);
}

void SlideAnimation::paintCrossFadeFrame(QPainter &p) const {
	// 整帧替换下层：材质下画布并不遮挡，底下已是切换后的内容。
	p.setCompositionMode(QPainter::CompositionMode_Source);
	p.drawImage(0, 0, _crossFadeFrame);
	p.setCompositionMode(QPainter::CompositionMode_SourceOver);
}

float64 SlideAnimation::progress() const {
	const auto slideLeft = (_direction == SlideDirection::FromLeft);
	const auto progress = _animation.value(slideLeft ? 0. : 1.);
	return slideLeft ? (1. - progress) : progress;
}

void SlideAnimation::setDirection(SlideDirection direction) {
	_direction = direction;
}

void SlideAnimation::setPixmaps(
		const QPixmap &oldContentCache,
		const QPixmap &newContentCache) {
	_cacheUnder = oldContentCache;
	_cacheOver = newContentCache;
}

void SlideAnimation::setTopBarShadow(bool enabled) {
	_topBarShadowEnabled = enabled;
}

void SlideAnimation::setTopSkip(int skip) {
	_topSkip = skip;
}

void SlideAnimation::setWithFade(bool withFade) {
	_withFade = withFade;
}

void SlideAnimation::setCrossFade(CrossFadeArgs args) {
	_crossFade = std::move(args);
}

void SlideAnimation::setRepaintCallback(RepaintCallback &&callback) {
	_repaintCallback = std::move(callback);
}

void SlideAnimation::setFinishedCallback(FinishedCallback &&callback) {
	_finishedCallback = std::move(callback);
}

void SlideAnimation::setTopBarMask(const QPixmap &mask) {
	_mask = mask;
}

void SlideAnimation::start() {
	const auto fromLeft = (_direction == SlideDirection::FromLeft);
	if (fromLeft) {
		std::swap(_cacheUnder, _cacheOver);
	}
	const auto fromBottom = (_direction == SlideDirection::FromBottom);
	const auto curve = fromBottom
		? anim::easeOutQuint
		: _crossFade
		? anim::easeOutCubic
		: transition();
	const auto duration = fromBottom
		? (st::slideDuration * 2)
		: (_crossFade && _crossFade->duration)
		? _crossFade->duration
		: st::slideDuration;
	_animation.start(
		[this] { animationCallback(); },
		fromLeft ? 1. : 0.,
		fromLeft ? 0. : 1.,
		duration,
		curve);
	if (const auto onstack = _repaintCallback) {
		onstack();
	}
}

void SlideAnimation::animationCallback() {
	if (const auto onstack = _repaintCallback) {
		onstack();
	}
	if (!_animation.animating()) {
		if (const auto onstack = _finishedCallback) {
			onstack();
		}
	}
}

} // namespace Window
