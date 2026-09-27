#include "ayu/ui/components/floating_surface.h"

#include "ayu/ui/components/floating_surface_host.h"

#include <QtCore/QEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QRegion>
#include <QtWidgets/QWidget>
#include <algorithm>
#include <utility>

namespace AyuUi {

void FloatingSurface::attach(QWidget *widget, FloatingSurfaceStyle style) {
	Expects(widget != nullptr);
	Expects(bool(style.background));
	if (const auto previous = dynamic_cast<FloatingSurface*>(
			widget->graphicsEffect())) {
		if (previous->_style.maskInput && !style.maskInput) {
			widget->clearMask();
		}
	}
	auto surface = new FloatingSurface(widget, std::move(style));
	widget->setAttribute(Qt::WA_OpaquePaintEvent, false);
	widget->setGraphicsEffect(surface);
	surface->rebind();
	surface->refreshGeometry();
}

FloatingSurface *FloatingSurface::find(QWidget *widget) {
	for (auto parent = widget; parent; parent = parent->parentWidget()) {
		if (const auto surface = dynamic_cast<FloatingSurface*>(
				parent->graphicsEffect())) {
			return surface;
		}
		if (parent->isWindow()) {
			break;
		}
	}
	return nullptr;
}

FloatingSurface::FloatingSurface(QWidget *widget, FloatingSurfaceStyle style)
: _widget(widget)
, _style(std::move(style)) {
}

FloatingSurface::~FloatingSurface() {
	if (_host) {
		_host->remove(this);
	}
}

QWidget *FloatingSurface::widget() const {
	return _widget.data();
}

bool FloatingSurface::hasBackdrop() const {
	return !_host.isNull();
}

void FloatingSurface::refreshGeometry() {
	if (!_widget) {
		return;
	}
	auto rects = _style.rects
		? _style.rects()
		: std::vector<QRect>{ _widget->rect() };
	std::erase_if(rects, [](QRect rect) { return rect.isEmpty(); });
	if (_rects != rects) {
		_rects = std::move(rects);
		_mask = QPixmap();
		updateInputMask();
		if (_host) {
			_host->scheduleAreas();
		}
		_widget->update();
		update();
	}
}

void FloatingSurface::setHost(FloatingSurfaceHost *host) {
	if (_host == host) {
		return;
	}
	if (_host) {
		_host->remove(this);
	}
	_host = host;
	if (_host) {
		_host->add(this);
	}
	// 接入或离开背景源时，子控件需要重新选择透明前景或纯色回退。
	if (_widget) {
		_widget->update();
	}
	update();
}

void FloatingSurface::rebind() {
	auto ancestors = std::vector<QPointer<QWidget>>();
	for (auto ancestor = widget(); ancestor; ancestor = ancestor->parentWidget()) {
		ancestors.push_back(ancestor);
		if (std::find(_ancestors.begin(), _ancestors.end(), ancestor)
				== _ancestors.end()) {
			ancestor->installEventFilter(this);
		}
		if (ancestor->isWindow()) {
			break;
		}
	}
	for (const auto &ancestor : _ancestors) {
		if (ancestor && std::find(ancestors.begin(), ancestors.end(), ancestor)
				== ancestors.end()) {
			ancestor->removeEventFilter(this);
		}
	}
	_ancestors = std::move(ancestors);
	setHost(FloatingSurfaceHost::find(widget()));
}

QRect FloatingSurface::visibleArea(QWidget *root) const {
	if (!_widget || !_widget->isVisibleTo(root)) {
		return {};
	}
	auto area = QRect(_widget->mapTo(root, QPoint()), _widget->size());
	// SlideWrap 动画会裁剪子控件，只采样实际露出的区域。
	for (auto parent = widget(); parent;
			parent = parent->parentWidget()) {
		area = area.intersected(QRect(parent->mapTo(root, QPoint()), parent->size()));
		if (parent == root) {
			break;
		}
	}
	return area;
}

bool FloatingSurface::eventFilter(QObject *watched, QEvent *event) {
	switch (event->type()) {
	case QEvent::ParentChange:
		rebind();
		break;
	case QEvent::Resize:
		if (watched == _widget.data()) {
			refreshGeometry();
		}
		break;
	case QEvent::Move:
	case QEvent::Show:
	case QEvent::Hide:
		break;
	default:
		return QGraphicsEffect::eventFilter(watched, event);
	}
	if (_host) {
		_host->scheduleAreas();
	}
	update();
	return QGraphicsEffect::eventFilter(watched, event);
}

void FloatingSurface::updateInputMask() {
	if (!_style.maskInput || !_widget) {
		return;
	}
	auto path = QPainterPath();
	path.setFillRule(Qt::WindingFill);
	for (const auto &rect : _rects) {
		const auto radius = std::min(qreal(_style.radius),
			std::min(rect.width(), rect.height()) / 2.);
		path.addRoundedRect(QRectF(rect), radius, radius);
	}
	const auto mask = QRegion(path.toFillPolygon().toPolygon());
	if (_widget->mask() != mask) {
		_widget->setMask(mask);
	}
}

void FloatingSurface::updateMask(QSize size, qreal ratio) {
	if (_mask.size() == size && _mask.devicePixelRatio() == ratio) {
		return;
	}
	_mask = QPixmap(size);
	_mask.setDevicePixelRatio(ratio);
	_mask.fill(Qt::transparent);
	auto painter = QPainter(&_mask);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(Qt::NoPen);
	painter.setBrush(Qt::white);
	for (const auto &rect : _rects) {
		const auto radius = std::min(qreal(_style.radius),
			std::min(rect.width(), rect.height()) / 2.);
		painter.drawRoundedRect(QRectF(rect), radius, radius);
	}
}

void FloatingSurface::draw(QPainter *p) {
	if (!_widget || _widget->size().isEmpty()) {
		return;
	}
	const auto opacity = _style.opacity
		? std::clamp(_style.opacity(), qreal(0.), qreal(1.))
		: qreal(1.);
	if (!opacity) {
		return;
	}
	auto offset = QPoint();
	const auto source = sourcePixmap(Qt::LogicalCoordinates, &offset, NoPad);
	const auto ratio = source.isNull()
		? _widget->devicePixelRatioF()
		: source.devicePixelRatio();
	auto result = QPixmap(_widget->size() * ratio);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	updateMask(result.size(), ratio);
	{
		auto painter = QPainter(&result);
		const auto tint = _style.background();
		painter.fillRect(_widget->rect(), tint);
		if (_host) {
			_host->paint(painter, this, tint);
		}
		painter.drawPixmap(offset, source);
		painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		painter.drawPixmap(0, 0, _mask);
		if (_style.border) {
			painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
			painter.setRenderHint(QPainter::Antialiasing);
			painter.setPen(QPen(_style.border(), _style.borderWidth));
			painter.setBrush(Qt::NoBrush);
			const auto half = _style.borderWidth / 2.;
			for (const auto &area : _rects) {
				const auto rect = QRectF(area).adjusted(half, half, -half, -half);
				const auto radius = std::max(0., std::min(qreal(_style.radius),
					std::min(area.width(), area.height()) / 2.) - half);
				painter.drawRoundedRect(rect, radius, radius);
			}
		}
	}
	p->save();
	p->setOpacity(p->opacity() * opacity);
	p->drawPixmap(0, 0, result);
	p->restore();
}

} // namespace AyuUi
