/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/layer_backdrop.h"

#include "ui/image/image_prepare.h"
#include "ui/layers/layer_widget.h"
#include "ui/widgets/shadow.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"

#include "styles/style_layers.h"

#include <QtGui/QGuiApplication>

namespace Ui {
namespace {

constexpr auto kCacheBackgroundFastTimeout = crl::time(200);
constexpr auto kCacheBackgroundFullTimeout = crl::time(1000);
constexpr auto kFadeBackgroundDuration = crl::time(200);

auto CapturingDepth = 0;

// Thread: Main.
[[nodiscard]] bool IsNightMode() {
	return (st::windowBg->c.lightnessF() < 0.5);
}

[[nodiscard]] QColor OverlayColor(int alpha) {
	return QColor(16, 16, 16, alpha);
}

[[nodiscard]] QImage ProcessBackground(QImage image, int alpha, int scale) {
	const auto size = image.size();
	auto p = QPainter(&image);
	p.fillRect(
		QRect(QPoint(), image.size() / image.devicePixelRatio()),
		OverlayColor(alpha));
	p.end();
	return Images::DitherImage(
		Images::BlurLargeImage(
			std::move(image).scaled(
				size / scale,
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation),
			24).scaled(
				size,
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation));
}

// 截取下层时只保留其阴影，贴边部分与图层容器采用相同的外扩规则。
void PaintSpecialLayerBackground(
		QPainter &p,
		QRect outer,
		QRect box,
		int radius) {
	static const auto shadow = BoxShadow(st::boxRoundShadow);
	const auto skip = shadow.extend()
		+ QMargins(radius, radius, radius, radius);
	shadow.paint(p, box.marginsAdded({
		(box.x() > outer.x()) ? 0 : skip.left(),
		(box.y() > outer.y()) ? 0 : skip.top(),
		(box.x() + box.width() < outer.x() + outer.width())
			? 0 : skip.right(),
		(box.y() + box.height() < outer.y() + outer.height())
			? 0 : skip.bottom(),
	}), radius);
	p.fillRect(outer, st::layerBg);
}

[[nodiscard]] std::vector<QPointer<QWidget>> DirectChildren(
		not_null<QWidget*> parent) {
	auto result = std::vector<QPointer<QWidget>>();
	for (const auto child : parent->children()) {
		const auto widget = qobject_cast<QWidget*>(child);
		if (widget) {
			result.emplace_back(widget);
		}
	}
	return result;
}

void RenderPart(
		QPainter &p,
		not_null<QWidget*> widget,
		QPoint position,
		QRect region,
		QPoint origin) {
	const auto rect = region.intersected({ position, widget->size() });
	auto part = QRegion(rect.translated(-position));
	const auto mask = widget->mask();
	if (!mask.isEmpty()) {
		part &= mask;
	}
	if (part.isEmpty() || !widget->updatesEnabled()) {
		return;
	}
	// render() puts the top-left of the masked region at the offset.
	RenderWidget(
		p,
		widget,
		position + part.boundingRect().topLeft() - origin,
		part,
		QWidget::DrawChildren);
}

} // namespace

QImage CaptureUnderLayer(
		not_null<LayerWidget*> layer,
		LayerWidget *underlying,
		Fn<void()> beforeRender) {
	const auto weak = QPointer<LayerWidget>(layer);
	auto stack = QPointer<QWidget>(layer->parentWidget());
	auto target = QPointer<QWidget>(stack ? stack->parentWidget() : nullptr);
	const auto special = QPointer<LayerWidget>(underlying);
	if (!target) {
		return QImage();
	}
	SendPendingMoveResizeEvents(target.data());
	if (!weak) {
		return QImage();
	}
	stack = weak->parentWidget();
	target = stack ? stack->parentWidget() : nullptr;
	if (!target) {
		return QImage();
	}
	if (beforeRender) {
		beforeRender();
	}
	if (!weak || !stack || !target) {
		return QImage();
	}
	const auto capture = QRect(
		stack->pos() + weak->pos(),
		weak->size());
	const auto origin = capture.topLeft();
	const auto ratio = style::DevicePixelRatio();
	auto image = QImage(
		capture.size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(Qt::transparent);
	++CapturingDepth;
	const auto restore = gsl::finally([] {
		--CapturingDepth;
	});
	auto p = QPainter(&image);
	RenderWidget(p, target.data(), QPoint(), capture, QWidget::IgnoreMask);
	const auto children = DirectChildren(target.data());
	for (const auto &child : children) {
		if (!child
			|| child->parentWidget() != target
			|| child->isHidden()
			|| child->isWindow()
			|| !child->updatesEnabled()) {
			continue;
		} else if (child != stack) {
			RenderPart(p, child.data(), child->pos(), capture, origin);
		} else if (special
			&& special->parentWidget() == stack
			&& !special->isHidden()
			&& !special->isWindow()
			&& special->updatesEnabled()) {
			p.save();
			p.translate(stack->pos() - origin);
			PaintSpecialLayerBackground(
				p,
				stack->rect(),
				special->geometry(),
				special->cornerRadius());
			p.restore();
			RenderPart(
				p,
				special.data(),
				stack->pos() + special->pos(),
				capture.intersected(stack->geometry()),
				origin);
		}
	}
	p.end();
	return image;
}

bool CapturingUnderLayer() {
	return (CapturingDepth > 0);
}

LayerBackdrop::LayerBackdrop(
	not_null<RpWidget*> owner,
	Fn<QImage()> capture,
	Fn<int(bool night)> overlayAlpha,
	Fn<QImage(QImage, int, int)> process)
: _owner(owner)
, _capture(std::move(capture))
, _overlayAlpha(std::move(overlayAlpha))
, _process(process ? std::move(process) : ProcessBackground)
, _timer([=] { checkCache(); }) {
	_owner->sizeValue(
	) | rpl::on_next([=] {
		checkStale();
	}, _lifetime);

	style::PaletteChanged(
	) | rpl::on_next([=] {
		checkStale();
	}, _lifetime);
}

void LayerBackdrop::start() {
	start(_capture());
}

void LayerBackdrop::start(QImage image) {
	if (image.isNull()) {
		return;
	}
	_fade.stop();
	_timer.cancel();
	_back = QImage();
	_next = QImage();
	const auto night = IsNightMode();
	_night = night;
	_readyGeneration = ++_generation;
	_image = _process(
		std::move(image),
		_overlayAlpha(night),
		style::ConvertScale(4));
}

bool LayerBackdrop::started() const {
	return !_image.isNull();
}

void LayerBackdrop::invalidate() {
	++_generation;
	checkStale();
}

void LayerBackdrop::paint(QPainter &p) const {
	const auto target = _owner->rect();
	const auto faded = _fade.value(1.);
	if (faded < 1.) {
		const auto opacity = p.opacity();
		const auto shown = opacity * faded;
		// The painter's opacity applies once to the cross-faded pair.
		p.setOpacity(opacity * (1. - faded) / (1. - shown));
		p.drawImage(target, _back);
		if (faded > 0.) {
			p.setOpacity(shown);
			p.drawImage(target, _image);
		}
		p.setOpacity(opacity);
	} else {
		p.drawImage(target, _image);
	}
}

void LayerBackdrop::paintDim(QPainter &p, float64 opacity) const {
	if (opacity <= 0.) {
		return;
	}
	const auto was = p.opacity();
	p.setOpacity(was * opacity);
	p.fillRect(_owner->rect(), OverlayColor(_overlayAlpha(IsNightMode())));
	p.setOpacity(was);
}

rpl::producer<> LayerBackdrop::changes() const {
	return _changes.events();
}

void LayerBackdrop::checkStale() {
	if (!started()) {
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	const auto &ready = _next.isNull() ? _image : _next;
	if (ready.size() == _owner->size() * ratio
		&& _night == IsNightMode()
		&& _readyGeneration == _generation) {
		_timer.cancel();
	} else if (!_caching && !_timer.isActive()) {
		_lastAreaChangeTime = crl::now();
		_timer.callOnce(kCacheBackgroundFastTimeout);
	}
}

void LayerBackdrop::checkCache() {
	if (_caching || _timer.isActive()) {
		return;
	}
	const auto now = crl::now();
	if (now - _lastAreaChangeTime < kCacheBackgroundFullTimeout
		&& QGuiApplication::mouseButtons() != 0) {
		_timer.callOnce(kCacheBackgroundFastTimeout);
		return;
	}
	cache();
}

void LayerBackdrop::cache() {
	_caching = true;
	const auto weak = base::make_weak(this);
	const auto night = IsNightMode();
	const auto alpha = _overlayAlpha(night);
	const auto scale = style::ConvertScale(4);
	const auto generation = _generation;
	const auto process = _process;
	auto image = _capture();
	if (image.isNull()) {
		_caching = false;
		return;
	}
	crl::async([=, image = std::move(image)]() mutable {
		auto result = process(std::move(image), alpha, scale);
		crl::on_main([=, result = std::move(result)]() mutable {
			if (const auto strong = weak.get()) {
				strong->ready(std::move(result), night, generation);
			}
		});
	});
}

void LayerBackdrop::ready(QImage image, bool night, int generation) {
	_caching = false;

	const auto required = _owner->size() * style::DevicePixelRatio();
	if (image.size() == required
		&& night == IsNightMode()
		&& generation == _generation) {
		_next = std::move(image);
		_night = night;
		_readyGeneration = generation;
		if (!_fade.animating()) {
			startFade();
		}
		_owner->update();
		_changes.fire({});
	} else if (_image.size() != required) {
		_timer.callOnce(kCacheBackgroundFastTimeout);
	} else {
		checkStale();
	}
}

void LayerBackdrop::startFade() {
	if (_next.isNull()) {
		return;
	}
	_back = std::move(_image);
	_image = base::take(_next);
	_fade.start([=] {
		_owner->update();
		_changes.fire({});
		if (!_fade.animating()) {
			_back = QImage();
			startFade();
		}
	}, 0., 1., kFadeBackgroundDuration);
}

} // namespace Ui
