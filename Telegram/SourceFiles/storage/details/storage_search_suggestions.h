#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QDataStream>

#include <optional>

namespace Storage::details {

struct SearchSuggestions {
	QByteArray topPeers;
	QByteArray recentPeers;
	QByteArray topGuestChatBots;
	QByteArray recentMoneyRecipients;
};

struct SearchSuggestionsReadResult {
	SearchSuggestions data;
	bool legacy = false;
};

void WriteSearchSuggestions(
	QDataStream &stream,
	const SearchSuggestions &data);
[[nodiscard]] std::optional<SearchSuggestionsReadResult> ReadSearchSuggestions(
	QDataStream &stream);

} // namespace Storage::details
