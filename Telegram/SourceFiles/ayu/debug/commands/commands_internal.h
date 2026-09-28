#pragma once

#include "ayu/debug/debug_commands.h"

#include "ayu/libs/json.hpp"

#include <QString>
#include <QStringList>

#include <map>

namespace Main {
class Session;
} // namespace Main

namespace AyuDebug::Commands {

using Handler = Result (*)(const QStringList &args);
using HandlerMap = std::map<QString, Handler>;

// 每个域一个注册表，实现文件在 commands/ 下与域同名。
[[nodiscard]] const HandlerMap &AppHandlers();
[[nodiscard]] const HandlerMap &SessionHandlers();
[[nodiscard]] const HandlerMap &ScenarioHandlers();
[[nodiscard]] const HandlerMap &SettingsHandlers();
[[nodiscard]] const HandlerMap &GhostHandlers();
[[nodiscard]] const HandlerMap &StorageHandlers();
[[nodiscard]] const HandlerMap &ScreenshotHandlers();
[[nodiscard]] const HandlerMap &ControlHandlers();
[[nodiscard]] const HandlerMap &MessageHandlers();
[[nodiscard]] const HandlerMap &WindowHandlers();
[[nodiscard]] const HandlerMap &NavigationHandlers();
[[nodiscard]] const HandlerMap &FilterHandlers();
[[nodiscard]] const HandlerMap &FeatureHandlers();
[[nodiscard]] const HandlerMap &AccountHandlers();

// 界面登录入口与命令入口共用同一份本地场景。
void seedFakeScenarios(not_null<Main::Session*> session);
[[nodiscard]] MTPMessage fakeTextMessage(not_null<PeerData*> peer, PeerId sender,
	int id, const QString &text, bool edited = false);

// json 序列化为单行字符串，所有 payload 的统一出口。
[[nodiscard]] inline QString Compact(const nlohmann::json &value) {
	return QString::fromStdString(value.dump());
}

// 当前活跃会话，无会话时为空；定义在 debug_commands.cpp。
[[nodiscard]] Main::Session *ActiveSession();
[[nodiscard]] PeerData *findPeer(const QString &id);
[[nodiscard]] HistoryItem *findMessage(const QString &peerId, const QString &messageId);

} // namespace AyuDebug::Commands
