#ifdef _DEBUG
#include "ayu/debug/commands/settings_registry.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "core/update_checker.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "spellcheck/spellcheck_types.h"
#include "ui/power_saving.h"
#include "storage/localstorage.h"
#include "settings.h"
#include "settings/settings_experimental.h"
#include "base/options.h"

namespace Core {
void to_json(nlohmann::json &j, const WindowPosition &v) {
	j = {{"moncrc", v.moncrc}, {"maximized", v.maximized}, {"scale", v.scale},
		{"x", v.x}, {"y", v.y}, {"w", v.w}, {"h", v.h}};
}
void from_json(const nlohmann::json &j, WindowPosition &v) {
	j.at("moncrc").get_to(v.moncrc); j.at("maximized").get_to(v.maximized);
	j.at("scale").get_to(v.scale); j.at("x").get_to(v.x); j.at("y").get_to(v.y);
	j.at("w").get_to(v.w); j.at("h").get_to(v.h);
}
void to_json(nlohmann::json &j, const WindowTitleContent &v) {
	j = {{"hideChatName", bool(v.hideChatName)}, {"hideAccountName", bool(v.hideAccountName)},
		{"hideTotalUnread", bool(v.hideTotalUnread)}};
}
void from_json(const nlohmann::json &j, WindowTitleContent &v) {
	v.hideChatName = j.at("hideChatName").get<bool>();
	v.hideAccountName = j.at("hideAccountName").get<bool>();
	v.hideTotalUnread = j.at("hideTotalUnread").get<bool>();
}
}
namespace Media {
void to_json(nlohmann::json &j, const VideoQuality &v) {
	j = {{"manual", bool(v.manual)}, {"height", uint32(v.height)}, {"original", bool(v.original)}};
}
void from_json(const nlohmann::json &j, VideoQuality &v) {
	v.manual = j.at("manual").get<bool>();
	v.height = j.at("height").get<uint32>();
	v.original = j.at("original").get<bool>();
}
}
void to_json(nlohmann::json &j, const LanguageId &v) {
	j = v ? v.twoLetterCode().toStdString() : std::string();
}
void from_json(const nlohmann::json &j, LanguageId &v) {
	const auto name = j.get<QString>();
	v = name.isEmpty() ? LanguageId() : LanguageId::FromName(name);
}
namespace Data {
void to_json(nlohmann::json &j, const ReactionId &v) {
	j = std::holds_alternative<DocumentId>(v.data)
		? nlohmann::json(std::get<DocumentId>(v.data)) : nlohmann::json(std::get<QString>(v.data));
}
void from_json(const nlohmann::json &j, ReactionId &v) {
	v = j.is_string() ? ReactionId{j.get<QString>()} : ReactionId{j.get<DocumentId>()};
}
void to_json(nlohmann::json &j, const UnreviewedAuth &v) {
	j = {{"hash", v.hash}, {"unconfirmed", v.unconfirmed}, {"date", v.date},
		{"device", v.device}, {"location", v.location}};
}
void from_json(const nlohmann::json &j, UnreviewedAuth &v) {
	j.at("hash").get_to(v.hash); j.at("unconfirmed").get_to(v.unconfirmed);
	j.at("date").get_to(v.date); j.at("device").get_to(v.device); j.at("location").get_to(v.location);
}
}
namespace MTP {
void to_json(nlohmann::json &j, const ProxyData &v) {
	j = {{"type", int(v.type)}, {"host", v.host}, {"port", v.port},
		{"user", v.user}, {"password", v.password}};
}
void from_json(const nlohmann::json &j, ProxyData &v) {
	v.type = ProxyData::Type(j.at("type").get<int>());
	j.at("host").get_to(v.host); j.at("port").get_to(v.port);
	j.at("user").get_to(v.user); j.at("password").get_to(v.password);
}
}
namespace Ui {
void to_json(nlohmann::json &j, const SendFilesWay &v) {
	j = {{"groupFiles", v.groupFiles()}, {"sendImagesAsPhotos", v.sendImagesAsPhotos()},
		{"sendLargePhotos", v.sendLargePhotos()}};
}
void from_json(const nlohmann::json &j, SendFilesWay &v) {
	v.setGroupFiles(j.at("groupFiles").get<bool>());
	v.setSendImagesAsPhotos(j.at("sendImagesAsPhotos").get<bool>());
	v.setSendLargePhotos(j.at("sendLargePhotos").get<bool>());
}
}
namespace base {
void to_json(nlohmann::json &j, const flat_set<QString> &v) {
	j = nlohmann::json::array();
	for (const auto &entry : v) j.push_back(entry);
}
void from_json(const nlohmann::json &j, flat_set<QString> &v) {
	for (const auto &entry : j.get<std::vector<QString>>()) v.insert(entry);
}
}

