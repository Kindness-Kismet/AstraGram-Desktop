#ifdef _DEBUG
#include "ayu/debug/commands/commands_internal.h"

#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/shortcuts.h"
#include "settings/settings_builder.h"
#include "settings/settings_search.h"
#include "settings/sections/settings_main.h"
#include "ayu/ui/settings/settings_main.h"
#include "window/window_session_controller.h"
#include "main/main_session.h"

#include <QApplication>
#include <QWindow>
#include <qpa/qwindowsysteminterface.h>

namespace AyuDebug::Commands {
namespace {

using Json = nlohmann::json;

Result listPages(const QStringList &args) {
	if (args.size() > 1) return Result::Err(u"usage: page.list [filter]"_q);
	const auto session = ActiveSession();
	if (!session) return Result::Err(u"an active session is required"_q);
	auto list = Json::array();
	auto &registry = Settings::Builder::SearchRegistry::Instance();
	for (const auto &entry : registry.collectAll(session)) {
		const auto path = registry.sectionPath(entry.section);
		if (!args.empty() && !entry.id.contains(args[0], Qt::CaseInsensitive)
			&& !entry.title.contains(args[0], Qt::CaseInsensitive)
			&& !path.contains(args[0], Qt::CaseInsensitive)) continue;
		list.push_back({{"id", entry.id.toStdString()}, {"title", entry.title.toStdString()},
			{"path", path.toStdString()}, {"checked", entry.checkIcon == Settings::Builder::SearchEntryCheckIcon::Checked},
			{"hasCheck", entry.checkIcon != Settings::Builder::SearchEntryCheckIcon::None},
			{"deeplink", entry.deeplink.toStdString()}});
	}
	return Result::Ok(Compact(list));
}

Result openPage(const QStringList &args) {
	if (args.size() != 1) return Result::Err(u"usage: page.open <alias or id from page.list>"_q);
	const auto session = ActiveSession();
	const auto controller = session ? session->tryResolveWindow() : nullptr;
	if (!controller) return Result::Err(u"an active session is required"_q);
	const auto &key = args.front();
	if (key == u"settings"_q || key == u"ayu"_q || key == u"search"_q) {
		controller->showSettings(key == u"settings"_q ? Settings::MainId()
			: key == u"ayu"_q ? Settings::AyuMainId() : Settings::Search::Id());
		return Result::Ok();
	}
	for (const auto &entry : Settings::Builder::SearchRegistry::Instance().collectAll(session)) {
		if (entry.id != key && !entry.altIds.contains(key)) continue;
		if (!entry.deeplink.isEmpty()) {
			Core::App().openLocalUrl(entry.deeplink, QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = base::make_weak(controller),
			}));
		} else {
			controller->setHighlightControlId(entry.id);
			controller->showSettings(entry.section);
		}
		return Result::Ok(entry.id);
	}
	return Result::Err(u"page not found; use page.list"_q);
}

