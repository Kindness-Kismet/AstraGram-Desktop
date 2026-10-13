#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"
#include "extras/debug/debug_login.h"
#include "extras/features/mention_by_id/mention_by_id.h"
#include "extras/features/video_cover/video_cover.h"

#include "apiwrap.h"
#include "api/api_common.h"
#include "chat_helpers/message_field.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_compose_stash.h"
#include "data/data_document.h"
#include "data/data_drafts.h"
#include "data/data_history_messages.h"
#include "data/data_media_types.h"
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
				{ 0, args[2].size(), PrepareMentionTag(user) },
			} };
			session->api().sendMessage(std::move(message));
			finishJob(id, true, Json{ { "state", "submitted" },
				{ "peerId", peer->id.value }, { "user", userInfo(user) } });
		}, [id](QString error) { finishJob(id, false, error.toStdString()); });
	return jobStarted(id);
}

// --media 对应发送窗口里的“压缩”，视频按可播放的媒体发送。
Result sendFile(const QStringList &arguments) {
	auto args = arguments;
	const auto asMedia = (args.removeAll(u"--media"_q) > 0);
	if (args.size() < 2 || args.size() > 3) return Result::Err(u"usage: message.send-file <peerId> <path> [caption] [--media]"_q);
	const auto peer = findPeer(args[0]);
	if (!peer) return Result::Err(u"peer not found"_q);
	const auto session = &peer->session();
	if (isSimulationSession(session)) return Result::Err(u"an authenticated session is required"_q);
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
			const auto type = asMedia ? SendMediaType::Photo : SendMediaType::File;
			weak->api().sendFiles(std::move(list), type, nullptr, action);
			finishJob(id, true, Json{ { "state", "submitted" }, { "peerId", peerId.value },
				{ "filename", QFileInfo(filename).fileName().toStdString() }, { "media", asMedia } });
		});
	});
	return jobStarted(id);
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

} // namespace

const HandlerMap &messageBusinessHandlers() {
	static const auto handlers = HandlerMap{
		{ u"peer.info"_q, &peerInfo },
		{ u"chat.members"_q, &members },
		{ u"chat.draft"_q, &draftInfo },
		{ u"message.list"_q, &localMessages },
		{ u"message.fetch"_q, &fetchMessages },
		{ u"message.media"_q, &rawMedia },
		{ u"message.media-file"_q, &saveAltFile },
		{ u"peer.resolve"_q, &resolvePeer },
		{ u"message.send-file"_q, &sendFile },
		{ u"video.cover"_q, &openVideoCover },
		{ u"mention.resolve"_q, &resolveMention },
		{ u"mention.send"_q, &sendMention },
	};
	return handlers;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
