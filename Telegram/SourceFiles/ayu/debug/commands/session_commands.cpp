#ifdef _DEBUG
#include "ayu/debug/commands/commands_internal.h"

#include "ayu/debug/debug_login.h"

#include "core/application.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/mtproto_dc_options.h"

namespace AyuDebug::Commands {
namespace {

using json = nlohmann::json;

// 与登录页共用初始化入口，场景数据随会话创建，不依赖客户端环境变量。
[[nodiscard]] Result FakeSession(const QStringList &args) {
	if (args.size() > 1) {
		return Result::Err(u"usage: session.fake [userId]"_q);
	}
	auto userId = int64(999999999);
	if (args.size() == 1) {
		auto ok = false;
		userId = args.front().toLongLong(&ok);
		if (!ok || userId <= 0) {
			return Result::Err(u"expected a positive integer userId"_q);
		}
	}
	if (const auto error = CreateFakeSession(userId); !error.isEmpty()) {
		return Result::Err(error);
	}
	return Result::Ok(Compact(json{
		{ "userId", userId },
		{ "note", "offline fake session, no server data" },
	}));
}

// 切到官方测试数据中心。测试号无需真手机号，能拿到真实会话和真实消息事件。
// 等价于登录界面输入 testmode（settings_codes.cpp:147）。
[[nodiscard]] Result TestMode(const QStringList &) {
	auto &domain = Core::App().domain();
	const auto was = domain.started()
		? domain.active().mtp().environment()
		: MTP::Environment::Production;
	if (const auto error = SwitchTestEnvironment(); !error.isEmpty()) {
		return Result::Err(error);
	}
	const auto target = (was == MTP::Environment::Production)
		? MTP::Environment::Test
		: MTP::Environment::Production;
	return Result::Ok(Compact(json{
		{ "from", (was == MTP::Environment::Production)
			? "production" : "test" },
		{ "to", (target == MTP::Environment::Production)
			? "production" : "test" },
	}));
}

} // namespace

const HandlerMap &SessionHandlers() {
	static const auto result = HandlerMap{
		{ u"session.fake"_q, &FakeSession },
		{ u"session.test-mode"_q, &TestMode },
	};
	return result;
}

} // namespace AyuDebug::Commands
#endif // _DEBUG
