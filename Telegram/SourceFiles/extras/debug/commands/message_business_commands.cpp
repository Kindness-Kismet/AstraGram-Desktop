#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"
#include "extras/debug/debug_login.h"
#include "extras/features/mention_by_id/mention_by_id.h"
#include "extras/features/message_shot/message_shot.h"
#include "extras/features/video_cover/video_cover.h"
#include "extras/utils/badge_helpers.h"

#include "apiwrap.h"
#include "api/api_common.h"
#include "api/api_editing.h"
#include "base/unixtime.h"
#include "chat_helpers/message_field.h"
#include "core/application.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_compose_stash.h"
#include "data/data_document.h"
#include "data/data_drafts.h"
#include "data/data_folder.h"
#include "data/data_history_messages.h"
#include "data/data_media_types.h"
#include "data/data_message_reactions.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_web_page.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"
#include "media/view/media_view_overlay_widget.h"
#include "storage/storage_media_prepare.h"
#include "storage/localimageloader.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/emoji_config.h"
#include "ui/text/text_entity.h"
#include "window/window_session_controller.h"

#include <QDir>
#include <QFile>
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
	const auto webpage = item->media() ? item->media()->webpage() : nullptr;
	auto reactions = nlohmann::json::array();
	for (const auto &reaction : item->reactions()) {
		reactions.push_back({ { "emoji", reaction.id.emoji().toStdString() },
			{ "count", reaction.count }, { "my", reaction.my } });
	}
	return {
		{ "peerId", item->history()->peer->id.value },
		{ "messageId", item->id.bare },
		{ "senderId", item->from()->id.value },
		{ "groupId", item->groupId().value },
		{ "photoId", photo ? photo->id : uint64(0) },
		{ "reactions", std::move(reactions) },
		{ "webpage", webpage ? nlohmann::json{
			{ "id", webpage->id }, { "pendingTill", webpage->pendingTill },
			{ "failed", bool(webpage->failed) },
		} : nlohmann::json(nullptr) },
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
		{ "deleted", item->isDeleted() },
		{ "pinned", item->isPinned() },
		{ "replyToId", item->replyToId().bare },
		{ "topicRootId", item->topicRootId().bare },
		{ "comments", item->repliesAreComments() },
		{ "repliesCount", item->repliesCount() },
		{ "ttlDestroyAt", item->ttlDestroyAt() },
		{ "serverMessage", IsServerMsgId(item->id) && !isSimulationSession(&item->history()->session()) },
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
	auto result = peer->isUser() ? userInfo(peer->asUser())
		: Json{ { "peerId", peer->id.value }, { "name", peer->name().toStdString() },
			{ "kind", peer->isChat() ? "chat" : "channel" } };
	result["verified"] = peer->isVerified();
	result["officialResource"] = isExteraPeer(peer);
	result["supporter"] = isSupporterPeer(peer);
	result["hasEmojiStatus"] = bool(peer->emojiStatusId());
	return Result::Ok(Compact(result));
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

HistoryItem *findAccountMessage(const QStringList &args) {
	auto session = ActiveSession();
	if (args.size() == 5) {
		auto ok = false;
		const auto index = args[4].toInt(&ok);
		session = ok ? sessionByIndex(index) : nullptr;
	}
	if (!session || isSimulationSession(session)) {
		return nullptr;
	}
	auto peerOk = false;
	auto messageOk = false;
	const auto peerId = PeerId(args[0].toULongLong(&peerOk));
	const auto messageId = MsgId(args[1].toLongLong(&messageOk));
	return peerOk && messageOk ? session->data().message(peerId, messageId) : nullptr;
}

Result reactToMessage(const QStringList &args) {
	if (args.size() != 3 && (args.size() != 5 || args[3] != u"--account"_q)) {
		return Result::Err(u"usage: message.react <peerId> <messageId> <emoji> [--account index]"_q);
	}
	const auto item = findAccountMessage(args);
	if (!item || !item->canReact() || !Ui::Emoji::Find(args[2])) {
		return Result::Err(u"message cannot react or emoji not found"_q);
	}
	item->toggleReaction(Data::ReactionId{ args[2] }, HistoryReactionSource::Selector);
	auto reactions = Json::array();
	for (const auto &reaction : item->reactions()) {
		reactions.push_back({ { "emoji", reaction.id.emoji().toStdString() },
			{ "count", reaction.count }, { "my", reaction.my } });
	}
	return Result::Ok(Compact(Json{ { "state", "submitted" },
		{ "reactions", std::move(reactions) } }));
}

Result setMessagePreview(const QStringList &args) {
	if ((args.size() != 3 && (args.size() != 5 || args[3] != u"--account"_q))
		|| (args[2] != u"on"_q && args[2] != u"off"_q)) {
		return Result::Err(u"usage: message.preview <peerId> <messageId> <on|off> [--account index]"_q);
	}
	const auto item = findAccountMessage(args);
	if (!item || !item->allowsEdit(base::unixtime::now())
		|| (item->media() && !item->media()->webpage())) {
		return Result::Err(u"an editable text message is required"_q);
	}
	auto webpage = Data::WebPageDraft::FromItem(item);
	webpage.removed = (args[2] == u"off"_q);
	const auto id = beginJob("message-preview");
	item->history()->session().lifetime().add([id] {
		finishJob(id, false, "session closed");
	});
	Api::EditTextMessage(item, item->originalText(), webpage, {},
		[id](mtpRequestId) { finishJob(id, true, "preview updated"); },
		[id](const QString &error, mtpRequestId) {
			finishJob(id, false, error.toStdString());
		}, false);
	return jobStarted(id);
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
	if (isSimulationSession(session)) return Result::Err(u"an authenticated session is required"_q);
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

Json describeRawDocument(const MTPDocument &document) {
	return document.match([](const MTPDdocumentEmpty &) {
		return Json{ { "empty", true } };
	}, [](const MTPDdocument &data) {
		auto attributes = Json::array();
		for (const auto &attribute : data.vattributes().v) {
			attribute.match([&](const MTPDdocumentAttributeVideo &video) {
				attributes.push_back(Json{ { "type", "video" },
					{ "duration", video.vduration().v },
					{ "w", video.vw().v }, { "h", video.vh().v },
					{ "streaming", video.is_supports_streaming() },
					{ "preloadPrefix", video.vpreload_prefix_size().value_or_empty() },
					{ "codec", qs(video.vvideo_codec().value_or_empty()).toStdString() } });
			}, [&](const MTPDdocumentAttributeFilename &file) {
				attributes.push_back(Json{ { "type", "filename" },
					{ "name", qs(file.vfile_name()).toStdString() } });
			}, [&](const MTPDdocumentAttributeImageSize &size) {
				attributes.push_back(Json{ { "type", "image" },
					{ "w", size.vw().v }, { "h", size.vh().v } });
			}, [&](const auto &) {
				attributes.push_back(Json{ { "type", "other" },
					{ "id", int64(attribute.type()) } });
			});
		}
		auto thumbs = Json::array();
		if (const auto list = data.vthumbs()) {
			for (const auto &thumb : list->v) {
				thumb.match([&](const auto &size) {
					thumbs.push_back(qs(size.vtype()).toStdString());
				});
			}
		}
		auto videoThumbs = Json::array();
		if (const auto list = data.vvideo_thumbs()) {
			for (const auto &thumb : list->v) {
				thumb.match([&](const MTPDvideoSize &size) {
					videoThumbs.push_back(Json{
						{ "type", qs(size.vtype()).toStdString() },
						{ "w", size.vw().v }, { "h", size.vh().v },
						{ "size", size.vsize().v } });
				}, [&](const auto &) {
					videoThumbs.push_back(Json{ { "type", "markup" } });
				});
			}
		}
		return Json{ { "id", std::to_string(data.vid().v) },
			{ "mime", qs(data.vmime_type()).toStdString() },
			{ "size", data.vsize().v }, { "dc", data.vdc_id().v },
			{ "attributes", std::move(attributes) },
			{ "thumbs", std::move(thumbs) },
			{ "videoThumbs", std::move(videoThumbs) } };
	});
}

// 只读取服务器返回的原始附件结构，不写入本地消息，不打开对话。
Result rawMedia(const QStringList &args) {
	if (args.isEmpty() || args.size() > 3) {
		return Result::Err(u"usage: message.media <peerId> [limit] [offsetId]"_q);
	}
	const auto peer = findPeer(args[0]);
	if (!peer) return Result::Err(u"peer not found"_q);
	const auto session = &peer->session();
	if (isSimulationSession(session)) return Result::Err(u"an authenticated session is required"_q);
	auto limitOk = true;
	auto offsetOk = true;
	const auto limit = args.size() >= 2 ? args[1].toInt(&limitOk) : 20;
	const auto offsetId = args.size() == 3 ? args[2].toInt(&offsetOk) : 0;
	if (!limitOk || limit < 1 || limit > 100) return Result::Err(u"limit must be between 1 and 100"_q);
	if (!offsetOk || offsetId < 0) return Result::Err(u"offsetId must be a non-negative message id"_q);
	const auto id = beginJob("message-media");
	session->lifetime().add([id] { finishJob(id, false, "session closed"); });
	session->api().request(MTPmessages_GetHistory(peer->input(),
		MTP_int(offsetId), MTP_int(0), MTP_int(0), MTP_int(limit), MTP_int(0), MTP_int(0), MTP_long(0)
	)).done([=](const MTPmessages_Messages &result) {
		result.match([&](const MTPDmessages_messagesNotModified &) {
			finishJob(id, false, "unexpected not-modified response");
		}, [&](const auto &data) {
			auto messages = Json::array();
			for (const auto &message : data.vmessages().v) {
				message.match([&](const MTPDmessage &item) {
					const auto media = item.vmedia();
					if (!media || media->type() != mtpc_messageMediaDocument) return;
					const auto &document = media->c_messageMediaDocument();
					auto alt = Json::array();
					if (const auto list = document.valt_documents()) {
						for (const auto &item : list->v) alt.push_back(describeRawDocument(item));
					}
					messages.push_back(Json{ { "messageId", item.vid().v },
						{ "document", document.vdocument()
							? describeRawDocument(*document.vdocument())
							: Json() },
						{ "altDocuments", std::move(alt) },
						{ "videoCover", bool(document.vvideo_cover()) },
						{ "videoTimestamp", document.vvideo_timestamp().value_or_empty() } });
				}, [](const auto &) {});
			}
			finishJob(id, true, Json{ { "peerId", peer->id.value }, { "messages", std::move(messages) } });
		});
	}).fail([id](const MTP::Error &error) {
		finishJob(id, false, error.type().toStdString());
	}).send();
	return jobStarted(id);
}

// 按类型保存消息附带的小文件，只请求一次，超过 1 MB 的直接拒绝。
Result saveAltFile(const QStringList &args) {
	constexpr auto kLimit = 1024 * 1024;
	if (args.size() != 4) {
		return Result::Err(u"usage: message.media-file <peerId> <messageId> <mime> <destination>"_q);
	}
	const auto peer = findPeer(args[0]);
	if (!peer) return Result::Err(u"peer not found"_q);
	const auto session = &peer->session();
	if (isSimulationSession(session)) return Result::Err(u"an authenticated session is required"_q);
	auto ok = true;
	const auto messageId = args[1].toInt(&ok);
	if (!ok || messageId <= 0) return Result::Err(u"expected a positive message id"_q);
	const auto mime = args[2];
	const auto path = QFileInfo(args[3]);
	if (!path.isAbsolute() || path.exists() || !path.dir().exists()) {
		return Result::Err(u"destination must be a new absolute path in an existing directory"_q);
	}
	const auto destination = path.absoluteFilePath();
	const auto id = beginJob("message-media-file");
	session->lifetime().add([id] { finishJob(id, false, "session closed"); });
	const auto download = [=](const MTPDdocument &document) {
		session->api().request(MTPupload_GetFile(
			MTP_flags(0),
			MTP_inputDocumentFileLocation(document.vid(), document.vaccess_hash(),
				document.vfile_reference(), MTP_string()),
			MTP_long(0),
			MTP_int(kLimit)
		)).done([=](const MTPupload_File &result) {
			if (result.type() != mtpc_upload_file) {
				finishJob(id, false, "unexpected cdn redirect");
				return;
			}
			const auto bytes = result.c_upload_file().vbytes().v;
			auto file = QFile(destination);
			if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
				finishJob(id, false, "could not write destination");
				return;
			}
			finishJob(id, true, Json{ { "path", destination.toStdString() }, { "size", bytes.size() } });
		}).fail([=](const MTP::Error &error) {
			finishJob(id, false, error.type().toStdString());
		}).toDC(MTP::downloadDcId(document.vdc_id().v, 0)).send();
	};
	const auto done = [=](const MTPmessages_Messages &result) {
		auto found = std::optional<MTPDocument>();
		result.match([](const MTPDmessages_messagesNotModified &) {
		}, [&](const auto &data) {
			for (const auto &message : data.vmessages().v) {
				if (message.type() != mtpc_message) continue;
				const auto media = message.c_message().vmedia();
				if (!media || media->type() != mtpc_messageMediaDocument) continue;
				const auto list = media->c_messageMediaDocument().valt_documents();
				if (!list) continue;
				for (const auto &document : list->v) {
					if (document.type() == mtpc_document
						&& qs(document.c_document().vmime_type()) == mime) {
						found = document;
						break;
					}
				}
			}
		});
		if (!found) {
			finishJob(id, false, "no attached file with this mime type");
		} else if (found->c_document().vsize().v > kLimit) {
			finishJob(id, false, "attached file is larger than 1 MB");
		} else {
			download(found->c_document());
		}
	};
	const auto fail = [=](const MTP::Error &error) {
		finishJob(id, false, error.type().toStdString());
	};
	const auto ids = MTP_vector<MTPInputMessage>(1, MTP_inputMessageID(MTP_int(messageId)));
	if (const auto channel = peer->asChannel()) {
		session->api().request(MTPchannels_GetMessages(channel->inputChannel(), ids)
		).done(done).fail(fail).send();
	} else {
		session->api().request(MTPmessages_GetMessages(ids)).done(done).fail(fail).send();
	}
	return jobStarted(id);
}

Result resolvePeer(const QStringList &args) {
	if (args.size() != 1) return Result::Err(u"usage: peer.resolve <username>"_q);
	const auto session = ActiveSession();
	if (!session || isSimulationSession(session)) return Result::Err(u"an authenticated session is required"_q);
	auto username = args[0];
	if (username.startsWith('@')) username = username.mid(1);
	const auto id = beginJob("peer-resolve");
	session->lifetime().add([id] { finishJob(id, false, "session closed"); });
	session->api().request(MTPcontacts_ResolveUsername(
		MTP_flags(0), MTP_string(username), MTP_string()
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		const auto &data = result.data();
		session->data().processUsers(data.vusers());
		session->data().processChats(data.vchats());
		const auto peer = session->data().peerLoaded(peerFromMTP(data.vpeer()));
		if (!peer) {
			finishJob(id, false, "peer not loaded");
			return;
		}
		finishJob(id, true, Json{ { "peerId", peer->id.value },
			{ "name", peer->name().toStdString() } });
	}).fail([id](const MTP::Error &error) {
		finishJob(id, false, error.type().toStdString());
	}).send();
	return jobStarted(id);
}

Result draftInfo(const QStringList &args) {
	if (args.empty() || args.size() > 2) {
		return Result::Err(u"usage: chat.draft <peerId> [topicRootId]"_q);
	}
	const auto peer = findPeer(args[0]);
	if (!peer) {
		return Result::Err(u"peer not found"_q);
	}
	auto topicOk = true;
	const auto topicRootId = MsgId(args.size() == 2
		? args[1].toLongLong(&topicOk)
		: 0);
	if (!topicOk || (args.size() == 2 && !IsServerMsgId(topicRootId))) {
		return Result::Err(u"expected a positive topic root message id"_q);
	}
	const auto history = peer->owner().history(peer);
	const auto draft = history->localDraft(topicRootId, PeerId());
	const auto &forward = history->forwardDraft(topicRootId, PeerId());
	const auto stash = history->composeStash(
		Data::DraftKey::Local(topicRootId, PeerId()));
	return Result::Ok(Compact(Json{
		{ "peerId", peer->id.value },
		{ "topicRootId", topicRootId.bare },
		{ "text", draft ? draft->textWithTags.text.toStdString() : "" },
		{ "present", draft != nullptr },
		{ "replyToId", draft ? draft->reply.messageId.msg.bare : 0 },
		{ "forwardCount", forward.ids.size() },
		{ "forwardOptions", int(forward.options) },
		{ "stash", stash ? Json{
			{ "text", stash->draft.textWithTags.text.toStdString() },
			{ "replyToId", stash->draft.reply.messageId.msg.bare },
			{ "forwardCount", stash->forward.ids.size() },
			{ "forwardOptions", int(stash->forward.options) },
			{ "filesCount", stash->files.files.size() + stash->files.filesToProcess.size() },
			{ "richMessage", stash->draft.hasRichMessage() },
		} : Json(nullptr) },
	}));
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
	if (isSimulationSession(session)) return Result::Err(u"an authenticated session is required"_q);
	const auto id = beginJob("mention-send");
	session->lifetime().add([id] { finishJob(id, false, "session closed"); });
	ExtrasMentionById::resolveUser(&session->api(), session, *userId,
		[=](not_null<UserData*> user) {
			auto action = Api::SendAction(session->data().history(peer));
			action.clearDraft = false;
			auto message = Api::MessageToSend(action);
			message.textWithTags = { args[2] + args[3], {
				{ 0, int(args[2].size()), PrepareMentionTag(user) },
			} };
			session->api().sendMessage(std::move(message));
			finishJob(id, true, Json{ { "state", "submitted" },
				{ "peerId", peer->id.value }, { "user", userInfo(user) } });
		}, [id](QString error) { finishJob(id, false, error.toStdString()); });
	return jobStarted(id);
}

Result sendAttachments(
		not_null<PeerData*> peer,
		QStringList filenames,
		QString caption,
		bool asMedia,
		bool album,
		QString coverFilename = {}) {
	const auto session = &peer->session();
	if (isSimulationSession(session)) return Result::Err(u"an authenticated session is required"_q);
	for (auto &filename : filenames) {
		const auto path = QFileInfo(filename);
		if (!path.isAbsolute() || !path.isFile()) {
			return Result::Err(u"expected absolute file paths"_q);
		}
		filename = path.absoluteFilePath();
	}
	if (!coverFilename.isEmpty()) {
		const auto path = QFileInfo(coverFilename);
		if (!asMedia || album || filenames.size() != 1
			|| !path.isAbsolute() || !path.isFile()
			|| !(peer->isSelf() || peer->isBot() || peer->isBroadcast())) {
			return Result::Err(u"cover requires one video sent as media to Saved Messages, a bot, or a channel"_q);
		}
		coverFilename = path.absoluteFilePath();
	}
	const auto id = beginJob("file-send");
	const auto weak = base::make_weak(session);
	const auto premium = session->premium();
	const auto peerId = peer->id;
	session->lifetime().add([id] { finishJob(id, false, "session closed"); });
	crl::async([=] {
		auto list = Storage::PrepareMediaList(filenames, 320, premium);
		auto cover = coverFilename.isEmpty()
			? Ui::PreparedList()
			: Storage::PrepareMediaList(QStringList{ coverFilename }, 320, premium);
		crl::on_main([=, list = std::move(list), cover = std::move(cover)]() mutable {
			if (!weak) return;
			if (list.error != Ui::PreparedList::Error::None
				|| list.files.size() != filenames.size()
				|| (album && !ranges::all_of(list.files, [](const Ui::PreparedFile &file) {
					return file.canBeInAlbumType(Ui::AlbumType::PhotoVideo);
				}))) {
				finishJob(id, false, "could not prepare the attachment");
				return;
			}
			if (!coverFilename.isEmpty()) {
				if (cover.error != Ui::PreparedList::Error::None
					|| cover.files.size() != 1
					|| cover.files.front().type != Ui::PreparedFile::Type::Photo
					|| !list.files.front().isVideoFile()) {
					finishJob(id, false, "could not prepare the video cover");
					return;
				}
				list.files.front().videoCover = std::make_unique<Ui::PreparedFile>(
					std::move(cover.files.front()));
			}
			list.files.front().caption = { caption };
			auto action = Api::SendAction(weak->data().history(peerId));
			action.clearDraft = false;
			const auto type = asMedia ? SendMediaType::Photo : SendMediaType::File;
			weak->api().sendFiles(std::move(list), type,
				album ? std::make_shared<SendingAlbum>() : nullptr, action);
			finishJob(id, true, Json{ { "state", "submitted" }, { "peerId", peerId.value },
				{ "filename", QFileInfo(filenames.front()).fileName().toStdString() },
				{ "count", filenames.size() }, { "album", album }, { "media", asMedia },
				{ "cover", !coverFilename.isEmpty() } });
		});
	});
	return jobStarted(id);
}

// --media 对应发送窗口里的“压缩”，视频按可播放的媒体发送。
Result sendFile(const QStringList &arguments) {
	auto args = arguments;
	const auto asMedia = (args.removeAll(u"--media"_q) > 0);
	auto coverFilename = QString();
	const auto coverIndex = args.indexOf(u"--cover"_q);
	if (coverIndex >= 0) {
		if (coverIndex + 1 >= args.size() || args[coverIndex + 1].isEmpty()) {
			return Result::Err(u"expected a file path after --cover"_q);
		}
		coverFilename = args.takeAt(coverIndex + 1);
		args.removeAt(coverIndex);
	}
	if (args.size() < 2 || args.size() > 3) {
		return Result::Err(u"usage: message.send-file <peerId> <path> [caption] [--media] [--cover path]"_q);
	}
	const auto peer = findPeer(args[0]);
	if (!peer) return Result::Err(u"peer not found"_q);
	return sendAttachments(peer, { args[1] }, args.value(2), asMedia, false, coverFilename);
}

Result sendAlbum(const QStringList &args) {
	if (args.size() < 3 || args.size() > 11) {
		return Result::Err(u"usage: message.send-album <peerId> <path> <path> [path...]"_q);
	}
	const auto peer = findPeer(args[0]);
	if (!peer) return Result::Err(u"peer not found"_q);
	return sendAttachments(peer, args.mid(1), {}, true, true);
}

// 与右键“查看视频封面”相同；视频没有封面和缩略图时直接报错。
Result openVideoCover(const QStringList &args) {
	if (args.size() != 2) return Result::Err(u"usage: video.cover <peerId> <messageId>"_q);
	const auto peer = findPeer(args[0]);
	if (!peer) return Result::Err(u"peer not found"_q);
	const auto item = peer->owner().message(peer->id, MsgId(args[1].toLongLong()));
	if (!item) return Result::Err(u"message is not loaded"_q);
	const auto media = item->media();
	const auto document = media ? media->document() : nullptr;
	if (!document) return Result::Err(u"message has no document"_q);
	if (!ExtrasVideoCover::hasCover(item, document)) return Result::Err(u"video has no cover or thumbnail"_q);
	const auto controller = peer->session().tryResolveWindow();
	if (!controller) return Result::Err(u"no window controller"_q);
	const auto cover = LookupVideoCover(document, item);
	ExtrasVideoCover::showCover(controller, item, document);
	return Result::Ok(Compact(Json{
		{ "source", cover ? "cover" : "thumbnail" },
		{ "thumbnailSize", document->thumbnailByteSize() } }));
}

// 与点击消息里的照片或视频相同，在媒体查看器中打开，可继续左右翻看同一会话的媒体。
Result openMedia(const QStringList &args) {
	if (args.size() != 2) return Result::Err(u"usage: media.open <peerId> <messageId>"_q);
	const auto peer = findPeer(args[0]);
	if (!peer) return Result::Err(u"peer not found"_q);
	const auto item = peer->owner().message(peer->id, MsgId(args[1].toLongLong()));
	if (!item) return Result::Err(u"message is not loaded"_q);
	const auto media = item->media();
	const auto photo = media ? media->photo() : nullptr;
	const auto document = media ? media->document() : nullptr;
	if (!photo && !document) return Result::Err(u"message has no photo or document"_q);
	const auto controller = peer->session().tryResolveWindow();
	if (!controller) return Result::Err(u"no window controller"_q);
	const auto context = Window::SessionController::MessageContext{
		.id = item->fullId(),
		.topicRootId = item->topicRootId(),
		.monoforumPeerId = item->sublistPeerId(),
	};
	if (photo) {
		controller->openPhoto(photo, context);
	} else {
		controller->openDocument(document, true, context);
	}
	return Result::Ok(Compact(Json{ { "type", photo ? "photo" : "document" } }));
}

Result mediaState(const QStringList &args) {
	if (!args.isEmpty()) {
		return Result::Err(u"usage: media.state"_q);
	}
	const auto viewer = Core::App().debugMediaView();
	if (!viewer) {
		return Result::Err(u"media viewer is not initialized"_q);
	}
	return Result::Ok(viewer->debugState());
}

Result saveMediaImage(const QStringList &args) {
	if (args.size() != 1 || !QFileInfo(args[0]).isAbsolute()) {
		return Result::Err(u"usage: media.save-image <newAbsolutePath>"_q);
	}
	if (QFileInfo::exists(args[0])) {
		return Result::Err(u"destination already exists"_q);
	}
	const auto viewer = Core::App().debugMediaView();
	if (!viewer || !viewer->debugSaveImage(args[0])) {
		return Result::Err(u"image is unavailable, restricted, or could not be saved"_q);
	}
	return Result::Ok(Compact(Json{ { "saved", true }, { "path", args[0].toStdString() } }));
}

Result checkCopyImage(const QStringList &args) {
	if (!args.isEmpty()) {
		return Result::Err(u"usage: media.copy-check"_q);
	}
	const auto viewer = Core::App().debugMediaView();
	const auto size = viewer ? viewer->debugCopyImage() : QSize();
	if (size.isEmpty()) {
		return Result::Err(u"image is unavailable, restricted, or could not be copied"_q);
	}
	return Result::Ok(Compact(Json{
		{ "copied", true }, { "imageSize", Json{ size.width(), size.height() } },
		{ "clipboardRestored", true },
	}));
}

Result openMessageShot(const QStringList &args) {
	if (args.size() < 2) {
		return Result::Err(u"usage: message.shot <peerId> <messageId>..."_q);
	}
	const auto peer = findPeer(args[0]);
	if (!peer) {
		return Result::Err(u"peer not found"_q);
	}
	const auto controller = peer->session().tryResolveWindow();
	if (!controller) {
		return Result::Err(u"no window controller"_q);
	}
	auto ids = MessageIdsList();
	for (const auto &arg : args.mid(1)) {
		auto ok = false;
		const auto id = arg.toLongLong(&ok);
		if (!ok || !peer->owner().message(peer->id, MsgId(id))) {
			return Result::Err(u"message not found"_q);
		}
		ids.push_back(FullMsgId(peer->id, MsgId(id)));
	}
	ExtrasFeatures::MessageShot::Show(controller, ids, [] {});
	return Result::Ok();
}

Result removeSimulationMessage(const QStringList &args) {
	if (args.size() != 2) {
		return Result::Err(u"usage: simulation.remove-message <peerId> <messageId>"_q);
	}
	const auto peer = findPeer(args[0]);
	if (!peer || !isSimulationSession(&peer->session())) {
		return Result::Err(u"simulation mode is required"_q);
	}
	auto ok = false;
	const auto id = args[1].toLongLong(&ok);
	const auto item = ok ? peer->owner().message(peer->id, MsgId(id)) : nullptr;
	if (!item) {
		return Result::Err(u"message not found"_q);
	}
	item->destroy();
	return Result::Ok();
}

// 只在本地改归属，走正式的移入移出列表流程，不发归档请求。
Result archiveSimulationChat(const QStringList &args) {
	if (args.size() != 2 || (args[1] != u"on"_q && args[1] != u"off"_q)) {
		return Result::Err(u"usage: simulation.archive <peerId> <on|off>"_q);
	}
	const auto peer = findPeer(args[0]);
	if (!peer || !isSimulationSession(&peer->session())) {
		return Result::Err(u"simulation mode is required"_q);
	}
	const auto history = peer->owner().historyLoaded(peer);
	if (!history || !history->inChatList()) {
		return Result::Err(u"chat is not in the chat list"_q);
	}
	if (args[1] == u"on"_q) {
		history->setFolder(peer->owner().folder(Data::Folder::kId));
	} else {
		history->clearFolder();
	}
	return Result::Ok(Compact(Json{
		{ "archived", history->folder() != nullptr },
	}));
}

} // namespace

