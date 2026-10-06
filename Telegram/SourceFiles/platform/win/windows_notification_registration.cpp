#include "platform/win/windows_notification_registration.h"

#include "platform/win/windows_app_user_model_id.h"
#include "platform/win/windows_toast_activator.h"
#include "extras/ui/extras_logo.h"

#include <QtCore/QSaveFile>
#include <QtCore/QSettings>
#include <QtCore/QUuid>

namespace Platform::Notifications {
namespace {

const auto kRegistrationOwner = u"AstraGram.Notifications.v1"_q;

QString ApplicationKey() {
	return u"HKEY_CURRENT_USER\\Software\\Classes\\AppUserModelId\\"_q
		+ QString::fromStdWString(AppUserModelId::Id());
}

QString ActivatorId() {
	return QUuid(__uuidof(ToastActivator)).toString().toUpper();
}

QString ActivatorKey() {
	return u"HKEY_CURRENT_USER\\Software\\Classes\\CLSID\\"_q
		+ ActivatorId();
}

QString RelativeKey(const QString &path) {
	Expects(path == ApplicationKey() || path == ActivatorKey());
	return path.mid(QStringView(u"HKEY_CURRENT_USER\\").size());
}

struct Registration {
	bool exists = false;
	QString owner;
	QString command;
};

std::optional<QString> ReadString(HKEY key, LPCWSTR name) {
	DWORD size = 0;
	auto status = RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, nullptr, &size);
	if (status == ERROR_FILE_NOT_FOUND) {
		return QString();
	}
	if (status != ERROR_SUCCESS) {
		return std::nullopt;
	}
	auto value = std::wstring(size / sizeof(wchar_t), L'\0');
	status = RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, value.data(), &size);
	return status == ERROR_SUCCESS
		? std::optional(QString::fromWCharArray(value.c_str()))
		: std::nullopt;
}

std::optional<Registration> ReadRegistration(const QString &path) {
	const auto relative = RelativeKey(path);
	HKEY key = nullptr;
	const auto status = RegOpenKeyExW(
		HKEY_CURRENT_USER,
		reinterpret_cast<LPCWSTR>(relative.utf16()),
		0,
		KEY_QUERY_VALUE,
		&key);
	if (status == ERROR_FILE_NOT_FOUND) {
		return Registration();
	}
	if (status != ERROR_SUCCESS) {
		LOG(("Notifications Error: Could not read registry key %1 (%2).")
			.arg(path).arg(status));
		return std::nullopt;
	}
	const auto guard = gsl::finally([&] { RegCloseKey(key); });
	const auto owner = ReadString(key, L"AstraGramRegistration");
	const auto command = ReadString(key, L"AstraGramOwner");
	if (!owner || !command) {
		LOG(("Notifications Error: Could not read registry ownership at %1.").arg(path));
		return std::nullopt;
	}
	return Registration{ true, *owner, *command };
}

QString LaunchCommand() {
	// 末尾反斜杠不能紧挨引号，否则会改变 Windows 参数边界。
	const auto directory = QDir::toNativeSeparators(
		QDir::cleanPath(cWorkingDir()) + u"/."_q);
	return '"' + QDir::toNativeSeparators(cExeDir() + cExeName())
		+ u"\" -toastactivated -noupdate -workdir \""_q + directory
		+ u"\" -key \""_q + cDataFile() + '"';
}

bool Sync(QSettings &settings) {
	settings.sync();
	if (settings.status() == QSettings::NoError) {
		return true;
	}
	LOG(("Notifications Error: Registry update failed at %1 (%2).")
		.arg(settings.fileName()).arg(int(settings.status())));
	return false;
}

bool CanRegister(const QString &path) {
	const auto registration = ReadRegistration(path);
	if (!registration) {
		return false;
	}
	if (!registration->exists || registration->owner == kRegistrationOwner) {
		return true;
	}
	LOG(("Notifications Error: Registry ownership mismatch at %1.").arg(path));
	return false;
}

} // namespace

bool RegisterApplication() {
#ifdef OS_WIN_STORE
	return true;
#else
	if (!CanRegister(ApplicationKey()) || !CanRegister(ActivatorKey())) {
		return false;
	}
	const auto iconPath = cWorkingDir() + u"tdata/notification-icon.png"_q;
	auto icon = QSaveFile(iconPath);
	if (!icon.open(QIODevice::WriteOnly)
		|| !ExtrasAssets::currentAppLogo().save(&icon, "PNG")
		|| !icon.commit()) {
		LOG(("Notifications Error: Could not save application icon."));
		return false;
	}

	const auto command = LaunchCommand();
	auto activator = QSettings(ActivatorKey(), QSettings::NativeFormat);
	activator.setValue(u"AstraGramRegistration"_q, kRegistrationOwner);
	activator.setValue(u"AstraGramOwner"_q, command);
	activator.setValue(u"LocalServer32/."_q, command);
	if (!Sync(activator)) {
		return false;
	}

	auto application = QSettings(ApplicationKey(), QSettings::NativeFormat);
	application.setValue(u"AstraGramRegistration"_q, kRegistrationOwner);
	application.setValue(u"AstraGramOwner"_q, command);
	application.setValue(u"DisplayName"_q,
#ifdef _DEBUG
		u"AstraGram Dev"_q);
#else
		u"AstraGram"_q);
#endif
	application.setValue(u"IconUri"_q, QDir::toNativeSeparators(iconPath));
	application.setValue(u"CustomActivator"_q, ActivatorId());
	return Sync(application);
#endif
}

bool UnregisterApplication(bool includePreviousPaths) {
#ifdef OS_WIN_STORE
	return true;
#else
	const auto command = LaunchCommand();
	const auto removeOwnedKey = [&](const QString &path) {
		const auto registration = ReadRegistration(path);
		if (!registration) {
			return false;
		}
		if (!registration->exists) {
			return true;
		}
		if (registration->owner != kRegistrationOwner) {
			LOG(("Notifications Error: Registry ownership mismatch at %1.").arg(path));
			return false;
		}
		if (!includePreviousPaths && registration->command != command) {
			return true;
		}
		// 仅允许当前构建的两个完整路径，绝不向父级目录递归删除。
		const auto relative = RelativeKey(path);
		const auto status = RegDeleteTreeW(
			HKEY_CURRENT_USER,
			reinterpret_cast<LPCWSTR>(relative.utf16()));
		if (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND) {
			return true;
		}
		LOG(("Notifications Error: Could not delete registry key %1 (%2).")
			.arg(path).arg(status));
		return false;
	};
	const auto application = removeOwnedKey(ApplicationKey());
	const auto activator = removeOwnedKey(ActivatorKey());
	return application && activator;
#endif
}

} // namespace Platform::Notifications
