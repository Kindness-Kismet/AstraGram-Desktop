#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/shortcuts.h"
#include "data/data_auto_download.h"
#include "data/data_chat_filters.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "dialogs/dialogs_key.h"
#include "extras/ui/settings/filters/settings_filters_list.h"
#include "extras/ui/settings/settings_appearance.h"
#include "extras/ui/settings/settings_chats.h"
#include "extras/ui/settings/settings_content.h"
#include "extras/ui/settings/settings_debug.h"
#include "extras/ui/settings/settings_extras.h"
#include "extras/ui/settings/settings_filters.h"
#include "extras/ui/settings/settings_general.h"
#include "extras/ui/settings/settings_main.h"
#include "extras/ui/settings/settings_other.h"
#include "info/downloads/info_downloads_widget.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "settings/business/settings_away_message.h"
#include "settings/business/settings_chat_intro.h"
#include "settings/business/settings_chat_links.h"
#include "settings/business/settings_chatbots.h"
#include "settings/business/settings_greeting.h"
#include "settings/business/settings_location.h"
#include "settings/business/settings_quick_replies.h"
#include "settings/business/settings_working_hours.h"
#include "settings/sections/settings_about.h"
#include "settings/sections/settings_active_sessions.h"
#include "settings/sections/settings_advanced.h"
#include "settings/sections/settings_archive.h"
#include "settings/sections/settings_auto_download.h"
#include "settings/sections/settings_blocked_peers.h"
#include "settings/sections/settings_business.h"
#include "settings/sections/settings_calls.h"
#include "settings/sections/settings_chat.h"
#include "settings/sections/settings_credits.h"
#include "settings/sections/settings_file_confirmations.h"
#include "settings/sections/settings_folders.h"
#include "settings/sections/settings_global_ttl.h"
#include "settings/sections/settings_information.h"
#include "settings/sections/settings_language.h"
#include "settings/sections/settings_local_passcode.h"
#include "settings/sections/settings_local_storage.h"
#include "settings/sections/settings_main.h"
#include "settings/sections/settings_notifications.h"
#include "settings/sections/settings_notifications_reactions.h"
#include "settings/sections/settings_notifications_type.h"
#include "settings/sections/settings_passkeys.h"
#include "settings/sections/settings_premium.h"
#include "settings/sections/settings_privacy_security.h"
#include "settings/sections/settings_shortcuts.h"
#include "settings/sections/settings_websites.h"
#include "settings/settings_builder.h"
#include "settings/settings_experimental.h"
#include "settings/settings_power_saving.h"
#include "settings/settings_workspace.h"
#include "storage/storage_domain.h"
#include "window/main_window.h"
#include "window/window_adaptive.h"
#include "window/window_session_controller.h"

#include <QApplication>
#include <QWindow>
#include <qpa/qwindowsysteminterface.h>