const HandlerMap &messageBusinessHandlers() {
	static const auto handlers = HandlerMap{
		{ u"peer.info"_q, &peerInfo },
		{ u"chat.members"_q, &members },
		{ u"chat.draft"_q, &draftInfo },
		{ u"message.list"_q, &localMessages },
		{ u"message.react"_q, &reactToMessage },
		{ u"message.preview"_q, &setMessagePreview },
		{ u"message.fetch"_q, &fetchMessages },
		{ u"message.media"_q, &rawMedia },
		{ u"message.media-file"_q, &saveAltFile },
		{ u"peer.resolve"_q, &resolvePeer },
		{ u"message.send-file"_q, &sendFile },
		{ u"message.send-album"_q, &sendAlbum },
		{ u"video.cover"_q, &openVideoCover },
		{ u"media.open"_q, &openMedia },
		{ u"media.state"_q, &mediaState },
		{ u"media.save-image"_q, &saveMediaImage },
		{ u"media.copy-check"_q, &checkCopyImage },
		{ u"message.shot"_q, &openMessageShot },
		{ u"simulation.remove-message"_q, &removeSimulationMessage },
		{ u"simulation.archive"_q, &archiveSimulationChat },
		{ u"mention.resolve"_q, &resolveMention },
		{ u"mention.send"_q, &sendMention },
	};
	return handlers;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
