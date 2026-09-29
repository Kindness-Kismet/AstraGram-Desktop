#include "extras/extras_lang.h"

#include "lang/lang_instance.h"

#include <QFile>
#include <QJsonParseError>

// 系统语言 id 到内置语言包文件名的归一
std::map<QString, QString> langMapping = {
	{"zh-hans-beta", "zh-hans"},
	{"zh-hans-raw", "zh-hans"},
};

constexpr auto postfixes = {
	"zero",
	"one",
	"two",
	"few",
	"many",
	"other"
};

[[nodiscard]] QString MappedLanguageId(const QString &id) {
	const auto i = langMapping.find(id);
	return (i != langMapping.end()) ? i->second : id;
}

ExtrasLanguage *ExtrasLanguage::instance = nullptr;

void ExtrasLanguage::init() {
	if (!instance) {
		instance = new ExtrasLanguage;
	}
	instance->loadBundledLanguage();
}

// 依次尝试当前语言与基础语言（自定义语言叠加官方基础包的组合），
// 都没有内置包时维持英文默认。
bool ExtrasLanguage::loadBundledLanguage() {
	const auto langId = Lang::GetInstance().id();
	const auto baseId = Lang::GetInstance().baseId();
	for (const auto &id : { langId, baseId }) {
		const auto mapped = MappedLanguageId(id);
		if (mapped.isEmpty()) {
			continue;
		}
		QFile file(u":/gui/langs/extras/%1.json"_q.arg(mapped));
		if (!file.open(QIODevice::ReadOnly)) {
			continue;
		}
		const auto data = file.readAll();
		file.close();

		QJsonParseError error{};
		const auto doc = QJsonDocument::fromJson(data, &error);
		if (error.error != QJsonParseError::NoError) {
			LOG(("Incorrect bundled language JSON: %1").arg(mapped));
			continue;
		}
		LOG(("Loading bundled AyuGram language: %1").arg(mapped));
		applyLanguageJson(doc);
		return true;
	}
	return false;
}

void ExtrasLanguage::applyLanguageJson(QJsonDocument doc) {
	const auto json = doc.object();
	for (const QString &brokenKey : json.keys()) {
		auto key = qsl("extras_") + brokenKey;
		auto val = json.value(brokenKey).toString().replace(qsl("&amp;"), qsl("&"));

		if (key.endsWith("_Android")) {
			continue;
		}

		for (const auto &postfix : postfixes) {
			if (key.endsWith(qsl("_") + postfix)) {
				key = key.replace(qsl("_") + postfix, qsl("#") + postfix);
				break;
			}
		}

		if (key.endsWith("_PC")) {
			key = key.replace("_PC", "");
		}

		if (val.contains(qsl("%1$d")) && !val.contains(qsl("%2$d"))) {
			val = val.replace(qsl("%1$d"), qsl("{count}"));
		} else if (val.contains(qsl("%1$d")) && val.contains(qsl("%2$d"))) {
			val = val.replace(qsl("%1$d"), qsl("{count1}")).replace(qsl("%2$d"), qsl("{count2}"));
		} else if (val.contains(qsl("%1$s")) && !val.contains(qsl("%2$s"))) {
			val = val.replace(qsl("%1$s"), qsl("{item}"));
		} else if (val.contains(qsl("%1$s")) && val.contains(qsl("%2$s"))) {
			val = val.replace(qsl("%1$s"), qsl("{item1}")).replace(qsl("%2$s"), qsl("{item2}"));
		}

		Lang::GetInstance().resetValue(key.toUtf8());
		Lang::GetInstance().applyValue(key.toUtf8(), val.toUtf8());
	}
	Lang::GetInstance().updatePluralRules();
}
