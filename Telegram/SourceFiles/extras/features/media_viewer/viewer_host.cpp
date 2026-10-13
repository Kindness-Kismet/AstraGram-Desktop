#include "extras/features/media_viewer/viewer_host.h"

#include "base/event_filter.h"
#include "base/platform/base_platform_info.h"
#include "extras/extras_settings.h"

#include <QtGui/QWindow>

namespace ExtrasFeatures::MediaViewer {

bool inWindowEnabled() {
	return Platform::IsWindows()
		&& ExtrasSettings::getInstance().mediaViewerInWindow();
}

ViewerHost::ViewerHost(
	not_null<QWidget*> viewer,
	not_null<QWidget*> host,
	Fn<void()> geometryChanged,
	Fn<void()> hostHidden)
: _viewer(viewer)
, _host(host.get()) {
	viewer->createWinId();
	host->createWinId();
	// 只在显示时生效，所以必须在查看器显示前设置。
	viewer->windowHandle()->setTransientParent(host->windowHandle());

	_filter.reset(base::install_event_filter(host, [=](not_null<QEvent*> e) {
		switch (e->type()) {
		case QEvent::Move:
		case QEvent::Resize:
			geometryChanged();
			break;
		case QEvent::Hide:
			// 最小化也会收到 Hide，此时系统会连同查看器一起隐藏。
			if (_host && _host->isHidden()) {
				hostHidden();
			}
			break;
		default:
			break;
		}
		return base::EventFilterResult::Continue;
	}).get());
}

ViewerHost::~ViewerHost() {
	if (const auto handle = _viewer->windowHandle()) {
		handle->setTransientParent(nullptr);
	}
}

QRect ViewerHost::geometry() const {
	return _host ? _host->geometry() : QRect();
}

bool ViewerHost::hosts(not_null<QWidget*> window) const {
	return _host == window.get();
}

void ViewerHost::minimizeHost() const {
	if (_host) {
		_host->setWindowState(_host->windowState() | Qt::WindowMinimized);
	}
}

} // namespace ExtrasFeatures::MediaViewer
