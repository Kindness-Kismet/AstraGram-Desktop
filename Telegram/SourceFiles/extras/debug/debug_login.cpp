#ifdef _DEBUG
#include "extras/debug/debug_login.h"
#include "extras/extras_settings.h"
#include "extras/debug/commands/commands_internal.h"

#include "core/application.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session_settings.h"
#include "main/main_session.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/mtproto_dc_options.h"
#include "storage/storage_account.h"

namespace ExtrasDebug {
namespace {

base::weak_ptr<Main::Session> SimulationSession;

} // namespace

bool isSimulationSession(not_null<Main::Session*> session) {
	return SimulationSession.get() == session.get();
}

// 复用原生会话恢复流程，每次进入自动生成本地场景；身份与场景消息不持久化。
QString enterSimulation(int64 userId) {
	// mtp() 直接解引用 _mtp，没有公开的就绪查询；domain.started() 是 tdesktop
	// 自己在 settings_codes.cpp:152 用的同一前提。
	if (!Core::App().domain().started()) {
		return u"domain is not started yet"_q;
	}
	auto &account = Core::App().activeAccount();
	if (account.sessionExists()) {
		return u"session already exists"_q;
	}
	if (userId <= 0) {
		return u"expected a positive userId"_q;
	}

	// 一旦有了会话，tdesktop 会开始发需要授权的请求；假密钥必然 401，而
	// main_account.cpp:459 的全局失败处理会把会话直接登出。换成空实现留住它。
	account.mtp().setGlobalFailHandler(nullptr);

	auto settings = account.local().readSessionSettingsForDebug();
	using Flag = MTPDuser::Flag;
	account.createSession(MTP_user(
		MTP_flags(Flag::f_self | Flag::f_first_name),
		MTP_long(userId),
		MTPlong(), // access_hash
		MTP_string("模拟用户"),
		MTPstring(), // last_name
		MTPstring(), // username
		MTPstring(), // phone
		MTPUserProfilePhoto(),
		MTPUserStatus(),
		MTPint(), // bot_info_version
		MTPVector<MTPRestrictionReason>(),
		MTPstring(), // bot_inline_placeholder
		MTPstring(), // lang_code
		MTPEmojiStatus(),
		MTPVector<MTPUsername>(),
		MTPRecentStory(),
		MTPPeerColor(), // color
		MTPPeerColor(), // profile_color
		MTPint(), // bot_active_users
		MTPlong(), // bot_verification_icon
		MTPlong(), // send_paid_messages_stars
		MTPlong()), std::move(settings));
	SimulationSession = base::make_weak(&account.session());
	ExtrasSettings::getInstance().setDevFeaturesEnabled(true);
	Commands::seedSimulationScenarios(&account.session());

	return QString();
}

QString SwitchTestEnvironment() {
	auto &domain = Core::App().domain();
	if (!domain.started()) {
		return u"domain is not started"_q;
	}
	if (domain.active().sessionExists()) {
		return u"already logged in; log out before switching environment"_q;
	}
	// addActivated 会新建账号，多账号时切换会留下多余的空账号，官方 testmode
	// 也是这个前提（settings_codes.cpp:150）。
	if (domain.accounts().size() != 1) {
		return u"expected exactly one account"_q;
	}
	const auto was = domain.active().mtp().environment();
	domain.addActivated((was == MTP::Environment::Production)
		? MTP::Environment::Test
		: MTP::Environment::Production);
	return QString();
}

} // namespace ExtrasDebug
#endif // _DEBUG
