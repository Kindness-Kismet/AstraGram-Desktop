/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_messages_search.h"
#include "rpl/never.h"

class History;
class PeerData;

namespace Data {
struct ReactionId;
} // namespace Data

namespace Api {

// 本地来源返回完整结果，顺序与原生搜索一致：最新消息在前。
struct MessagesSearchLocalSource {
	Fn<FoundMessages(const MessagesSearch::Request&)> search;
	rpl::producer<> changes = rpl::never<>();
};

// Search in both of history and migrated history, if it exists.
class MessagesSearchMerged final {
public:
	using Request = MessagesSearch::Request;
	using CachedRequests = base::flat_set<Request>;
	using LocalSource = MessagesSearchLocalSource;

	MessagesSearchMerged(not_null<History*> history, LocalSource local = {});

	void search(const Request &search);
	void searchMore();
	void disableMigrated();

	[[nodiscard]] const FoundMessages &messages() const;
	[[nodiscard]] const Request &request() const;

	[[nodiscard]] rpl::producer<> newFounds() const;
	[[nodiscard]] rpl::producer<> nextFounds() const;
	[[nodiscard]] rpl::producer<> updatedFounds() const;

private:
	void clear();
	void addFound(const FoundMessages &data);
	void applyApiFound(const FoundMessages &data);
	void applyMigratedFound(const FoundMessages &data);
	void updateCombinedState(const FoundMessages &data);
	void startMigrated();
	void fireFound();
	void refreshLocal();

	MessagesSearch _apiSearch;
	LocalSource _localSource;
	Request _request;

	std::optional<MessagesSearch> _migratedSearch;
	FoundMessages _concatedFound;

	bool _apiFinished = false;
	bool _apiFailed = false;
	bool _migratedStarted = false;
	bool _migratedFinished = false;
	bool _migratedFailed = false;
	bool _outputStarted = false;
	int _apiTotal = -1;
	int _migratedTotal = -1;

	rpl::event_stream<> _newFounds;
	rpl::event_stream<> _nextFounds;
	rpl::event_stream<> _updatedFounds;

	rpl::lifetime _lifetime;

};

} // namespace Api
