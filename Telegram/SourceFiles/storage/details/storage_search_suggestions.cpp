#include "storage/details/storage_search_suggestions.h"

#include <QtCore/QMap>
#include <QtCore/QString>

#include <utility>

namespace Storage::details {
namespace {

// 旧 topPeers 的第二个 32 位字段只能是 0 或 1，无法与此标识重合。
const auto kFormat = QByteArrayLiteral("AstraGram.SearchSuggestions");
constexpr auto kVersion = quint32(1);
const auto kTopPeers = QStringLiteral("topPeers");
const auto kRecentPeers = QStringLiteral("recentPeers");
const auto kTopGuestChatBots = QStringLiteral("topGuestChatBots");
const auto kRecentMoneyRecipients = QStringLiteral("recentMoneyRecipients");

[[nodiscard]] std::optional<SearchSuggestionsReadResult> ReadLegacySuggestions(
		QDataStream &stream,
		QByteArray topPeers) {
	auto result = SearchSuggestionsReadResult{
		.data = { .topPeers = std::move(topPeers) },
		.legacy = true,
	};
	stream >> result.data.recentPeers;
	if (!stream.atEnd()) {
		auto settingsSearches = QByteArray();
		stream >> settingsSearches;
	}
	if (!stream.atEnd()) {
		stream >> result.data.topGuestChatBots;
	}
	if (!stream.atEnd()) {
		stream >> result.data.recentMoneyRecipients;
	}
	if (stream.status() != QDataStream::Ok) {
		return std::nullopt;
	}
	return result;
}

} // namespace

void WriteSearchSuggestions(
		QDataStream &stream,
		const SearchSuggestions &data) {
	const auto fields = QMap<QString, QByteArray>{
		{ kTopPeers, data.topPeers },
		{ kRecentPeers, data.recentPeers },
		{ kTopGuestChatBots, data.topGuestChatBots },
		{ kRecentMoneyRecipients, data.recentMoneyRecipients },
	};
	stream << kFormat << kVersion << fields;
}

std::optional<SearchSuggestionsReadResult> ReadSearchSuggestions(
		QDataStream &stream) {
	auto first = QByteArray();
	stream >> first;
	if (stream.status() != QDataStream::Ok) {
		return std::nullopt;
	}
	if (first != kFormat) {
		return ReadLegacySuggestions(stream, std::move(first));
	}
	auto version = quint32();
	stream >> version;
	if (stream.status() != QDataStream::Ok || version != kVersion) {
		return std::nullopt;
	}
	auto fields = QMap<QString, QByteArray>();
	stream >> fields;
	if (stream.status() != QDataStream::Ok) {
		return std::nullopt;
	}
	return SearchSuggestionsReadResult{
		.data = {
			.topPeers = fields.value(kTopPeers),
			.recentPeers = fields.value(kRecentPeers),
			.topGuestChatBots = fields.value(kTopGuestChatBots),
			.recentMoneyRecipients = fields.value(kRecentMoneyRecipients),
		},
	};
}

} // namespace Storage::details
