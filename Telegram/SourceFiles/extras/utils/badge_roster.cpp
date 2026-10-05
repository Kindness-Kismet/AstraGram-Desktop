#include "extras/utils/badge_roster.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <limits>

namespace {

constexpr auto kMaxEntries = 1000;
constexpr auto kMaxTextLength = 256;

[[nodiscard]] uint64 parseDecimal(const QJsonValue &value, uint64 maximum) {
	if (!value.isString()) {
		return 0;
	}
	const auto text = value.toString();
	auto ok = false;
	const auto result = text.toULongLong(&ok);
	return (ok && result && result <= maximum && QString::number(result) == text)
		? result
		: 0;
}

[[nodiscard]] bool parseIds(
		const QJsonValue &value,
		std::unordered_set<ID> &result) {
	if (!value.isArray()) {
		return false;
	}
	const auto array = value.toArray();
	if (array.size() > kMaxEntries) {
		return false;
	}
	for (const auto &entry : array) {
		const auto id = parseDecimal(entry, std::numeric_limits<ID>::max());
		if (!id || !result.insert(ID(id)).second) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] bool parseCustomBadges(
		const QJsonValue &value,
		std::unordered_map<ID, CustomBadge> &result) {
	if (!value.isArray()) {
		return false;
	}
	const auto array = value.toArray();
	if (array.size() > kMaxEntries) {
		return false;
	}
	for (const auto &entry : array) {
		if (!entry.isObject()) {
			return false;
		}
		const auto object = entry.toObject();
		const auto id = parseDecimal(object.value("id"), std::numeric_limits<ID>::max());
		if (!id || object.size() != 2 || !object.value("badge").isObject()) {
			return false;
		}
		const auto badge = object.value("badge").toObject();
		const auto documentId = parseDecimal(
			badge.value("documentId"),
			std::numeric_limits<DocumentId>::max());
		const auto hasText = badge.contains("text");
		if (!documentId || badge.size() != (hasText ? 2 : 1)
			|| (hasText && !badge.value("text").isString())) {
			return false;
		}
		const auto text = badge.value("text").toString();
		const auto points = text.toUcs4();
		if (points.size() > kMaxTextLength) {
			return false;
		}
		for (const auto point : points) {
			if (QChar::category(point) == QChar::Other_Control && point != '\n') {
				return false;
			}
		}
		if (!result.emplace(ID(id), CustomBadge{
			.emojiStatusId = EmojiStatusId{ documentId },
			.text = text,
		}).second) {
			return false;
		}
	}
	return true;
}

} // namespace

std::optional<BadgeRoster> parseBadgeRoster(const QByteArray &response) {
	if (response.size() > kBadgeRosterMaxBytes) {
		return std::nullopt;
	}
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(response, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		return std::nullopt;
	}
	const auto root = document.object();
	auto result = BadgeRoster();
	if (root.size() != 5
		|| !parseIds(root.value("developers"), result.developers)
		|| !parseIds(root.value("officialChannels"), result.officialChannels)
		|| !parseIds(root.value("supporters"), result.supporters)
		|| !parseIds(root.value("supporterChannels"), result.supporterChannels)
		|| !parseCustomBadges(root.value("customBadges"), result.customBadges)) {
		return std::nullopt;
	}
	return result;
}