const std::map<QString, Shortcuts::Command> &actions() {
	static const auto result = std::map<QString, Shortcuts::Command>{
		{u"close"_q, Shortcuts::Command::Close},
		{u"lock"_q, Shortcuts::Command::Lock},
		{u"minimize"_q, Shortcuts::Command::Minimize},
		{u"quit"_q, Shortcuts::Command::Quit},
		{u"reopen-closed-window"_q, Shortcuts::Command::ReopenClosedWindow},
		{u"close-other-windows"_q, Shortcuts::Command::CloseOtherWindows},
		{u"media-play"_q, Shortcuts::Command::MediaPlay},
		{u"media-pause"_q, Shortcuts::Command::MediaPause},
		{u"media-play-pause"_q, Shortcuts::Command::MediaPlayPause},
		{u"media-stop"_q, Shortcuts::Command::MediaStop},
		{u"media-previous"_q, Shortcuts::Command::MediaPrevious},
		{u"media-next"_q, Shortcuts::Command::MediaNext},
		{u"search"_q, Shortcuts::Command::Search},
		{u"chat-previous"_q, Shortcuts::Command::ChatPrevious},
		{u"chat-next"_q, Shortcuts::Command::ChatNext},
		{u"chat-first"_q, Shortcuts::Command::ChatFirst},
		{u"chat-last"_q, Shortcuts::Command::ChatLast},
		{u"chat-self"_q, Shortcuts::Command::ChatSelf},
		{u"chat-pinned1"_q, Shortcuts::Command::ChatPinned1},
		{u"chat-pinned2"_q, Shortcuts::Command::ChatPinned2},
		{u"chat-pinned3"_q, Shortcuts::Command::ChatPinned3},
		{u"chat-pinned4"_q, Shortcuts::Command::ChatPinned4},
		{u"chat-pinned5"_q, Shortcuts::Command::ChatPinned5},
		{u"chat-pinned6"_q, Shortcuts::Command::ChatPinned6},
		{u"chat-pinned7"_q, Shortcuts::Command::ChatPinned7},
		{u"chat-pinned8"_q, Shortcuts::Command::ChatPinned8},
		{u"show-account1"_q, Shortcuts::Command::ShowAccount1},
		{u"show-account2"_q, Shortcuts::Command::ShowAccount2},
		{u"show-account3"_q, Shortcuts::Command::ShowAccount3},
		{u"show-account4"_q, Shortcuts::Command::ShowAccount4},
		{u"show-account5"_q, Shortcuts::Command::ShowAccount5},
		{u"show-account6"_q, Shortcuts::Command::ShowAccount6},
		{u"show-all-chats"_q, Shortcuts::Command::ShowAllChats},
		{u"show-folder1"_q, Shortcuts::Command::ShowFolder1},
		{u"show-folder2"_q, Shortcuts::Command::ShowFolder2},
		{u"show-folder3"_q, Shortcuts::Command::ShowFolder3},
		{u"show-folder4"_q, Shortcuts::Command::ShowFolder4},
		{u"show-folder5"_q, Shortcuts::Command::ShowFolder5},
		{u"show-folder6"_q, Shortcuts::Command::ShowFolder6},
		{u"show-folder-last"_q, Shortcuts::Command::ShowFolderLast},
		{u"folder-next"_q, Shortcuts::Command::FolderNext},
		{u"folder-previous"_q, Shortcuts::Command::FolderPrevious},
		{u"show-scheduled"_q, Shortcuts::Command::ShowScheduled},
		{u"show-archive"_q, Shortcuts::Command::ShowArchive},
		{u"show-contacts"_q, Shortcuts::Command::ShowContacts},
		{u"just-send-message"_q, Shortcuts::Command::JustSendMessage},
		{u"send-silent-message"_q, Shortcuts::Command::SendSilentMessage},
		{u"schedule-message"_q, Shortcuts::Command::ScheduleMessage},
		{u"compose-ai-apply-in-place"_q, Shortcuts::Command::ComposeAiApplyInPlace},
		{u"show-rich-editor"_q, Shortcuts::Command::ShowRichEditor},
		{u"toggle-web-page-preview"_q, Shortcuts::Command::ToggleWebPagePreview},
		{u"record-voice"_q, Shortcuts::Command::RecordVoice},
		{u"record-round"_q, Shortcuts::Command::RecordRound},
		{u"read-chat"_q, Shortcuts::Command::ReadChat},
		{u"archive-chat"_q, Shortcuts::Command::ArchiveChat},
		{u"media-viewer-fullscreen"_q, Shortcuts::Command::MediaViewerFullscreen},
		{u"show-chat-menu"_q, Shortcuts::Command::ShowChatMenu},
		{u"show-chat-preview"_q, Shortcuts::Command::ShowChatPreview},
		{u"show-admin-log"_q, Shortcuts::Command::ShowAdminLog},
		{u"support-reload-templates"_q, Shortcuts::Command::SupportReloadTemplates},
		{u"support-toggle-muted"_q, Shortcuts::Command::SupportToggleMuted},
		{u"support-scroll-to-current"_q, Shortcuts::Command::SupportScrollToCurrent},
		{u"support-history-back"_q, Shortcuts::Command::SupportHistoryBack},
		{u"support-history-forward"_q, Shortcuts::Command::SupportHistoryForward},
	};
	return result;
}

Result listActions(const QStringList &args) {
	if (!args.empty()) return Result::Err(u"usage: action.list"_q);
	auto result = Json::array();
	for (const auto &[name, command] : actions()) result.push_back(name.toStdString());
	return Result::Ok(Compact(result));
}

// 按绑定的按键走 Qt 快捷键匹配，与真实按键一致；Qt 只在应用有活动窗口时匹配。
Result runAction(const QStringList &args) {
	if (args.size() != 1) return Result::Err(u"usage: action.run <name>"_q);
	const auto i = actions().find(args.front());
	if (i == actions().end()) return Result::Err(u"unknown action; use action.list"_q);
	const auto window = QApplication::activeWindow();
	if (!window || !window->windowHandle()) {
		return Result::Err(u"application window is not active; shortcuts need focus"_q);
	}
	for (const auto &[keys, commands] : Shortcuts::KeysCurrents()) {
		if (!commands.contains(i->second)) continue;
		const auto text = keys.toString(QKeySequence::PortableText);
		auto handled = true;
		for (auto k = 0; handled && k != int(keys.count()); ++k) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
			const auto key = int(keys[k].key());
			const auto modifiers = keys[k].keyboardModifiers();
#else // Qt >= 6.0.0
			const auto key = int(keys[k] & ~Qt::KeyboardModifierMask);
			const auto modifiers = Qt::KeyboardModifiers(keys[k] & Qt::KeyboardModifierMask);
#endif // Qt < 6.0.0
			handled = QWindowSystemInterface::handleShortcutEvent(
				window->windowHandle(), 0, key, modifiers, 0, 0, 0);
		}
		return handled ? Result::Ok(text)
			: Result::Err(u"shortcut "_q + text + u" is unavailable in the current view"_q);
	}
	return Result::Err(u"no key is bound to this action"_q);
}

} // namespace

const HandlerMap &NavigationHandlers() {
	static const auto result = HandlerMap{
		{u"page.list"_q, &listPages},
		{u"page.open"_q, &openPage},
		{u"action.list"_q, &listActions},
		{u"action.run"_q, &runAction},
	};
	return result;
}

} // namespace AyuDebug::Commands
#endif
