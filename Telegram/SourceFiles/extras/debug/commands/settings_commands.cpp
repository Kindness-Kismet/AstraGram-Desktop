#ifdef _DEBUG
#include "extras/debug/commands/settings_registry.h"

#include "extras/extras_settings.h"
#include "extras/features/filters/filters_cache_controller.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "main/main_session.h"
#include "ui/chat/chat_style.h"
#include "window/themes/window_theme.h"
#include "window/notifications_manager.h"
#include "settings/settings_experimental.h"
#include "base/options.h"

namespace ExtrasDebug::Commands {
namespace {

Json snapshot() {
	auto root = Json::object();
	for (const auto &[key, entry] : settingsEntries()) {
		auto *node = &root;
		const auto path = key.split(u'.');
		for (const auto &part : path) node = &(*node)[part.toStdString()];
		*node = entry.get();
	}
	return root;
}

const Json *findValue(const Json &root, const QString &key) {
	auto *node = &root;
	for (const auto &part : key.split(u'.')) {
		if (!node->is_object()) return nullptr;
		const auto i = node->find(part.toStdString());
		if (i == node->end()) return nullptr;
		node = &*i;
	}
	return node;
}

Result settingsKeys(const QStringList &args) {
	if (args.size() > 1) return Result::Err(u"usage: settings.keys [prefix]"_q);
	auto names = Json::array();
	for (const auto &[key, entry] : settingsEntries()) {
		if (args.empty() || key.startsWith(args.front())) names.push_back(key);
	}
	return Result::Ok(Compact(names));
}

Result settingsDump(const QStringList &args) {
	if (args.size() > 1) return Result::Err(u"usage: settings.dump [group]"_q);
	const auto all = snapshot();
	const auto value = args.empty() ? &all : findValue(all, args.front());
	return value ? Result::Ok(Compact(*value)) : Result::Err(u"settings group not found"_q);
}

Result settingsGet(const QStringList &args) {
	if (args.size() != 1) return Result::Err(u"usage: settings.get <key>"_q);
	return getSetting(args.front());
}

Result settingsSet(const QStringList &args) {
	if (args.size() != 2) return Result::Err(u"usage: settings.set <key> <value>"_q);
	return setSetting(args[0], args[1]);
}

Result settingsSchema(const QStringList &args) {
	if (args.size() > 1) return Result::Err(u"usage: settings.schema [prefix]"_q);
	auto experimental = std::map<QString, std::pair<bool, bool>>();
	for (const auto option : Settings::experimentalOptionsForDebug()) {
		experimental.emplace(u"experimental."_q + option->id(),
			std::make_pair(option->relevant(), option->restartRequired()));
	}
	auto list = Json::array();
	for (const auto &[key, entry] : settingsEntries()) {
		if (!args.empty() && !key.startsWith(args.front())) continue;
		const auto value = entry.get();
		auto item = Json{{"key", key}, {"type", value.type_name()}, {"value", value},
			{"writable", true}};
		if (const auto option = experimental.find(key); option != experimental.end()) {
			item["writable"] = option->second.first;
			item["restartRequired"] = option->second.second;
		}
		list.push_back(std::move(item));
	}
	return Result::Ok(Compact(list));
}

Result resetBackground(const QStringList &args) {
	if (!args.empty()) return Result::Err(u"usage: theme.reset-background"_q);
	const auto background = Window::Theme::Background();
	background->reset();
	return Result::Ok();
}

Result setTheme(const QStringList &args) {
	if (args.size() != 1 || (args.front() != u"dark"_q && args.front() != u"light"_q)) {
		return Result::Err(u"usage: theme.set <dark|light>"_q);
	}
	Core::App().settings().setSystemDarkModeEnabled(false);
	Core::App().saveSettingsDelayed();
	if (Window::Theme::IsNightMode() != (args.front() == u"dark"_q)) {
		Window::Theme::ToggleNightMode();
		Window::Theme::KeepApplied();
	}
	return Result::Ok(Window::Theme::IsNightMode() ? u"dark"_q : u"light"_q);
}

} // namespace

SettingsMap settingsEntries() {
	SettingsMap result;
	addExtrasSettings(result);
	addCoreSettings(result);
	addSessionSettings(result);
	addProxySettings(result);
	return result;
}

Result getSetting(const QString &key) {
	const auto all = snapshot();
	const auto value = findValue(all, key);
	return value ? Result::Ok(Compact(*value)) : Result::Err(u"unknown setting: "_q + key);
}

Result setSetting(const QString &key, const QString &raw) {
	auto entries = settingsEntries();
	const auto i = entries.find(key);
	if (i == entries.end()) return Result::Err(u"unknown setting; use settings.keys to list writable keys"_q);
	const auto current = i->second.get();
	auto value = current.is_string() ? Json(raw.toStdString())
		: Json::parse(raw.toStdString(), nullptr, false);
	if (value.is_discarded()) return Result::Err(u"invalid JSON value"_q);
	if (current.is_number_integer() && !value.is_number_integer()) {
		return Result::Err(u"expected an integer"_q);
	}
	const auto enums = std::map<QString, int>{
		{u"core.notifyView"_q, 2}, {u"core.notificationsCorner"_q, 4},
		{u"core.sendSubmitWay"_q, 3}, {u"core.floatPlayerColumn"_q, 2},
		{u"core.workMode"_q, 2}, {u"core.closeBehavior"_q, 2},
		{u"core.playerRepeatMode"_q, 2}, {u"core.playerOrderMode"_q, 2},
		{u"core.quickDialogAction"_q, 5}, {u"session.supportSwitch"_q, 2},
		{u"session.selectorTab"_q, 3},
		{u"core.chatQuickAction"_q, 2}, {u"core.chatFiltersTabsMode"_q, 3},
		{u"session.setupEmailState"_q, 4},
	};
	if (const auto limit = enums.find(key); limit != enums.end()) {
		int parsed = 0;
		if (!readSetting(value, parsed).isEmpty() || parsed < 0 || parsed > limit->second) {
			return Result::Err(u"enum value out of range"_q);
		}
	}
	if (key == u"core.floatPlayerCorner"_q && value != 1 && value != 4 && value != 64 && value != 256) {
		return Result::Err(u"expected a corner flag: 1, 4, 64 or 256"_q);
	}
	// 写入前校验范围，避免订阅者收到越界状态。
	const auto ranges = std::map<QString, std::pair<double, double>>{
		{u"messageBubbleRadius"_q, {0, Ui::kBubbleRadiusSliderMax}},
		{u"wideMultiplier"_q, {0.5, 4.0}},
		{u"messageStickerScale"_q, {0.5, 1.6}},
		{u"stickerPanelScale"_q, {1.0, 4.0}},
		{u"recentStickersCount"_q, {1, 200}},
		{u"avatarCorners"_q, {0, 23}},
		{u"core.songVolume"_q, {0, 1}},
		{u"core.videoVolume"_q, {0, 1}},
		{u"core.callInputVolume"_q, {0, 100}},
		{u"core.callOutputVolume"_q, {0, 200}},
		{u"core.notificationsVolume"_q, {0, 100}},
		{u"core.notificationsCount"_q, {1, 5}},
		{u"core.videoPlaybackSpeed"_q, {0.5, 2.5}},
		{u"core.voicePlaybackSpeed"_q, {0.5, 2.5}},
		{u"core.audioPlaybackSpeed"_q, {0.5, 2.5}},
	};
	if (const auto range = ranges.find(key); range != ranges.end() && value.is_number()) {
		const auto number = value.get<double>();
		if (!std::isfinite(number) || number < range->second.first || number > range->second.second) {
			return Result::Err(u"setting value out of range"_q);
		}
	}
	if (key == u"windowMaterial"_q) {
		int parsed = 0;
		if (!readSetting(value, parsed).isEmpty() || parsed < 0 || parsed > 3) {
			return Result::Err(u"window material must be between 0 and 3"_q);
		}
	}
	const auto applied = i->second.set(value);
	if (!applied.ok) return applied;
	using Change = Window::Notifications::ChangeType;
	const auto notificationChanges = std::map<QString, Change>{
		{u"core.soundNotify"_q, Change::SoundEnabled},
		{u"core.desktopNotify"_q, Change::DesktopEnabled},
		{u"core.skipToastsInFocus"_q, Change::DesktopEnabled},
		{u"core.flashBounceNotify"_q, Change::FlashBounceEnabled},
		{u"core.notifyView"_q, Change::ViewParams},
		{u"core.includeMutedCounter"_q, Change::IncludeMuted},
		{u"core.includeMutedCounterFolders"_q, Change::IncludeMuted},
		{u"core.countUnreadMessages"_q, Change::CountMessages},
		{u"core.notificationsCount"_q, Change::MaxCount},
		{u"core.notificationsCorner"_q, Change::Corner},
		{u"core.notificationsDisplayChecksum"_q, Change::Corner},
	};
	if (const auto change = notificationChanges.find(key); change != notificationChanges.end()) {
		Core::App().notifications().notifySettingsChanged(change->second);
	}
	if (key == u"core.nativeNotifications"_q) {
		Core::App().notifications().createManager();
	}
	if (key.startsWith(u"core."_q) || key.startsWith(u"proxy."_q)) {
		Core::App().saveSettingsDelayed();
	} else if (key.startsWith(u"session."_q)) {
		ActiveSession()->saveSettings();
	}
	if (key == u"filtersEnabled"_q || key == u"filtersEnabledInChats"_q
		|| key == u"hideFromBlocked"_q) {
		FiltersCacheController::rebuildCache();
		FiltersCacheController::fireUpdate();
	}
	return getSetting(key);
}

const HandlerMap &SettingsHandlers() {
	static const auto result = HandlerMap{
		{u"settings.keys"_q, &settingsKeys},
		{u"settings.dump"_q, &settingsDump},
		{u"settings.schema"_q, &settingsSchema},
		{u"settings.get"_q, &settingsGet},
		{u"settings.set"_q, &settingsSet},
		{u"theme.reset-background"_q, &resetBackground},
		{u"theme.set"_q, &setTheme},
	};
	return result;
}

} // namespace ExtrasDebug::Commands
#endif
