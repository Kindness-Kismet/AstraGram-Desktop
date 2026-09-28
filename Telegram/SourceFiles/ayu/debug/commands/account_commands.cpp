#ifdef _DEBUG
#include "ayu/debug/commands/settings_registry.h"

#include "ayu/debug/debug_login.h"
#include "api/api_global_privacy.h"
#include "api/api_user_privacy.h"
#include "apiwrap.h"
#include "core/application.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"

namespace AyuDebug::Commands {
namespace {

using Json = nlohmann::json;

Result listAccounts(const QStringList &args) {
	if (!args.empty()) return Result::Err(u"usage: session.list"_q);
	auto result = Json::array();
	for (const auto &entry : Core::App().domain().accounts()) {
		const auto &account = *entry.account;
		const auto session = account.sessionExists() ? &account.session() : nullptr;
		result.push_back({{"index", entry.index}, {"active", &account == &Core::App().activeAccount()},
			{"hasSession", session != nullptr}, {"userId", session ? Json(session->userId().bare) : Json(nullptr)},
			{"fake", session && isFakeSession(session)}});
	}
	return Result::Ok(Compact(result));
}

Result activateAccount(const QStringList &args) {
	if (args.size() != 1) return Result::Err(u"usage: session.activate <index>"_q);
	auto ok = false;
	const auto index = args[0].toInt(&ok);
	if (!ok) return Result::Err(u"expected an account index"_q);
	for (const auto &entry : Core::App().domain().accounts()) {
		if (entry.index != index) continue;
		Core::App().domain().activate(entry.account.get());
		return Result::Ok();
	}
	return Result::Err(u"account not found"_q);
}

Result describePeerSettings(not_null<PeerData*> peer) {
	auto &settings = peer->session().settings();
	return Result::Ok(Compact(Json{
		{"autoDownload", int(settings.autoDownload().peerOverride(peer->id))},
		{"subsectionTabsMode", settings.subsectionTabsMode(peer->id)},
		{"groupStickersHidden", settings.isGroupStickersSectionHidden(peer->id)},
		{"groupEmojiHidden", settings.isGroupEmojiSectionHidden(peer->id)},
	}));
}

// 只修改内存中的会话设置，由调用方保存。
Result applyPeerSetting(not_null<PeerData*> peer, const QString &key, const QString &value) {
	auto &settings = peer->session().settings();
	auto ok = false;
	const auto number = value.toInt(&ok);
	if (key == u"autoDownload"_q) {
		if (!ok) return Result::Err(u"expected an integer"_q);
		if (number < 0 || number > 2) return Result::Err(u"autoDownload override must be between 0 and 2"_q);
		settings.autoDownload().setPeerOverride(peer->id, Data::AutoDownload::Override(number));
		return Result::Ok();
	}
	if (key == u"subsectionTabsMode"_q) {
		if (!ok) return Result::Err(u"expected an integer"_q);
		settings.setSubsectionTabsMode(peer->id, number);
		return Result::Ok();
	}
	if (key != u"groupStickersHidden"_q && key != u"groupEmojiHidden"_q) return Result::Err(u"unknown peer setting"_q);
	if (value != u"true"_q && value != u"false"_q) return Result::Err(u"expected true or false"_q);
	const auto hidden = (value == u"true"_q);
	if (key == u"groupStickersHidden"_q) {
		if (hidden) settings.setGroupStickersSectionHidden(peer->id);
		else settings.removeGroupStickersSectionHidden(peer->id);
	} else if (hidden) {
		settings.setGroupEmojiSectionHidden(peer->id);
	} else {
		settings.removeGroupEmojiSectionHidden(peer->id);
	}
	return Result::Ok();
}

Result peerSettings(const QStringList &args) {
	if (args.empty() || args.size() > 3 || args.size() == 2) {
		return Result::Err(u"usage: session.peer-settings <peerId> [key value]"_q);
	}
	const auto peer = findPeer(args[0]);
	if (!peer) return Result::Err(u"peer not found"_q);
	if (args.size() == 1) return describePeerSettings(peer);
	if (auto result = applyPeerSetting(peer, args[1], args[2]); !result.ok) return result;
	peer->session().saveSettings();
	return describePeerSettings(peer);
}


Result threadSettings(const QStringList &args) {
	if (args.size() != 3 && args.size() != 5) return Result::Err(u"usage: session.thread-settings <peerId> <topicId> <subpeerId> [key value]"_q);
	const auto peer = findPeer(args[0]);
	auto topicOk = false;
	auto subpeerOk = false;
	const auto topic = args[1].toInt(&topicOk);
	const auto subpeer = args[2].toULongLong(&subpeerOk);
	if (!peer || !topicOk || topic < 0 || !subpeerOk) return Result::Err(u"invalid peer or thread id"_q);
	auto &settings = peer->session().settings();
	const auto describe = [&] {
		return Result::Ok(Compact(Json{
			{"hiddenPinnedMessageId", settings.hiddenPinnedMessageId(peer->id, MsgId(topic), PeerId(subpeer)).bare},
			{"ringtoneVolume", settings.ringtoneVolume(peer->id, MsgId(topic), PeerId(subpeer))},
		}));
	};
	if (args.size() == 3) return describe();
	auto ok = false;
	const auto value = args[4].toInt(&ok);
	if (!ok || value < 0) return Result::Err(u"expected a non-negative integer"_q);
	if (args[3] == u"hiddenPinnedMessageId"_q) {
		settings.setHiddenPinnedMessageId(peer->id, MsgId(topic), PeerId(subpeer), MsgId(value));
	} else if (args[3] == u"ringtoneVolume"_q) {
		if (value > 100) return Result::Err(u"volume must be between 0 and 100"_q);
		settings.setRingtoneVolume(peer->id, MsgId(topic), PeerId(subpeer), ushort(value));
	} else {
		return Result::Err(u"unknown thread setting"_q);
	}
	peer->session().saveSettings();
	return describe();
}

Result globalPrivacy(const QStringList &args) {
	if (!args.empty()) return Result::Err(u"usage: privacy.get"_q);
	const auto session = ActiveSession();
	if (!session) return Result::Err(u"an active session is required"_q);
	const auto &privacy = session->api().globalPrivacy();
	return Result::Ok(Compact(Json{
		{"localOnly", isFakeSession(session)},
		{"archiveAndMute", privacy.archiveAndMuteCurrent()},
		{"unarchiveOnNewMessage", int(privacy.unarchiveOnNewMessageCurrent())},
		{"hideReadTime", privacy.hideReadTimeCurrent()},
		{"requirePremium", privacy.newRequirePremiumCurrent()},
		{"chargeStars", privacy.newChargeStarsCurrent()},
		{"disallowedGiftTypes", privacy.disallowedGiftTypesCurrent().value()},
		{"paidReactionShownPeer", privacy.paidReactionShownPeerCurrent().value},
	}));
}

Result setPrivacy(const QStringList &args) {
	if (args.size() != 2) return Result::Err(u"usage: privacy.set <key> <value>"_q);
	const auto session = ActiveSession();
	if (!session || isFakeSession(session)) return Result::Err(u"an authenticated session is required"_q);
	auto &privacy = session->api().globalPrivacy();
	const auto &key = args[0];
	const auto value = Json::parse(args[1].toStdString(), nullptr, false);
	if (value.is_discarded()) return Result::Err(u"invalid JSON value"_q);
	int64 number = 0;
	if (value.is_number()) {
		if (const auto error = readSetting(value, number); !error.isEmpty()) return Result::Err(error);
	}
	if (key == u"archiveAndMute"_q || key == u"hideReadTime"_q || key == u"requirePremium"_q) {
		if (!value.is_boolean()) return Result::Err(u"expected a boolean"_q);
		if (key == u"archiveAndMute"_q) privacy.updateArchiveAndMute(value.get<bool>());
		else if (key == u"hideReadTime"_q) privacy.updateHideReadTime(value.get<bool>());
		else privacy.updateMessagesPrivacy(value.get<bool>(), privacy.newChargeStarsCurrent());
	} else if (key == u"unarchiveOnNewMessage"_q) {
		if (!value.is_number_integer() || number < 0 || number > 2) return Result::Err(u"invalid unarchive mode"_q);
		privacy.updateUnarchiveOnNewMessage(Api::UnarchiveOnNewMessage(number));
	} else if (key == u"chargeStars"_q) {
		if (!value.is_number_integer() || number < 0 || number > 1000000) return Result::Err(u"invalid message price"_q);
		privacy.updateMessagesPrivacy(privacy.newRequirePremiumCurrent(), int(number));
	} else if (key == u"disallowedGiftTypes"_q) {
		if (!value.is_number_integer() || number < 0 || number > 63) return Result::Err(u"invalid gift flags"_q);
		privacy.updateDisallowedGiftTypes(Api::DisallowedGiftTypes::from_raw(uchar(number)));
	} else if (key == u"paidReactionShownPeer"_q) {
		if (!value.is_number_unsigned() && (!value.is_number_integer() || value.get<int64>() < 0)) return Result::Err(u"invalid peer id"_q);
		privacy.updatePaidReactionShownPeer(PeerId(value.get<uint64>()));
	} else {
		return Result::Err(u"unknown privacy setting"_q);
	}
	return Result::Ok(u"request submitted; use privacy.reload and privacy.get to check server state"_q);
}

Result reloadPrivacy(const QStringList &args) {
	if (!args.empty()) return Result::Err(u"usage: privacy.reload"_q);
	const auto session = ActiveSession();
	if (!session || isFakeSession(session)) return Result::Err(u"an authenticated session is required"_q);
	session->api().globalPrivacy().reload();
	return Result::Ok(u"reload requested"_q);
}

} // namespace

const HandlerMap &AccountHandlers() {
	static const auto result = HandlerMap{
		{u"session.list"_q, &listAccounts}, {u"session.activate"_q, &activateAccount},
		{u"session.peer-settings"_q, &peerSettings}, {u"privacy.get"_q, &globalPrivacy},
		{u"session.thread-settings"_q, &threadSettings},
		{u"privacy.set"_q, &setPrivacy}, {u"privacy.reload"_q, &reloadPrivacy},
	};
	return result;
}

} // namespace AyuDebug::Commands
#endif
