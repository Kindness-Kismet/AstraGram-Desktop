/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_search_filter.h"
#include "base/flat_map.h"
#include "base/flat_set.h"
#include "base/qt/qt_compare.h"
#include "base/timer.h"
#include "data/data_message_reaction_id.h"

class HistoryItem;
class History;
class PeerData;

namespace Data {
struct ReactionId;
} // namespace Data

namespace Api {

struct FoundMessages {
	int total = -1;
	MessageIdsList messages;
	QString nextToken;
	bool finished = false;
	bool totalOnly = false;
	bool failed = false;
};

class MessagesSearch final {
public:
	struct Request {
		QString query;
		PeerData *from = nullptr;
		std::vector<Data::ReactionId> tags;
		MsgId topMsgId;
		SearchFilter filter = SearchFilter::NoFilter;
		PeerData *savedPeer = nullptr;

		friend inline bool operator==(
			const Request &,
			const Request &) = default;
		friend inline auto operator<=>(
			const Request &,
			const Request &) = default;
	};

	explicit MessagesSearch(not_null<History*> history);
	~MessagesSearch();

	void searchMessages(Request request);
	void searchMore();

	[[nodiscard]] rpl::producer<FoundMessages> messagesFounds() const;
	[[nodiscard]] static bool matchesLocalFilter(
		not_null<HistoryItem*> item,
		SearchFilter filter);

private:
	friend class MessagesSearchMerged;

	enum class Mode : uchar {
		Normal,
		Text,
		FromAndText,
		FromAndType,
	};

	using TLMessages = MTPmessages_Messages;
	void searchRequest();
	void searchCandidateProbe();
	void searchCandidatePage();
	void searchReceived(
		const TLMessages &result,
		mtpRequestId requestId,
		const QString &nextToken);
	void searchProbeReceived(
		bool fromSide,
		const TLMessages &result,
		mtpRequestId requestId,
		const QString &nextToken);
	void searchProbeFailed(bool fromSide);
	void searchCandidateReceived(
		const TLMessages &result,
		mtpRequestId requestId,
		const QString &nextToken);
	void searchCandidateFailed();
	void searchCandidateRefill();
	void candidateSideFinished();
	void maybeChooseCandidate();
	void maybeFireCandidateOutput(bool force);
	[[nodiscard]] bool matchesSender(not_null<HistoryItem*> item) const;
	[[nodiscard]] static bool matchesText(not_null<HistoryItem*> item);

	void cancelCurrentRequests();
	void cancel();

	const not_null<History*> _history;

	base::flat_map<QString, TLMessages> _cacheOfStartByToken;

	Request _request;
	Mode _mode = Mode::Normal;
	int _generation = 0;
	MsgId _offsetId;

	int _searchInHistoryRequest = 0; // Not real mtpRequestId.
	mtpRequestId _requestId = 0;

	bool _probeDone = false;
	bool _probeFromFinished = false;
	bool _probeTypeFinished = false;
	int _probeHistoryA = 0; // Not real mtpRequestId.
	int _probeHistoryB = 0; // Not real mtpRequestId.
	mtpRequestId _probeRequestA = 0;
	mtpRequestId _probeRequestB = 0;
	int _probeTotalA = -1;
	int _probeTotalB = -1;

	bool _candidateFrom = false;
	bool _candidateStarted = false;
	bool _candidateFinished = false;
	MsgId _candidateOffset = 0;
	int _candidateTotal = -1;
	int _discardedCount = 0;
	int _foundCount = 0;
	int _lastReportedTotal = -2;
	base::flat_set<MsgId> _candidateSeen;
	MessageIdsList _pendingMessages;
	TLMessages _probeResultA;
	TLMessages _probeResultB;
	base::Timer _refillTimer;
	bool _finished = false;
	bool _failed = false;

	rpl::event_stream<FoundMessages> _messagesFounds;

};

} // namespace Api
