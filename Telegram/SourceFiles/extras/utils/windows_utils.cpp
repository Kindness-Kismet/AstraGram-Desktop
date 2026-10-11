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
		UniqueFileId selfId) {
	const auto shellLink = base::WinRT::TryCreateInstance<IShellLink>(
		CLSID_ShellLink);
	if (!shellLink) {
		return;
	}
	const auto persistFile = shellLink.try_as<IPersistFile>();
	if (!persistFile
		|| FAILED(persistFile->Load(shortcut.c_str(), STGM_READWRITE))) {
		return;
	}
	WCHAR target[MAX_PATH] = { 0 };
	if (FAILED(shellLink->GetPath(target, MAX_PATH, nullptr, 0))
		|| Platform::AppUserModelId::GetUniqueFileId(target) != selfId) {
		return;
	}
	shellLink->SetIconLocation(iconPath.c_str(), 0);
	persistFile->Save(shortcut.c_str(), TRUE);
}

void processPinned(const std::wstring &iconPath, UniqueFileId selfId) {
	const auto path = Platform::AppUserModelId::PinnedIconsPath();
	if (path.isEmpty()) {
		return;
	}
	const auto native = QDir::toNativeSeparators(path).toStdWString();
	auto findData = WIN32_FIND_DATA();
	const auto findHandle = FindFirstFileEx(
		(native + L"*").c_str(),
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
		processIcon(native + findData.cFileName, iconPath, selfId);
	} while (FindNextFile(findHandle, &findData));
	FindClose(findHandle);
}

void processStartMenu(const std::wstring &iconPath, UniqueFileId selfId) {
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
			selfId);
	}
}

} // namespace

void reloadAppIconFromTaskBar() {
	const auto selfId = Platform::AppUserModelId::MyExecutablePathId();
	if (!selfId || FAILED(CoInitialize(nullptr))) {
		return;
	}
	const auto coGuard = gsl::finally([] { CoUninitialize(); });

	const auto iconPath = ExtrasAssets::appIcoPath().toStdWString();
	processPinned(iconPath, selfId);
	processStartMenu(iconPath, selfId);

	SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
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
