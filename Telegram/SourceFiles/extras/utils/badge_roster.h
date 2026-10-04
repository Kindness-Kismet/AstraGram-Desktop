#pragma once

#include "extras/data/entities.h"
#include "data/data_types.h"

#include <optional>
#include <unordered_map>
#include <unordered_set>

struct CustomBadge {
	EmojiStatusId emojiStatusId;
	QString text;

	friend bool operator==(const CustomBadge &, const CustomBadge &) = default;
};

struct BadgeRoster {
	std::unordered_set<ID> developers;
	std::unordered_set<ID> officialChannels;
	std::unordered_set<ID> supporters;
	std::unordered_set<ID> supporterChannels;
	std::unordered_map<ID, CustomBadge> customBadges;

	friend bool operator==(const BadgeRoster &, const BadgeRoster &) = default;
};

inline constexpr auto kBadgeRosterMaxBytes = 64 * 1024;

[[nodiscard]] std::optional<BadgeRoster> parseBadgeRoster(
	const QByteArray &response);
