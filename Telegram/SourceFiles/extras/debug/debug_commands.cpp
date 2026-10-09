#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "core/application.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_download_manager.h"
#include "data/data_user.h"
#include "history/history.h"
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
	if (ok && PeerId(value) == session->userPeerId()) return session->user();
	return ok ? session->data().peerLoaded(PeerId(value)) : nullptr;
}

HistoryItem *findMessage(const QString &peerId, const QString &messageId) {
	const auto peer = findPeer(peerId);
	auto ok = false;
	const auto id = messageId.toLongLong(&ok);
	return peer && ok ? findSessionMessage(&peer->session(), { peer->id, MsgId(id) }) : nullptr;
}

HistoryItem *findSessionMessage(not_null<Main::Session*> session, FullMsgId id) {
	if (const auto item = session->data().message(id)) return item;
	if (id.msg <= WelcomeMaxMsgId) return nullptr;
	// 下载管理器生成的条目不在会话消息表中，编号仍属于同一个账号。
	const auto matches = [&](not_null<HistoryItem*> item) {
		return item->fullId() == id && &item->history()->session() == session.get();
	};
	auto &manager = Core::App().downloadManager();
	for (const auto entry : manager.loadingList()) {
		if (matches(entry->object.item)) return entry->object.item;
	}
	for (const auto entry : manager.loadedList()) {
		if (matches(entry->object->item)) return entry->object->item;
	}
	return nullptr;
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
			&Commands::simulationHandlers(),
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
			&Commands::downloadHandlers(),
			&Commands::messageBusinessHandlers(),
			&Commands::transferHandlers(),
			&Commands::animHandlers(),
			&Commands::editorHandlers(),
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
