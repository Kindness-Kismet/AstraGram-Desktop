#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"
#include "extras/debug/debug_login.h"
#include "extras/debug/window_focus.h"
#include "extras/features/window_material/window_material.h"

#include "core/application.h"
#include "core/core_screenshot_protection.h"
#include "core/update_checker.h"
#include "core/version.h"
#include "logs.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "storage/localstorage.h"
#include "storage/storage_domain.h"
#include "ui/widgets/rp_window.h"
#include "window/window_controller.h"

namespace ExtrasDebug::Commands {
namespace {

using json = nlohmann::json;

[[nodiscard]] Result AppInfo(const QStringList &) {
	const auto session = ActiveSession();
	const auto window = Core::App().activeWindow();
	auto info = json{
		{ "version", AppVersionStr },
		{ "versionCode", AppVersion },
		{ "configuration", "Debug" },
		{ "workingDir", cWorkingDir().toStdString() },
		{ "debugLogs", Logs::DebugEnabled() },
		{ "hasSession", session != nullptr },
		{ "simulationMode", session && isSimulationSession(session) },
		{ "isolatedDebug", cDebugProfile() },
		{ "offline", cDebugOffline() },
		{ "hasWindow", window != nullptr },
		{ "focus_mode", ExtrasDebug::WindowFocus::modeName().toStdString() },
		{ "screenshotProtected", window
			&& Core::App().screenshotProtection().active(window->widget()) },
		{ "windowMaterialActive", window
			&& ExtrasFeatures::WindowMaterial::isActive(window->widget().get()) },
		{ "passcodeLocked", Core::App().passcodeLocked() },
		{ "hasPasscode", Core::App().domain().local().hasPasscode() },
		{ "appLockEnabled", Core::App().domain().local().appLockEnabled() },
	};
	if (session) {
		info["userId"] = session->userId().bare;
	}
	return Result::Ok(Compact(info));
}

[[nodiscard]] Result Ping(const QStringList &) {
	return Result::Ok(u"pong"_q);
}

[[nodiscard]] Result CheckUpdate(const QStringList &args) {
	if (args.size() > 1) {
		return Result::Err(u"usage: app.check-update [http://127.0.0.1:port]"_q);
	}
	if (Core::UpdaterDisabled()) {
		return Result::Err(u"updater is disabled"_q);
	}
	if (!args.isEmpty() && !Core::SetDebugUpdatePrefix(args.front())) {
		return Result::Err(u"local updates require an isolated offline profile and a loopback HTTP prefix"_q);
	}
	// 只触发正式检查，下载与验签结果由状态查询报告。
	Core::UpdateChecker().checkNow();
	return Result::Ok(u"update check started"_q);
}

[[nodiscard]] Result installUpdate(const QStringList &args) {
	if (!args.isEmpty()) {
		return Result::Err(u"usage: app.install-update"_q);
	}
	if (!cDebugProfile() || !cDebugOffline()) {
		return Result::Err(u"update installation requires an isolated offline profile"_q);
	}
	if (Core::UpdaterDisabled() || !Core::checkReadyUpdate()) {
		return Result::Err(u"no verified update is ready"_q);
	}
	crl::on_main([] { Core::Restart(); });
	return Result::Ok(u"verified update restart scheduled"_q);
}

// 报告更新源配置：文件内容与内存里解析出的前缀不一致时，说明前缀被固定值
// 覆写过，更新请求不会走文件里写的地址。
[[nodiscard]] Result UpdateInfo(const QStringList &args) {
	if (!args.isEmpty()) {
		return Result::Err(u"usage: app.update-info"_q);
	}
	if (Core::UpdaterDisabled()) {
		return Result::Err(u"updater is disabled"_q);
	}
	const auto file = cWorkingDir() + u"tdata/prefix"_q;
	auto content = QString();
	if (QFile f(file); f.open(QIODevice::ReadOnly)) {
		content = QString::fromUtf8(f.readAll()).trimmed();
	}
	const auto checker = Core::UpdateChecker();
	const auto state = checker.state();
	return Result::Ok(Compact(json{
		{ "autoCheckUpdates", cAutoUpdate() },
		{ "debugPrefix", Core::DebugUpdatePrefix().toStdString() },
		{ "state", state == Core::UpdateChecker::State::Ready
			? "ready"
			: state == Core::UpdateChecker::State::Download ? "downloading" : "idle" },
		{ "downloadedBytes", checker.already() },
		{ "totalBytes", checker.size() },
		{ "readyVersion", Core::ReadyUpdateVersion() },
		{ "prefixFile", file.toStdString() },
		{ "prefixFileContent", content.toStdString() },
		{ "resolvedPrefix", Local::readAutoupdatePrefix().toStdString() },
		{ "updatesFolder", (cWorkingDir() + u"tupdates"_q).toStdString() },
	}));
}

[[nodiscard]] Result Quit(const QStringList &) {
	// 立即退出会让 OK 还没写出去就断链，客户端读到的是连接重置。排到事件循环
	// 尾部，等 Reply 把响应刷进 socket 之后再退。
	crl::on_main([] {
		Core::Quit();
	});
	return Result::Ok();
}

[[nodiscard]] Result Help(const QStringList &) {
	auto names = json::array();
	for (const auto &name : CommandNames()) {
		names.push_back(name.toStdString());
	}
	return Result::Ok(Compact(names));
}

[[nodiscard]] Result crashLog(const QStringList &args) {
	if (!args.empty()) return Result::Err(u"usage: crash.log"_q);
	auto file = QFile(cWorkingDir() + u"crash.log"_q);
	if (!file.open(QIODevice::ReadOnly)) return Result::Err(u"crash log not found"_q);
	return Result::Ok(Compact(json{{"text", QString::fromUtf8(file.readAll()).toStdString()}}));
}

} // namespace

const HandlerMap &AppHandlers() {
	static const auto result = HandlerMap{
		{ u"app.ping"_q, &Ping },
		{ u"app.quit"_q, &Quit },
		{ u"app.info"_q, &AppInfo },
		{ u"app.check-update"_q, &CheckUpdate },
		{ u"app.install-update"_q, &installUpdate },
		{ u"app.update-info"_q, &UpdateInfo },
		{ u"app.help"_q, &Help },
		{ u"crash.log"_q, &crashLog },
	};
	return result;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
