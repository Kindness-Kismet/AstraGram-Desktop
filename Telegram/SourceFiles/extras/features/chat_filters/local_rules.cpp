#include "extras/features/chat_filters/local_rules.h"

#include "data/data_peer.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QStringList>

namespace ExtrasChatFilters {
namespace {

[[nodiscard]] QString foldToken(const QString &text) {
	auto result = QString();
	for (const auto ch : text.normalized(QString::NormalizationForm_KC).toCaseFolded()) {
		if (ch.isLetterOrNumber()) {
			result.append(ch);
		}
	}
	return result;
}

[[nodiscard]] QJsonArray serializeList(
		const std::vector<Data::ChatFilterRule> &rules) {
	auto result = QJsonArray();
	for (const auto &rule : rules) {
		result.append(rule.raw);
	}
	return result;
}

[[nodiscard]] std::vector<Data::ChatFilterRule> deserializeList(
		const QJsonArray &array) {
	auto text = QStringList();
	for (const auto &value : array) {
		text.push_back(value.toString());
	}
	return parseRules(text.join(u'\n'));
}

} // namespace

std::vector<Data::ChatFilterRule> parseRules(const QString &text) {
	auto result = std::vector<Data::ChatFilterRule>();
	auto token = QString();
	const auto flush = [&] {
		auto raw = token.trimmed();
		token.clear();
		auto folded = foldToken(raw);
		if (folded.size() < 2
			|| ranges::contains(result, folded, &Data::ChatFilterRule::folded)) {
			return;
		}
		result.push_back({ std::move(raw), std::move(folded) });
	};
	for (const auto ch : text) {
		if (ch == u',' || ch == u';' || ch == u'，' || ch == u'；'
			|| ch == u'\n' || ch == u'\r') {
			flush();
		} else {
			token.append(ch);
		}
	}
	flush();
	return result;
}

QString rulesToText(const std::vector<Data::ChatFilterRule> &rules) {
	auto result = QStringList();
	for (const auto &rule : rules) {
		result.push_back(rule.raw);
	}
	return result.join(u", "_q);
}

bool matchesHistory(
		const std::vector<Data::ChatFilterRule> &rules,
		not_null<History*> history) {
	if (rules.empty()) {
		return false;
	}
	const auto peer = history->peer;
	const auto name = foldToken(peer->name());
	const auto username = foldToken(peer->username());
	const auto user = peer->asUser();
	const auto phone = user ? foldToken(user->phone()) : QString();
	if (ranges::any_of(rules, [&](const Data::ChatFilterRule &rule) {
		return name.contains(rule.folded)
			|| username.contains(rule.folded)
			|| phone.contains(rule.folded);
	})) {
		return true;
	}
	const auto last = history->lastMessage();
	if (!last || ranges::none_of(rules, [](const Data::ChatFilterRule &rule) {
		return rule.folded.size() >= 4;
	})) {
		return false;
	}
	const auto message = foldToken(last->originalText().text);
	return ranges::any_of(rules, [&](const Data::ChatFilterRule &rule) {
		// 正文至少匹配四个字母或数字，避免短词误分组。
		return rule.folded.size() >= 4 && message.contains(rule.folded);
	});
}

QByteArray serializeRules(const RulesById &rules) {
	auto result = QJsonObject();
	for (const auto &[id, value] : rules) {
		result.insert(QString::number(id), QJsonObject{
			{ u"include"_q, serializeList(value.include) },
			{ u"exclude"_q, serializeList(value.exclude) },
		});
	}
	return rules.empty()
		? QByteArray()
		: QJsonDocument(result).toJson(QJsonDocument::Compact);
}

RulesById deserializeRules(const QByteArray &data) {
	auto result = RulesById();
	if (data.isEmpty()) {
		return result;
	}
	const auto document = QJsonDocument::fromJson(data);
	if (!document.isObject()) {
		LOG(("Chat filters: Invalid local rules data."));
		return result;
	}
	const auto object = document.object();
	for (auto i = object.begin(); i != object.end(); ++i) {
		auto ok = false;
		const auto id = i.key().toInt(&ok);
		if (!ok || id <= 0 || !i.value().isObject()) {
			continue;
		}
		const auto value = i.value().toObject();
		auto rules = Data::ChatFilterLocalRules{
			deserializeList(value.value(u"include"_q).toArray()),
			deserializeList(value.value(u"exclude"_q).toArray()),
		};
		if (!rules.empty()) {
			result.emplace(id, std::move(rules));
		}
	}
	return result;
}

} // namespace ExtrasChatFilters
