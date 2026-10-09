#include "storage/details/storage_search_suggestions.h"

#include <QtCore/QMap>
#include <QtCore/QString>

#include <iostream>

namespace {

using namespace Storage::details;

int Failures = 0;
int Checks = 0;

void Check(bool passed, const char *name) {
	++Checks;
	if (!passed) {
		++Failures;
		std::cerr << "FAILED: " << name << '\n';
	}
}

std::optional<SearchSuggestionsReadResult> Read(const QByteArray &bytes) {
	auto stream = QDataStream(bytes);
	stream.setVersion(QDataStream::Qt_5_1);
	return ReadSearchSuggestions(stream);
}

QByteArray Legacy(int fields) {
	auto bytes = QByteArray();
	auto stream = QDataStream(&bytes, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << QByteArray("top") << QByteArray("recent");
	if (fields >= 3) {
		stream << QByteArray("searches");
	}
	if (fields >= 4) {
		stream << QByteArray("guest-bots");
	}
	if (fields >= 5) {
		stream << QByteArray("recipients");
	}
	return bytes;
}

QByteArray Tagged(quint32 version) {
	auto bytes = QByteArray();
	auto stream = QDataStream(&bytes, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << QByteArray("AstraGram.SearchSuggestions") << version;
	stream << QMap<QString, QByteArray>{
		{ QStringLiteral("topPeers"), QByteArray("top") },
		{ QStringLiteral("recentPeers"), QByteArray("recent") },
		{ QStringLiteral("topGuestChatBots"), QByteArray("guest-bots") },
		{ QStringLiteral("futureField"), QByteArray("ignored") },
	};
	return bytes;
}

} // namespace

int main() {
	for (auto fields = 2; fields <= 5; ++fields) {
		const auto bytes = Legacy(fields);
		const auto result = Read(bytes);
		Check(result && result->legacy, "legacy format accepted");
		Check(result && result->data.topPeers == "top"
			&& result->data.recentPeers == "recent", "legacy peers preserved");
		Check(result && result->data.topGuestChatBots
			== (fields >= 4 ? "guest-bots" : ""), "legacy optional bots");
		Check(result && result->data.recentMoneyRecipients
			== (fields >= 5 ? "recipients" : ""), "legacy optional recipients");
		Check(!Read(bytes.left(bytes.size() - 1)), "truncated legacy field rejected");
	}
	const auto old = Read(Tagged(1));
	Check(old && !old->legacy && old->data.recentMoneyRecipients.isEmpty(),
		"old tagged format defaults to no recipients");
	Check(old && old->data.topPeers == "top" && old->data.recentPeers == "recent"
		&& old->data.topGuestChatBots == "guest-bots", "unknown tagged fields ignored");
	Check(!Read(Tagged(2)), "unsupported tagged version rejected");
	Check(!Read(QByteArray()), "empty input rejected");

	const auto expected = SearchSuggestions{
		.topPeers = QByteArray("top"),
		.recentPeers = QByteArray("recent"),
		.topGuestChatBots = QByteArray("guest-bots"),
		.recentMoneyRecipients = QByteArray("recipients\0data", 15),
	};
	auto bytes = QByteArray();
	auto stream = QDataStream(&bytes, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	WriteSearchSuggestions(stream, expected);
	const auto result = Read(bytes);
	Check(result && !result->legacy && result->data.topPeers == expected.topPeers
		&& result->data.recentPeers == expected.recentPeers
		&& result->data.topGuestChatBots == expected.topGuestChatBots
		&& result->data.recentMoneyRecipients == expected.recentMoneyRecipients,
		"tagged binary recipients round trip");
	Check(!Read(bytes.left(bytes.size() - 1)), "truncated tagged field rejected");
	std::cout << Checks << " checks, " << Failures << " failures\n";
	return Failures ? 1 : 0;
}
