#pragma once

#include "rpl/lifetime.h"

#include <QJsonDocument>

namespace ExtrasLang {

[[nodiscard]] QByteArray normalizeChatTranslation(
	const QString &languageId,
	const QByteArray &key,
	const QByteArray &value);

} // namespace ExtrasLang

// 自有文案与官方文案覆盖均从内置资源加载。
class ExtrasLanguage {
public:
	static void init();

private:
	ExtrasLanguage() = default;

	static ExtrasLanguage *instance;

	bool loadBundledLanguage();
	void applyLanguageJson(QJsonDocument doc);

	rpl::lifetime _lifetime;
};
