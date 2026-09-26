#include "ayu/features/window_material/platform/window_material_platform.h"
#include "ayu/ayu_settings.h"

#include <QtCore/QOperatingSystemVersion>
#include <QtGui/QWindow>
#include <QtWidgets/QWidget>
#include <Windows.h>
#include <dwmapi.h>

namespace AyuFeatures::WindowMaterial::Platform {
namespace {

constexpr auto kBackdropAttribute = DWORD(38);
constexpr auto kDarkAttribute = DWORD(20);
constexpr auto kCaptionAttribute = DWORD(35);

bool supported() {
	const auto version = QOperatingSystemVersion::current();
	return version.majorVersion() >= 10 && version.microVersion() >= 22621;
}

bool transparencyAllowed() {
	HIGHCONTRASTW contrast{ sizeof(HIGHCONTRASTW) };
	if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)
		|| (contrast.dwFlags & HCF_HIGHCONTRASTON)) {
		return false;
	}
	BOOL composition = FALSE;
	if (FAILED(DwmIsCompositionEnabled(&composition)) || !composition) {
		return false;
	}
	DWORD enabled = 1;
	DWORD size = sizeof(enabled);
	const auto result = RegGetValueW(
		HKEY_CURRENT_USER,
		L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
		L"EnableTransparency", RRF_RT_REG_DWORD, nullptr, &enabled, &size);
	return (result == ERROR_FILE_NOT_FOUND)
		|| (result == ERROR_SUCCESS && enabled != 0);
}

class WindowsBackend final : public Backend {
public:
	explicit WindowsBackend(not_null<QWidget*> widget) : _widget(widget) {
	}

	~WindowsBackend() override { clear(); }

	bool apply(::WindowMaterial mode, bool dark) override {
		const auto handle = reinterpret_cast<HWND>(_widget->winId());
		if (_handle != handle) {
			clear();
			_handle = handle;
		}
		const auto requested = (mode == ::WindowMaterial::Mica)
			|| (mode == ::WindowMaterial::Acrylic);
		const auto qwindow = _widget->windowHandle();
		if (!requested || !supported() || !transparencyAllowed()
			|| !qwindow || qwindow->format().alphaBufferSize() <= 0
			|| (GetWindowLongPtrW(handle, GWL_EXSTYLE) & WS_EX_LAYERED)) {
			clear();
			return false;
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
		const auto caption = DWORD(0xFFFFFFFE);
		if (!_configured) {
			DwmGetWindowAttribute(handle, kDarkAttribute, &_previousDark, sizeof(_previousDark));
		}
		_configured = true;
		if (FAILED(DwmSetWindowAttribute(handle, kDarkAttribute, &darkValue, sizeof(darkValue)))
			|| FAILED(DwmExtendFrameIntoClientArea(handle, &margins))
			|| FAILED(DwmSetWindowAttribute(handle, kBackdropAttribute, &backdrop, sizeof(backdrop)))) {
			clear();
			return false;
		}
		DwmSetWindowAttribute(handle, kCaptionAttribute, &caption, sizeof(caption));
		_active = true;
		_mode = mode;
		_dark = dark;
		return true;
	}

private:
	void clear() {
		if (_configured && _handle && IsWindow(_handle)) {
			const auto none = DWORD(1);
			const auto margins = MARGINS{};
			DwmSetWindowAttribute(_handle, kBackdropAttribute, &none, sizeof(none));
			DwmExtendFrameIntoClientArea(_handle, &margins);
			DwmSetWindowAttribute(_handle, kDarkAttribute, &_previousDark, sizeof(_previousDark));
		}
		_active = false;
		_configured = false;
	}

	const not_null<QWidget*> _widget;
	HWND _handle = nullptr;
	BOOL _previousDark = FALSE;
	::WindowMaterial _mode = ::WindowMaterial::Off;
	bool _dark = false;
	bool _active = false;
	bool _configured = false;
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
