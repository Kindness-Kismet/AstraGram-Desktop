#include "ayu/features/window_material/platform/window_material_platform.h"
#include "ayu/ayu_settings.h"
#include "base/debug_log.h"

#include <QtCore/QOperatingSystemVersion>
#include <QtCore/QVariant>
#include <QtGui/QWindow>
#include <QtWidgets/QWidget>
#include <Windows.h>
#include <dwmapi.h>

namespace AyuFeatures::WindowMaterial::Platform {
namespace {

constexpr auto kBackdropAttribute = DWORD(38);
constexpr auto kDarkAttribute = DWORD(20);
constexpr auto kCapability = "_q_ayuNativeBackdropSupported";
constexpr auto kMarker = L"AyuGramNativeBackdrop";
constexpr auto kOwner = L"AyuGramNativeBackdropOwner";

bool supported() {
	const auto version = QOperatingSystemVersion::current();
	return version.majorVersion() >= 10 && version.microVersion() >= 22621;
}

struct Failure {
	const char *reason = nullptr;
	qint64 code = 0;
};

Failure transparencyFailure() {
	HIGHCONTRASTW contrast{ sizeof(HIGHCONTRASTW) };
	if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)) {
		return { "high-contrast-query", GetLastError() };
	} else if (contrast.dwFlags & HCF_HIGHCONTRASTON) {
		return { "high-contrast-enabled" };
	}
	BOOL composition = FALSE;
	const auto hr = DwmIsCompositionEnabled(&composition);
	if (FAILED(hr)) {
		return { "composition-query", hr };
	} else if (!composition) {
		return { "composition-disabled" };
	}
	DWORD enabled = 1;
	DWORD size = sizeof(enabled);
	const auto result = RegGetValueW(
		HKEY_CURRENT_USER,
		L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
		L"EnableTransparency", RRF_RT_REG_DWORD, nullptr, &enabled, &size);
	if (result == ERROR_FILE_NOT_FOUND) {
		return {};
	} else if (result != ERROR_SUCCESS) {
		return { "transparency-query", result };
	}
	return enabled ? Failure() : Failure{ "transparency-disabled" };
}

class WindowsBackend final : public Backend {
public:
	explicit WindowsBackend(not_null<QWidget*> widget) : _widget(widget) {
	}

	~WindowsBackend() override { releaseHandle(); }

	bool apply(::WindowMaterial mode, bool dark) override {
		const auto handle = reinterpret_cast<HWND>(_widget->winId());
		if (_handle != handle) {
			releaseHandle();
			_handle = handle;
			_lastStatus.clear();
		}
		const auto requested = (mode == ::WindowMaterial::Mica)
			|| (mode == ::WindowMaterial::Acrylic);
		if (!supported()) {
			return fail({ "unsupported-windows-build" }, requested);
		}
		if (const auto failure = prepareHandle(); failure.reason) {
			return fail(failure, requested);
		}
		if (!requested) {
			clear();
			report("off", 0);
			return false;
		}
		if (const auto failure = transparencyFailure(); failure.reason) {
			return fail(failure);
		}
		const auto backdrop = DWORD(mode == ::WindowMaterial::Mica ? 2 : 3);
		DWORD actual = 0;
		if (_active && _mode == mode && _dark == dark
			&& SUCCEEDED(DwmGetWindowAttribute(handle, kBackdropAttribute, &actual, sizeof(actual)))
			&& actual == backdrop) {
			return true;
		}
		const auto darkValue = BOOL(dark);
		const auto margins = MARGINS{ -1, -1, -1, -1 };
		if (!_configured) {
			_previousDark = BOOL(dark);
			auto previousDark = BOOL();
			if (SUCCEEDED(DwmGetWindowAttribute(handle, kDarkAttribute, &previousDark, sizeof(previousDark)))) {
				_previousDark = previousDark;
			}
		}
		_configured = true;
		auto hr = DwmSetWindowAttribute(handle, kDarkAttribute, &darkValue, sizeof(darkValue));
		if (FAILED(hr)) {
			return fail({ "set-dark-mode", hr });
		}
		hr = DwmExtendFrameIntoClientArea(handle, &margins);
		if (FAILED(hr)) {
			return fail({ "extend-frame", hr });
		}
		hr = DwmSetWindowAttribute(handle, kBackdropAttribute, &backdrop, sizeof(backdrop));
		if (FAILED(hr)) {
			return fail({ "set-backdrop", hr });
		}
		_active = true;
		_mode = mode;
		_dark = dark;
		report(mode == ::WindowMaterial::Mica ? "active-mica" : "active-acrylic", 0);
		return true;
	}

private:
	bool ownsMarker() {
		return _handle && GetPropW(_handle, kOwner) == reinterpret_cast<HANDLE>(this)
			&& GetPropW(_handle, kMarker) == reinterpret_cast<HANDLE>(1);
	}

