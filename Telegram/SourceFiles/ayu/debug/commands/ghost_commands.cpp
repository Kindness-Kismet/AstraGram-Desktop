#ifdef _DEBUG
#include "ayu/debug/commands/commands_internal.h"

#include "ayu/ayu_settings.h"

namespace AyuDebug::Commands {
namespace {

using json = nlohmann::json;

[[nodiscard]] Result GhostStatus(const QStringList &) {
	const auto session = ActiveSession();
	if (!session) {
		return Result::Err(u"no active session"_q);
	}
	const auto &settings = AyuSettings::getInstance();
	const auto &account = AyuSettings::ghost(session);
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

} // namespace AyuDebug::Commands
#endif // _DEBUG
