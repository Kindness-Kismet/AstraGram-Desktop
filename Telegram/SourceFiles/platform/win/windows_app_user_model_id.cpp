/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/windows_app_user_model_id.h"

namespace Platform {
namespace AppUserModelId {
namespace {

constexpr auto kMaxFileLen = MAX_PATH * 2;

const PROPERTYKEY pkey_AppUserModel_ID = { { 0x9F4C2855, 0x9F79, 0x4B39, { 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } }, 5 };

} // namespace

QString PinnedIconsPath() {
	WCHAR wstrPath[kMaxFileLen] = {};
	if (GetEnvironmentVariable(L"APPDATA", wstrPath, kMaxFileLen)) {
		auto appData = QDir(QString::fromStdWString(std::wstring(wstrPath)));
		return appData.absolutePath()
			+ u"/Microsoft/Internet Explorer/Quick Launch/User Pinned/TaskBar/"_q;
	}
	return QString();
}

const std::wstring &MyExecutablePath() {
	static const auto Path = [&] {
		auto result = std::wstring(kMaxFileLen, 0);
		const auto length = GetModuleFileName(
			GetModuleHandle(nullptr),
			result.data(),
			kMaxFileLen);
		if (!length || length == kMaxFileLen) {
			result.clear();
		} else {
			result.resize(length + 1);
		}
		return result;
	}();
	return Path;
}

UniqueFileId MyExecutablePathId() {
	return GetUniqueFileId(MyExecutablePath().c_str());
}

UniqueFileId GetUniqueFileId(LPCWSTR path) {
	auto info = BY_HANDLE_FILE_INFORMATION{};
	const auto file = CreateFile(
		path,
		0,
		0,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return {};
	}
	const auto result = GetFileInformationByHandle(file, &info);
	CloseHandle(file);
	if (!result) {
		return {};
	}
	return {
		.part1 = info.dwVolumeSerialNumber,
		.part2 = ((std::uint64_t(info.nFileIndexLow) << 32)
			| std::uint64_t(info.nFileIndexHigh)),
	};
}

QString systemShortcutPath() {
	WCHAR wstrPath[kMaxFileLen] = {};
	if (GetEnvironmentVariable(L"APPDATA", wstrPath, kMaxFileLen)) {
		auto appData = QDir(QString::fromStdWString(std::wstring(wstrPath)));
		const auto path = appData.absolutePath();
		return path + u"/Microsoft/Windows/Start Menu/Programs/"_q;
	}
	return QString();
}

const std::wstring &Id() {
#ifdef OS_WIN_STORE
	static const auto Result = std::wstring(L"AstraGram.AstraGramDesktop.Store");
#elif defined _DEBUG
	static const auto Result = std::wstring(L"AstraGram.AstraGramDesktop.Debug");
#else
	static const auto Result = std::wstring(L"AstraGram.AstraGramDesktop");
#endif
	return Result;
}

const PROPERTYKEY &Key() {
	return pkey_AppUserModel_ID;
}

} // namespace AppUserModelId
} // namespace Platform
