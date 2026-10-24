/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_messages_search.h"

#include "apiwrap.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_channel.h"
#include "data/data_histories.h"
#include "data/data_media_types.h"
#include "data/data_message_reaction_id.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "storage/storage_shared_media.h"
#include "ui/text/text.h"

#include <algorithm>

#include "styles/style_dialogs.h"

namespace Api {
namespace {

constexpr auto kSearchPerPage = 25;
constexpr auto kRefillDelay = crl::time(300);

[[nodiscard]] MessageIdsList HistoryItemsFromTL(
		not_null<Data::Session*> data,
		const QVector<MTPMessage> &messages) {
	auto result = MessageIdsList();
	for (const auto &message : messages) {
		const auto peerId = PeerFromMessage(message);
		if (data->peerLoaded(peerId)) {
			if (DateFromMessage(message)) {
				const auto item = data->addNewMessage(
					message,
					MessageFlags(),
					NewMessageType::Existing);
				result.push_back(item->fullId());
			}
		} else {
			LOG(("API Error: a search results with not loaded peer %1"
				).arg(peerId.value));
		}
	}
	return result;
}

[[nodiscard]] QString RequestToToken(
		const MessagesSearch::Request &request) {
	auto result = request.query;
	if (request.from) {
		result += '\n' + QString::number(request.from->id.value);
	}
	if (request.savedPeer) {
		result += u"\nsaved:"_q + QString::number(
			request.savedPeer->id.value);
	}
	if (request.topMsgId) {
		result += u"\ntop:"_q + QString::number(request.topMsgId.bare);
	}
	for (const auto &tag : request.tags) {
		result += '\n';
		if (const auto customId = tag.custom()) {
			result += u"custom"_q + QString::number(customId);
		} else {
			result += u"emoji"_q + tag.emoji();
		}
	}
	const auto addFilter = [&](SearchFilter filter) {
		switch (filter) {
		case SearchFilter::NoFilter: break;
		case SearchFilter::Pinned: result += u"\npinned"_q; break;
		case SearchFilter::Text: result += u"\ntext"_q; break;
		case SearchFilter::Photo: result += u"\nphoto"_q; break;
		case SearchFilter::Video: result += u"\nvideo"_q; break;
		case SearchFilter::Voice: result += u"\nvoice"_q; break;
		case SearchFilter::Round: result += u"\nround"_q; break;
		case SearchFilter::File: result += u"\nfile"_q; break;
		case SearchFilter::Music: result += u"\nmusic"_q; break;
		case SearchFilter::Gif: result += u"\ngif"_q; break;
		}
	};
	addFilter(request.filter);
	return result;
}

[[nodiscard]] FoundMessages ParseFoundMessages(
		not_null<Data::Session*> owner,
		not_null<PeerData*> peer,
		bool applyData,
		const MTPmessages_Messages &result,
		const QString &nextToken = QString()) {
	auto found = result.match([&](const MTPDmessages_messages &data) {
		if (applyData) {
			owner->processUsers(data.vusers());
			owner->processChats(data.vchats());
			peer->processTopics(data.vtopics());
		}
		auto items = HistoryItemsFromTL(owner, data.vmessages().v);
		const auto total = int(data.vmessages().v.size());
		return FoundMessages{ total, std::move(items), nextToken, true };
	}, [&](const MTPDmessages_messagesSlice &data) {
		if (applyData) {
			owner->processUsers(data.vusers());
			owner->processChats(data.vchats());
			peer->processTopics(data.vtopics());
		}
		auto items = HistoryItemsFromTL(owner, data.vmessages().v);
		// data.vnext_rate() is used only in global search.
		const auto total = int(data.vcount().v);
		return FoundMessages{ total, std::move(items), nextToken };
	}, [&](const MTPDmessages_channelMessages &data) {
		if (applyData) {
			owner->processUsers(data.vusers());
			owner->processChats(data.vchats());
			if (const auto channel = peer->asChannel()) {
				channel->ptsReceived(data.vpts().v);
			} else {
				LOG(("API Error: "
					"received messages.channelMessages when no channel "
					"was passed!"));
			}
			peer->processTopics(data.vtopics());
		}
		auto items = HistoryItemsFromTL(owner, data.vmessages().v);
		const auto total = int(data.vcount().v);
		return FoundMessages{ total, std::move(items), nextToken };
	}, [&](const MTPDmessages_messagesNotModified &) {
		return FoundMessages{ .nextToken = nextToken, .finished = true };
	});
	return found;
}

[[nodiscard]] MTPMessagesFilter PrepareFilter(SearchFilter filter) {
	switch (filter) {
	case SearchFilter::Pinned:
		return MTP_inputMessagesFilterPinned();
	case SearchFilter::Text:
		return MTP_inputMessagesFilterEmpty();
	case SearchFilter::Photo:
		return MTP_inputMessagesFilterPhotos();
	case SearchFilter::Video:
		return MTP_inputMessagesFilterVideo();
	case SearchFilter::Voice:
		return MTP_inputMessagesFilterVoice();
	case SearchFilter::Round:
		return MTP_inputMessagesFilterRoundVideo();
	case SearchFilter::File:
		return MTP_inputMessagesFilterDocument();
	case SearchFilter::Music:
		return MTP_inputMessagesFilterMusic();
	case SearchFilter::Gif:
		return MTP_inputMessagesFilterGif();
	case SearchFilter::NoFilter:
		return MTP_inputMessagesFilterEmpty();
	}
	return MTP_inputMessagesFilterEmpty();
}

[[nodiscard]] MTPmessages_Search PrepareSearchRequest(
		not_null<History*> history,
		const MessagesSearch::Request &request,
		PeerData *from,
		SearchFilter filter,
		MsgId offsetId) {
	using Flag = MTPmessages_Search::Flag;
	const auto fromPeer = history->peer->isUser() ? nullptr : from;
	const auto savedPeer = history->peer->isSelf()
		? (request.savedPeer ? request.savedPeer : from)
		: nullptr;
	return MTPmessages_Search(
		MTP_flags((fromPeer ? Flag::f_from_id : Flag())
			| (savedPeer ? Flag::f_saved_peer_id : Flag())
			| (request.topMsgId ? Flag::f_top_msg_id : Flag())
			| (request.tags.empty()
				? Flag()
				: Flag::f_saved_reaction)),
		history->peer->input(),
		MTP_string(request.query),
		(fromPeer ? fromPeer->input() : MTP_inputPeerEmpty()),
		(savedPeer ? savedPeer->input() : MTP_inputPeerEmpty()),
		MTP_vector_from_range(
			request.tags | ranges::views::transform(Data::ReactionToMTP)),
		MTP_int(request.topMsgId),
		PrepareFilter(filter),
		MTP_int(0),
		MTP_int(0),
		MTP_int(offsetId),
		MTP_int(0),
		MTP_int(kSearchPerPage),
		MTP_int(0),
		MTP_int(0),
		MTP_long(0));
}

[[nodiscard]] MTPmessages_GetReplies PrepareRepliesRequest(
		not_null<History*> history,
		const MessagesSearch::Request &request,
		MsgId offsetId) {
	return MTPmessages_GetReplies(
		history->peer->input(),
		MTP_int(request.topMsgId),
		MTP_int(offsetId),
		MTP_int(0),
		MTP_int(0),
		MTP_int(kSearchPerPage),
		MTP_int(0),
		MTP_int(0),
		MTP_long(0));
}

[[nodiscard]] bool UseRepliesForTextCandidate(
		const MessagesSearch::Request &request) {
	return request.filter == SearchFilter::Text
		&& request.query.isEmpty()
		&& !request.from
		&& request.topMsgId
		&& request.tags.empty()
		&& !request.savedPeer;
}

[[nodiscard]] bool IsDisplayedAsAnimatedEmoji(
		not_null<HistoryItem*> item) {
	const auto text = Ui::Text::String(
		st::defaultTextStyle,
		item->originalText());
	return Core::App().settings().largeEmoji()
		&& (text.isIsolatedEmoji() || text.isOnlyCustomEmoji());
}

} // namespace

MessagesSearch::MessagesSearch(not_null<History*> history)
: _history(history) {
}

MessagesSearch::~MessagesSearch() {
	cancelCurrentRequests();
}

void MessagesSearch::cancelCurrentRequests() {
	_refillTimer.cancel();
	_history->owner().histories().cancelRequest(
		base::take(_searchInHistoryRequest));
	_history->owner().histories().cancelRequest(
		base::take(_probeHistoryA));
	_history->owner().histories().cancelRequest(
		base::take(_probeHistoryB));
	_history->session().api().request(base::take(_requestId)).cancel();
	_history->session().api().request(base::take(_probeRequestA)).cancel();
	_history->session().api().request(base::take(_probeRequestB)).cancel();
}

void MessagesSearch::cancel() {
	++_generation;
	cancelCurrentRequests();
}

void MessagesSearch::searchMessages(Request request) {
	++_generation;
	cancelCurrentRequests();
	_request = std::move(request);
	_mode = Mode::Normal;
	_candidateFrom = false;
	_candidateStarted = false;
	_candidateFinished = false;
	_candidateOffset = 0;
	_candidateTotal = -1;
	_discardedCount = 0;
	_foundCount = 0;
	_lastReportedTotal = -2;
	_candidateSeen.clear();
	_pendingMessages.clear();
	_probeDone = false;
	_probeFromFinished = false;
	_probeTypeFinished = false;
	_probeTotalA = -1;
	_probeTotalB = -1;
	_probeResultA = TLMessages();
	_probeResultB = TLMessages();
	_finished = false;
	_failed = false;
	_offsetId = {};
	if (_request.filter == SearchFilter::Text) {
		_mode = _request.from ? Mode::FromAndText : Mode::Text;
		_candidateFrom = (_request.from != nullptr);
	} else if (IsMediaFilter(_request.filter) && _request.from) {
		_mode = Mode::FromAndType;
	} else {
		searchRequest();
	}
	if (_mode == Mode::Text || _mode == Mode::FromAndText) {
		_candidateStarted = true;
		searchCandidatePage();
	} else if (_mode == Mode::FromAndType) {
		searchCandidateProbe();
	}
}

void MessagesSearch::searchMore() {
	if (_searchInHistoryRequest || _requestId || _finished
		|| _probeRequestA || _probeRequestB || _refillTimer.isActive()
		|| _failed) {
		return;
	}
	if (_mode == Mode::Normal) {
		searchRequest();
	} else if (_candidateStarted && !_candidateFinished) {
		searchCandidatePage();
	}
}

void MessagesSearch::searchRequest() {
	const auto nextToken = RequestToToken(_request);
	if (!_offsetId) {
		const auto it = _cacheOfStartByToken.find(nextToken);
		if (it != end(_cacheOfStartByToken)) {
			_requestId = 0;
			searchReceived(it->second, _requestId, nextToken);
			return;
		}
	}
	auto callback = [=](Fn<void()> finish) {
		const auto from = _request.from;
		const auto generation = _generation;
		_requestId = _history->session().api().request(
			PrepareSearchRequest(
				_history,
				_request,
				from,
				_request.filter,
				_offsetId)).done([=](const TLMessages &result, mtpRequestId id) {
			if (generation != _generation) {
				finish();
				return;
			}
			_searchInHistoryRequest = 0;
			searchReceived(result, id, nextToken);
			finish();
		}).fail([=](const MTP::Error &error, mtpRequestId id) {
			if (generation != _generation) {
				finish();
				return;
			}
			_searchInHistoryRequest = 0;

			if (_requestId == id) {
				_requestId = 0;
			}
			if (error.type() == u"SEARCH_QUERY_EMPTY"_q) {
				_finished = true;
				_messagesFounds.fire({ 0, {}, nextToken, true });
			} else {
				_failed = true;
				_messagesFounds.fire({
					.nextToken = nextToken,
					.failed = true,
				});
			}

			finish();
		}).send();
		return _requestId;
	};
	_searchInHistoryRequest = _history->owner().histories().sendRequest(
		_history,
		Data::Histories::RequestType::History,
		std::move(callback));
}

void MessagesSearch::searchReceived(
		const TLMessages &result,
		mtpRequestId requestId,
		const QString &nextToken) {
	if (requestId != _requestId) {
		return;
	}
	auto &owner = _history->owner();
	auto found = ParseFoundMessages(
		&owner,
		_history->peer,
		requestId != 0,
		result,
		nextToken);
	if (!_offsetId) {
		_cacheOfStartByToken.emplace(nextToken, result);
	}
	_requestId = 0;
	if (found.messages.empty()) {
		_finished = true;
	} else {
		const auto next = found.messages.back().msg;
		if (_offsetId && next >= _offsetId) {
			_finished = true;
		} else {
			_offsetId = next;
		}
	}
	found.finished = found.finished || _finished;
	_finished = found.finished;
	_messagesFounds.fire(std::move(found));
}

void MessagesSearch::searchCandidateProbe() {
	const auto generation = _generation;
	const auto nextToken = RequestToToken(_request);
	const auto send = [&](bool fromSide) {
		const auto historyRequest = _history->owner().histories().sendRequest(
			_history,
			Data::Histories::RequestType::History,
			[=](Fn<void()> finish) mutable {
				const auto from = fromSide ? _request.from : nullptr;
				const auto filter = fromSide
					? SearchFilter::NoFilter
					: _request.filter;
				const auto request = _history->session().api().request(
					PrepareSearchRequest(
						_history,
						_request,
						from,
						filter,
						0))
					.done([=](const TLMessages &result, mtpRequestId id) {
						if (generation == _generation) {
							if (fromSide) {
								_probeHistoryA = 0;
							} else {
								_probeHistoryB = 0;
							}
							searchProbeReceived(
								fromSide,
								result,
								id,
								nextToken);
						}
						finish();
					})
					.fail([=](const MTP::Error &, mtpRequestId) {
						if (generation == _generation) {
							if (fromSide) {
								_probeHistoryA = 0;
							} else {
								_probeHistoryB = 0;
							}
							searchProbeFailed(fromSide);
						}
						finish();
					})
					.send();
				if (fromSide) {
					_probeRequestA = request;
				} else {
					_probeRequestB = request;
				}
				return request;
			});
		if (fromSide) {
			_probeHistoryA = historyRequest;
		} else {
			_probeHistoryB = historyRequest;
		}
	};
	send(true);
	send(false);
}

void MessagesSearch::searchProbeReceived(
		bool fromSide,
		const TLMessages &result,
		mtpRequestId requestId,
		const QString &nextToken) {
	auto &requestSlot = fromSide ? _probeRequestA : _probeRequestB;
	if (requestId != requestSlot) {
		return;
	}
	requestSlot = 0;
	const auto found = ParseFoundMessages(
		&_history->owner(),
		_history->peer,
		true,
		result,
		nextToken);
	if (fromSide) {
		_probeFromFinished = true;
		_probeTotalA = found.total;
		_probeResultA = result;
	} else {
		_probeTypeFinished = true;
		_probeTotalB = found.total;
		_probeResultB = result;
	}
	maybeChooseCandidate();
}

void MessagesSearch::searchProbeFailed(
		bool fromSide) {
	if (_failed) {
		return;
	}
	(fromSide ? _probeRequestA : _probeRequestB) = 0;
	_failed = true;
	cancelCurrentRequests();
	_messagesFounds.fire({
		.nextToken = RequestToToken(_request),
		.failed = true,
	});
}

void MessagesSearch::maybeChooseCandidate() {
	if (_probeDone || !_probeFromFinished || !_probeTypeFinished) {
		return;
	}
	_probeDone = true;
	_candidateFrom = (_probeTotalB < 0)
		|| (_probeTotalA >= 0 && _probeTotalA <= _probeTotalB);
	_candidateTotal = _candidateFrom ? _probeTotalA : _probeTotalB;
	_candidateStarted = true;
	if (_candidateTotal < 0) {
		_candidateFinished = true;
		_finished = true;
		_messagesFounds.fire({ 0, {}, RequestToToken(_request), true });
		return;
	}
	searchCandidateReceived(
		_candidateFrom ? _probeResultA : _probeResultB,
		0,
		RequestToToken(_request));
}

void MessagesSearch::searchCandidatePage() {
	if (_candidateFinished || _searchInHistoryRequest || _requestId) {
		return;
	}
	const auto generation = _generation;
	const auto nextToken = RequestToToken(_request);
	const auto from = _candidateFrom ? _request.from : nullptr;
	const auto filter = _candidateFrom
		? SearchFilter::NoFilter
		: _request.filter;
	_searchInHistoryRequest = _history->owner().histories().sendRequest(
		_history,
		Data::Histories::RequestType::History,
		[=](Fn<void()> finish) mutable {
			const auto done = [=](
					const TLMessages &result,
					mtpRequestId id) {
				if (generation == _generation) {
					_searchInHistoryRequest = 0;
					searchCandidateReceived(result, id, nextToken);
				}
				finish();
			};
			const auto fail = [=](const MTP::Error &, mtpRequestId) {
				if (generation == _generation) {
					_searchInHistoryRequest = 0;
					searchCandidateFailed();
				}
				finish();
			};
			const auto send = [&](auto request) {
				return _history->session().api().request(
					std::move(request)
				).done(done).fail(fail).send();
			};
			_requestId = UseRepliesForTextCandidate(_request)
				? send(PrepareRepliesRequest(
					_history,
					_request,
					_candidateOffset))
				: send(PrepareSearchRequest(
					_history,
					_request,
					from,
					filter,
					_candidateOffset));
			return _requestId;
		});
}

void MessagesSearch::searchCandidateReceived(
		const TLMessages &result,
		mtpRequestId requestId,
		const QString &nextToken) {
	if (requestId && requestId != _requestId) {
		return;
	}
	_requestId = 0;
	const auto found = ParseFoundMessages(
		&_history->owner(),
		_history->peer,
		requestId != 0,
		result,
		nextToken);
	if (found.total >= 0 && _candidateTotal < 0) {
		_candidateTotal = found.total;
	}
	if (found.messages.empty()) {
		candidateSideFinished();
		maybeFireCandidateOutput(true);
		return;
	}
	const auto previousOffset = _candidateOffset;
	const auto nextOffset = found.messages.back().msg;
	if (previousOffset && nextOffset >= previousOffset) {
		candidateSideFinished();
	} else {
		_candidateOffset = nextOffset;
	}

	for (const auto &id : found.messages) {
		if (!_candidateSeen.emplace(id.msg).second) {
			continue;
		}
		const auto item = _history->owner().message(id);
		const auto senderMatches = item && matchesSender(item);
		const auto matchesFilter = item
			&& matchesLocalFilter(item, _request.filter);
		const auto matchesOther = item && ((_mode == Mode::FromAndType)
			? (_candidateFrom ? matchesFilter : senderMatches)
			: matchesText(item));
		if (matchesOther && senderMatches && matchesFilter) {
			_pendingMessages.push_back(id);
			++_foundCount;
		} else {
			++_discardedCount;
		}
	}
	if (found.finished) {
		_candidateFinished = true;
		_finished = true;
	}
	maybeFireCandidateOutput(_candidateFinished);
}

void MessagesSearch::searchCandidateFailed() {
	if (_failed) {
		return;
	}
	_requestId = 0;
	_failed = true;
	_refillTimer.cancel();
	maybeFireCandidateOutput(true);
}

void MessagesSearch::candidateSideFinished() {
	_candidateFinished = true;
	_finished = true;
	_refillTimer.cancel();
}

void MessagesSearch::searchCandidateRefill() {
	if (!_candidateFinished) {
		searchCandidatePage();
	}
}

void MessagesSearch::maybeFireCandidateOutput(bool force) {
	const auto total = std::max(
		(_candidateTotal < 0)
			? -1
			: _candidateTotal - _discardedCount,
		_foundCount);
	if (!force && _pendingMessages.size() < 5) {
		if (total != _lastReportedTotal) {
			_lastReportedTotal = total;
			_messagesFounds.fire({
				.total = total,
				.nextToken = RequestToToken(_request),
				.totalOnly = true,
			});
		}
		if (!_candidateFinished && !_refillTimer.isActive()) {
			const auto generation = _generation;
			_refillTimer.setCallback([=] {
				if (generation == _generation) {
					searchCandidateRefill();
				}
			});
			_refillTimer.callOnce(kRefillDelay);
		}
		return;
	}
	if (_pendingMessages.empty() && !_candidateFinished && !_failed) {
		return;
	}
	auto messages = base::take(_pendingMessages);
	_lastReportedTotal = total;
	_messagesFounds.fire({
		.total = total,
		.messages = std::move(messages),
		.nextToken = RequestToToken(_request),
		.finished = _candidateFinished,
		.failed = _failed,
	});
}

bool MessagesSearch::matchesSender(not_null<HistoryItem*> item) const {
	if (!_request.from) {
		return true;
	} else if (_history->peer->isSelf()) {
		return item->sublistPeerId() == _request.from->id;
	}
	return item->from() == _request.from;
}

bool MessagesSearch::matchesText(not_null<HistoryItem*> item) {
	return !item->isService()
		&& !item->originalText().text.isEmpty()
		&& !IsDisplayedAsAnimatedEmoji(item)
		&& (!item->media() || item->media()->webpage());
}

bool MessagesSearch::matchesLocalFilter(
		not_null<HistoryItem*> item,
		SearchFilter filter) {
	using Type = Storage::SharedMediaType;
	const auto types = item->sharedMediaTypes();
	switch (filter) {
	case SearchFilter::NoFilter: return true;
	case SearchFilter::Pinned: return item->isPinned();
	case SearchFilter::Text: return matchesText(item);
	case SearchFilter::Photo: return types.test(Type::Photo);
	case SearchFilter::Video: return types.test(Type::Video);
	case SearchFilter::Voice: return types.test(Type::VoiceFile);
	case SearchFilter::Round: return types.test(Type::RoundFile);
	case SearchFilter::File: return types.test(Type::File);
	case SearchFilter::Music: return types.test(Type::MusicFile);
	case SearchFilter::Gif: return types.test(Type::GIF);
	}
	return false;
}

rpl::producer<FoundMessages> MessagesSearch::messagesFounds() const {
	return _messagesFounds.events();
}

} // namespace Api
