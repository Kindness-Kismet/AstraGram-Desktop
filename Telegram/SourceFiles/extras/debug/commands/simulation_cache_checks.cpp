#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "extras/data/conversation_cache.h"
#include "extras/extras_settings.h"
#include "extras/debug/debug_login.h"
#include "data/data_channel.h"
#include "data/data_history_messages.h"
#include "data/data_messages.h"
#include "data/data_search_controller.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "settings.h"

#include <memory>

namespace ExtrasDebug::Commands {
namespace {

using Json = nlohmann::json;
using Query = Extras::HistoryCacheQuery;
using Slice = Extras::CachedHistorySlice;

std::weak_ptr<SimulationCacheCheck> RunningCheck;

struct SliceState {
	std::vector<MsgId> ids;
	int count = 0;
	bool atTop = false;
	bool atBottom = false;
	bool hit = false;
};

SliceState describeSlice(const std::optional<Slice> &slice) {
	auto result = SliceState();
	if (!slice) {
		return result;
	}
	result.hit = true;
	result.atTop = slice->atTop;
	result.atBottom = slice->atBottom;
	slice->messages.match([](const MTPDmessages_messagesNotModified &) {
	}, [&](const auto &data) {
		for (const auto &message : data.vmessages().v) {
			result.ids.push_back(IdFromMessage(message));
		}
		if constexpr (std::is_same_v<std::decay_t<decltype(data)>, MTPDmessages_messages>) {
			result.count = data.vmessages().v.size();
		} else {
			result.count = data.vcount().v;
		}
	});
	return result;
}

Json sliceNumbers(const SliceState &state) {
	auto ids = Json::array();
	for (const auto id : state.ids) {
		ids.push_back(id.bare);
	}
	return { { "messageIds", ids }, { "count", state.count },
		{ "hit", state.hit }, { "atTop", state.atTop },
		{ "atBottom", state.atBottom } };
}

MTPmessages_Messages response(const QVector<MTPMessage> &messages, int count) {
	return MTP_messages_messagesSlice(
		MTP_flags(0), MTP_int(count), MTPint(), MTPint(), MTPSearchPostsFlood(),
		MTP_vector<MTPMessage>(messages), MTP_vector<MTPForumTopic>(),
		MTP_vector<MTPChat>(), MTP_vector<MTPUser>());
}

} // namespace

// 固定样本只验证真实缓存边界，不建立联网视图或修改缓存内部范围。
class SimulationCacheCheck final
		: public std::enable_shared_from_this<SimulationCacheCheck> {
public:
	SimulationCacheCheck(not_null<Main::Session*> session, uint64 job)
	: _session(base::make_weak(session))
	, _job(job)
	, _peerBase(890000000 + job * 10)
	, _messageBase(3000000 + int(job) * 100)
	, _enabled(session->conversationCache()._enabled) {
		session->conversationCache()._enabled = true;
	}

	~SimulationCacheCheck() {
		finish(false);
	}

	void start() {
		const auto weak = weak_from_this();
		_session->lifetime().add([weak, job = _job] {
			if (const auto check = weak.lock()) {
				check->finish(false);
			}
			finishJob(job, false, "session closed");
		});
		previewDeleted();
	}

private:
	UserData *user(int index) {
		const auto result = _session->data().user(UserId(_peerBase + index));
		result->setName(u"缓存边界验证 %1"_q.arg(index + 1), {}, {}, {});
		result->setAccessHash(0);
		result->setFlags(UserDataFlag::MessageMoneyRestrictionsKnown);
		result->setBarSettings(PeerBarSettings());
		result->setLoadedStatus(PeerData::LoadedStatus::Full);
		return result.get();
	}

	ChannelData *forum() {
		const auto result = _session->data().channel(ChannelId(_peerBase + 3));
		result->setName(u"缓存话题边界验证"_q, {});
		result->setAccessHash(0);
		result->setFlags(ChannelDataFlag::Megagroup | ChannelDataFlag::Forum);
		result->setBarSettings(PeerBarSettings());
		result->setLoadedStatus(PeerData::LoadedStatus::Full);
		return result.get();
	}

	MTPMessage message(not_null<PeerData*> peer, int offset) const {
		return simulationTextMessage(peer, _session->userPeerId(),
			_messageBase + offset, u"固定缓存边界样本"_q);
	}

	Query latest(not_null<PeerData*> peer) const {
		auto query = Query();
		const auto request = Api::PrepareHistoryRequest(peer,
			ServerMaxMsgId - 1, Data::LoadDirection::Around, &query);
		auto actual = mtpBuffer();
		auto expected = mtpBuffer();
		request.write(actual);
		MTPmessages_GetHistory(peer->input(), MTP_int(0), MTP_int(0), MTP_int(0),
			MTP_int(50), MTP_int(0), MTP_int(0), MTP_long(0)).write(expected);
		_queryPass = _queryPass && actual == expected
			&& query.offsetId == MsgId(0) && query.addOffset == 0
			&& query.limit == 50 && query.latest && query.allowNearest;
		return query;
	}

	void read(Query query, Fn<void(std::optional<Slice>)> done) {
		const auto self = shared_from_this();
		_session->conversationCache().getHistory(query,
			[self, done = std::move(done)](std::optional<Slice> slice) {
				if (!self->_session || self->_finished) {
					return;
				}
				done(std::move(slice));
			});
	}

	void put(const Query &query, const QVector<MTPMessage> &messages,
			int count, bool refresh = true) {
		_session->conversationCache().putHistory(query, response(messages, count), refresh);
	}

	void setTop(not_null<PeerData*> peer, const MTPMessage &data) {
		const auto item = _session->data().addNewMessage(data, {}, NewMessageType::Last);
		Expects(item != nullptr);
		_session->data().history(peer)->applyDialogTopMessage(item->id);
	}

	std::optional<Api::HistoryResult> deliver(
			not_null<PeerData*> peer,
			const Query &query,
			const std::optional<Slice> &slice) {
		if (!slice) {
			return std::nullopt;
		}
		auto parsed = Api::ParseCachedHistoryResult(peer, ServerMaxMsgId - 1,
			Data::LoadDirection::Around, query, *slice);
		_session->data().history(peer)->messages().addSlice(
			std::vector<MsgId>(parsed.messageIds), parsed.noSkipRange,
			slice->atTop && slice->atBottom
				? std::optional<int>(parsed.fullCount) : std::nullopt);
		return parsed;
	}

	void record(int id, bool pass, Json numbers) {
		numbers["caseId"] = id;
		numbers["pass"] = pass;
		_results.push_back(std::move(numbers));
		_passed = _passed && pass;
	}

	void previewDeleted() {
		const auto peer = user(0);
		const auto top = MsgId(_messageBase + 3);
		const auto older = MsgId(_messageBase + 1);
		setTop(peer, message(peer, 3));
		auto query = latest(peer);
		read(query, [=, this](std::optional<Slice> slice) mutable {
			const auto before = describeSlice(slice);
			const auto parsed = deliver(peer, query, slice);
			const auto preview = _session->data().history(peer)->messages().snapshot({ top, 10, 10 });
			const auto previewPass = _queryPass && before.ids == std::vector<MsgId>{ top }
				&& before.atBottom && !before.atTop && before.count >= 2
				&& parsed && parsed->noSkipRange.from == top
				&& parsed->noSkipRange.till == ServerMaxMsgId
				&& !preview.count && preview.messageIds.contains(top);
			query.deleteTill = top;
			put(query, { message(peer, 1) }, 1);
			read(query, [=, this](std::optional<Slice> refreshed) {
				const auto after = describeSlice(refreshed);
				deliver(peer, query, refreshed);
				const auto history = _session->data().history(peer);
				const auto snapshot = history->messages().snapshot({ older, 10, 10 });
				const auto last = history->lastServerMessage();
				const auto pass = previewPass && after.ids == std::vector<MsgId>{ older }
					&& after.atTop && after.atBottom && after.count == 1
					&& last && last->id == older && !snapshot.messageIds.contains(top)
					&& snapshot.messageIds.contains(older) && snapshot.count == 1;
				auto numbers = sliceNumbers(after);
				numbers["previewId"] = top.bare;
				numbers["topId"] = last ? last->id.bare : 0;
				numbers["previewTotalUnknown"] = !preview.count;
				record(1, pass, std::move(numbers));
				unknownLast();
			});
		});
	}

	void unknownLast() {
		const auto peer = user(1);
		auto query = latest(peer);
		read(query, [=, this](std::optional<Slice>) mutable {
			query.deleteTill = MsgId();
			put(query, { message(peer, 13), message(peer, 12), message(peer, 11) }, 3, false);
			const auto history = _session->data().history(peer);
			history->restoreCachedChatListMetadata(1);
			read(query, [=, this](std::optional<Slice> initial) mutable {
				const auto before = describeSlice(initial);
				query.deleteTill = before.ids.empty() ? MsgId() : before.ids.front();
				put(query, { message(peer, 12), message(peer, 11) }, 2, false);
				read(query, [=, this](std::optional<Slice> refreshed) {
					const auto after = describeSlice(refreshed);
					const auto pass = !history->lastServerMessageKnown()
						&& before.ids == std::vector<MsgId>{ MsgId(_messageBase + 13),
							MsgId(_messageBase + 12), MsgId(_messageBase + 11) }
						&& query.deleteTill == MsgId(_messageBase + 13)
						&& after.ids == std::vector<MsgId>{ MsgId(_messageBase + 12), MsgId(_messageBase + 11) }
						&& after.count == 2 && after.atTop && after.atBottom;
					auto numbers = sliceNumbers(after);
					numbers["deleteTill"] = query.deleteTill.bare;
					numbers["lastUnknown"] = !history->lastServerMessageKnown();
					record(2, pass, std::move(numbers));
					newMessageAfterRequest();
				});
			});
		});
	}

	void newMessageAfterRequest() {
		const auto peer = user(2);
		auto query = latest(peer);
		read(query, [=, this](std::optional<Slice>) mutable {
			put(query, { message(peer, 23), message(peer, 22), message(peer, 21) }, 3);
			setTop(peer, message(peer, 23));
			query.deleteTill = MsgId(_messageBase + 23);
			const auto newest = message(peer, 24);
			const auto preserveDeleted = ExtrasSettings::getInstance().saveDeletedMessages();
			const auto item = _session->data().addNewMessage(newest, {}, NewMessageType::Unread);
			Expects(item != nullptr);
			_session->data().history(peer)->applyDialogTopMessage(item->id);
			put(query, { message(peer, 22), message(peer, 21) }, 2);
			read(query, [=, this](std::optional<Slice> refreshed) {
				const auto after = describeSlice(refreshed);
				const auto history = _session->data().history(peer);
				const auto last = history->lastServerMessage();
				const auto snapshot = history->messages().snapshot({ MsgId(_messageBase + 24), 10, 10 });
				const auto removed = _session->data().message(peer->id, MsgId(_messageBase + 23));
				const auto deletionApplied = preserveDeleted
					? removed && removed->isDeleted()
					: !removed && !snapshot.messageIds.contains(MsgId(_messageBase + 23));
				const auto pass = after.ids == std::vector<MsgId>{ MsgId(_messageBase + 24),
					MsgId(_messageBase + 22), MsgId(_messageBase + 21) }
					&& after.count >= 3 && last && last->id == MsgId(_messageBase + 24)
					&& deletionApplied
					&& _session->data().message(peer->id, MsgId(_messageBase + 24));
				auto numbers = sliceNumbers(after);
				numbers["deleteTill"] = query.deleteTill.bare;
				numbers["topId"] = last ? last->id.bare : 0;
				numbers["preserveDeleted"] = preserveDeleted;
				numbers["deletedRetained"] = removed != nullptr;
				numbers["deletedMarked"] = removed && removed->isDeleted();
				record(3, pass, std::move(numbers));
				subdomainIsolation();
			});
		});
	}

	void subdomainIsolation() {
		const auto peer = forum();
		const auto source = user(4);
		const auto top = MsgId(_messageBase + 33);
		setTop(peer, message(peer, 33));
		const auto main = latest(peer);
		read(main, [=, this](std::optional<Slice> preview) {
			deliver(peer, main, preview);
			auto topic = main;
			topic.key.topicRootId = MsgId(_messageBase + 30);
			topic.deleteTill = top;
			put(topic, { message(peer, 31) }, 1);
			read(topic, [=, this](std::optional<Slice> topicSlice) {
				auto sublist = main;
				sublist.key.sublistPeer = source->id;
				sublist.deleteTill = top;
				put(sublist, { message(peer, 32) }, 1);
				read(sublist, [=, this](std::optional<Slice> sublistSlice) {
					read(main, [=, this](std::optional<Slice> mainSlice) {
						const auto topicState = describeSlice(topicSlice);
						const auto sublistState = describeSlice(sublistSlice);
						const auto mainState = describeSlice(mainSlice);
						const auto history = _session->data().history(peer);
						const auto last = history->lastServerMessage();
						const auto snapshot = history->messages().snapshot({ top, 10, 10 });
						const auto pass = topicState.ids == std::vector<MsgId>{ MsgId(_messageBase + 31) }
							&& sublistState.ids == std::vector<MsgId>{ MsgId(_messageBase + 32) }
							&& topicState.atBottom && sublistState.atBottom
							&& mainState.ids == std::vector<MsgId>{ top }
							&& !mainState.atTop && mainState.atBottom
							&& last && last->id == top && snapshot.messageIds.contains(top);
						record(4, pass, { { "topic", sliceNumbers(topicState) },
							{ "sublist", sliceNumbers(sublistState) },
							{ "main", sliceNumbers(mainState) }, { "topId", last ? last->id.bare : 0 } });
						outOfOrderMessages();
					});
				});
			});
		});
	}

	void outOfOrderMessages() {
		const auto peer = user(5);
		const auto query = latest(peer);
		read(query, [=, this](std::optional<Slice>) {
			put(query, { message(peer, 52), message(peer, 51) }, 20);
			setTop(peer, message(peer, 55));
			read(query, [=, this](std::optional<Slice>) {
				auto &cache = _session->conversationCache();
				for (const auto offset : { 56, 54, 55, 54 }) {
					cache.messageReceived(message(peer, offset), NewMessageType::Unread);
				}
				const auto info = cache.debugHistoryInfo(query.key);
				auto around = query;
				around.latest = false;
				around.allowNearest = false;
				around.offsetId = MsgId(_messageBase + 54);
				around.addOffset = -around.limit;
				read(around, [=, this](std::optional<Slice> slice) {
					const auto state = describeSlice(slice);
					const auto pass = state.ids == std::vector<MsgId>{ MsgId(_messageBase + 56),
						MsgId(_messageBase + 55), MsgId(_messageBase + 54) }
						&& !state.atTop && state.atBottom && info.cachedMessages == 5
						&& info.ranges.size() == 2 && info.ranges[0].till == MsgId(_messageBase + 52)
						&& info.ranges[1].from == MsgId(_messageBase + 54)
						&& info.ranges[1].till == ServerMaxMsgId;
					auto numbers = sliceNumbers(state);
					numbers["cachedMessages"] = info.cachedMessages;
					numbers["ranges"] = info.ranges.size();
					record(5, pass, std::move(numbers));
					clearPendingConfirmation(peer);
				});
			});
		});
	}

	void clearPendingConfirmation(not_null<PeerData*> peer) {
		auto &cache = _session->conversationCache();
		const auto id = FullMsgId(peer->id, MsgId(_messageBase + 56));
		cache._pendingConfirmedMessages.emplace(id);
		cache._restoring = true;
		cache.messageReceived(message(peer, 56), NewMessageType::Existing);
		cache._restoring = false;
		const auto survivedRestore = cache._pendingConfirmedMessages.contains(id);
		const auto self = shared_from_this();
		cache.clearStorage(peer->id, [this, self, id, survivedRestore](Storage::Cache::Error error) {
			const auto cleared = !_session->conversationCache()._pendingConfirmedMessages.contains(id);
			record(6, survivedRestore && cleared && error.type == Storage::Cache::Error::Type::None,
				{ { "survivedRestore", survivedRestore }, { "cleared", cleared } });
			dynamicUpdatesDuringLoad();
		});
	}

	void dynamicUpdatesDuringLoad() {
		const auto peer = user(6);
		const auto query = latest(peer);
		const auto pollId = uint64(_messageBase + 61);
		const auto pageId = uint64(_messageBase + 62);
		const auto results = [](bool partial, int votes) {
			using Flag = MTPDpollResults::Flag;
			using AnswerFlag = MTPDpollAnswerVoters::Flag;
			return MTP_pollResults(MTP_flags(Flag::f_results | Flag::f_total_voters
				| (partial ? Flag::f_min : Flag(0))),
				MTP_vector<MTPPollAnswerVoters>({ MTP_pollAnswerVoters(
					MTP_flags(AnswerFlag::f_voters | (partial ? AnswerFlag(0) : AnswerFlag::f_chosen)),
					MTP_bytes("0"), MTP_int(votes), MTP_vector<MTPPeer>()) }),
				MTP_int(votes), MTP_vector<MTPPeer>(), MTPstring(),
				MTP_vector<MTPMessageEntity>(), MTPMessageMedia());
		};
		const auto poll = MTP_poll(MTP_long(pollId), MTP_flags(0),
			MTP_textWithEntities(MTP_string("Cache update check"), MTP_vector<MTPMessageEntity>()),
			MTP_vector<MTPPollAnswer>(), MTPint(), MTPint(), MTP_vector<MTPstring>(), MTP_long(0));
		const auto pollMessage = simulationMediaMessage(peer, _session->userPeerId(),
			int(pollId), {}, MTP_messageMediaPoll(MTP_flags(0), poll, results(false, 1), MTPMessageMedia()));
		const auto pageMessage = simulationMediaMessage(peer, _session->userPeerId(),
			int(pageId), {}, MTP_messageMediaWebPage(MTP_flags(0),
				MTP_webPagePending(MTP_flags(0), MTP_long(pageId), MTPstring(), MTP_int(1))));
		put(query, { pageMessage, pollMessage }, 2);
		auto &cache = _session->conversationCache();
		const auto pending = !cache.debugHistoryInfo(query.key).loaded;
		cache.pollUpdated(MTP_updateMessagePoll(MTP_flags(0), MTPPeer(), MTPint(), MTPint(),
			MTP_long(pollId), MTPPoll(), results(true, 3)).c_updateMessagePoll());
		cache.webPageUpdated(MTP_webPageEmpty(MTP_flags(0), MTP_long(pageId), MTPstring()));
		const auto reaction = [](bool partial, int count) {
			using Flag = MTPDmessageReactions::Flag;
			return MTP_messageReactions(MTP_flags(partial ? Flag::f_min : Flag(0)),
				MTP_vector<MTPReactionCount>({ MTP_reactionCount(
					MTP_flags(partial ? MTPDreactionCount::Flag(0) : MTPDreactionCount::Flag::f_chosen_order),
					MTP_int(0), MTP_reactionEmoji(MTP_string("\xf0\x9f\x91\x8d")), MTP_int(count)) }),
				MTP_vector<MTPMessagePeerReaction>(), MTP_vector<MTPMessageReactor>());
		};
		cache.reactionsUpdated(FullMsgId(peer->id, MsgId(pollId)), reaction(false, 2));
		cache.reactionsUpdated(FullMsgId(peer->id, MsgId(pollId)), reaction(true, 4));
		read(query, [=, this](std::optional<Slice> slice) {
			auto pollPass = false;
			auto pagePass = false;
			auto reactionPass = false;
			if (slice && slice->messages.type() == mtpc_messages_messagesSlice) {
				for (const auto &message : slice->messages.c_messages_messagesSlice().vmessages().v) {
					const auto &data = message.c_message();
					if (data.vid().v == pollId) {
						const auto &poll = data.vmedia()->c_messageMediaPoll().vresults().data();
						pollPass = poll.vtotal_voters()->v == 3 && poll.vresults()->v.front().data().is_chosen();
						if (const auto reactions = data.vreactions()) {
							const auto &count = reactions->data().vresults().v.front().data();
							reactionPass = count.vcount().v == 4 && count.vchosen_order();
						}
					} else if (data.vid().v == pageId) {
						pagePass = data.vmedia()->c_messageMediaWebPage().vwebpage().type() == mtpc_webPageEmpty;
					}
				}
			}
			record(7, pending && pollPass && pagePass && reactionPass,
				{ { "startedWhileLoading", pending }, { "pollChoicePreserved", pollPass },
					{ "webPageUpdated", pagePass }, { "reactionChoicePreserved", reactionPass } });
			sparsePollUpdates(peer, query, pollId);
		});
	}

	void sparsePollUpdates(not_null<PeerData*> peer, Query query, uint64 pollId) {
		using Flag = MTPDpollResults::Flag;
		using AnswerFlag = MTPDpollAnswerVoters::Flag;
		const auto partial = MTP_pollResults(MTP_flags(Flag::f_min | Flag::f_results),
			MTP_vector<MTPPollAnswerVoters>({ MTP_pollAnswerVoters(MTP_flags(AnswerFlag::f_correct),
				MTP_bytes("0"), MTPint(), MTP_vector<MTPPeer>()) }),
			MTPint(), MTP_vector<MTPPeer>(), MTPstring(), MTP_vector<MTPMessageEntity>(), MTPMessageMedia());
		const auto added = MTP_pollResults(MTP_flags(Flag::f_results | Flag::f_total_voters),
			MTP_vector<MTPPollAnswerVoters>({ MTP_pollAnswerVoters(MTP_flags(AnswerFlag::f_voters),
				MTP_bytes("1"), MTP_int(2), MTP_vector<MTPPeer>()) }),
			MTP_int(5), MTP_vector<MTPPeer>(), MTPstring(), MTP_vector<MTPMessageEntity>(), MTPMessageMedia());
		for (const auto &results : { partial, added }) {
			_session->conversationCache().pollUpdated(MTP_updateMessagePoll(MTP_flags(0),
				MTPPeer(), MTPint(), MTPint(), MTP_long(pollId), MTPPoll(), results).c_updateMessagePoll());
		}
		read(query, [=, this](std::optional<Slice> slice) {
			auto preserved = false;
			if (slice) {
				for (const auto &message : slice->messages.c_messages_messagesSlice().vmessages().v) {
					if (IdFromMessage(message).bare != pollId) continue;
					const auto &results = message.c_message().vmedia()->c_messageMediaPoll().vresults().data();
					const auto answers = results.vresults();
					if (answers && answers->v.size() == 2) {
						const auto &first = answers->v.front().data();
						preserved = first.is_chosen() && first.is_correct()
							&& first.vvoters() && first.vvoters()->v == 3
							&& answers->v.back().data().vvoters()->v == 2;
					}
				}
			}
			record(8, preserved, { { "omittedAnswersAndCountsPreserved", preserved } });
			const auto empty = MTP_pollResults(MTP_flags(Flag::f_total_voters),
				MTP_vector<MTPPollAnswerVoters>(), MTP_int(0), MTP_vector<MTPPeer>(),
				MTPstring(), MTP_vector<MTPMessageEntity>(), MTPMessageMedia());
			_session->conversationCache().pollUpdated(MTP_updateMessagePoll(MTP_flags(0),
				MTPPeer(), MTPint(), MTPint(), MTP_long(pollId), MTPPoll(), empty).c_updateMessagePoll());
			_session->conversationCache().reactionsUpdated(FullMsgId(peer->id, MsgId(pollId)),
				MTP_messageReactions(MTP_flags(0), MTP_vector<MTPReactionCount>(),
					MTP_vector<MTPMessagePeerReaction>(), MTP_vector<MTPMessageReactor>()));
			read(query, [=, this](std::optional<Slice> slice) {
				auto cleared = false;
				if (slice) {
					for (const auto &message : slice->messages.c_messages_messagesSlice().vmessages().v) {
						if (IdFromMessage(message).bare != pollId) continue;
						const auto &data = message.c_message();
						const auto &results = data.vmedia()->c_messageMediaPoll().vresults().data();
						cleared = results.vtotal_voters() && results.vtotal_voters()->v == 0
							&& results.vresults() && ranges::none_of(results.vresults()->v, [](const auto &answer) {
								return answer.data().is_chosen();
							}) && data.vreactions() && data.vreactions()->data().vresults().v.isEmpty();
					}
				}
				record(9, cleared, { { "withdrawnVotesAndReactionsCleared", cleared } });
				finish(_passed);
			});
		});
	}

	void finish(bool pass) {
		if (_finished) {
			return;
		}
		_finished = true;
		if (const auto session = _session.get()) {
			auto &cache = session->conversationCache();
			cache._enabled = _enabled;
			if (!_enabled) {
				cache._saveTimer.cancel();
			}
		}
		finishJob(_job, pass, { { "cases", _results }, { "pass", pass },
			{ "enabledRestored", bool(_session) }, { "latestRequestPass", _queryPass },
			{ "networkRequested", false } });
	}

	const base::weak_ptr<Main::Session> _session;
	const uint64 _job;
	const uint64 _peerBase;
	const int _messageBase;
	const bool _enabled;
	Json _results = Json::array();
	bool _passed = true;
	mutable bool _queryPass = true;
	bool _finished = false;
};

const HandlerMap &simulationCacheHandlers() {
	static const auto result = HandlerMap{
		{ u"simulation.cache-history-check"_q, [](const QStringList &args) {
			if (!args.empty()) {
				return Result::Err(u"usage: simulation.cache-history-check"_q);
			}
			const auto session = ActiveSession();
			if (!cDebugProfile() || !session || !isSimulationSession(session)) {
				return Result::Err(u"an isolated simulation profile is required"_q);
			}
			if (!RunningCheck.expired()) {
				return Result::Err(u"cache history check is already running"_q);
			}
			const auto job = beginJob("simulation-cache-history-check");
			const auto check = std::make_shared<SimulationCacheCheck>(session, job);
			RunningCheck = check;
			check->start();
			return jobStarted(job);
		} },
	};
	return result;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
