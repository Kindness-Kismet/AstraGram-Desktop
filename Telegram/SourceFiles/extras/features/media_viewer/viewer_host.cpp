#include "extras/features/media_viewer/viewer_host.h"

#include "base/event_filter.h"
#include "base/platform/base_platform_info.h"
#include "extras/extras_settings.h"
#include "extras/features/window_material/platform/window_material_platform.h"
#include "ui/image/image_prepare.h"
#include "ui/ui_utility.h"
#include "styles/style_basic.h"

#include <QtGui/QScreen>
#include <QtGui/QWindow>

#ifdef Q_OS_WIN
#include <Windows.h>
#include <dwmapi.h>
#endif

namespace ExtrasFeatures::MediaViewer {
namespace {

// 先缩小再模糊，等效于大半径模糊，放大回原尺寸时顺带柔化。
constexpr auto kBackdropDownscale = 8;
constexpr auto kBackdropBlurRadius = 3;

[[nodiscard]] QImage prepareBackdrop(const QImage &image, QSize size) {
	auto downscaled = image.scaled(
		size / kBackdropDownscale,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	downscaled = Images::BlurLargeImage(std::move(downscaled), kBackdropBlurRadius);
	auto result = downscaled.scaled(
		size,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	QPainter(&result).fillRect(result.rect(), backdropTint());
	result.setDevicePixelRatio(image.devicePixelRatio());
	return result;
}

} // namespace

QColor backdropTint() {
	return QColor(10, 12, 16, 189);
}

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
	updateCorners();

	_filter.reset(base::install_event_filter(host, [=](not_null<QEvent*> e) {
		switch (e->type()) {
		case QEvent::Move:
		case QEvent::Resize:
		case QEvent::WindowStateChange:
			updateCorners();
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
	setFullScreenBackdrop(false);
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
	return _cornerRadius;
}

QMarginsF ViewerHost::cornerMargins() const {
	return _cornerMargins;
}

void ViewerHost::updateCorners() {
	_cornerRadius = 0;
	_cornerMargins = QMarginsF();
#ifdef Q_OS_WIN
	const auto squared = Qt::WindowMaximized | Qt::WindowFullScreen;
	if (!_host
		|| !Platform::IsWindows11OrGreater()
		|| (_host->windowState() & squared)) {
		return;
	}
	const auto handle = reinterpret_cast<HWND>(_host->winId());
	auto frame = RECT();
	auto client = RECT();
	auto origin = POINT();
	if (FAILED(DwmGetWindowAttribute(handle, DWMWA_EXTENDED_FRAME_BOUNDS,
			&frame, sizeof(frame)))
		|| !GetClientRect(handle, &client)
		|| !ClientToScreen(handle, &origin)) {
		return;
	}
	// 系统圆角围绕可见外框，客户区已缩进边框；不能按应用界面缩放重新裁一遍。
	const auto ratio = _host->devicePixelRatioF();
	static const auto dpiForWindow = reinterpret_cast<UINT (WINAPI *)(HWND)>(
		GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
	Expects(dpiForWindow != nullptr);
	_cornerMargins = QMarginsF(
		(origin.x - frame.left) / ratio,
		(origin.y - frame.top) / ratio,
		(frame.right - origin.x - client.right) / ratio,
		(frame.bottom - origin.y - client.bottom) / ratio);
	_cornerRadius = qRound(8. * dpiForWindow(handle) / (96. * ratio));
#endif
}

QImage ViewerHost::grabBackdrop() const {
	if (!_host) {
		return {};
	}
	// 宿主开启材质时绘制会把底色清成透明，只能渲染完再垫到下层。
	auto image = Ui::GrabWidgetToImage(_host, QRect(), Qt::transparent);
	auto p = QPainter(&image);
	p.setCompositionMode(QPainter::CompositionMode_DestinationOver);
	p.fillRect(image.rect(), st::windowBg);
	p.end();
	return prepareBackdrop(image, image.size());
}

void ViewerHost::setFullScreenBackdrop(bool enabled) {
	if (!enabled) {
		if (_backdropBackend) {
			_backdropBackend->release();
		}
		_nativeBackdrop = false;
		_viewer->setProperty("ExtrasWindowMaterialActive", false);
		return;
	}
	if (!_backdropBackend) {
		_backdropBackend = WindowMaterial::Platform::create(_viewer);
	}
	_nativeBackdrop = _backdropBackend->apply(::WindowMaterial::Acrylic, true);
	if (!_nativeBackdrop) {
		_backdropBackend->release();
	}
	_viewer->setProperty("ExtrasWindowMaterialActive", _nativeBackdrop);
}

bool ViewerHost::nativeBackdrop() const {
	return _nativeBackdrop;
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