namespace AyuDebug::Commands {

void addCoreSettings(SettingsMap &entries) {
	auto &settings = Core::App().settings();
	for (const auto option : Settings::experimentalOptionsForDebug()) {
		entries.emplace(u"experimental."_q + option->id(), SettingEntry{
			[option] { return std::visit([](const auto &value) { return settingJson(value); }, option->value()); },
			[option](const Json &value) {
				if (!option->relevant()) return Result::Err(u"experimental option is unavailable on this platform"_q);
				return std::visit([&](const auto &current) {
					std::decay_t<decltype(current)> parsed{};
					if (const auto error = readSetting(value, parsed); !error.isEmpty()) return Result::Err(error);
					option->set(std::move(parsed));
					return Result::Ok();
				}, option->value());
			},
		});
	}
	entries.emplace(u"core.autoUpdate"_q, SettingEntry{
		[] { return Json(cAutoUpdate()); },
		[](const Json &value) {
			bool enabled = false;
			if (const auto error = readSetting(value, enabled); !error.isEmpty()) return Result::Err(error);
			cSetAutoUpdate(enabled);
			if (Core::UpdaterDisabled()) return Result::Ok();
			if (enabled) Core::UpdateChecker().start();
			else Core::UpdateChecker().stop();
			return Result::Ok();
		},
	});
	entries.emplace(u"core.startMinimized"_q, SettingEntry{
		[] { return Json(cStartMinimized()); },
		[](const Json &value) {
			bool enabled = false;
			if (const auto error = readSetting(value, enabled); !error.isEmpty()) return Result::Err(error);
			cSetStartMinimized(enabled);
			return Result::Ok();
		},
	});
	entries.emplace(u"core.configScale"_q, SettingEntry{
		[] { return Json(cConfigScale()); },
		[](const Json &value) {
			int scale = 0;
			if (const auto error = readSetting(value, scale); !error.isEmpty()) return Result::Err(error);
			if (style::CheckScale(scale) != scale) return Result::Err(u"unsupported interface scale"_q);
			cSetConfigScale(scale);
			return Result::Ok();
		},
	});
	const auto soundKeys = QStringList{
		u"msg_incoming"_q, u"call_incoming"_q, u"call_outgoing"_q,
		u"call_busy"_q, u"call_connect"_q, u"call_end"_q,
	};
	entries.emplace(u"core.soundOverrides"_q, SettingEntry{
		[&settings, soundKeys] {
			auto result = Json::object();
			for (const auto &key : soundKeys) result[key.toStdString()] = settings.getSoundPath(key);
			return result;
		},
		[&settings, soundKeys](const Json &value) {
			if (!value.is_object() || value.size() != soundKeys.size()) return Result::Err(u"expected all six sound paths"_q);
			for (const auto &key : soundKeys) {
				QString path;
				if (!value.contains(key.toStdString())) return Result::Err(u"missing sound path"_q);
				if (const auto error = readSetting(value.at(key.toStdString()), path); !error.isEmpty()) return Result::Err(error);
				if (!QFile::exists(path)) return Result::Err(u"sound file not found"_q);
			}
			settings.clearSoundOverrides();
			for (const auto &key : soundKeys) settings.setSoundOverride(key, value.at(key.toStdString()).get<QString>());
			return Result::Ok();
		},
	});
	addSetting(entries, u"core.adaptiveForWide"_q, [&] { return settings.adaptiveForWide(); }, settings, &Core::Settings::setAdaptiveForWide);
	addSetting(entries, u"core.moderateModeEnabled"_q, [&] { return settings.moderateModeEnabled(); }, settings, &Core::Settings::setModerateModeEnabled);
	addSetting(entries, u"core.songVolume"_q, [&] { return settings.songVolume(); }, settings, &Core::Settings::setSongVolume);
	addSetting(entries, u"core.videoVolume"_q, [&] { return settings.videoVolume(); }, settings, &Core::Settings::setVideoVolume);
	addSetting(entries, u"core.askDownloadPath"_q, [&] { return settings.askDownloadPath(); }, settings, &Core::Settings::setAskDownloadPath);
	addSetting(entries, u"core.downloadPath"_q, [&] { return settings.downloadPath(); }, settings, &Core::Settings::setDownloadPath);
	addSetting(entries, u"core.downloadPathBookmark"_q, [&] { return settings.downloadPathBookmark(); }, settings, &Core::Settings::setDownloadPathBookmark);
	addSetting(entries, u"core.soundNotify"_q, [&] { return settings.soundNotify(); }, settings, &Core::Settings::setSoundNotify);
	addSetting(entries, u"core.desktopNotify"_q, [&] { return settings.desktopNotify(); }, settings, &Core::Settings::setDesktopNotify);
	addSetting(entries, u"core.flashBounceNotify"_q, [&] { return settings.flashBounceNotify(); }, settings, &Core::Settings::setFlashBounceNotify);
	addSetting(entries, u"core.notifyView"_q, [&] { return settings.notifyView(); }, settings, &Core::Settings::setNotifyView);
	addSetting(entries, u"core.nativeNotifications"_q, [&] { return settings.nativeNotifications(); }, settings, &Core::Settings::setNativeNotifications);
	addSetting(entries, u"core.skipToastsInFocus"_q, [&] { return settings.skipToastsInFocus(); }, settings, &Core::Settings::setSkipToastsInFocus);
	addSetting(entries, u"core.notificationsCount"_q, [&] { return settings.notificationsCount(); }, settings, &Core::Settings::setNotificationsCount);
	addSetting(entries, u"core.notificationsCorner"_q, [&] { return settings.notificationsCorner(); }, settings, &Core::Settings::setNotificationsCorner);
	addSetting(entries, u"core.notificationsDisplayChecksum"_q, [&] { return settings.notificationsDisplayChecksum(); }, settings, &Core::Settings::setNotificationsDisplayChecksum);
	addSetting(entries, u"core.includeMutedCounter"_q, [&] { return settings.includeMutedCounter(); }, settings, &Core::Settings::setIncludeMutedCounter);
	addSetting(entries, u"core.includeMutedCounterFolders"_q, [&] { return settings.includeMutedCounterFolders(); }, settings, &Core::Settings::setIncludeMutedCounterFolders);
	addSetting(entries, u"core.countUnreadMessages"_q, [&] { return settings.countUnreadMessages(); }, settings, &Core::Settings::setCountUnreadMessages);
	addSetting(entries, u"core.notifyAboutPinned"_q, [&] { return settings.notifyAboutPinned(); }, settings, &Core::Settings::setNotifyAboutPinned);
	addSetting(entries, u"core.autoLock"_q, [&] { return settings.autoLock(); }, settings, &Core::Settings::setAutoLock);
	addSetting(entries, u"core.playbackDeviceId"_q, [&] { return settings.playbackDeviceId(); }, settings, &Core::Settings::setPlaybackDeviceId);
	addSetting(entries, u"core.captureDeviceId"_q, [&] { return settings.captureDeviceId(); }, settings, &Core::Settings::setCaptureDeviceId);
	addSetting(entries, u"core.cameraDeviceId"_q, [&] { return settings.cameraDeviceId(); }, settings, &Core::Settings::setCameraDeviceId);
	addSetting(entries, u"core.callPlaybackDeviceId"_q, [&] { return settings.callPlaybackDeviceId(); }, settings, &Core::Settings::setCallPlaybackDeviceId);
	addSetting(entries, u"core.callCaptureDeviceId"_q, [&] { return settings.callCaptureDeviceId(); }, settings, &Core::Settings::setCallCaptureDeviceId);
	addSetting(entries, u"core.callOutputVolume"_q, [&] { return settings.callOutputVolume(); }, settings, &Core::Settings::setCallOutputVolume);
	addSetting(entries, u"core.callInputVolume"_q, [&] { return settings.callInputVolume(); }, settings, &Core::Settings::setCallInputVolume);
	addSetting(entries, u"core.callAudioDuckingEnabled"_q, [&] { return settings.callAudioDuckingEnabled(); }, settings, &Core::Settings::setCallAudioDuckingEnabled);
	addSetting(entries, u"core.groupCallPushToTalk"_q, [&] { return settings.groupCallPushToTalk(); }, settings, &Core::Settings::setGroupCallPushToTalk);
	addSetting(entries, u"core.groupCallPushToTalkShortcut"_q, [&] { return settings.groupCallPushToTalkShortcut(); }, settings, &Core::Settings::setGroupCallPushToTalkShortcut);
	addSetting(entries, u"core.groupCallPushToTalkDelay"_q, [&] { return settings.groupCallPushToTalkDelay(); }, settings, &Core::Settings::setGroupCallPushToTalkDelay);
	addSetting(entries, u"core.groupCallNoiseSuppression"_q, [&] { return settings.groupCallNoiseSuppression(); }, settings, &Core::Settings::setGroupCallNoiseSuppression);
	addSetting(entries, u"core.lastSeenWarningSeen"_q, [&] { return settings.lastSeenWarningSeen(); }, settings, &Core::Settings::setLastSeenWarningSeen);
	addSetting(entries, u"core.sendFilesWay"_q, [&] { return settings.sendFilesWay(); }, settings, &Core::Settings::setSendFilesWay);
	addSetting(entries, u"core.sendSubmitWay"_q, [&] { return settings.sendSubmitWay(); }, settings, &Core::Settings::setSendSubmitWay);
	addSetting(entries, u"core.noWarningExtensions"_q, [&] { return settings.noWarningExtensions(); }, settings, &Core::Settings::setNoWarningExtensions);
	addSetting(entries, u"core.ipRevealWarning"_q, [&] { return settings.ipRevealWarning(); }, settings, &Core::Settings::setIpRevealWarning);
	addSetting(entries, u"core.loopAnimatedStickers"_q, [&] { return settings.loopAnimatedStickers(); }, settings, &Core::Settings::setLoopAnimatedStickers);
	addSetting(entries, u"core.largeEmoji"_q, [&] { return settings.largeEmoji(); }, settings, &Core::Settings::setLargeEmoji);
	addSetting(entries, u"core.replaceEmoji"_q, [&] { return settings.replaceEmoji(); }, settings, &Core::Settings::setReplaceEmoji);
	addSetting(entries, u"core.systemTextReplace"_q, [&] { return settings.systemTextReplace(); }, settings, &Core::Settings::setSystemTextReplace);
	addSetting(entries, u"core.suggestEmoji"_q, [&] { return settings.suggestEmoji(); }, settings, &Core::Settings::setSuggestEmoji);
	addSetting(entries, u"core.suggestStickersByEmoji"_q, [&] { return settings.suggestStickersByEmoji(); }, settings, &Core::Settings::setSuggestStickersByEmoji);
	addSetting(entries, u"core.suggestAnimatedEmoji"_q, [&] { return settings.suggestAnimatedEmoji(); }, settings, &Core::Settings::setSuggestAnimatedEmoji);
	addSetting(entries, u"core.cornerReaction"_q, [&] { return settings.cornerReaction(); }, settings, &Core::Settings::setCornerReaction);
	addSetting(entries, u"core.cornerReply"_q, [&] { return settings.cornerReply(); }, settings, &Core::Settings::setCornerReply);
	addSetting(entries, u"core.pullToNextChannel"_q, [&] { return settings.pullToNextChannel(); }, settings, &Core::Settings::setPullToNextChannel);
	addSetting(entries, u"core.spellcheckerEnabled"_q, [&] { return settings.spellcheckerEnabled(); }, settings, &Core::Settings::setSpellcheckerEnabled);
	addSetting(entries, u"core.dictionariesEnabled"_q, [&] { return settings.dictionariesEnabled(); }, settings, &Core::Settings::setDictionariesEnabled);
	addSetting(entries, u"core.autoDownloadDictionaries"_q, [&] { return settings.autoDownloadDictionaries(); }, settings, &Core::Settings::setAutoDownloadDictionaries);
	addSetting(entries, u"core.videoPipGeometry"_q, [&] { return settings.videoPipGeometry(); }, settings, &Core::Settings::setVideoPipGeometry);
	addSetting(entries, u"core.photoEditorBrush"_q, [&] { return settings.photoEditorBrush(); }, settings, &Core::Settings::setPhotoEditorBrush);
	addSetting(entries, u"core.rememberedSongVolume"_q, [&] { return settings.rememberedSongVolume(); }, settings, &Core::Settings::setRememberedSongVolume);
	addSetting(entries, u"core.rememberedSoundNotifyFromTray"_q, [&] { return settings.rememberedSoundNotifyFromTray(); }, settings, &Core::Settings::setRememberedSoundNotifyFromTray);
	addSetting(entries, u"core.rememberedFlashBounceNotifyFromTray"_q, [&] { return settings.rememberedFlashBounceNotifyFromTray(); }, settings, &Core::Settings::setRememberedFlashBounceNotifyFromTray);
	addSetting(entries, u"core.mainMenuAccountsShown"_q, [&] { return settings.mainMenuAccountsShown(); }, settings, &Core::Settings::setMainMenuAccountsShown);
	addSetting(entries, u"core.tabbedSelectorSectionEnabled"_q, [&] { return settings.tabbedSelectorSectionEnabled(); }, settings, &Core::Settings::setTabbedSelectorSectionEnabled);
	addSetting(entries, u"core.thirdSectionInfoEnabled"_q, [&] { return settings.thirdSectionInfoEnabled(); }, settings, &Core::Settings::setThirdSectionInfoEnabled);
	addSetting(entries, u"core.thirdSectionExtendedBy"_q, [&] { return settings.thirdSectionExtendedBy(); }, settings, &Core::Settings::setThirdSectionExtendedBy);
	addSetting(entries, u"core.tabbedReplacedWithInfo"_q, [&] { return settings.tabbedReplacedWithInfo(); }, settings, &Core::Settings::setTabbedReplacedWithInfo);
	addSetting(entries, u"core.floatPlayerColumn"_q, [&] { return settings.floatPlayerColumn(); }, settings, &Core::Settings::setFloatPlayerColumn);
	addSetting(entries, u"core.floatPlayerCorner"_q, [&] { return settings.floatPlayerCorner(); }, settings, &Core::Settings::setFloatPlayerCorner);
	addSetting(entries, u"core.recordVideoMessages"_q, [&] { return settings.recordVideoMessages(); }, settings, &Core::Settings::setRecordVideoMessages);
	addSetting(entries, u"core.thirdColumnWidth"_q, [&] { return settings.thirdColumnWidth(); }, settings, &Core::Settings::setThirdColumnWidth);
	addSetting(entries, u"core.notifyFromAll"_q, [&] { return settings.notifyFromAll(); }, settings, &Core::Settings::setNotifyFromAll);
	addSetting(entries, u"core.nativeWindowFrame"_q, [&] { return settings.nativeWindowFrame(); }, settings, &Core::Settings::setNativeWindowFrame);
	addSetting(entries, u"core.systemDarkMode"_q, [&] { return settings.systemDarkMode(); }, settings, &Core::Settings::setSystemDarkMode);
	addSetting(entries, u"core.systemDarkModeEnabled"_q, [&] { return settings.systemDarkModeEnabled(); }, settings, &Core::Settings::setSystemDarkModeEnabled);
	addSetting(entries, u"core.systemAccentColorEnabled"_q, [&] { return settings.systemAccentColorEnabled(); }, settings, &Core::Settings::setSystemAccentColorEnabled);
	addSetting(entries, u"core.windowTitleContent"_q, [&] { return settings.windowTitleContent(); }, settings, &Core::Settings::setWindowTitleContent);
	addSetting(entries, u"core.windowPosition"_q, [&] { return settings.windowPosition(); }, settings, &Core::Settings::setWindowPosition);
	addSetting(entries, u"core.workMode"_q, [&] { return settings.workMode(); }, settings, &Core::Settings::setWorkMode);
	addSetting(entries, u"core.disableOpenGL"_q, [&] { return settings.disableOpenGL(); }, settings, &Core::Settings::setDisableOpenGL);
	addSetting(entries, u"core.closeBehavior"_q, [&] { return settings.closeBehavior(); }, settings, &Core::Settings::setCloseBehavior);
	addSetting(entries, u"core.trayIconMonochrome"_q, [&] { return settings.trayIconMonochrome(); }, settings, &Core::Settings::setTrayIconMonochrome);
	addSetting(entries, u"core.customDeviceModel"_q, [&] { return settings.customDeviceModel(); }, settings, &Core::Settings::setCustomDeviceModel);
	addSetting(entries, u"core.playerRepeatMode"_q, [&] { return settings.playerRepeatMode(); }, settings, &Core::Settings::setPlayerRepeatMode);
	addSetting(entries, u"core.playerOrderMode"_q, [&] { return settings.playerOrderMode(); }, settings, &Core::Settings::setPlayerOrderMode);
	addSetting(entries, u"core.accountsOrder"_q, [&] { return settings.accountsOrder(); }, settings, &Core::Settings::setAccountsOrder);
	addSetting(entries, u"core.hardwareAcceleratedVideo"_q, [&] { return settings.hardwareAcceleratedVideo(); }, settings, &Core::Settings::setHardwareAcceleratedVideo);
	addSetting(entries, u"core.macWarnBeforeQuit"_q, [&] { return settings.macWarnBeforeQuit(); }, settings, &Core::Settings::setMacWarnBeforeQuit);
	addSetting(entries, u"core.chatQuickAction"_q, [&] { return settings.chatQuickAction(); }, settings, &Core::Settings::setChatQuickAction);
	addSetting(entries, u"core.translateButtonEnabled"_q, [&] { return settings.translateButtonEnabled(); }, settings, &Core::Settings::setTranslateButtonEnabled);
	addSetting(entries, u"core.usePlatformTranslation"_q, [&] { return settings.usePlatformTranslation(); }, settings, &Core::Settings::setUsePlatformTranslation);
	addSetting(entries, u"core.translateChatEnabled"_q, [&] { return settings.translateChatEnabled(); }, settings, &Core::Settings::setTranslateChatEnabled);
	addSetting(entries, u"core.translateTo"_q, [&] { return settings.translateTo(); }, settings, &Core::Settings::setTranslateTo);
	addSetting(entries, u"core.skipTranslationLanguages"_q, [&] { return settings.skipTranslationLanguages(); }, settings, &Core::Settings::setSkipTranslationLanguages);
	addSetting(entries, u"core.rememberedDeleteMessageOnlyForYou"_q, [&] { return settings.rememberedDeleteMessageOnlyForYou(); }, settings, &Core::Settings::setRememberedDeleteMessageOnlyForYou);
	addSetting(entries, u"core.mediaViewPosition"_q, [&] { return settings.mediaViewPosition(); }, settings, &Core::Settings::setMediaViewPosition);
	addSetting(entries, u"core.macRoundIconDigest"_q, [&] { return settings.macRoundIconDigest(); }, settings, &Core::Settings::setMacRoundIconDigest);
	addSetting(entries, u"core.storiesClickTooltipHidden"_q, [&] { return settings.storiesClickTooltipHidden(); }, settings, &Core::Settings::setStoriesClickTooltipHidden);
	addSetting(entries, u"core.ttlVoiceClickTooltipHidden"_q, [&] { return settings.ttlVoiceClickTooltipHidden(); }, settings, &Core::Settings::setTtlVoiceClickTooltipHidden);
	addSetting(entries, u"core.ivPosition"_q, [&] { return settings.ivPosition(); }, settings, &Core::Settings::setIvPosition);
	addSetting(entries, u"core.callPanelPosition"_q, [&] { return settings.callPanelPosition(); }, settings, &Core::Settings::setCallPanelPosition);
	addSetting(entries, u"core.customFontFamily"_q, [&] { return settings.customFontFamily(); }, settings, &Core::Settings::setCustomFontFamily);
	addSetting(entries, u"core.systemUnlockEnabled"_q, [&] { return settings.systemUnlockEnabled(); }, settings, &Core::Settings::setSystemUnlockEnabled);
	addSetting(entries, u"core.weatherInCelsius"_q, [&] { return settings.weatherInCelsius(); }, settings, &Core::Settings::setWeatherInCelsius);
	addSetting(entries, u"core.tonsiteStorageToken"_q, [&] { return settings.tonsiteStorageToken(); }, settings, &Core::Settings::setTonsiteStorageToken);
	addSetting(entries, u"core.ivZoom"_q, [&] { return settings.ivZoom(); }, settings, &Core::Settings::setIvZoom);
	addSetting(entries, u"core.chatFiltersHorizontal"_q, [&] { return settings.chatFiltersHorizontal(); }, settings, &Core::Settings::setChatFiltersHorizontal);
	addSetting(entries, u"core.chatFiltersTabsMode"_q, [&] { return settings.chatFiltersTabsMode(); }, settings, &Core::Settings::setChatFiltersTabsMode);
	addSetting(entries, u"core.videoQuality"_q, [&] { return settings.videoQuality(); }, settings, &Core::Settings::setVideoQuality);
	addSetting(entries, u"core.quickDialogAction"_q, [&] { return settings.quickDialogAction(); }, settings, &Core::Settings::setQuickDialogAction);
	addSetting(entries, u"core.notificationsVolume"_q, [&] { return settings.notificationsVolume(); }, settings, &Core::Settings::setNotificationsVolume);
	addSetting(entries, u"core.mediaGridZoomStep"_q, [&] { return settings.mediaGridZoomStep(); }, settings, &Core::Settings::setMediaGridZoomStep);
	addSetting(entries, u"core.videoPlaybackSpeed"_q, [&] { return settings.videoPlaybackSpeed(); }, settings, &Core::Settings::setVideoPlaybackSpeed);
	addSetting(entries, u"core.voicePlaybackSpeed"_q, [&] { return settings.voicePlaybackSpeed(); }, settings, &Core::Settings::setVoicePlaybackSpeed);
	addSetting(entries, u"core.audioPlaybackSpeed"_q, [&] { return settings.audioPlaybackSpeed(); }, settings, &Core::Settings::setAudioPlaybackSpeed);
	addSetting(entries, u"core.ignoreBatterySaving"_q,
		[&] { return settings.ignoreBatterySaving(); }, settings,
		&Core::Settings::setIgnoreBatterySavingValue);
	for (const auto withoutChat : {false, true}) {
		entries.emplace(withoutChat ? u"core.dialogsNoChatWidthRatio"_q : u"core.dialogsWithChatWidthRatio"_q, SettingEntry{
			[&settings, withoutChat] { return Json(withoutChat ? settings.dialogsNoChatWidthRatio() : settings.dialogsWithChatWidthRatio()); },
			[&settings, withoutChat](const Json &value) {
				double ratio = 0;
				if (const auto error = readSetting(value, ratio); !error.isEmpty()) return Result::Err(error);
				if (ratio < 0 || ratio > 1) return Result::Err(u"width ratio must be between 0 and 1"_q);
				settings.updateDialogsWidthRatio(ratio, withoutChat);
				return Result::Ok();
			},
		});
	}
	entries.emplace(u"core.themesAccentColors"_q, SettingEntry{
		[&] { return settingJson(settings.themesAccentColors().serialize()); },
		[&](const Json &value) {
			QByteArray data;
			if (const auto error = readSetting(value, data); !error.isEmpty()) return Result::Err(error);
			Window::Theme::AccentColors colors;
			if (!colors.setFromSerialized(data)) return Result::Err(u"invalid theme accent data"_q);
			settings.setThemesAccentColors(std::move(colors));
			return Result::Ok();
		},
	});
	entries.emplace(u"core.powerSaving"_q, SettingEntry{
		[] { return Json(PowerSaving::Current().value()); },
		[](const Json &value) {
			uint32 flags = 0;
			if (const auto error = readSetting(value, flags); !error.isEmpty()) return Result::Err(error);
			if (flags & ~uint32(PowerSaving::kAll)) return Result::Err(u"invalid power saving flags"_q);
			PowerSaving::Set(PowerSaving::Flags::from_raw(flags));
			return Result::Ok();
		},
	});
}

void addSessionSettings(SettingsMap &entries) {
	const auto session = ActiveSession();
	if (!session) return;
	auto &settings = session->settings();
	for (auto i = 0; i != 3; ++i) {
		const auto kind = Data::DefaultNotify(i);
		entries.emplace(u"session.defaultRingtoneVolume.%1"_q.arg(i), SettingEntry{
			[&settings, kind] { return Json(settings.ringtoneVolume(kind)); },
			[&settings, kind](const Json &value) {
				ushort volume = 0;
				if (const auto error = readSetting(value, volume); !error.isEmpty()) return Result::Err(error);
				if (volume > 100) return Result::Err(u"volume must be between 0 and 100"_q);
				settings.setRingtoneVolume(kind, volume);
				return Result::Ok();
			},
		});
	}
	addSetting(entries, u"session.supportSwitch"_q, [&] { return settings.supportSwitch(); }, settings, &Main::SessionSettings::setSupportSwitch);
	addSetting(entries, u"session.supportFixChatsOrder"_q, [&] { return settings.supportFixChatsOrder(); }, settings, &Main::SessionSettings::setSupportFixChatsOrder);
	addSetting(entries, u"session.supportTemplatesAutocomplete"_q, [&] { return settings.supportTemplatesAutocomplete(); }, settings, &Main::SessionSettings::setSupportTemplatesAutocomplete);
	addSetting(entries, u"session.supportChatsTimeSlice"_q, [&] { return settings.supportChatsTimeSlice(); }, settings, &Main::SessionSettings::setSupportChatsTimeSlice);
	addSetting(entries, u"session.supportAllSearchResults"_q, [&] { return settings.supportAllSearchResults(); }, settings, &Main::SessionSettings::setSupportAllSearchResults);
	addSetting(entries, u"session.supportAllSilent"_q, [&] { return settings.supportAllSilent(); }, settings, &Main::SessionSettings::setSupportAllSilent);
	addSetting(entries, u"session.selectorTab"_q, [&] { return settings.selectorTab(); }, settings, &Main::SessionSettings::setSelectorTab);
	addSetting(entries, u"session.archiveCollapsed"_q, [&] { return settings.archiveCollapsed(); }, settings, &Main::SessionSettings::setArchiveCollapsed);
	addSetting(entries, u"session.archiveInMainMenu"_q, [&] { return settings.archiveInMainMenu(); }, settings, &Main::SessionSettings::setArchiveInMainMenu);
	addSetting(entries, u"session.dialogsFiltersEnabled"_q, [&] { return settings.dialogsFiltersEnabled(); }, settings, &Main::SessionSettings::setDialogsFiltersEnabled);
	addSetting(entries, u"session.lastNonPremiumLimitDownload"_q, [&] { return settings.lastNonPremiumLimitDownload(); }, settings, &Main::SessionSettings::setLastNonPremiumLimitDownload);
	addSetting(entries, u"session.lastNonPremiumLimitUpload"_q, [&] { return settings.lastNonPremiumLimitUpload(); }, settings, &Main::SessionSettings::setLastNonPremiumLimitUpload);
	addSetting(entries, u"session.unreviewed"_q, [&] { return settings.unreviewed(); }, settings, &Main::SessionSettings::setUnreviewed);
	addSetting(entries, u"session.setupEmailState"_q, [&] { return settings.setupEmailState(); }, settings, &Main::SessionSettings::setSetupEmailState);
	addSetting(entries, u"session.moderateCommonGroups"_q, [&] { return settings.moderateCommonGroups(); }, settings, &Main::SessionSettings::setModerateCommonGroups);
	addSetting(entries, u"session.phoneNumberHidden"_q, [&] { return settings.phoneNumberHidden(); }, settings, &Main::SessionSettings::setPhoneNumberHidden);
	addSetting(entries, u"session.extraFavoriteReactions"_q, [&] { return settings.extraFavoriteReactions(); }, settings, &Main::SessionSettings::setExtraFavoriteReactions);
	for (auto source = 0; source != Data::AutoDownload::kSourcesCount; ++source) {
		for (auto type = 0; type != Data::AutoDownload::kTypesCount; ++type) {
			const auto origin = Data::AutoDownload::Source(source);
			const auto media = Data::AutoDownload::Type(type);
			entries.emplace(u"session.autoDownload.%1.%2"_q.arg(source).arg(type), SettingEntry{
				[&settings, origin, media] { return Json(settings.autoDownload().bytesLimit(origin, media)); },
				[&settings, origin, media](const Json &value) {
					int64 limit = 0;
					if (const auto error = readSetting(value, limit); !error.isEmpty()) return Result::Err(error);
					if (limit < 0 || limit > Data::AutoDownload::kMaxBytesLimit) return Result::Err(u"download limit out of range"_q);
					settings.autoDownload().setBytesLimit(origin, media, limit);
					return Result::Ok();
				},
			});
		}
	}
}

void addProxySettings(SettingsMap &entries) {
	auto &settings = Core::App().settings().proxy();
	addSetting(entries, u"proxy.tryIPv6"_q, [&] { return settings.tryIPv6(); }, settings, &Core::SettingsProxy::setTryIPv6);
	addSetting(entries, u"proxy.useProxyForCalls"_q, [&] { return settings.useProxyForCalls(); }, settings, &Core::SettingsProxy::setUseProxyForCalls);
	addSetting(entries, u"proxy.proxyRotationEnabled"_q, [&] { return settings.proxyRotationEnabled(); }, settings, &Core::SettingsProxy::setProxyRotationEnabled);
	addSetting(entries, u"proxy.proxyRotationTimeout"_q, [&] { return settings.proxyRotationTimeout(); }, settings, &Core::SettingsProxy::setProxyRotationTimeout);
	addSetting(entries, u"proxy.checkIpWarningShown"_q, [&] { return settings.checkIpWarningShown(); }, settings, &Core::SettingsProxy::setCheckIpWarningShown);
	addSetting(entries, u"proxy.proxyRotationPreferredIndices"_q, [&] { return settings.proxyRotationPreferredIndices(); }, settings, &Core::SettingsProxy::setProxyRotationPreferredIndices);
	entries.emplace(u"proxy.settings"_q, SettingEntry{
		[&] { return Json(int(settings.settings())); },
		[&](const Json &value) {
			int mode = 0;
			if (const auto error = readSetting(value, mode); !error.isEmpty()) return Result::Err(error);
			if (mode < 0 || mode > 2) return Result::Err(u"invalid proxy mode"_q);
			if (mode == 1 && !settings.selected().valid()) return Result::Err(u"configure a valid proxy first"_q);
			Core::App().setCurrentProxy(settings.selected(), MTP::ProxyData::Settings(mode));
			return Result::Ok();
		},
	});
	entries.emplace(u"proxy.selected"_q, SettingEntry{
		[&] { return Json(settings.selected()); },
		[&](const Json &value) {
			MTP::ProxyData proxy;
			if (const auto error = readSetting(value, proxy); !error.isEmpty()) return Result::Err(error);
			if (int(proxy.type) < 0 || int(proxy.type) > int(MTP::ProxyData::Type::Web) || proxy.port > 65535) {
				return Result::Err(u"invalid proxy type or port"_q);
			}
			if (!proxy.valid() && proxy.type != MTP::ProxyData::Type::None) return Result::Err(u"invalid proxy configuration"_q);
			if (!proxy.valid() && settings.isEnabled()) return Result::Err(u"disable proxy mode before clearing the selected proxy"_q);
			Core::App().setCurrentProxy(proxy, settings.settings());
			return Result::Ok();
		},
	});
	entries.emplace(u"proxy.list"_q, SettingEntry{
		[&] { return Json(settings.list()); },
		[&](const Json &value) {
			std::vector<MTP::ProxyData> proxies;
			if (const auto error = readSetting(value, proxies); !error.isEmpty()) return Result::Err(error);
			for (const auto &proxy : proxies) {
				if (!proxy.valid() || int(proxy.type) < 0 || int(proxy.type) > int(MTP::ProxyData::Type::Web) || proxy.port > 65535) {
					return Result::Err(u"proxy list contains an invalid entry"_q);
				}
			}
			settings.setList(proxies);
			settings.connectionTypeChangesNotify();
			return Result::Ok();
		},
	});
}

} // namespace AyuDebug::Commands
#endif
