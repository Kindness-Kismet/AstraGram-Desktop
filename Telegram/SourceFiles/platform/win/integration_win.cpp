/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/integration_win.h"

#include "base/platform/win/base_windows_winrt.h"
#include "base/platform/win/base_windows_wrl.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/sandbox.h"
#include "lang/lang_keys.h"
#include "platform/win/windows_app_user_model_id.h"
#include "platform/win/windows_taskbar_buttons.h"
#include "platform/win/tray_win.h"
#include "platform/win/wallet_protection_win.h"
#include "platform/platform_integration.h"
#include "platform/platform_specific.h"
#include "mainwindow.h"
#include "tray.h"
#include "window/window_controller.h"


#include <QtCore/QAbstractNativeEventFilter>
#include <private/qguiapplication_p.h>

#include <propvarutil.h>
#include <propkey.h>
#include <windows.ui.viewmanagement.h>

namespace Platform {
namespace {

constexpr auto kPowerBroadcastDebounce = crl::time(1000);

[[nodiscard]] std::optional<QColor> ReadSystemAccentColor() {
	if (!base::Platform::SupportsWRL()) {
		return std::nullopt;
	}
	const auto &className = RuntimeClass_Windows_UI_ViewManagement_UISettings;
	auto header = HSTRING_HEADER();
	auto name = HSTRING();
	if (FAILED(WindowsCreateStringReference(
			className,
			UINT32(std::size(className) - 1),
			&header,
			&name))) {
		return std::nullopt;
	}
	auto instance = winrt::com_ptr<IInspectable>();
	if (FAILED(RoActivateInstance(name, instance.put()))) {
		return std::nullopt;
	}
	using namespace ABI::Windows::UI::ViewManagement;
	auto settings = winrt::com_ptr<IUISettings3>();
	if (FAILED(instance->QueryInterface(
			__uuidof(IUISettings3),
			settings.put_void()))) {
		return std::nullopt;
	}
	auto color = ABI::Windows::UI::Color();
	if (FAILED(settings->GetColorValue(UIColorType_Accent, &color))) {
		return std::nullopt;
	}
	return QColor(color.R, color.G, color.B, color.A);
}

} // namespace

void WindowsIntegration::init() {
	RegisterWalletProtectionProvider();
	_powerBroadcastTimer.setCallback([=] {
		if (_powerState == PowerState::SuspendNotified) {
			_powerState = PowerState::SuspendSettled;
		} else if (_powerState == PowerState::AutomaticResumeNotified) {
			_powerState = PowerState::AutomaticResumeSettled;
		} else if (_powerState == PowerState::UserResumeNotified) {
			_powerState = PowerState::Awake;
		}
	});
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
	using namespace QNativeInterface::Private;
	const auto native = qApp->nativeInterface<QWindowsApplication>();
	if (native) {
		native->setHasBorderInFullScreenDefault(true);
	}
#endif // Qt >= 6.5.0
	QCoreApplication::instance()->installNativeEventFilter(this);
	_taskbarCreatedMsgId = RegisterWindowMessage(L"TaskbarButtonCreated");
	_systemAccentColorRefresh.setCallback([=] { refreshSystemAccentColor(); });
	refreshSystemAccentColor();
}

WindowsIntegration::~WindowsIntegration() = default;

ITaskbarList3 *WindowsIntegration::taskbarList() const {
	return _taskbarList.get();
}

std::optional<QColor> WindowsIntegration::systemAccentColor() const {
	return _systemAccentColor.current();
}

rpl::producer<std::optional<QColor>>
WindowsIntegration::systemAccentColorValue() const {
	return _systemAccentColor.value();
}

void WindowsIntegration::refreshSystemAccentColor() {
	// 系统广播会发给多个窗口，只发布真正变化的颜色。
	_systemAccentColor = ReadSystemAccentColor();
}

void WindowsIntegration::scheduleSystemAccentColorRefresh() {
	if (_systemAccentColorRefresh.isActive()) {
		return;
	}
	// 广播返回后再应用主题，避免在原生窗口回调中重建界面。
	_systemAccentColorRefresh.callOnce(0);
}

WindowsIntegration &WindowsIntegration::Instance() {
	return static_cast<WindowsIntegration&>(Integration::Instance());
}

bool WindowsIntegration::nativeEventFilter(
		const QByteArray &eventType,
		void *message,
		native_event_filter_result *result) {
	return Core::Sandbox::Instance().customEnterFromEventLoop([&] {
		const auto msg = static_cast<MSG*>(message);
		return processEvent(
			msg->hwnd,
			msg->message,
			msg->wParam,
			msg->lParam,
			(LRESULT*)result);
	});
}

void WindowsIntegration::createCustomJumpList() {
	_jumpList = base::WinRT::TryCreateInstance<ICustomDestinationList>(
		CLSID_DestinationList);
	if (_jumpList) {
		refreshCustomJumpList();
	}
}

void AddJumpListItem(IObjectCollection* collection, const QString& title, const QString& args, const QString& icon) {
	auto shellLink = base::WinRT::TryCreateInstance<IShellLink>(CLSID_ShellLink);
	if (!shellLink) {
		return;
	}

	const auto exe = QDir::toNativeSeparators(cExeDir() + cExeName());
	const auto dir = QDir::toNativeSeparators(QDir(cWorkingDir()).absolutePath());
	shellLink->SetArguments(args.toStdWString().c_str());
	shellLink->SetPath(exe.toStdWString().c_str());
	shellLink->SetWorkingDirectory(dir.toStdWString().c_str());
	shellLink->SetIconLocation(icon.toStdWString().c_str(), 0);

	if (const auto propertyStore = shellLink.try_as<IPropertyStore>()) {
		auto appIdPropVar = PROPVARIANT();
		HRESULT hr = InitPropVariantFromString(AppUserModelId::Id().c_str(), &appIdPropVar);
		if (SUCCEEDED(hr)) {
			hr = propertyStore->SetValue(AppUserModelId::Key(), appIdPropVar);
			PropVariantClear(&appIdPropVar);
		}
		auto titlePropVar = PROPVARIANT();
		hr = InitPropVariantFromString(title.toStdWString().c_str(), &titlePropVar);
		if (SUCCEEDED(hr)) {
			hr = propertyStore->SetValue(PKEY_Title, titlePropVar);
			PropVariantClear(&titlePropVar);
		}
		propertyStore->Commit();
	}

	collection->AddObject(shellLink.get());
}

void WindowsIntegration::refreshCustomJumpList() {
	auto added = false;
	auto maxSlots = UINT();
	auto removed = (IObjectArray*)nullptr;
	auto hr = _jumpList->BeginList(&maxSlots, IID_PPV_ARGS(&removed));
	if (!SUCCEEDED(hr)) {
		return;
	}
	const auto guard = gsl::finally([&] {
		if (added) {
			_jumpList->CommitList();
		} else {
			_jumpList->AbortList();
		}
	});

	auto collection = base::WinRT::TryCreateInstance<IObjectCollection>(
		CLSID_EnumerableObjectCollection);
	if (!collection) {
		return;
	}

	// add "Enter with Ghost" item
	AddJumpListItem(collection.get(), tr::extras_GhostModeShortcut(tr::now), "-ghost", Tray::GhostJumpListIconPath());

	// add "Quit" item
	AddJumpListItem(collection.get(), tr::lng_quit_from_tray(tr::now), "-quit", Tray::QuitJumpListIconPath());

	_jumpList->AddUserTasks(collection.get());
	added = true;
}

void WindowsIntegration::setupTaskbarButtons(HWND window) {
	const auto controller = Core::App().activePrimaryWindow();
	if (!controller
		|| reinterpret_cast<HWND>(controller->widget()->winId()) != window) {
		return;
	}
	if (!_taskbarButtons || _taskbarButtons->window() != window) {
		_taskbarButtons = std::make_unique<TaskbarButtons>(
			_taskbarList.get(),
			window);
	}
	_taskbarButtons->buttonsCreated();
}

bool WindowsIntegration::processEvent(
		HWND hWnd,
		UINT msg,
		WPARAM wParam,
		LPARAM lParam,
		LRESULT *result) {
	if (msg && msg == _taskbarCreatedMsgId) {
		if (!_taskbarList) {
			_taskbarList = base::WinRT::TryCreateInstance<ITaskbarList3>(
				CLSID_TaskbarList,
				CLSCTX_ALL);
			if (_taskbarList) {
				createCustomJumpList();
			}
		}
		if (_taskbarList) {
			setupTaskbarButtons(hWnd);
		}
	}

	switch (msg) {
	case WM_COMMAND:
		if (HIWORD(wParam) == THBN_CLICKED && _taskbarButtons) {
			_taskbarButtons->buttonClicked(LOWORD(wParam));
		}
		break;

	case WM_ENDSESSION:
		Core::Sandbox::NotifySystemShuttingDown();
		Core::Quit();
		break;

	case WM_TIMECHANGE:
		Core::App().checkAutoLockIn(100);
		break;

	case WM_WTSSESSION_CHANGE:
		if (wParam == WTS_SESSION_LOGOFF
			|| wParam == WTS_SESSION_LOCK) {
			Core::App().setScreenIsLocked(true);
		} else if (wParam == WTS_SESSION_LOGON
			|| wParam == WTS_SESSION_UNLOCK) {
			Core::App().setScreenIsLocked(false);
		}
		break;

	case WM_POWERBROADCAST:
		if (wParam == PBT_APMSUSPEND) {
			if (_powerState != PowerState::SuspendNotified) {
				Core::App().notifySystemSleep();
			}
			_powerState = PowerState::SuspendNotified;
			_powerBroadcastTimer.callOnce(kPowerBroadcastDebounce);
		} else if (wParam == PBT_APMRESUMEAUTOMATIC) {
			if (_powerState == PowerState::Awake
				|| _powerState == PowerState::AutomaticResumeSettled
				|| _powerState == PowerState::UserResumeNotified) {
				Core::App().notifySystemSleep();
			}
			_powerState = PowerState::AutomaticResumeNotified;
			_powerBroadcastTimer.callOnce(kPowerBroadcastDebounce);
		} else if (wParam == PBT_APMRESUMESUSPEND) {
			if (_powerState == PowerState::Awake) {
				Core::App().notifySystemSleep();
			}
			_powerState = PowerState::UserResumeNotified;
			_powerBroadcastTimer.callOnce(kPowerBroadcastDebounce);
		}
		break;

	case WM_SETTINGCHANGE:
		scheduleSystemAccentColorRefresh();
		RefreshTaskbarThemeValue();
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
		Core::App().settings().setSystemDarkMode(Platform::IsDarkMode());
#endif // Qt < 6.5.0
		Core::App().tray().updateIconCounters();
		if (_taskbarButtons) {
			_taskbarButtons->refreshTheme();
		}
		if (_jumpList) {
			refreshCustomJumpList();
		}
		break;

	case WM_DWMCOLORIZATIONCOLORCHANGED:
		scheduleSystemAccentColorRefresh();
		break;
	}
	return false;
}

std::unique_ptr<Integration> CreateIntegration() {
	return std::make_unique<WindowsIntegration>();
}

} // namespace Platform
