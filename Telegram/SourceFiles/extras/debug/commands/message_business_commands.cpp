#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"
#include "extras/debug/debug_login.h"
#include "extras/features/mention_by_id/mention_by_id.h"

#include "apiwrap.h"
#include "api/api_common.h"
#include "chat_helpers/message_field.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_document.h"
#include "data/data_drafts.h"
#include "data/data_history_messages.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"
#include "storage/storage_media_prepare.h"
#include "storage/localimageloader.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/text/text_entity.h"

#include <QFileInfo>

namespace ExtrasDebug::Commands {

nlohmann::json describeMessage(not_null<HistoryItem*> item) {
	auto entities = nlohmann::json::array();
	for (const auto &entity : item->originalText().entities) {
		auto details = nlohmann::json{ { "type", int(entity.type()) },
			{ "offset", entity.offset() }, { "length", entity.length() } };
		if (entity.type() == EntityType::MentionName) {
			const auto fields = TextUtilities::MentionNameDataToFields(entity.data());
			details["userId"] = fields.userId;
			details["accountId"] = fields.selfId;
		} else {
			details["data"] = entity.data().toStdString();
		}
		entities.push_back(std::move(details));
	}
	const auto document = item->media() ? item->media()->document() : nullptr;
	const auto photo = item->media() ? item->media()->photo() : nullptr;
	const auto forwarded = item->Get<HistoryMessageForwarded>();
	return {
		{ "peerId", item->history()->peer->id.value },
		{ "messageId", item->id.bare },
		{ "senderId", item->from()->id.value },
		{ "groupId", item->groupId().value },
		{ "photoId", photo ? photo->id : uint64(0) },
		{ "forwarded", forwarded ? nlohmann::json{
			{ "senderId", forwarded->originalSender ? forwarded->originalSender->id.value : uint64(0) },
			{ "messageId", forwarded->originalId.bare },
			{ "hiddenSender", bool(forwarded->originalHiddenSenderInfo) },
			{ "savedFromPeerId", forwarded->savedFromPeer ? forwarded->savedFromPeer->id.value : uint64(0) },
			{ "savedFromMessageId", forwarded->savedFromMsgId.bare },
		} : nlohmann::json(nullptr) },
		{ "text", item->originalText().text.toStdString() },
		{ "entities", std::move(entities) },
		{ "outgoing", item->out() },
		{ "serverMessage", IsServerMsgId(item->id) && !isFakeSession(&item->history()->session()) },
		{ "document", document ? nlohmann::json{
			{ "id", document->id }, { "filename", document->filename().toStdString() },
			{ "mime", document->mimeString().toStdString() }, { "size", document->size },
			{ "voice", document->isVoiceMessage() }, { "song", document->isSong() },
			{ "video", document->isVideoFile() }, { "loading", document->loading() },
			{ "ready", document->loadOffset() }, { "failed", document->status == FileDownloadFailed },
		} : nlohmann::json(nullptr) },
	};
}

namespace {

using Json = nlohmann::json;

Json userInfo(not_null<UserData*> user) {
	return { { "userId", peerToUser(user->id).bare },
		{ "peerId", user->id.value }, { "name", user->name().toStdString() },
		{ "username", user->username().toStdString() },
		{ "hasUsername", !user->username().isEmpty() || !user->usernames().empty() },
		{ "self", user->isSelf() }, { "bot", user->isBot() },
		{ "canMention", ExtrasMentionById::canMention(user) } };
}

Result peerInfo(const QStringList &args) {
	if (args.size() != 1) return Result::Err(u"usage: peer.info <peerId>"_q);
	const auto peer = findPeer(args[0]);
	if (!peer) return Result::Err(u"peer not found"_q);
	if (const auto user = peer->asUser()) return Result::Ok(Compact(userInfo(user)));
	return Result::Ok(Compact(Json{ { "peerId", peer->id.value },
		{ "name", peer->name().toStdString() }, { "kind", peer->isChat() ? "chat" : "channel" } }));
}

Result members(const QStringList &args) {
	if (args.isEmpty() || args.size() > 2 || (args.size() == 2 && args[1] != u"no-username"_q)) {
		return Result::Err(u"usage: chat.members <peerId> [no-username]"_q);
	}
	const auto peer = findPeer(args[0]);
	if (!peer) return Result::Err(u"peer not found"_q);
	auto users = base::flat_set<not_null<UserData*>>();
	if (const auto chat = peer->asChat()) {
		for (const auto user : chat->participants) users.emplace(user);
	} else if (const auto channel = peer->asChannel(); channel && channel->mgInfo) {
		for (const auto user : channel->mgInfo->lastParticipants) users.emplace(user);
	} else {
		return Result::Err(u"expected a group"_q);
	}
	auto list = Json::array();
	for (const auto user : users) {
		if (args.size() == 2 && (!user->username().isEmpty() || !user->usernames().empty())) continue;
		list.push_back(userInfo(user));
	}
	return Result::Ok(Compact(Json{ { "cachedOnly", true }, { "members", std::move(list) } }));
}

Result localMessages(const QStringList &args) {
	if (args.isEmpty() || args.size() > 2) return Result::Err(u"usage: message.list <peerId> [limit]"_q);
	const auto peer = findPeer(args[0]);
	if (!peer) return Result::Err(u"peer not found"_q);
	auto ok = true;
	const auto limit = args.size() == 2 ? args[1].toInt(&ok) : 20;
	if (!ok || limit < 1 || limit > 100) return Result::Err(u"limit must be between 1 and 100"_q);
	const auto history = peer->owner().history(peer);
	const auto snapshot = history->messages().snapshot({ ServerMaxMsgId, limit, 0 });
	auto result = Json::array();
	for (const auto id : ranges::views::reverse(snapshot.messageIds)) {
		if (result.size() >= limit) break;
		if (const auto item = peer->owner().message(peer->id, id)) result.push_back(describeMessage(item));
	}
	return Result::Ok(Compact(Json{ { "cachedOnly", true }, { "messages", std::move(result) } }));
}

Result fetchMessages(const QStringList &args) {
	if (args.isEmpty() || args.size() > 2) return Result::Err(u"usage: message.fetch <peerId> [limit]"_q);
	const auto peer = findPeer(args[0]);
	if (!peer) return Result::Err(u"peer not found"_q);
	const auto session = &peer->session();
	if (isFakeSession(session)) return Result::Err(u"an authenticated session is required"_q);
	auto ok = true;
	const auto limit = args.size() == 2 ? args[1].toInt(&ok) : 20;
	if (!ok || limit < 1 || limit > 100) return Result::Err(u"limit must be between 1 and 100"_q);
	const auto id = beginJob("message-fetch");
	session->lifetime().add([id] { finishJob(id, false, "session closed"); });
	session->api().request(MTPmessages_GetHistory(peer->input(),
		MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(limit), MTP_int(0), MTP_int(0), MTP_long(0)
	)).done([=](const MTPmessages_Messages &result) {
		result.match([&](const MTPDmessages_messagesNotModified &) {
			finishJob(id, false, "unexpected not-modified response");
		}, [&](const auto &data) {
			session->data().processUsers(data.vusers());
			session->data().processChats(data.vchats());
			auto messages = Json::array();
			for (const auto &message : data.vmessages().v) {
				if (const auto item = session->data().addNewMessage(
					message, MessageFlags(), NewMessageType::Existing)) {
					messages.push_back(describeMessage(item));
				}
			}
			finishJob(id, true, Json{ { "peerId", peer->id.value }, { "messages", std::move(messages) } });
		});
	}).fail([id](const MTP::Error &error) {
		finishJob(id, false, error.type().toStdString());
	}).send();
	return jobStarted(id);
}

Result draftInfo(const QStringList &args) {
	if (args.size() != 1) return Result::Err(u"usage: chat.draft <peerId>"_q);
	const auto peer = findPeer(args[0]);
	if (!peer) return Result::Err(u"peer not found"_q);
	const auto draft = peer->owner().history(peer)->localDraft(MsgId(), PeerId());
	return Result::Ok(Compact(Json{ { "peerId", peer->id.value },
		{ "text", draft ? draft->textWithTags.text.toStdString() : "" },
		{ "present", draft != nullptr } }));
}

Result resolveMention(const QStringList &args) {
	if (args.size() != 1) return Result::Err(u"usage: mention.resolve <userId>"_q);
	const auto session = ActiveSession();
	const auto userId = ExtrasMentionById::parseUserId(args[0]);
	if (!session) return Result::Err(u"no active session"_q);
	if (!userId) return Result::Err(u"invalid userId"_q);
	const auto id = beginJob("mention-resolve");
	session->lifetime().add([id] { finishJob(id, false, "session closed"); });
	ExtrasMentionById::resolveUser(&session->api(), session, *userId,
		[id](not_null<UserData*> user) { finishJob(id, true, userInfo(user)); },
		[id](QString error) { finishJob(id, false, error.toStdString()); });
	return jobStarted(id);
}

Result sendMention(const QStringList &args) {
	if (args.size() != 4) return Result::Err(u"usage: mention.send <peerId> <userId> <displayText> <followingText>"_q);
	const auto peer = findPeer(args[0]);
	const auto userId = ExtrasMentionById::parseUserId(args[1]);
	if (!peer) return Result::Err(u"peer not found"_q);
	if (!userId || args[2].trimmed().isEmpty()) return Result::Err(u"invalid userId or empty display text"_q);
	const auto session = &peer->session();
	if (isFakeSession(session)) return Result::Err(u"an authenticated session is required"_q);
	const auto id = beginJob("mention-send");
	session->lifetime().add([id] { finishJob(id, false, "session closed"); });
	ExtrasMentionById::resolveUser(&session->api(), session, *userId,
		[=](not_null<UserData*> user) {
			auto action = Api::SendAction(session->data().history(peer));
			action.clearDraft = false;
			auto message = Api::MessageToSend(action);
			message.textWithTags = { args[2] + args[3], {
				{ 0, args[2].size(), PrepareMentionTag(user) },
			} };
			session->api().sendMessage(std::move(message));
			finishJob(id, true, Json{ { "state", "submitted" },
				{ "peerId", peer->id.value }, { "user", userInfo(user) } });
		}, [id](QString error) { finishJob(id, false, error.toStdString()); });
	return jobStarted(id);
}

Result sendFile(const QStringList &args) {
	if (args.size() < 2 || args.size() > 3) return Result::Err(u"usage: message.send-file <peerId> <path> [caption]"_q);
	const auto peer = findPeer(args[0]);
	if (!peer) return Result::Err(u"peer not found"_q);
	const auto session = &peer->session();
	if (isFakeSession(session)) return Result::Err(u"an authenticated session is required"_q);
	const auto path = QFileInfo(args[1]);
	if (!path.isAbsolute() || !path.isFile()) return Result::Err(u"expected an absolute file path"_q);
	const auto id = beginJob("file-send");
	const auto weak = base::make_weak(session);
	const auto premium = session->premium();
	const auto peerId = peer->id;
	const auto filename = path.absoluteFilePath();
	const auto caption = args.value(2);
	session->lifetime().add([id] { finishJob(id, false, "session closed"); });
	crl::async([=] {
		auto list = Storage::PrepareMediaList(QStringList{ filename }, 320, premium);
		crl::on_main([=, list = std::move(list)]() mutable {
			if (!weak) return;
			if (list.error != Ui::PreparedList::Error::None || list.files.size() != 1) {
				finishJob(id, false, "could not prepare the attachment");
				return;
			}
			list.files.front().caption = { caption };
			auto action = Api::SendAction(weak->data().history(peerId));
			action.clearDraft = false;
			weak->api().sendFiles(std::move(list), SendMediaType::File, nullptr, action);
			finishJob(id, true, Json{ { "state", "submitted" }, { "peerId", peerId.value },
				{ "filename", QFileInfo(filename).fileName().toStdString() } });
		});
	});
	return jobStarted(id);
}

} // namespace

const HandlerMap &messageBusinessHandlers() {
	static const auto handlers = HandlerMap{
		{ u"peer.info"_q, &peerInfo },
		{ u"chat.members"_q, &members },
		{ u"chat.draft"_q, &draftInfo },
		{ u"message.list"_q, &localMessages },
		{ u"message.fetch"_q, &fetchMessages },
		{ u"message.send-file"_q, &sendFile },
		{ u"mention.resolve"_q, &resolveMention },
		{ u"mention.send"_q, &sendMention },
	};
	return handlers;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
