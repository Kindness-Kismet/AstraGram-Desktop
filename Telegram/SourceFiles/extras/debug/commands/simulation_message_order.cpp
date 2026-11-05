#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "extras/debug/debug_login.h"
#include "extras/debug/simulation_media.h"
#include "base/unixtime.h"
#include "base/weak_ptr.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "settings.h"

#include <map>

namespace ExtrasDebug::Commands {
namespace {

using Json = nlohmann::json;

struct MessageOrderSample {
	int caseId = 0;
	PeerId peerId;
	MsgId localId;
	MsgId incomingId;
	MsgId confirmedId;
	TimeId localDate = 0;
	TimeId incomingDate = 0;
	QString localKind;
	QString incomingKind;
	MTPMessage localMessage;
	MTPMessage incomingMessage;
	bool localAdded = false;
	bool incomingAdded = false;
	bool confirmed = false;
};

struct MessageOrderState final : base::has_weak_ptr {
	explicit MessageOrderState(not_null<Main::Session*> session)
	: session(base::make_weak(session)) {
	}

	base::weak_ptr<Main::Session> session;
	std::map<int, MessageOrderSample> samples;
	int lastCaseId = 0;
	int lastMessageId = 6000000;
	TimeId lastDate = 0;
};

base::weak_ptr<MessageOrderState> OrderState;

[[nodiscard]] bool validKind(const QString &kind) {
	return kind == u"text"_q
		|| kind == u"long"_q
		|| kind == u"sticker"_q
		|| kind == u"photo"_q
		|| kind == u"file"_q;
}

[[nodiscard]] std::optional<SimulationMedia> mediaKind(const QString &kind) {
	if (kind == u"sticker"_q) {
		return SimulationMedia::Sticker;
	}
	if (kind == u"photo"_q) {
		return SimulationMedia::Photo;
	}
	if (kind == u"file"_q) {
		return SimulationMedia::File;
	}
	return std::nullopt;
}

[[nodiscard]] MTPMessage makeOrderMessage(
		not_null<PeerData*> peer,
		PeerId sender,
		MsgId id,
		TimeId date,
		const QString &kind,
		const QString &label) {
	using Flag = MTPDmessage::Flag;
	const auto sticker = (kind == u"sticker"_q);
	const auto text = sticker ? QString() : kind == u"long"_q
		? (label + u"：多行消息用于验证较大的气泡高度。\n"_q).repeated(12).trimmed()
		: label;
	const auto kindOfMedia = mediaKind(kind);
	const auto media = kindOfMedia
		? simulationMedia(&peer->session(), *kindOfMedia, int(id.bare))
		: MTPMessageMedia();
	return MTP_message(
		MTP_flags(Flag::f_from_id
			| ((sender == peer->session().userPeerId()) ? Flag::f_out : Flag())
			| (kindOfMedia ? Flag::f_media : Flag())),
		MTP_int(id.bare), peerToMTP(sender), MTPint(), MTPstring(),
		peerToMTP(peer->id), MTPPeer(), MTPMessageFwdHeader(),
		MTPlong(), MTPlong(), MTPPeer(), MTPMessageReplyHeader(),
		MTP_int(date), MTP_string(text), media, MTPReplyMarkup(),
		MTPVector<MTPMessageEntity>(), MTPint(), MTPint(), MTPMessageReplies(),
		MTPint(), MTPstring(), MTPlong(), MTPMessageReactions(),
		MTPVector<MTPRestrictionReason>(), MTPint(), MTPint(), MTPlong(),
		MTPFactCheck(), MTPint(), MTPlong(), MTPSuggestedPost(), MTPint(),
		MTPstring(), MTPRichMessage());
}

[[nodiscard]] Json describeOrderState(
		not_null<Main::Session*> session,
		const MessageOrderSample &state) {
	const auto describe = [&](MsgId id) {
		const auto item = session->data().message(state.peerId, id);
		return item ? Json{
			{ "exists", true },
			{ "id", item->id.bare },
			{ "date", item->date() },
			{ "sending", item->isSending() },
		} : Json{ { "exists", false } };
	};
	return {
		{ "caseId", state.caseId },
		{ "peerId", state.peerId.value },
		{ "localId", state.localId.bare },
		{ "incomingId", state.incomingId.bare },
		{ "confirmedId", state.confirmedId.bare },
		{ "localDate", state.localDate },
		{ "incomingDate", state.incomingDate },
		{ "localKind", state.localKind.toStdString() },
		{ "incomingKind", state.incomingKind.toStdString() },
		{ "confirmed", state.confirmed },
		{ "local", describe(state.confirmed ? state.confirmedId : state.localId) },
		{ "incoming", describe(state.incomingId) },
	};
}

Result prepareOrder(
		not_null<Main::Session*> session,
		const QStringList &args) {
	if ((args.size() != 4 && args.size() != 6)
		|| !validKind(args[1])
		|| !validKind(args[2])
		|| (args[3] != u"before"_q && args[3] != u"same"_q && args[3] != u"after"_q)
		|| (args.size() == 6 && args[4] != u"--with"_q)) {
		return Result::Err(u"usage: simulation.message-order prepare <text|long|sticker|photo|file> <text|long|sticker|photo|file> <before|same|after> [--with caseId]"_q);
	}
	const auto peer = simulationPeer(session, u"group"_q);
	const auto sender = simulationPeer(session, u"private"_q);
	if (!peer || !sender) {
		return Result::Err(u"the group and private simulation scenes are required"_q);
	}
	auto states = OrderState.get();
	if (!states || states->session.get() != session) {
		// 样本由模拟会话持有，会话关闭时一并释放媒体与编号映射。
		states = session->lifetime().make_state<MessageOrderState>(session);
		OrderState = base::make_weak(states);
	}
	auto date = std::max(base::unixtime::now(), states->lastDate + 3);
	if (args.size() == 6) {
		auto ok = false;
		const auto caseId = args[5].toInt(&ok);
		if (!ok || caseId <= 0) {
			return Result::Err(u"caseId must be a positive integer"_q);
		}
		const auto i = states->samples.find(caseId);
		if (i == states->samples.end()) {
			return Result::Err(u"unknown message order caseId"_q);
		}
		date = i->second.incomingDate;
	}
	states->lastDate = std::max(states->lastDate, date);
	auto &state = states->samples[++states->lastCaseId];
	state.caseId = states->lastCaseId;
	state.peerId = peer->id;
	state.localId = session->data().nextLocalMessageId();
	state.incomingId = MsgId(++states->lastMessageId);
	state.confirmedId = MsgId(++states->lastMessageId);
	state.localDate = date + (args[3] == u"before"_q ? -1 : args[3] == u"after"_q ? 1 : 0);
	state.incomingDate = date;
	state.localKind = args[1];
	state.incomingKind = args[2];
	state.localMessage = makeOrderMessage(peer, session->userPeerId(),
		state.confirmedId, date + 1, state.localKind, u"并发顺序验收：我方"_q);
	state.incomingMessage = makeOrderMessage(peer, sender->id,
		state.incomingId, state.incomingDate, state.incomingKind, u"并发顺序验收：来信"_q);
	return Result::Ok(Compact(describeOrderState(session, state)));
}

} // namespace

Result simulationMessageOrder(const QStringList &args) {
	const auto session = ActiveSession();
	if (!cDebugProfile() || !session || !isSimulationSession(session)) {
		return Result::Err(u"an isolated simulation profile is required"_q);
	}
	if (args.isEmpty()) {
		return Result::Err(u"usage: simulation.message-order <prepare|local|incoming|confirm|state> [options]"_q);
	}
	if (args.front() == u"prepare"_q) {
		return prepareOrder(session, args);
	}
	if (args.size() > 2) {
		return Result::Err(u"usage: simulation.message-order <local|incoming|confirm|state> [caseId]"_q);
	}
	const auto states = OrderState.get();
	if (!states || states->session.get() != session) {
		return Result::Err(u"prepare a message order sample first"_q);
	}
	auto caseId = states->lastCaseId;
	if (args.size() == 2) {
		auto ok = false;
		caseId = args[1].toInt(&ok);
		if (!ok || caseId <= 0) {
			return Result::Err(u"caseId must be a positive integer"_q);
		}
	}
	const auto i = states->samples.find(caseId);
	if (i == states->samples.end()) {
		return Result::Err(u"unknown message order caseId"_q);
	}
	auto &state = i->second;
	if (args.front() == u"state"_q) {
		return Result::Ok(Compact(describeOrderState(session, state)));
	}
	if (args.front() == u"local"_q) {
		if (state.localAdded) {
			return Result::Err(u"the local sample already exists"_q);
		}
		const auto &data = state.localMessage.c_message();
		session->data().history(state.peerId)->addNewLocalMessage({
			.id = state.localId,
			.flags = MessageFlag::BeingSent | MessageFlag::Outgoing,
			.from = session->userPeerId(),
			.date = state.localDate,
		}, TextWithEntities{ qs(data.vmessage()) }, data.vmedia()
			? *data.vmedia() : MTPMessageMedia(MTP_messageMediaEmpty()));
		state.localAdded = true;
	} else if (args.front() == u"incoming"_q) {
		if (state.incomingAdded) {
			return Result::Err(u"the incoming sample already exists"_q);
		}
		const auto item = session->data().addNewMessage(
			state.incomingMessage, MessageFlags(), NewMessageType::Unread);
		Ensures(item != nullptr);
		state.incomingAdded = true;
	} else if (args.front() == u"confirm"_q) {
		if (!state.localAdded || state.confirmed) {
			return Result::Err(u"an unconfirmed local sample is required"_q);
		}
		const auto item = session->data().message(state.peerId, state.localId);
		if (!item || !item->isSending()) {
			return Result::Err(u"the pending local sample is unavailable"_q);
		}
		// 沿正式确认链路换号并登记历史索引，保留同一条消息及其视图。
		item->setRealId(state.confirmedId);
		item->applySentMessage(state.localMessage.c_message());
		Ensures(session->data().message(state.peerId, state.confirmedId) == item);
		state.confirmed = true;
	} else {
		return Result::Err(u"unknown message order action"_q);
	}
	session->data().sendHistoryChangeNotifications();
	return Result::Ok(Compact(describeOrderState(session, state)));
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
