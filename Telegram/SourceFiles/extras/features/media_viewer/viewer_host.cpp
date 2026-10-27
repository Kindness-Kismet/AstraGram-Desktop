#include "extras/features/media_viewer/viewer_host.h"

#include "ui/effects/layer_backdrop.h"

#include "base/event_filter.h"
#include "base/debug_log.h"
#include "base/platform/base_platform_info.h"
#include "extras/extras_settings.h"
#include "extras/features/window_material/platform/window_material_platform.h"
#include "extras/features/streamer_mode/streamer_mode.h"
#include "ui/image/image_prepare.h"
#include "ui/layers/layer_widget.h"
#include "ui/rp_widget.h"
#include "ui/widgets/rp_window.h"
#include "ui/platform/ui_platform_window_title.h"
#include "ui/ui_utility.h"
#include "styles/style_basic.h"

#include <QtCore/QAbstractNativeEventFilter>
#include <QtCore/QCoreApplication>
#include <QtGui/QScreen>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>

#ifdef Q_OS_WIN
#include <Windows.h>
#include <dwmapi.h>
#endif

namespace ExtrasFeatures::MediaViewer {
namespace {

void startWindowMove(QWidget *window) {
	if (!window) {
		return;
	}
	if (const auto handle = window->windowHandle()) {
		handle->startSystemMove();
	}
}

// 先缩小再模糊，等效于大半径模糊，放大回原尺寸时顺带柔化。
constexpr auto kBackdropDownscale = 8;
constexpr auto kBackdropBlurRadius = 3;

#ifdef Q_OS_WIN
// 保留查看器的全屏合成余量，激活期间宿主保持激活外观。
class ViewerWindowFilter final : public QAbstractNativeEventFilter {
public:
	ViewerWindowFilter(not_null<QWidget*> viewer, not_null<QWidget*> host)
	: _viewer(viewer)
	, _host(host.get()) {
	}

