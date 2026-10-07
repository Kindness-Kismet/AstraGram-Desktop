#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "extras/extras_settings.h"
#include "extras/extras_state.h"
#include "extras/data/extras_database.h"
#include "extras/data/messages_storage.h"
#include "extras/debug/debug_login.h"
#include "extras/debug/commands/message_archive_tests.h"
#include "extras/features/filters/filters_controller.h"
#include "extras/features/translator/message_translation.h"
#include "extras/ui/context_menu/context_menu.h"
#include "extras/utils/telegram_helpers.h"
#include "core/application.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"
#include "settings.h"
#include <QFileInfo>

namespace ExtrasDebug::Commands {
namespace {

using Json = nlohmann::json;

Json describeStored(const ExtrasMessageBase &message) {
	return {{"id", message.fakeId}, {"messageId", message.messageId},
		{"dialogId", message.dialogId}, {"topicId", message.topicId},
		{"fromId", message.fromId}, {"text", message.text},
		{"date", message.date}, {"editDate", message.editDate},
		{"savedAt", message.entityCreateDate}};
}

Result storageStats(const QStringList &args) {
	if (!args.empty()) return Result::Err(u"usage: storage.stats"_q);
	const auto &settings = ExtrasSettings::getInstance();
	const auto path = cWorkingDir() + u"tdata/extrasdata.db"_q;
	const auto info = QFileInfo(path);
	return Result::Ok(Compact(Json{
		{"saveDeletedMessages", settings.saveDeletedMessages()},
		{"saveMessagesHistory", settings.saveMessagesHistory()},
		{"saveForBots", settings.saveForBots()},
		{"databasePath", path.toStdString()},
		{"databaseExists", info.exists()},
		{"databaseBytes", info.exists() ? info.size() : 0},
		{"filters", Database::getCount()},
		{"archiveReady", Database::messageArchiveReady()},
		{"archiveError", Database::messageArchiveError().toStdString()},
	}));
}

Result deletedMessages(const QStringList &args) {
	if (args.empty() || args.size() > 3) return Result::Err(u"usage: storage.deleted <peerId> [limit] [search]"_q);
	if (Core::App().passcodeLocked()) return Result::Err(u"local passcode is locked"_q);
	const auto peer = findPeer(args[0]);
	if (!peer) return Result::Err(u"peer not found"_q);
	auto ok = true;
	const auto limit = args.size() > 1 ? args[1].toInt(&ok) : 100;
	if (!ok || limit < 1 || limit > 1000) return Result::Err(u"limit must be between 1 and 1000"_q);
	auto result = Json::array();
	for (const auto &message : ExtrasMessages::getDeletedMessages(peer, 0, 0, 0, limit,
		args.size() == 3 ? args[2] : QString())) result.push_back(describeStored(message));
	if (!Database::messageArchiveError().isEmpty()) {
		return Result::Err(u"message archive is unavailable; stored data was preserved"_q);
	}
	return Result::Ok(Compact(result));
}

Result editedMessages(const QStringList &args) {
	if (args.size() < 2 || args.size() > 3) return Result::Err(u"usage: storage.edits <peerId> <messageId> [limit]"_q);
	if (Core::App().passcodeLocked()) return Result::Err(u"local passcode is locked"_q);
	const auto peer = findPeer(args[0]);
	auto idOk = false;
	const auto id = args[1].toInt(&idOk);
	auto limitOk = true;
	const auto limit = args.size() == 3 ? args[2].toInt(&limitOk) : 100;
	if (!peer || !idOk || id <= 0) return Result::Err(u"expected a loaded peer and a positive message id"_q);
	if (!limitOk || limit < 1 || limit > 1000) return Result::Err(u"limit must be between 1 and 1000"_q);
	auto result = Json::array();
	for (const auto &message : Database::getEditedMessages(
		ActiveSession()->userId().bare & PeerId::kChatTypeMask,
		getDialogIdFromPeer(peer), id, 0, 0, limit)) result.push_back(describeStored(message));
	if (!Database::messageArchiveError().isEmpty()) {
		return Result::Err(u"message archive is unavailable; stored data was preserved"_q);
	}
	return Result::Ok(Compact(result));
}

Result inspectMessage(const QStringList &args) {
	if (args.size() != 2) return Result::Err(u"usage: message.inspect <peerId> <messageId>"_q);
	const auto item = findMessage(args[0], args[1]);
	if (!item) return Result::Err(u"message not found"_q);
	const auto translation = item->translation();
	auto result = describeMessage(item);
	result.update(Json{
		{"peerId", item->history()->peer->id.value}, {"messageId", item->id.bare},
		{"text", item->originalText().text.toStdString()}, {"outgoing", item->out()},
		{"deleted", item->isDeleted()}, {"hidden", ExtrasState::isHidden(item)},
		{"filtered", FiltersController::filtered(item)}, {"hasRevisions", ExtrasMessages::hasRevisions(item)},
		{"hasView", item->mainView() != nullptr},
		{"deletedOpacity", item->mainView() ? Json(item->mainView()->deletedOpacity()) : Json(nullptr)},
		{"translatedText", item->translatedText().text.toStdString()},
		{"translationShown", item->translationDisplayed()},
		{"translationManual", translation && translation->manualTo.has_value()},
		{"translationRequested", translation && translation->requested},
		{"translationFailed", translation && translation->failed},
		{"chatTranslationActive", bool(item->history()->translatedTo())},
	});
	return Result::Ok(Compact(result));
}

Result translateMessage(const QStringList &args) {
	if (args.size() != 2) return Result::Err(u"usage: message.translate <peerId> <messageId>"_q);
	const auto item = findMessage(args[0], args[1]);
	if (!item) return Result::Err(u"message not found"_q);
	item->history()->session().messageTranslations().translate(item, [](QString error) {
		LOG(("Translation: %1").arg(error));
	});
	return inspectMessage(args);
}

Result showOriginalMessage(const QStringList &args) {
	if (args.size() != 2) return Result::Err(u"usage: message.show-original <peerId> <messageId>"_q);
	const auto item = findMessage(args[0], args[1]);
	if (!item) return Result::Err(u"message not found"_q);
	item->history()->session().messageTranslations().showOriginal(item);
	return inspectMessage(args);
}

Result editLocalMessage(const QStringList &args) {
	if (args.size() != 3) return Result::Err(u"usage: message.edit-local <peerId> <messageId> <text>"_q);
	const auto session = ActiveSession();
	if (!session || !isFakeSession(session)) return Result::Err(u"an in-process fake session is required"_q);
	const auto item = findMessage(args[0], args[1]);
	if (!item) return Result::Err(u"message not found"_q);
	if (item->media() || item->isService()) return Result::Err(u"only local text samples can be edited"_q);
	session->data().updateEditedMessage(fakeTextMessage(
		item->history()->peer, item->from()->id, item->id.bare, args[2], true));
	return inspectMessage({args[0], args[1]});
}

Result deleteLocalMessage(const QStringList &args) {
	if (args.size() != 2) return Result::Err(u"usage: message.delete-local <peerId> <messageId>"_q);
	const auto session = ActiveSession();
	if (!session || !isFakeSession(session)) return Result::Err(u"an in-process fake session is required"_q);
	const auto item = findMessage(args[0], args[1]);
	if (!item) return Result::Err(u"message not found"_q);
	processMessageDelete(item);
	return Result::Ok();
}

Result hideMessage(const QStringList &args) {
	if (args.size() != 2) return Result::Err(u"usage: message.hide <peerId> <messageId>"_q);
	const auto item = findMessage(args[0], args[1]);
	if (!item) return Result::Err(u"message not found"_q);
	if (item->history()->peer->isSelf()) return Result::Err(u"hiding is unavailable in Saved Messages"_q);
	ExtrasUi::HideMessage(item);
	return Result::Ok();
}

} // namespace

const HandlerMap &StorageHandlers() {
	static const auto result = HandlerMap{
		{u"storage.stats"_q, &storageStats},
		{u"storage.verify-archive"_q, &verifyMessageArchive},
		{u"storage.deleted"_q, &deletedMessages},
		{u"storage.edits"_q, &editedMessages},
		{u"message.inspect"_q, &inspectMessage},
		{u"message.translate"_q, &translateMessage},
		{u"message.show-original"_q, &showOriginalMessage},
		{u"message.edit-local"_q, &editLocalMessage},
		{u"message.delete-local"_q, &deleteLocalMessage},
		{u"message.hide"_q, &hideMessage},
	};
	return result;
}

} // namespace ExtrasDebug::Commands
#endif
