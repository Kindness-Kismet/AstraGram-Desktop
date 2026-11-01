#include "extras/extras_lang.h"

#include "lang/lang_instance.h"
#include "lang_auto_counts.h"

#include <QFile>
#include <QJsonParseError>
#include <QRegularExpression>

namespace ExtrasLang {

QByteArray normalizeChatTranslation(
		const QString &languageId,
		const QByteArray &key,
		const QByteArray &value) {
	if (!languageId.startsWith(u"zh-hans"_q)
		&& !languageId.startsWith(u"zh-cn"_q)) {
		return value;
	}
	auto text = QString::fromUtf8(value);
	if (!text.contains(u"对话"_q)) {
		return value;
	}
	const auto index = Lang::GetKeyIndex(QLatin1String(key));
	if (index == Lang::kKeysCount) {
		return value;
	}
	static const auto chatTerms = QRegularExpression(
		u"\\b(chats?|conversations?)\\b"_q,
		QRegularExpression::CaseInsensitiveOption);
	if (!chatTerms.match(Lang::GetOriginalValue(index)).hasMatch()) {
		return value;
	}
	// 只统一聊天术语，保留对话框名称和翻译占位符。
	static const auto translatedTerm = QRegularExpression(u"对话(?!框)"_q);
	return text.replace(translatedTerm, u"聊天"_q).toUtf8();
}

} // namespace ExtrasLang

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
	Expects(!instance);

	instance = new ExtrasLanguage;
	instance->loadBundledLanguage();
	// 切换语言会还原全部文案，语言包下载完成后重新叠加内置文案。
	Lang::GetInstance().updated(
	) | rpl::on_next([] {
		instance->loadBundledLanguage();
	}, instance->_lifetime);
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
		LOG(("Loading bundled AstraGram language: %1").arg(mapped));
		applyLanguageJson(doc);
		return true;
	}
	return false;
}

void ExtrasLanguage::applyLanguageJson(QJsonDocument doc) {
	const auto json = doc.object();
	for (const QString &brokenKey : json.keys()) {
		auto key = brokenKey.startsWith(u"lng_"_q)
			? brokenKey
			: qsl("extras_") + brokenKey;
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
