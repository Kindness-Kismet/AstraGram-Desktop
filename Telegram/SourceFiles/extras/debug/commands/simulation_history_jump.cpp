#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "extras/debug/debug_login.h"
#include "base/weak_ptr.h"
#include "base/unixtime.h"
#include "data/data_history_messages.h"
#include "data/data_search_controller.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "settings.h"

namespace ExtrasDebug::Commands {
namespace {

using Json = nlohmann::json;

struct HistoryJumpState final : base::has_weak_ptr {
	explicit HistoryJumpState(not_null<Main::Session*> session)
	: session(base::make_weak(session)) {
	}

	base::weak_ptr<Main::Session> session;
	PeerId peerId;
	int caseId = 0;
	int firstId = 0;
	int lastId = 0;
	int count = 0;
	int delivered = 0;
	TimeId date = 0;
	bool ready = false;
};

base::weak_ptr<HistoryJumpState> JumpState;

[[nodiscard]] MTPMessage makeMessage(
		not_null<HistoryJumpState*> state,
		not_null<PeerData*> peer,
		int id,
		const QString &text) {
	return MTP_message(
		MTP_flags(MTPDmessage::Flag::f_from_id),
		MTP_int(id), peerToMTP(peer->id), MTPint(), MTPstring(),
		peerToMTP(peer->id), MTPPeer(), MTPMessageFwdHeader(),
		MTPlong(), MTPlong(), MTPPeer(), MTPMessageReplyHeader(),
		MTP_int(state->date + id - state->firstId), MTP_string(text),
		MTPMessageMedia(), MTPReplyMarkup(),
		MTPVector<MTPMessageEntity>(), MTPint(), MTPint(), MTPMessageReplies(),
		MTPint(), MTPstring(), MTPlong(), MTPMessageReactions(),
		MTPVector<MTPRestrictionReason>(), MTPint(), MTPint(), MTPlong(),
		MTPFactCheck(), MTPint(), MTPlong(), MTPSuggestedPost(), MTPint(),
		MTPstring(), MTPRichMessage());
}

[[nodiscard]] MTPmessages_Messages response(
		const QVector<MTPMessage> &messages,
		int count) {
	return MTP_messages_messagesSlice(
		MTP_flags(0), MTP_int(count), MTPint(), MTPint(), MTPSearchPostsFlood(),
		MTP_vector<MTPMessage>(messages), MTP_vector<MTPForumTopic>(),
		MTP_vector<MTPChat>(), MTP_vector<MTPUser>());
}

[[nodiscard]] Json describe(not_null<HistoryJumpState*> state) {
	return {
		{ "peerId", state->peerId.value },
		{ "firstId", state->firstId },
		{ "oldLastId", state->firstId + 79 },
		{ "lastId", state->lastId },
		{ "count", state->count },
		{ "delivered", state->delivered },
		{ "ready", state->ready },
	};
}

Result prepare(not_null<Main::Session*> session, int tailCount) {
	auto state = JumpState.get();
	if (!state || state->session.get() != session) {
		state = session->lifetime().make_state<HistoryJumpState>(session);
		JumpState = base::make_weak(state);
	}
	++state->caseId;
	state->firstId = 8000000 + state->caseId * 1000;
	state->count = 80 + tailCount;
	state->lastId = state->firstId + state->count - 1;
	state->date = base::unixtime::now() - state->count;
	state->delivered = 0;
	state->ready = false;
	const auto peer = session->data().user(UserId(890700000 + state->caseId));
	peer->setName(u"置底历史缺口验证 %1"_q.arg(state->caseId), {}, {}, {});
	peer->setAccessHash(0);
	peer->setFlags(UserDataFlag::MessageMoneyRestrictionsKnown);
	peer->setBarSettings(PeerBarSettings());
	peer->setLoadedStatus(PeerData::LoadedStatus::Full);
	state->peerId = peer->id;
	const auto history = session->data().history(peer);
	history->clearFolder();
	auto oldIds = std::vector<MsgId>();
	for (auto i = 79; i >= 0; --i) {
		const auto id = state->firstId + i;
		const auto message = makeMessage(state, peer, id,
			u"旧历史 %1"_q.arg(i + 1));
		session->data().addNewMessage(message, {}, NewMessageType::Existing);
		oldIds.push_back(MsgId(id));
	}
	ranges::sort(oldIds);
	history->messages().addSlice(std::move(oldIds),
		{ MsgId(0), MsgId(state->firstId + 79) }, state->count);
	const auto newest = makeMessage(state, peer, state->lastId,
		u"尾部消息 %1"_q.arg(state->count));
	const auto item = session->data().addNewMessage(newest, {}, NewMessageType::Last);
	Expects(item != nullptr);
	history->applyDialogTopMessage(item->id);
	history->inboxRead(item->id);
	history->setUnreadCount(0);
	state->ready = true;
	return Result::Ok(Compact(describe(state)));
}

} // namespace

// 只在独立模拟配置构造历史缺口，分步交付内存消息与正式历史索引。
Result simulationHistoryJump(const QStringList &args) {
	const auto session = ActiveSession();
	if (!cDebugProfile() || !session || !isSimulationSession(session)) {
		return Result::Err(u"an isolated simulation profile is required"_q);
	}
	if (args.empty()) {
		return Result::Err(u"usage: simulation.history-jump <prepare|deliver|incoming|state> [count]"_q);
	}
	if (args.front() == u"prepare"_q || args.front() == u"deliver"_q) {
		if (args.size() != 2) {
			return Result::Err(u"prepare and deliver require a count"_q);
		}
		auto ok = false;
		const auto count = args[1].toInt(&ok);
		if (!ok || count < 1 || count > 500) {
			return Result::Err(u"count must be between 1 and 500"_q);
		}
		if (args.front() == u"prepare"_q) {
			return prepare(session, count);
		}
	}
	const auto state = JumpState.get();
	if (!state || state->session.get() != session || !state->ready) {
		return Result::Err(u"prepare a history jump sample first"_q);
	}
	const auto peer = session->data().peer(state->peerId);
	const auto history = session->data().history(peer);
	if (args.front() == u"deliver"_q) {
		const auto count = std::min(args[1].toInt(), state->count);
		auto messages = QVector<MTPMessage>();
		for (auto i = 0; i != count; ++i) {
			messages.push_back(makeMessage(state, peer,
				state->lastId - i, u"尾部消息 %1"_q.arg(state->count - i)));
		}
		const auto data = response(messages, state->count);
		auto parsed = Api::ParseHistoryResult(peer, ServerMaxMsgId - 1,
			Data::LoadDirection::Around, data);
		history->messages().addSlice(std::move(parsed.messageIds),
			{ count == state->count ? MsgId(0) : MsgId(state->lastId - count + 1),
				ServerMaxMsgId }, parsed.fullCount);
		state->delivered = count;
	} else if (args.front() == u"incoming"_q && args.size() == 1) {
		const auto message = makeMessage(state, peer, ++state->lastId,
			u"实时来信验证"_q);
		++state->count;
		session->data().addNewMessage(message, {}, NewMessageType::Unread);
	} else if (args.front() != u"state"_q || args.size() != 1) {
		return Result::Err(u"usage: simulation.history-jump <prepare|deliver|incoming|state> [count]"_q);
	}
	return Result::Ok(Compact(describe(state)));
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
