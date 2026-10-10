#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "extras/debug/debug_login.h"

#include "main/main_session.h"

namespace ExtrasDebug::Commands {
namespace {

using json = nlohmann::json;

// 与登录页共用初始化入口，场景数据随会话创建，不依赖客户端环境变量。
[[nodiscard]] Result enterSimulationMode(const QStringList &args) {
	if (args.size() > 1) {
		return Result::Err(u"usage: simulation.enter [userId]"_q);
	}
	auto userId = int64(999999999);
	if (args.size() == 1) {
		auto ok = false;
		userId = args.front().toLongLong(&ok);
		if (!ok || userId <= 0) {
			return Result::Err(u"expected a positive integer userId"_q);
		}
	}
	if (const auto error = enterSimulation(userId); !error.isEmpty()) {
		return Result::Err(error);
	}
	return Result::Ok(Compact(json{
		{ "userId", ActiveSession()->userId().bare },
		{ "note", "local simulation mode" },
	}));
}

} // namespace

const HandlerMap &SessionHandlers() {
	static const auto result = HandlerMap{
		{ u"simulation.enter"_q, &enterSimulationMode },
	};
	return result;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
