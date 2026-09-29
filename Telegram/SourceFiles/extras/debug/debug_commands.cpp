#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "core/application.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history_item.h"

namespace ExtrasDebug::Commands {

Main::Session *ActiveSession() {
	return Core::App().maybePrimarySession();
}

PeerData *findPeer(const QString &id) {
	const auto session = ActiveSession();
	if (!session) return nullptr;
	if (id == u"self"_q || id == u"0"_q) return session->user();
	auto ok = false;
	const auto value = id.toULongLong(&ok);
	return ok ? session->data().peerLoaded(PeerId(value)) : nullptr;
}

HistoryItem *findMessage(const QString &peerId, const QString &messageId) {
	const auto peer = findPeer(peerId);
	auto ok = false;
	const auto id = messageId.toInt(&ok);
	return peer && ok ? peer->owner().message(peer->id, MsgId(id)) : nullptr;
}

} // namespace ExtrasDebug::Commands

namespace ExtrasDebug {
namespace {

// 各领域持有自己的注册表，统一汇总指令入口。
[[nodiscard]] const Commands::HandlerMap &Handlers() {
	static const auto result = [] {
		auto all = Commands::HandlerMap{};
		for (const auto *part : {
			&Commands::AppHandlers(),
			&Commands::SessionHandlers(),
			&Commands::ScenarioHandlers(),
			&Commands::SettingsHandlers(),
			&Commands::GhostHandlers(),
			&Commands::StorageHandlers(),
			&Commands::ScreenshotHandlers(),
			&Commands::ControlHandlers(),
			&Commands::MessageHandlers(),
			&Commands::WindowHandlers(),
			&Commands::NavigationHandlers(),
			&Commands::FilterHandlers(),
			&Commands::FeatureHandlers(),
			&Commands::AccountHandlers(),
			&Commands::playerHandlers(),
		}) {
			all.insert(part->begin(), part->end());
		}
		return all;
	}();
	return result;
}

} // namespace

Result Execute(const QString &command, const QStringList &args) {
	const auto &handlers = Handlers();
	const auto i = handlers.find(command);
	if (i == handlers.end()) {
		return Result::Err(u"unknown command "_q + command);
	}
	// 处理函数会碰 nlohmann 与 Qt 的解析路径，任何一处抛出都不该带走监听。
	try {
		return i->second(args);
	} catch (const std::exception &e) {
		return Result::Err(QString::fromUtf8(e.what()));
	}
}

QStringList CommandNames() {
	auto result = QStringList();
	for (const auto &[name, handler] : Handlers()) {
		result.push_back(name);
	}
	return result;
}

} // namespace ExtrasDebug
#endif // _DEBUG
