#include "extras/features/media_viewer/viewer_host.h"

#include "base/event_filter.h"
#include "base/platform/base_platform_info.h"
#include "extras/extras_settings.h"
#include "extras/features/window_material/platform/window_material_platform.h"
#include "ui/image/image_prepare.h"
#include "ui/ui_utility.h"
#include "styles/style_basic.h"

#include <QtCore/QAbstractNativeEventFilter>
#include <QtCore/QCoreApplication>
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

#ifdef Q_OS_WIN
// 查看器是独立窗口，激活后宿主会改画未激活的边框和阴影；
// 查看器激活期间宿主保持激活外观。
class ActivationFilter final : public QAbstractNativeEventFilter {
public:
	ActivationFilter(not_null<QWidget*> viewer, not_null<QWidget*> host)
	: _viewer(viewer)
	, _host(host.get()) {
	}

	bool nativeEventFilter(
			const QByteArray &eventType,
			void *message,
			native_event_filter_result *result) override {
		const auto msg = static_cast<MSG*>(message);
		if ((msg->message != WM_NCACTIVATE && msg->message != WM_ACTIVATE)
			|| eventType != "windows_generic_MSG"
			|| !_host
			|| msg->hwnd != reinterpret_cast<HWND>(_viewer->winId())) {
			return false;
		}
		const auto host = reinterpret_cast<HWND>(_host->winId());
		if (msg->message == WM_NCACTIVATE) {
			if (msg->wParam) {
				SendMessage(host, WM_NCACTIVATE, TRUE, 0);
			}
		} else if (LOWORD(msg->wParam) == WA_INACTIVE
			&& reinterpret_cast<HWND>(msg->lParam) != host) {
			// 激活转给宿主时由系统重新激活宿主，转给其他窗口时宿主随查看器失活。
			SendMessage(host, WM_NCACTIVATE, FALSE, 0);
		}
		return false;
	}

private:
	const not_null<QWidget*> _viewer;
	const QPointer<QWidget> _host;

};
#endif // Q_OS_WIN

} // namespace

QColor backdropTint() {
	return QColor(10, 12, 16, 189);
}

QImage grabOpaque(not_null<QWidget*> widget, QRect rect) {
	// 开启材质时绘制会把底色清成透明，只能渲染完再垫到下层。
	auto image = Ui::GrabWidgetToImage(widget, rect, Qt::transparent);
	auto p = QPainter(&image);
	p.setCompositionMode(QPainter::CompositionMode_DestinationOver);
	p.fillRect(image.rect(), st::windowBg);
	p.end();
	return image;
}

QImage prepareBackdrop(const QImage &image) {
	const auto size = image.size();
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
#ifdef Q_OS_WIN
	_activationFilter = std::make_unique<ActivationFilter>(viewer, host);
	QCoreApplication::instance()->installNativeEventFilter(
		_activationFilter.get());
#endif // Q_OS_WIN

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

float64 ViewerHost::cornerRadius() const {
	return _cornerRadius;
}

QMarginsF ViewerHost::cornerMargins() const {
	return _cornerMargins;
}

void ViewerHost::updateCorners() {
	_cornerRadius = 0.;
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
	auto border = UINT();
	auto client = RECT();
	auto origin = POINT();
	if (FAILED(DwmGetWindowAttribute(handle, DWMWA_EXTENDED_FRAME_BOUNDS,
			&frame, sizeof(frame)))
		|| FAILED(DwmGetWindowAttribute(
			handle,
			DWMWA_VISIBLE_FRAME_BORDER_THICKNESS,
			&border,
			sizeof(border)))
		|| !GetClientRect(handle, &client)
		|| !ClientToScreen(handle, &origin)) {
		return;
	}
	// 系统圆角围绕可见外框，边框画在外框内侧；沿边框内沿裁切才不会盖住边框。
	const auto ratio = _host->devicePixelRatioF();
	static const auto dpiForWindow = reinterpret_cast<UINT (WINAPI *)(HWND)>(
		GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
	Expects(dpiForWindow != nullptr);
	const auto inner = int(border);
	_cornerMargins = QMarginsF(
		(origin.x - frame.left - inner) / ratio,
		(origin.y - frame.top - inner) / ratio,
		(frame.right - origin.x - client.right - inner) / ratio,
		(frame.bottom - origin.y - client.bottom - inner) / ratio);
	_cornerRadius = (8. * dpiForWindow(handle) / 96. - inner) / ratio;
#endif
}

QImage ViewerHost::grabBackdrop() const {
	if (!_host) {
		return {};
	}
	return prepareBackdrop(grabOpaque(_host));
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