	Failure prepareHandle() {
		const auto window = _widget->windowHandle();
		if (!window || !window->property(kCapability).toBool()) {
			return { "qt-native-backdrop-patch-missing" };
		}
		if (!ownsMarker()) {
			if (GetPropW(_handle, kMarker) || GetPropW(_handle, kOwner)) {
				return { "native-backdrop-marker-conflict" };
			}
			if (!SetPropW(_handle, kOwner, reinterpret_cast<HANDLE>(this))) {
				return { "set-owner-property", GetLastError() };
			}
			if (!SetPropW(_handle, kMarker, reinterpret_cast<HANDLE>(1))) {
				const auto error = GetLastError();
				RemovePropW(_handle, kOwner);
				return { "set-backdrop-property", error };
			}
		}
		if (window->format().alphaBufferSize() <= 0) {
			return { "alpha-buffer-missing" };
		} else if (window->opacity() != 1.
			|| window->flags().testFlag(Qt::WindowTransparentForInput)) {
			return { "layered-window-required" };
		}
		SetLastError(ERROR_SUCCESS);
		const auto style = GetWindowLongPtrW(_handle, GWL_EXSTYLE);
		if (!style && GetLastError() != ERROR_SUCCESS) {
			return { "read-window-style", GetLastError() };
		}
		if (style & WS_EX_LAYERED) {
			// Qt 创建窗口时尚未看到标记；后续绘制由专用补丁保持非分层窗口。
			SetLastError(ERROR_SUCCESS);
			const auto previous = SetWindowLongPtrW(_handle, GWL_EXSTYLE, style & ~WS_EX_LAYERED);
			if (!previous && GetLastError() != ERROR_SUCCESS) {
				return { "clear-layered-style", GetLastError() };
			}
			_frameChangePending = true;
		}
		if (_frameChangePending) {
			if (!SetWindowPos(_handle, nullptr, 0, 0, 0, 0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED)) {
				return { "refresh-window-frame", GetLastError() };
			}
			SetLastError(ERROR_SUCCESS);
			const auto actual = GetWindowLongPtrW(_handle, GWL_EXSTYLE);
			if (!actual && GetLastError() != ERROR_SUCCESS) {
				return { "verify-window-style", GetLastError() };
			} else if (actual & WS_EX_LAYERED) {
				return { "layered-style-remained" };
			}
			_frameChangePending = false;
		}
		return {};
	}

	void report(const char *reason, qint64 code) {
		const auto window = _widget->windowHandle();
		const auto status = QString("WindowMaterial: %1; build=%2; qt=%3; alpha=%4; exstyle=0x%5; code=%6")
			.arg(QString::fromLatin1(reason))
			.arg(QOperatingSystemVersion::current().microVersion())
			.arg(QString::fromLatin1(qVersion()))
			.arg(window ? window->format().alphaBufferSize() : -1)
			.arg(qulonglong(_handle ? GetWindowLongPtrW(_handle, GWL_EXSTYLE) : 0), 0, 16)
			.arg(code);
		if (_lastStatus != status) {
			_lastStatus = status;
			LOG((status));
		}
	}

	bool fail(Failure failure, bool requested = true) {
		clear();
		report(requested ? failure.reason : "off", requested ? failure.code : 0);
		return false;
	}

	void clear() {
		if (_configured && ownsMarker()) {
			const auto none = DWORD(1);
			const auto margins = MARGINS{};
			DwmSetWindowAttribute(_handle, kBackdropAttribute, &none, sizeof(none));
			DwmExtendFrameIntoClientArea(_handle, &margins);
			DwmSetWindowAttribute(_handle, kDarkAttribute, &_previousDark, sizeof(_previousDark));
		}
		_active = false;
		_configured = false;
	}

	void releaseHandle() {
		clear();
		if (_handle && GetPropW(_handle, kOwner) == reinterpret_cast<HANDLE>(this)) {
			if (GetPropW(_handle, kMarker) == reinterpret_cast<HANDLE>(1)) {
				RemovePropW(_handle, kMarker);
			}
			RemovePropW(_handle, kOwner);
		}
		_handle = nullptr;
		_frameChangePending = false;
	}

	const not_null<QWidget*> _widget;
	HWND _handle = nullptr;
	QString _lastStatus;
	BOOL _previousDark = FALSE;
	::WindowMaterial _mode = ::WindowMaterial::Off;
	bool _dark = false;
	bool _active = false;
	bool _configured = false;
	bool _frameChangePending = false;
};

} // namespace

std::unique_ptr<Backend> create(not_null<QWidget*> window) {
	return std::make_unique<WindowsBackend>(window);
}

std::vector<::WindowMaterial> availableModes() {
	return supported()
		? std::vector{ ::WindowMaterial::Off, ::WindowMaterial::Mica, ::WindowMaterial::Acrylic }
		: std::vector{ ::WindowMaterial::Off };
}

} // namespace AyuFeatures::WindowMaterial::Platform
