#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "extras/extras_settings.h"

namespace ExtrasDebug::Commands {
namespace {

using json = nlohmann::json;

[[nodiscard]] Result GhostStatus(const QStringList &) {
	const auto session = ActiveSession();
	if (!session) {
		return Result::Err(u"no active session"_q);
	}
	const auto &settings = ExtrasSettings::getInstance();
	const auto &account = ExtrasSettings::ghost(session);
	return Result::Ok(Compact(json{
		{ "useGlobalGhostMode", settings.useGlobalGhostMode() },
		{ "enabled", account.isGhostModeActive() },
		{ "sendWithoutSound", account.shouldSendWithoutSound() },
		{ "scheduledMessages", account.isUseScheduledMessages() },
		{ "account", json(account) },
	}));
}

} // namespace

const HandlerMap &GhostHandlers() {
	static const auto result = HandlerMap{
		{ u"ghost.status"_q, &GhostStatus },
	};
	return result;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