namespace ExtrasDebug::Commands {
namespace {

using Json = nlohmann::json;
using SettingsType = ::Settings::Type;
using Controller = Window::SessionController;

struct Page {
	QString key;
	SettingsType (*id)() = nullptr;
	// 没有固定页面标识的入口，按当前账号状态选择页面。
	void (*open)(not_null<Controller*>) = nullptr;
};

// 搜索索引只能定位到条目所在页，子页和流程入口在这里逐个登记。
const std::vector<Page> &pages() {
	using namespace ::Settings;
	using Notify = Data::DefaultNotify;
	using Source = Data::AutoDownload::Source;
	static const auto result = std::vector<Page>{
		{ u"settings"_q, &MainId },
		{ u"info"_q, &InformationId },
		{ u"notifications"_q, &NotificationsId },
		{ u"notifications-private"_q, [] { return NotificationsType::Id(Notify::User); } },
		{ u"notifications-groups"_q, [] { return NotificationsType::Id(Notify::Group); } },
		{ u"notifications-channels"_q, [] { return NotificationsType::Id(Notify::Broadcast); } },
		{ u"notifications-reactions"_q, &NotificationsReactions::Id },
		{ u"privacy"_q, &PrivacySecurityId },
		{ u"sessions"_q, &SessionsId },
		{ u"blocked"_q, &BlockedPeersId },
		{ u"websites"_q, &WebsitesId },
		{ u"auto-delete"_q, &GlobalTTLId },
		{ u"passkeys"_q, &PasskeysId },
		{ u"file-confirmations"_q, &FileConfirmationsId },
		{ u"local-passcode"_q, nullptr, [](not_null<Controller*> controller) {
			controller->showSettings(controller->session().domain().local().hasPasscode()
				? LocalPasscodeCheckId()
				: LocalPasscodeCreateId());
		} },
		{ u"cloud-password"_q, nullptr, [](not_null<Controller*> controller) {
			controller->showCloudPassword();
		} },
		{ u"chat"_q, &::Settings::ChatId },
		{ u"archive-settings"_q, &ArchiveId },
		{ u"shortcuts"_q, &ShortcutsId },
		{ u"folders"_q, &FoldersId },
		{ u"advanced"_q, &AdvancedId },
		{ u"auto-download-private"_q, [] { return AutoDownloadId(Source::User); } },
		{ u"auto-download-groups"_q, [] { return AutoDownloadId(Source::Group); } },
		{ u"auto-download-channels"_q, [] { return AutoDownloadId(Source::Channel); } },
		{ u"local-storage"_q, &LocalStorageId },
		{ u"downloads"_q, nullptr, [](not_null<Controller*> controller) {
			controller->showSection(Info::Downloads::Make(controller->session().user()));
		} },
		{ u"experimental"_q, &Experimental::Id },
		{ u"calls"_q, &CallsId },
		{ u"power-saving"_q, &PowerSavingId },
		{ u"language"_q, &::Settings::LanguageId },
		{ u"about"_q, &AboutId },
		{ u"premium"_q, &PremiumId },
		{ u"business"_q, &BusinessId },
		{ u"business-greeting"_q, &GreetingId },
		{ u"business-away"_q, &AwayMessageId },
		{ u"business-replies"_q, &QuickRepliesId },
		{ u"business-hours"_q, &WorkingHoursId },
		{ u"business-location"_q, &LocationId },
		{ u"business-links"_q, &ChatLinksId },
		{ u"business-intro"_q, &ChatIntroId },
		{ u"business-bots"_q, &ChatbotsId },
		{ u"stars"_q, &CreditsId },
		{ u"currency"_q, &CurrencyId },
		{ u"extras"_q, &ExtrasMainId },
		{ u"extras-ghost"_q, &ExtrasGhostId },
		{ u"extras-archive"_q, &ExtrasArchive::Id },
		{ u"extras-filters"_q, &ExtrasFiltersId },
		{ u"extras-filters-list"_q, &ExtrasFiltersList::Id },
		{ u"extras-general"_q, &ExtrasGeneralId },
		{ u"extras-appearance"_q, &ExtrasAppearanceId },
		{ u"extras-chats"_q, &ExtrasChatsId },
		{ u"extras-text"_q, &ExtrasText::Id },
		{ u"extras-other"_q, &ExtrasOtherId },
		{ u"extras-debug"_q, &ExtrasDebugId },
	};
	return result;
}

// 未进入搜索索引的子页先打开上级页，导航高亮与返回路径和手动进入一致。
const std::map<QString, QString> &parentKeys() {
	static const auto result = std::map<QString, QString>{
		{ u"notifications-groups"_q, u"notifications"_q },
		{ u"notifications-channels"_q, u"notifications"_q },
		{ u"local-storage"_q, u"advanced"_q },
		{ u"local-passcode"_q, u"privacy"_q },
		{ u"cloud-password"_q, u"privacy"_q },
		{ u"business-greeting"_q, u"business"_q },
		{ u"business-away"_q, u"business"_q },
		{ u"business-replies"_q, u"business"_q },
		{ u"business-hours"_q, u"business"_q },
		{ u"business-location"_q, u"business"_q },
		{ u"business-links"_q, u"business"_q },
		{ u"business-intro"_q, u"business"_q },
		{ u"business-bots"_q, u"business"_q },
		{ u"extras-filters-list"_q, u"extras-filters"_q },
	};
	return result;
}

[[nodiscard]] Json pageKey(const SettingsType &type) {
	if (!type) {
		return nullptr;
	}
	for (const auto &page : pages()) {
		if (page.id && page.id() == type) {
			return page.key.toStdString();
		}
	}
	return nullptr;
}

// 切换过程中新旧设置区可能同时存在，只取可见的一个。
[[nodiscard]] ::Settings::Workspace *findWorkspace(not_null<Controller*> controller) {
	const auto list = controller->widget()->findChildren<QWidget*>(
		u"settings-workspace"_q);
	for (const auto widget : list) {
		if (widget->isVisible()) {
			return dynamic_cast<::Settings::Workspace*>(widget);
		}
	}
	return nullptr;
}

void showCategory(not_null<Controller*> controller, const SettingsType &type) {
	const auto workspace = findWorkspace(controller);
	if (!workspace || !workspace->openCategory(type)) {
		controller->showSettings(type);
	}
}

// 按手动路径打开：切换左侧分类，再逐级进入子页。
void showPage(not_null<Controller*> controller, const Page &page) {
	const auto type = page.id ? page.id() : SettingsType();
	const auto workspace = findWorkspace(controller);
	if (type && workspace && workspace->state().section == type) {
		return;
	}
	const auto category = type
		? ::Settings::Builder::SearchRegistry::Instance().sectionCategory(type)
		: SettingsType();
	if (const auto i = parentKeys().find(page.key); i != parentKeys().end()) {
		showPage(controller, *ranges::find(pages(), i->second, &Page::key));
	} else if (category != type) {
		showCategory(controller, category);
	} else if (type && workspace && workspace->openCategory(type)) {
		return;
	}
	if (page.open) {
		page.open(controller);
	} else {
		controller->showSettings(type);
	}
}

Result listSections(const QStringList &args) {
	if (!args.empty()) return Result::Err(u"usage: page.sections"_q);
	const auto &registry = ::Settings::Builder::SearchRegistry::Instance();
	auto list = Json::array();
	for (const auto &page : pages()) {
		const auto type = page.id ? page.id() : nullptr;
		const auto title = type ? registry.sectionTitle(type) : QString();
		list.push_back({
			{ "key", page.key.toStdString() },
			{ "title", title.isEmpty() ? Json(nullptr) : Json(title.toStdString()) },
			{ "path", type ? registry.sectionPath(type).toStdString() : std::string() },
			{ "flow", !page.id },
		});
	}
	return Result::Ok(Compact(list));
}

Result currentPage(const QStringList &args) {
	if (!args.empty()) return Result::Err(u"usage: page.current"_q);
	const auto session = ActiveSession();
	const auto controller = session ? session->tryResolveWindow() : nullptr;
	if (!controller) return Result::Err(u"an active session is required"_q);
	const auto window = controller->widget();
	const auto &adaptive = controller->adaptive();
	auto result = Json{
		{ "window", {
			{ "width", window->width() },
			{ "height", window->height() },
			{ "layout", adaptive.isOneColumn() ? "one-column"
				: adaptive.isThreeColumn() ? "three-column"
				: "normal" },
		} },
		{ "layer", controller->isLayerShown() },
		{ "thirdSection", controller->content()->isThirdSectionShown() },
		{ "folder", controller->activeChatsFilterCurrent() },
		{ "archive", controller->openedFolder().current() != nullptr },
	};
	if (const auto workspace = findWorkspace(controller)) {
		const auto state = workspace->state();
		const auto &registry = ::Settings::Builder::SearchRegistry::Instance();
		result["kind"] = "settings";
		result["section"] = pageKey(state.section);
		if (result["section"].is_null() && state.section) {
			// 未登记键名的页面（流程中间步骤等）用工厂类型名标识。
			result["sectionClass"] = typeid(*state.section).name();
		}
		result["path"] = registry.sectionPath(state.section).toStdString();
		result["category"] = pageKey(state.category);
		result["settingsLayout"] = {
			{ "wide", state.wide },
			{ "navigationShown", state.navigationShown },
			{ "contentShown", state.contentShown },
			{ "width", workspace->width() },
			{ "wideThreshold", state.wideThreshold },
		};
	} else if (const auto peer = controller->activeChatCurrent().peer()) {
		result["kind"] = "chat";
		result["peerId"] = peer->id.value;
		result["name"] = peer->name().toStdString();
	} else {
		result["kind"] = controller->content()->isMainSectionShown()
			? "section"
			: "dialogs";
	}
	return Result::Ok(Compact(result));
}

Result switchFolder(const QStringList &args) {
	if (args.size() > 1) return Result::Err(u"usage: chat.folder [id|all]"_q);
	const auto session = ActiveSession();
	const auto controller = session ? session->tryResolveWindow() : nullptr;
	if (!controller) return Result::Err(u"an active session is required"_q);
	const auto &list = session->data().chatsFilters().list();
	if (args.empty()) {
		auto result = Json::array();
		for (const auto &filter : list) {
			result.push_back({
				{ "id", filter.id() },
				{ "title", (filter.id()
					? filter.title().text.text
					: tr::lng_filters_all(tr::now)).toStdString() },
				{ "active", filter.id() == controller->activeChatsFilterCurrent() },
			});
		}
		return Result::Ok(Compact(result));
	}
	auto ok = (args[0] == u"all"_q);
	const auto id = ok ? FilterId(0) : FilterId(args[0].toInt(&ok));
	if (!ok) return Result::Err(u"expected a folder id or all"_q);
	// 隐藏“所有聊天”时列表里没有 0，与文件夹栏一致。
	const auto known = ranges::any_of(list, [&](const Data::ChatFilter &filter) {
		return filter.id() == id;
	});
	if (!known) return Result::Err(u"folder not found; use chat.folder"_q);
	controller->setActiveChatsFilter(id);
	return Result::Ok(QString::number(id));
}

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
	if (args.size() != 1) {
		return Result::Err(u"usage: page.open <key from page.sections or id from page.list>"_q);
	}
	const auto session = ActiveSession();
	const auto controller = session ? session->tryResolveWindow() : nullptr;
	if (!controller) return Result::Err(u"an active session is required"_q);
	const auto &key = args.front();
	for (const auto &page : pages()) {
		if (page.key != key) continue;
		showPage(controller, page);
		return Result::Ok(key);
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
		{u"stash-message"_q, Shortcuts::Command::StashMessage},
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
		{u"page.sections"_q, &listSections},
		{u"page.current"_q, &currentPage},
		{u"chat.folder"_q, &switchFolder},
		{u"action.list"_q, &listActions},
		{u"action.run"_q, &runAction},
	};
	return result;
}

} // namespace ExtrasDebug::Commands
#endif
