#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "extras/debug/debug_login.h"
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
#include "ui/emoji_config.h"

namespace ExtrasDebug::Commands {
namespace {

using Json = nlohmann::json;

Main::Session *savedSession() {
	const auto session = ActiveSession();
	return session && !isSimulationSession(session) ? session : nullptr;
}

Result state(const QStringList &args) {
	const auto session = savedSession();
	if (!session || args.empty()) return Result::Err(u"usage: message.cache-state <savedMessageId>..."_q);
	auto items = Json::array();
	for (const auto &id : args) {
		const auto item = findMessage(QString::number(session->userPeerId().value), id);
		if (!item) { items.push_back({ { "messageId", id.toStdString() }, { "present", false } }); continue; }
		auto result = Json{ { "messageId", item->id.bare }, { "present", true },
			{ "server", IsServerMsgId(item->id) }, { "groupId", item->groupId().value },
			{ "reactionsAsTags", item->reactionsAreTags() },
			{ "sendingReactions", session->data().reactions().sending(item) } };
		const auto media = item->media();
		if (const auto dice = dynamic_cast<Data::MediaDice*>(media)) {
			result["dice"] = { { "emoji", dice->emoji().toStdString() }, { "value", dice->value() } };
		}
		if (const auto poll = media ? media->poll() : nullptr) {
			auto answers = Json::array();
			for (const auto &answer : poll->answers) {
				answers.push_back({ { "votes", answer.votes }, { "chosen", answer.chosen }, { "correct", answer.correct } });
			}
			result["poll"] = { { "id", poll->id }, { "voters", poll->totalVoters },
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
		{ u"message.cache-state"_q, &state },
		{ u"message.cache-react"_q, &react },
	};
	return handlers;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
