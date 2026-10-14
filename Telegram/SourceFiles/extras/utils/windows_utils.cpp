#ifdef Q_OS_WIN

#include "extras/utils/windows_utils.h"

#include "extras/ui/extras_logo.h"
#include "base/platform/win/base_windows_winrt.h"
#include "platform/win/windows_app_user_model_id.h"

#include <ShlObj_core.h>

namespace {

using Platform::AppUserModelId::UniqueFileId;

// 只改指向本程序的快捷方式，其他客户端的同名或同目录快捷方式保持原样。
void processIcon(
		const std::wstring &shortcut,
		const std::wstring &iconPath,
		UniqueFileId selfId,
		bool iconUpdated) {
	const auto shellLink = base::WinRT::TryCreateInstance<IShellLink>(
		CLSID_ShellLink);
	if (!shellLink) {
		return;
	}
	const auto persistFile = shellLink.try_as<IPersistFile>();
	if (!persistFile
		|| FAILED(persistFile->Load(shortcut.c_str(), STGM_READ))) {
		return;
	}
	WCHAR target[MAX_PATH] = { 0 };
	if (FAILED(shellLink->GetPath(target, MAX_PATH, nullptr, 0))
		|| Platform::AppUserModelId::GetUniqueFileId(target) != selfId) {
		return;
	}
	WCHAR currentIcon[MAX_PATH] = { 0 };
	auto currentIndex = 0;
	const auto read = shellLink->GetIconLocation(
		currentIcon,
		MAX_PATH,
		&currentIndex);
	if (FAILED(read)) {
		LOG(("Icon Error: Could not read shortcut icon %1: %2")
			.arg(QString::fromStdWString(shortcut)).arg(uint32(read)));
		return;
	}
	const auto currentPath = QDir::cleanPath(QDir::fromNativeSeparators(
		QString::fromWCharArray(currentIcon)));
	const auto expectedPath = QDir::cleanPath(QDir::fromNativeSeparators(
		QString::fromStdWString(iconPath)));
	const auto same = !currentIndex
		&& !currentPath.compare(expectedPath, Qt::CaseInsensitive);
	if (!same) {
		const auto set = shellLink->SetIconLocation(iconPath.c_str(), 0);
		if (FAILED(set)) {
			LOG(("Icon Error: Could not set shortcut icon %1: %2")
				.arg(QString::fromStdWString(shortcut)).arg(uint32(set)));
			return;
		}
		const auto saved = persistFile->Save(shortcut.c_str(), TRUE);
		if (FAILED(saved)) {
			LOG(("Icon Error: Could not save shortcut %1: %2")
				.arg(QString::fromStdWString(shortcut)).arg(uint32(saved)));
			return;
		}
	}
	if (same && !iconUpdated) {
		return;
	}
	// 同路径的图标内容也可能更新，但无需刷新整个桌面的图标缓存。
	SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, shortcut.c_str(), nullptr);
	DEBUG_LOG(("Icon: Notified shortcut %1").arg(QString::fromStdWString(shortcut)));
}

void processPinned(
		const std::wstring &iconPath,
		UniqueFileId selfId,
		bool iconUpdated) {
	const auto path = Platform::AppUserModelId::PinnedIconsPath();
	if (path.isEmpty()) {
		return;
	}
	const auto native = QDir::toNativeSeparators(path).toStdWString();
	auto findData = WIN32_FIND_DATA();
	const auto findHandle = FindFirstFileEx(
		(native + L"*.lnk").c_str(),
		FindExInfoStandard,
		&findData,
		FindExSearchNameMatch,
		nullptr,
		0);
	if (findHandle == INVALID_HANDLE_VALUE) {
		return;
	}
	do {
		if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			continue;
		}
		processIcon(native + findData.cFileName, iconPath, selfId, iconUpdated);
	} while (FindNextFile(findHandle, &findData));
	FindClose(findHandle);
}

void processStartMenu(
		const std::wstring &iconPath,
		UniqueFileId selfId,
		bool iconUpdated) {
	const auto path = Platform::AppUserModelId::systemShortcutPath();
	if (path.isEmpty()) {
		return;
	}
	const auto shortcuts = {
		path + u"AstraGram/AstraGram.lnk"_q,
		path + u"AstraGram.lnk"_q,
	};
	for (const auto &shortcut : shortcuts) {
		if (!QFile::exists(shortcut)) {
			continue;
		}
		processIcon(
			QDir::toNativeSeparators(shortcut).toStdWString(),
			iconPath,
			selfId,
			iconUpdated);
	}
}

} // namespace

void reloadAppIconFromTaskBar() {
	const auto status = ExtrasAssets::loadAppIco();
	if (status == ExtrasAssets::AppIcoStatus::Failed) {
		return;
	}
	const auto selfId = Platform::AppUserModelId::MyExecutablePathId();
	if (!selfId || FAILED(CoInitialize(nullptr))) {
		return;
	}
	const auto coGuard = gsl::finally([] { CoUninitialize(); });

	const auto iconPath = QDir::toNativeSeparators(
		ExtrasAssets::appIcoPath()).toStdWString();
	const auto iconUpdated = (status == ExtrasAssets::AppIcoStatus::Updated);
	processPinned(iconPath, selfId, iconUpdated);
	processStartMenu(iconPath, selfId, iconUpdated);
}

bool isCapabilityDenied(const QString &capability) {
	const auto base = u"Software\\Microsoft\\Windows\\CurrentVersion"
		"\\CapabilityAccessManager\\ConsentStore\\"_q + capability;
	const auto denied = [](HKEY root, const QString &path) {
		const auto key = path.toStdWString();
		wchar_t value[16] = { 0 };
		auto size = DWORD(sizeof(value));
		return (RegGetValueW(
			root,
			key.c_str(),
			L"Value",
			RRF_RT_REG_SZ,
			nullptr,
			value,
			&size) == ERROR_SUCCESS) && !wcscmp(value, L"Deny");
	};
	// 依次为设备总开关、当前用户开关、桌面应用开关。
	return denied(HKEY_LOCAL_MACHINE, base)
		|| denied(HKEY_CURRENT_USER, base)
		|| denied(HKEY_CURRENT_USER, base + u"\\NonPackaged"_q);
}

#endif
