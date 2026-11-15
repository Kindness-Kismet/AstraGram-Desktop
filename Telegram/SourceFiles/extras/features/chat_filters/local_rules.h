#pragma once

#include "base/flat_map.h"

class History;

namespace Data {

struct ChatFilterRule {
	QString raw;
	QString folded;

	friend bool operator==(const ChatFilterRule&, const ChatFilterRule&) = default;
};

struct ChatFilterLocalRules {
	std::vector<ChatFilterRule> include;
	std::vector<ChatFilterRule> exclude;

	[[nodiscard]] bool empty() const {
		return include.empty() && exclude.empty();
	}

	friend bool operator==(
		const ChatFilterLocalRules&,
		const ChatFilterLocalRules&) = default;
};

} // namespace Data

namespace ExtrasChatFilters {

using RulesById = base::flat_map<FilterId, Data::ChatFilterLocalRules>;

[[nodiscard]] std::vector<Data::ChatFilterRule> parseRules(const QString &text);
[[nodiscard]] QString rulesToText(const std::vector<Data::ChatFilterRule> &rules);
[[nodiscard]] bool matchesHistory(
	const std::vector<Data::ChatFilterRule> &rules,
	not_null<History*> history);
[[nodiscard]] QByteArray serializeRules(const RulesById &rules);
[[nodiscard]] RulesById deserializeRules(const QByteArray &data);

} // namespace ExtrasChatFilters
