#include "extras/ui/components/floating_surface_host.h"

#include "extras/ui/components/floating_surface.h"

#include <QtGui/QPainter>
#include <QtWidgets/QWidget>
#include <algorithm>
#include <utility>

namespace ExtrasUi {

FloatingSurfaceHost::FloatingSurfaceHost(
		QWidget *root,
		ChatFrostedBackground::Capture capture)
: QObject(root)
, _root(root)
, _background(root, std::move(capture), [=] { repaintSurfaces(); }) {
	_areasTimer.setSingleShot(true);
	connect(&_areasTimer, &QTimer::timeout, this, [=] { refreshAreas(); });
	// 页面可能先创建控件，再提供背景源；两种构造顺序都自动接入。
	auto widgets = root->findChildren<QWidget*>();
	widgets.push_front(root);
	for (const auto widget : widgets) {
		if (const auto surface = dynamic_cast<FloatingSurface*>(
				widget->graphicsEffect())) {
			surface->rebind();
		}
	}
}

FloatingSurfaceHost::~FloatingSurfaceHost() {
	_areasTimer.stop();
	for (const auto &surface : _surfaces) {
		if (!surface) {
			continue;
		}
		surface->_host = nullptr;
		if (const auto widget = surface->widget()) {
			widget->update();
		}
		surface->update();
		QTimer::singleShot(0, surface.data(), [surface] {
			surface->rebind();
		});
	}
}

FloatingSurfaceHost *FloatingSurfaceHost::find(QWidget *widget) {
	for (auto parent = widget; parent; parent = parent->parentWidget()) {
		for (const auto child : parent->children()) {
			if (const auto host = dynamic_cast<FloatingSurfaceHost*>(child)) {
				return host;
			}
		}
		if (parent->isWindow()) {
			break;
		}
	}
	return nullptr;
}

void FloatingSurfaceHost::add(FloatingSurface *surface) {
	Expects(std::find(_surfaces.begin(), _surfaces.end(), surface)
		== _surfaces.end());
	_surfaces.push_back(surface);
	scheduleAreas();
}

void FloatingSurfaceHost::remove(FloatingSurface *surface) {
	_surfaces.erase(std::remove(_surfaces.begin(), _surfaces.end(), surface),
		_surfaces.end());
	scheduleAreas();
}

void FloatingSurfaceHost::scheduleAreas() {
	if (!_areasTimer.isActive()) {
		_areasTimer.start(0);
	}
}

void FloatingSurfaceHost::refreshAreas() {
	auto areas = std::vector<QRect>();
	for (const auto &surface : _surfaces) {
		const auto area = surface ? surface->visibleArea(_root) : QRect();
		if (!area.isEmpty()) {
			areas.push_back(area);
		}
	}
	_background.setAreas(std::move(areas));
	_background.invalidate();
}

void FloatingSurfaceHost::repaintSurfaces() {
	for (const auto &surface : _surfaces) {
		if (surface) {
			// 背景变动只更新合成结果，保留 Qt 缓存的文字、图标和涟漪。
			surface->update();
		}
	}
}

void FloatingSurfaceHost::invalidate(QRect area) {
	_background.invalidate(area);
}

void FloatingSurfaceHost::clear() {
	_background.clear();
}

void FloatingSurfaceHost::paint(
		QPainter &p,
		FloatingSurface *surface,
		QColor tint) {
	const auto area = surface->visibleArea(_root);
	if (area.isEmpty()) {
		return;
	}
	const auto offset = surface->widget()->mapTo(_root, QPoint());
	p.save();
	p.translate(-offset);
	_background.paint(p, area, tint);
	p.restore();
}

} // namespace ExtrasUi
