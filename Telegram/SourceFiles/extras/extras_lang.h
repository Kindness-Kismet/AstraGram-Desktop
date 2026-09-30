#pragma once

#include <QJsonDocument>

// AstraGram 自有文案的语言覆盖，全部从 qrc 内置资源加载，不走网络。
class ExtrasLanguage {
public:
	static void init();

private:
	ExtrasLanguage() = default;

	static ExtrasLanguage *instance;

	bool loadBundledLanguage();
	void applyLanguageJson(QJsonDocument doc);
};
