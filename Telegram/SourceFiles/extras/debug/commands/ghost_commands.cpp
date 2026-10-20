#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "extras/extras_settings.h"
#include "main/main_session.h"

namespace ExtrasDebug::Commands {
namespace {

using json = nlohmann::json;

[[nodiscard]] Result GhostStatus(const QStringList &) {
	const auto session = ActiveSession();
	if (!session) {
		return Result::Err(u"no active session"_q);
	}
	const auto &account = ExtrasSettings::ghost(session);
	return Result::Ok(Compact(json{
		{ "userId", session->userId().bare },
		{ "enabled", account.isGhostModeActive() },
		{ "sendWithoutSound", account.shouldSendWithoutSound() },
		{ "scheduledMessages", account.isUseScheduledMessages() },
		{ "account", json(account) },
	}));
}

Result accountSettings(const QStringList &args) {
	if (args.size() != 1 && args.size() != 3) {
		return Result::Err(u"usage: settings.account <userId> [ghost.enabled|streamer.enabled value]"_q);
	}
	auto ok = false;
	const auto id = args[0].toULongLong(&ok);
	if (!ok || !id) {
		return Result::Err(u"expected a positive account id"_q);
	}
	auto &ghost = ExtrasSettings::ghost(id);
	auto &streamer = ExtrasSettings::streamer(id);
	if (args.size() == 1) {
		return Result::Ok(Compact(json{
			{ "userId", id }, { "ghost", json(ghost) }, { "streamer", json(streamer) },
		}));
	}
	if ((args[1] != u"ghost.enabled" && args[1] != u"streamer.enabled")
		|| (args[2] != u"true" && args[2] != u"false")) {
		return Result::Err(u"expected ghost.enabled or streamer.enabled and a boolean"_q);
	}
	const auto enabled = args[2] == u"true";
	const auto job = beginJob("account-settings-save");
	ExtrasSettings::beginBatchUpdate();
	if (args[1] == u"ghost.enabled") {
		ghost.setGhostModeEnabled(enabled);
	} else {
		streamer.setEnabled(enabled);
	}
	ExtrasSettings::save();
	ExtrasSettings::endBatchUpdate([=](bool saved) {
		finishJob(job, saved, { { "saved", saved }, { "userId", id } });
	});
	return jobStarted(job);
}

} // namespace

const HandlerMap &GhostHandlers() {
	static const auto result = HandlerMap{
		{ u"ghost.status"_q, &GhostStatus },
		{ u"settings.account"_q, &accountSettings },
	};
	return result;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
