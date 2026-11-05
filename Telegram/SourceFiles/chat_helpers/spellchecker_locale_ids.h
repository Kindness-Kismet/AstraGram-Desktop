/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QtGlobal>

namespace Spellchecker {

inline constexpr auto kManifestIdScheme = 6;

[[nodiscard]] int LocalIdScheme();

[[nodiscard]] int ConvertLangId(int langId, int fromScheme, int toScheme);

[[nodiscard]] inline int LangIdFromManifest(int langId) {
	return ConvertLangId(langId, kManifestIdScheme, LocalIdScheme());
}

[[nodiscard]] int RecoverLangIdFromSerialized(quint64 raw, int scheme);

[[nodiscard]] inline int LangIdSchemeForAppVersion(int version) {
#if defined Q_OS_MAC || defined Q_PROCESSOR_ARM
	return kManifestIdScheme;
#else
	// 本项目在 7.3.1 内切换 Qt；该版本缺少修订号，保留原编号避免误转。
	return (version > 0 && version < 7003001) ? 5 : kManifestIdScheme;
#endif
}

inline constexpr auto kDictionariesIdSchemePref = "dictionaries_id_scheme";

} // namespace Spellchecker
