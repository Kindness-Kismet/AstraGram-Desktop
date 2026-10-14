#include "extras/features/media_viewer/viewer_host.h"

#include "base/event_filter.h"
#include "base/platform/base_platform_info.h"
#include "extras/extras_settings.h"
#include "ui/image/image_prepare.h"
#include "ui/ui_utility.h"
#include "styles/style_basic.h"
#include "styles/style_extras_styles.h"

#include <QtGui/QWindow>

namespace ExtrasFeatures::MediaViewer {
namespace {

// 先缩小再模糊，等效于大半径模糊，放大回原尺寸时顺带柔化。
constexpr auto kBackdropDownscale = 8;
constexpr auto kBackdropBlurRadius = 3;

[[nodiscard]] QColor backdropOverlay() {
	return QColor(10, 12, 16, 189);
}

} // namespace

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

QScreen *ViewerHost::screen() const {
	return _host ? _host->screen() : nullptr;
}

int ViewerHost::cornerRadius() const {
	const auto squared = Qt::WindowMaximized | Qt::WindowFullScreen;
	if (!_host
		|| !Platform::IsWindows11OrGreater()
		|| (_host->windowState() & squared)) {
		return 0;
	}
	return st::extrasViewerHostRadius;
}

QImage ViewerHost::grabBackdrop() const {
	if (!_host) {
		return QImage();
	}
	// 宿主可能用了透明材质，先垫窗口底色，背景才是不透明的。
	auto image = Ui::GrabWidgetToImage(_host, QRect(), st::windowBg->c);
	const auto ratio = image.devicePixelRatio();
	const auto full = image.size();
	auto small = image.scaled(
		full / kBackdropDownscale,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	small = Images::BlurLargeImage(std::move(small), kBackdropBlurRadius);
	auto result = small.scaled(
		full,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	QPainter(&result).fillRect(result.rect(), backdropOverlay());
	result.setDevicePixelRatio(ratio);
	return result;
}

bool ViewerHost::hosts(not_null<QWidget*> window) const {
	return _host == window.get();
}

QWidget *ViewerHost::hostWidget() const {
	return _host.data();
}

void ViewerHost::minimizeHost() const {
	if (_host) {
		_host->setWindowState(_host->windowState() | Qt::WindowMinimized);
	}
}

} // namespace ExtrasFeatures::MediaViewer