	bool nativeEventFilter(
			const QByteArray &eventType,
			void *message,
			native_event_filter_result *result) override {
		const auto msg = static_cast<MSG*>(message);
		if ((msg->message != WM_NCACTIVATE
				&& msg->message != WM_ACTIVATE
				&& msg->message != WM_WINDOWPOSCHANGING)
			|| eventType != "windows_generic_MSG"
			|| !_host
			|| msg->hwnd != reinterpret_cast<HWND>(_viewer->winId())) {
			return false;
		}
		if (msg->message == WM_WINDOWPOSCHANGING && _viewer->isFullScreen()) {
			preserveFullScreenBorder(reinterpret_cast<WINDOWPOS*>(msg->lParam));
			return false;
		}
		const auto host = reinterpret_cast<HWND>(_host->winId());
		if (msg->message == WM_NCACTIVATE) {
			if (msg->wParam) {
				SendMessage(host, WM_NCACTIVATE, TRUE, 0);
			}
		} else if (msg->message == WM_ACTIVATE
			&& LOWORD(msg->wParam) == WA_INACTIVE
			&& reinterpret_cast<HWND>(msg->lParam) != host) {
			// 激活转给宿主时由系统重新激活宿主，转给其他窗口时宿主随查看器失活。
			SendMessage(host, WM_NCACTIVATE, FALSE, 0);
		}
		return false;
	}

private:
	void preserveFullScreenBorder(not_null<WINDOWPOS*> position) const {
		if (position->flags & SWP_NOSIZE) {
			return;
		}
		auto rect = RECT{
			position->x,
			position->y,
			position->x + position->cx,
			position->y + position->cy,
		};
		if (position->flags & SWP_NOMOVE) {
			if (!GetWindowRect(position->hwnd, &rect)) {
				return;
			}
			rect.right = rect.left + position->cx;
			rect.bottom = rect.top + position->cy;
		}
		auto info = MONITORINFO{ .cbSize = sizeof(MONITORINFO) };
		const auto monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONULL);
		if (!GetMonitorInfo(monitor, &info) || !EqualRect(&rect, &info.rcMonitor)) {
			return;
		}
		// 系统重排窗口也须保留 Qt 的 1 像素余量，避免 OpenGL 窗口占用整屏输出。
		++position->cx;
#ifdef _DEBUG
		LOG(("MediaViewer: Preserved fullscreen border at %1,%2 %3x%4.")
			.arg(rect.left).arg(rect.top).arg(position->cx).arg(position->cy));
#endif
	}

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

QImage grabUnderLayer(not_null<Ui::LayerWidget*> layer) {
	auto image = Ui::CaptureUnderLayer(layer);
	if (image.isNull()) {
		return image;
	}
	auto p = QPainter(&image);
	p.setCompositionMode(QPainter::CompositionMode_DestinationOver);
	p.fillRect(QRect(QPoint(), image.size() / image.devicePixelRatio()), st::windowBg);
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
	_windowFilter = std::make_unique<ViewerWindowFilter>(viewer, host);
	QCoreApplication::instance()->installNativeEventFilter(
		_windowFilter.get());
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

void ViewerHost::startMove() const {
	startWindowMove(_host);
}

EmbeddedViewerHost::EmbeddedViewerHost(
		not_null<QWidget*> viewer,
		not_null<Ui::RpWidget*> host,
		Fn<void()> geometryChanged,
		Fn<void()> hostHidden,
		Fn<void()> hostDestroyed)
: _viewer(viewer)
, _host(host.get())
, _window(dynamic_cast<Ui::RpWindow*>(host->window()))
, _originalParent(viewer->parentWidget())
, _previousFocus(QApplication::focusWidget())
, _previousScreenshotProtected(
	host->window()->property("ExtrasContentScreenshotProtected").toBool()) {
	Expects(_window != nullptr);
	viewer->setObjectName(u"iv.mediaViewer"_q);
	// 媒体覆盖整个文章窗口，标题栏也进入遮罩范围。
	viewer->setParent(_window);
	viewer->setGeometry(_window->rect());
	_window->hitTestRequests() | rpl::on_next([=](
			not_null<Ui::Platform::HitTestRequest*> request) {
		if (_viewer->isVisible()) {
			request->result = Ui::Platform::HitTestResult::Client;
		}
	}, _lifetime);
	_filter.reset(base::install_event_filter(
		viewer,
		_window,
		[=](not_null<QEvent*> event) {
			if (event->type() == QEvent::Resize) {
				geometryChanged();
			} else if (event->type() == QEvent::Hide) {
				hostHidden();
			}
			return base::EventFilterResult::Continue;
		}).get());
	host->lifetime().add([weak = base::make_weak(this), hostDestroyed] {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		// 界面控件的生命周期先于 QWidget 析构结束，此时子控件仍完整。
		strong->_hostClosing = true;
		strong->_viewer->hide();
		strong->_viewer->setParent(strong->_originalParent);
		hostDestroyed();
	});
}

EmbeddedViewerHost::~EmbeddedViewerHost() {
	_viewer->hide();
	_viewer->setParent(_originalParent);
	_viewer->setObjectName(QString());
	restoreScreenshotProtection();
}

void EmbeddedViewerHost::restoreScreenshotProtection() const {
	if (_host && !_hostClosing) {
		_host->window()->setProperty(
			"ExtrasContentScreenshotProtected",
			_previousScreenshotProtected);
		StreamerMode::refreshWidgetWindow(_host);
	}
}

QRect EmbeddedViewerHost::geometry() const {
	return _window ? _window->rect() : QRect();
}

QImage EmbeddedViewerHost::grabBackdrop() const {
	Expects(_window != nullptr);
	const auto shown = !_viewer->isHidden();
	const auto focused = shown && Ui::InFocusChain(_viewer);
	if (shown) _viewer->hide();
	auto result = prepareBackdrop(grabOpaque(_window));
	if (shown) {
		_viewer->show();
		_viewer->raise();
		if (focused) _viewer->setFocus();
	}
	return result;
}

bool EmbeddedViewerHost::hosts(not_null<QWidget*> window) const {
	return _host && _host->window() == window.get();
}

void EmbeddedViewerHost::activate() const {
	if (_host) {
		_viewer->raise();
		_host->window()->activateWindow();
	}
}

void EmbeddedViewerHost::minimizeHost() const {
	if (_host) {
		const auto window = _host->window();
		window->setWindowState(window->windowState() | Qt::WindowMinimized);
	}
}

void EmbeddedViewerHost::startMove() const {
	startWindowMove(_window);
}

void EmbeddedViewerHost::restoreFocus() const {
	restoreScreenshotProtection();
	if (!_hostClosing && _previousFocus && _previousFocus->isVisible()) {
		_previousFocus->setFocus();
	}
}

void EmbeddedViewerHost::setContentScreenshotProtected(
		bool protectedContent) const {
	Expects(_host != nullptr);
	_host->window()->setProperty(
		"ExtrasContentScreenshotProtected",
		protectedContent || _previousScreenshotProtected);
	StreamerMode::refreshWidgetWindow(_host);
}

} // namespace ExtrasFeatures::MediaViewer
