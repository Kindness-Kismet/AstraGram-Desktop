#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "extras/debug/debug_login.h"
#include "apiwrap.h"
#include "api/api_common.h"
#include "api/api_polls.h"
#include "api/api_sending.h"
#include "chat_helpers/stickers_emoji_pack.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_message_reactions.h"
#include "data/data_web_page.h"
#include "data/data_media_types.h"
#include "data/data_poll.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "media/player/media_player_instance.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/emoji_config.h"
#include "ui/widgets/fields/input_field.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>

namespace ExtrasDebug::Commands {
namespace {

using Json = nlohmann::json;

Main::Session *savedSession() {
	const auto session = ActiveSession();
	return session && !isSimulationSession(session) ? session : nullptr;
}

Api::SendAction savedAction(not_null<Main::Session*> session) {
	auto action = Api::SendAction(session->data().history(session->userPeerId()));
	action.clearDraft = false;
	return action;
}

Result sample(const QStringList &args) {
	const auto session = savedSession();
	if (!session) return Result::Err(u"an authenticated session is required"_q);
	if (args.empty()) return Result::Err(u"usage: message.cache-sample <poll|poll-media|reply|contact|location|voice|round|album|document|sticker|custom-emoji> [args]"_q);
	const auto type = args[0];
	const auto action = savedAction(session);
	if ((type == u"poll"_q && args.size() == 1)
		|| (type == u"poll-media"_q && args.size() == 3)) {
		auto poll = PollData(&session->data(), 0);
		poll.question = { u"缓存验证：重启后能否保留投票？"_q };
		poll.answers = {
			{ .text = { u"能保留"_q }, .option = QByteArray(1, '0') },
			{ .text = { u"继续检查"_q }, .option = QByteArray(1, '1') },
		};
		if (type == u"poll-media"_q) {
			const auto self = QString::number(session->userPeerId().value);
			const auto image = findMessage(self, args[1]);
			const auto file = findMessage(self, args[2]);
			const auto photo = image && image->media() ? image->media()->photo() : nullptr;
			const auto document = file && file->media() ? file->media()->document() : nullptr;
			if (!photo || !document) return Result::Err(u"expected saved photo and document messages"_q);
			poll.setFlags(PollData::Flag::Quiz);
			poll.attachedMedia.photo = photo;
			poll.answers[0].media.photo = photo;
			poll.answers[0].correct = true;
			poll.answers[1].media.document = document;
			poll.solution = { u"缓存验证：图片选项为正确答案。"_q };
			poll.solutionMedia.document = document;
		}
		const auto job = beginJob("cache-sample-poll");
		session->lifetime().add([job] { finishJob(job, false, "session closed"); });
		session->api().polls().create(poll, {}, action,
			[job] { finishJob(job, true, Json{ { "confirmed", true } }); },
			[job](bool) { finishJob(job, false, "poll send failed"); });
		return jobStarted(job);
	}
	if (type == u"reply"_q && args.size() == 2) {
		const auto item = findMessage(QString::number(session->userPeerId().value), args[1]);
		if (!item || !IsServerMsgId(item->id)) return Result::Err(u"saved source message not found"_q);
		auto replyAction = action;
		replyAction.replyTo.messageId = item->fullId();
		auto message = Api::MessageToSend(replyAction);
		message.textWithTags = { u"缓存验证：离线恢复附件回复。"_q, {} };
		session->api().sendMessage(std::move(message));
		return Result::Ok(Compact(Json{ { "state", "submitted" } }));
	}
	if (type == u"contact"_q && args.size() == 1) {
		const auto job = beginJob("cache-sample-contact");
		session->api().shareContact(u"+12025550123"_q,
			u"缓存测试"_q, u"虚构联系人"_q, action,
			[job](bool success) { finishJob(job, success, Json{ { "confirmed", success } }); });
		return jobStarted(job);
	}
	if (type == u"location"_q && args.size() == 1) {
		Api::SendLocation(action, 0., 0.);
		return Result::Ok(Compact(Json{ { "state", "submitted" } }));
	}
	if ((type == u"voice"_q || type == u"round"_q) && args.size() == 3) {
		auto file = QFile(args[1]);
		auto ok = false;
		const auto duration = args[2].toInt(&ok);
		if (!ok || duration <= 0 || !file.open(QIODevice::ReadOnly)
			|| file.size() > 5 * 1024 * 1024) {
			return Result::Err(u"expected a file up to 5 MB and duration in milliseconds"_q);
		}
		session->api().sendVoiceMessage(file.readAll(), {}, duration,
			type == u"round"_q, action);
		return Result::Ok(Compact(Json{ { "state", "submitted" } }));
	}
	if (type == u"document"_q && args.size() == 3) {
		const auto item = findMessage(args[1], args[2]);
		const auto document = item && item->media() ? item->media()->document() : nullptr;
		if (!document) return Result::Err(u"source document not found"_q);
		Api::SendExistingDocument(Api::MessageToSend(action), document);
		return Result::Ok(Compact(Json{ { "state", "submitted" } }));
	}
	if (type == u"sticker"_q && args.size() == 2) {
		const auto document = session->data().document(args[1].toULongLong());
		if (!document->sticker() || !document->hasRemoteLocation()) {
			return Result::Err(u"expected a loaded sticker document"_q);
		}
		Api::SendExistingDocument(Api::MessageToSend(action), document);
		return Result::Ok(Compact(Json{ { "state", "submitted" } }));
	}
	if (type == u"custom-emoji"_q && args.size() == 3) {
		auto ok = false;
		const auto id = args[1].toULongLong(&ok);
		if (!ok || !id || args[2].isEmpty()) return Result::Err(u"expected a document id and emoji text"_q);
		auto message = Api::MessageToSend(action);
		message.textWithTags = { args[2], { { 0, args[2].size(),
			Ui::InputField::CustomEmojiLink(Data::SerializeCustomEmojiId(id)) } } };
		session->api().sendMessage(std::move(message));
		return Result::Ok(Compact(Json{ { "state", "submitted" } }));
	}
	if (type == u"album"_q && args.size() == 3) {
		for (const auto &path : args.mid(1)) {
			if (!QFileInfo(path).isAbsolute() || !QFileInfo(path).isFile()) {
				return Result::Err(u"expected absolute image paths"_q);
			}
		}
		const auto job = beginJob("cache-sample-album");
		const auto weak = base::make_weak(session);
		const auto premium = session->premium();
		crl::async([=] {
			auto list = Storage::PrepareMediaList(args.mid(1), 320, premium);
			crl::on_main([=, list = std::move(list)]() mutable {
				if (!weak) { finishJob(job, false, "session closed"); return; }
				if (list.error != Ui::PreparedList::Error::None || list.files.size() != 2) {
					finishJob(job, false, "could not prepare album");
					return;
				}
				weak->api().sendFiles(std::move(list), SendMediaType::Photo,
					std::make_shared<SendingAlbum>(), savedAction(weak.get()));
				finishJob(job, true, Json{ { "state", "submitted" } });
			});
		});
		return jobStarted(job);
	}
	return Result::Err(u"invalid sample type or arguments"_q);
}

Result vote(const QStringList &args) {
	const auto session = savedSession();
	if (!session || args.size() != 2) return Result::Err(u"usage: message.cache-vote <savedMessageId> <answerIndex>"_q);
	const auto item = findMessage(QString::number(session->userPeerId().value), args[0]);
	const auto poll = item && item->media() ? item->media()->poll() : nullptr;
	auto ok = false;
	const auto index = args[1].toInt(&ok);
	if (!poll || !ok || index < 0 || index >= poll->answers.size()) return Result::Err(u"poll or answer not found"_q);
	session->api().polls().sendVotes(item->fullId(), { poll->answers[index].option });
	return Result::Ok(Compact(Json{ { "state", "submitted" } }));
}

Result play(const QStringList &args) {
	const auto session = savedSession();
	if (!session || args.size() != 1) {
		return Result::Err(u"usage: message.cache-play <savedMessageId>"_q);
	}
	const auto item = findMessage(QString::number(session->userPeerId().value), args[0]);
	const auto document = item && item->media() ? item->media()->document() : nullptr;
	if (!document || (!document->isVoiceMessage()
		&& !document->isVideoMessage() && !document->isSong())) {
		return Result::Err(u"expected a voice message, round video, or music document"_q);
	}
	Media::Player::instance()->play(AudioMsgId(document, item->fullId()));
	return Result::Ok(Compact(Json{ { "state", "submitted" } }));
}

Result state(const QStringList &args) {
	const auto session = savedSession();
	if (!session || args.empty()) return Result::Err(u"usage: message.cache-state <savedMessageId>..."_q);
	auto items = Json::array();
	for (const auto &id : args) {
		const auto item = findMessage(QString::number(session->userPeerId().value), id);
		if (!item) { items.push_back({ { "messageId", id.toStdString() }, { "present", false } }); continue; }
		auto result = Json{ { "messageId", item->id.bare }, { "present", true },
			{ "replyToId", item->replyToId().bare },
			{ "server", IsServerMsgId(item->id) }, { "groupId", item->groupId().value },
			{ "hiddenLinks", item->hideLinks() },
			{ "reactionsAsTags", item->reactionsAreTags() },
			{ "sendingReactions", session->data().reactions().sending(item) } };
		const auto media = item->media();
		if (const auto dice = dynamic_cast<Data::MediaDice*>(media)) {
			result["dice"] = { { "emoji", dice->emoji().toStdString() }, { "value", dice->value() } };
		}
		if (const auto poll = media ? media->poll() : nullptr) {
			const auto describe = [](const PollMedia &media) {
				return Json{ { "photoId", media.photo ? media.photo->id : 0 },
					{ "documentId", media.document ? media.document->id : 0 } };
			};
			auto answers = Json::array();
			for (const auto &answer : poll->answers) {
				answers.push_back({ { "votes", answer.votes }, { "chosen", answer.chosen },
					{ "correct", answer.correct }, { "media", describe(answer.media) } });
			}
			result["poll"] = { { "id", poll->id }, { "voters", poll->totalVoters },
				{ "attachedMedia", describe(poll->attachedMedia) },
				{ "solutionMedia", describe(poll->solutionMedia) },
				{ "closed", poll->closed() }, { "answers", std::move(answers) } };
		}
		if (const auto document = media ? media->document() : nullptr) {
			const auto view = document->activeMediaView();
			result["document"] = { { "id", document->id }, { "size", document->size },
				{ "voice", document->isVoiceMessage() }, { "round", document->isVideoMessage() },
				{ "song", document->isSong() }, { "animation", document->isAnimation() },
				{ "sticker", document->sticker() != nullptr }, { "loaded", view && view->loaded() } };
		}
		if (const auto photo = media ? media->photo() : nullptr) {
			const auto view = photo->activeMediaView();
			result["photo"] = { { "id", photo->id }, { "loaded", view && view->loaded() } };
		}
		if (const auto page = media ? media->webpage() : nullptr) {
			result["webpage"] = { { "id", page->id }, { "pending", page->pendingTill > 0 },
				{ "failed", bool(page->failed) }, { "pendingTill", page->pendingTill },
				{ "titleBytes", page->title.size() }, { "descriptionBytes", page->description.text.size() } };
		}
		result["contact"] = media && media->sharedContact();
		result["location"] = media && media->location();
		auto reactions = Json::array();
		for (const auto &reaction : item->reactions()) {
			reactions.push_back({ { "emoji", reaction.id.emoji().toStdString() },
				{ "count", reaction.count }, { "my", reaction.my } });
		}
		result["reactions"] = std::move(reactions);
		auto emoji = Json::array();
		for (const auto &entity : item->originalText().entities) {
			if (entity.type() != EntityType::CustomEmoji) continue;
			const auto id = Data::ParseCustomEmojiData(entity.data());
			emoji.push_back({ { "id", id }, { "metadataLoaded", session->data().document(id)->sticker() != nullptr } });
		}
		result["customEmoji"] = std::move(emoji);
		items.push_back(std::move(result));
	}
	return Result::Ok(Compact(Json{ { "userId", session->userId().bare },
		{ "premium", session->premium() }, { "items", std::move(items) } }));
}

Result react(const QStringList &args) {
	const auto session = savedSession();
	if (!session || args.size() != 2) return Result::Err(u"usage: message.cache-react <savedMessageId> <emoji>"_q);
	const auto item = findMessage(QString::number(session->userPeerId().value), args[0]);
	if (!item || !item->canReact() || !Ui::Emoji::Find(args[1])) return Result::Err(u"message cannot react or emoji not found"_q);
	if (item->reactionsAreTags() && !session->premium()) {
		return Result::Err(u"Saved Messages reaction tags require Premium"_q);
	}
	item->toggleReaction(Data::ReactionId{ args[1] }, HistoryReactionSource::Selector);
	return Result::Ok(Compact(Json{ { "state", "submitted" } }));
}

} // namespace

const HandlerMap &messageCacheHandlers() {
	static const auto handlers = HandlerMap{
		{ u"message.cache-sample"_q, &sample },
		{ u"message.cache-vote"_q, &vote },
		{ u"message.cache-play"_q, &play },
		{ u"message.cache-state"_q, &state },
		{ u"message.cache-react"_q, &react },
	};
	return handlers;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
