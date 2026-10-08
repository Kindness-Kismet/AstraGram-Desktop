#include "extras/features/settings_transfer/settings_transfer_fields.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_auto_download.h"
#include "extras/extras_settings.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "media/media_common.h"
#include "settings/settings_notifications_common.h"
#include "ui/chat/attach/attach_send_files_way.h"
#include "ui/chat/chat_style_radius.h"

namespace Extras::SettingsTransfer {
namespace {

void addSendFilesFields(Fields &fields, Core::Settings &settings) {
	using Way = Ui::SendFilesWay;
	const auto add = [&](const char *key, auto getter, auto setter) {
		fields.emplace(key, Field{
			.value = std::invoke(getter, settings.sendFilesWay()),
			.set = [&settings, setter](const Json &value) {
				auto way = settings.sendFilesWay();
				std::invoke(setter, way, value.get<bool>());
				settings.setSendFilesWay(way);
			},
			.valid = [](const Json &value) {
				return value.is_boolean();
			},
		});
	};
	add("sendFilesWay.groupFiles",
		&Way::groupFiles, &Way::setGroupFiles);
	add("sendFilesWay.sendImagesAsPhotos",
		&Way::sendImagesAsPhotos, &Way::setSendImagesAsPhotos);
	add("sendFilesWay.sendLargePhotos",
		&Way::sendLargePhotos, &Way::setSendLargePhotos);
}

}

Fields officialFields() {
	using S = Core::Settings;
	auto &settings = Core::App().settings();
	auto fields = Fields();
	addField(fields, "adaptiveForWide", settings,
		&S::adaptiveForWide, &S::setAdaptiveForWide);
	addField(fields, "moderateModeEnabled", settings,
		&S::moderateModeEnabled, &S::setModerateModeEnabled);
	addField(fields, "songVolume", settings,
		&S::songVolume, &S::setSongVolume, 0, 1);
	addField(fields, "videoVolume", settings,
		&S::videoVolume, &S::setVideoVolume, 0, 1);
	addField(fields, "askDownloadPath", settings,
		&S::askDownloadPath, &S::setAskDownloadPath);
	addField(fields, "downloadPath", settings,
		&S::downloadPath, &S::setDownloadPath);
	addField(fields, "soundNotify", settings,
		&S::soundNotify, &S::setSoundNotify);
	addField(fields, "desktopNotify", settings,
		&S::desktopNotify, &S::setDesktopNotify);
	addField(fields, "flashBounceNotify", settings,
		&S::flashBounceNotify, &S::setFlashBounceNotify);
	addField(fields, "notifyView", settings,
		&S::notifyView, &S::setNotifyView, 0, 2);
	addField(fields, "nativeNotifications", settings,
		&S::nativeNotifications, &S::setNativeNotifications);
	addField(fields, "skipToastsInFocus", settings,
		&S::skipToastsInFocus, &S::setSkipToastsInFocus);
	addField(fields, "notificationsCount", settings,
		&S::notificationsCount, &S::setNotificationsCount, 1, ::Settings::kMaxNotificationsCount);
	addField(fields, "notificationsCorner", settings,
		&S::notificationsCorner, &S::setNotificationsCorner, 0, 4);
	addField(fields, "includeMutedCounter", settings,
		&S::includeMutedCounter, &S::setIncludeMutedCounter);
	addField(fields, "includeMutedCounterFolders", settings,
		&S::includeMutedCounterFolders, &S::setIncludeMutedCounterFolders);
	addField(fields, "countUnreadMessages", settings,
		&S::countUnreadMessages, &S::setCountUnreadMessages);
	addField(fields, "notifyAboutPinned", settings,
		&S::notifyAboutPinned, &S::setNotifyAboutPinned);
	addField(fields, "callAudioDuckingEnabled", settings,
		&S::callAudioDuckingEnabled, &S::setCallAudioDuckingEnabled);
	addField(fields, "groupCallNoiseSuppression", settings,
		&S::groupCallNoiseSuppression, &S::setGroupCallNoiseSuppression);
	addField(fields, "sendSubmitWay", settings,
		&S::sendSubmitWay, &S::setSendSubmitWay, 0, 1);
	addField(fields, "ipRevealWarning", settings,
		&S::ipRevealWarning, &S::setIpRevealWarning);
	addField(fields, "loopAnimatedStickers", settings,
		&S::loopAnimatedStickers, &S::setLoopAnimatedStickers);
	addField(fields, "largeEmoji", settings,
		&S::largeEmoji, &S::setLargeEmoji);
	addField(fields, "replaceEmoji", settings,
		&S::replaceEmoji, &S::setReplaceEmoji);
	addField(fields, "systemTextReplace", settings,
		&S::systemTextReplace, &S::setSystemTextReplace);
	addField(fields, "suggestEmoji", settings,
		&S::suggestEmoji, &S::setSuggestEmoji);
	addField(fields, "suggestStickersByEmoji", settings,
		&S::suggestStickersByEmoji, &S::setSuggestStickersByEmoji);
	addField(fields, "suggestAnimatedEmoji", settings,
		&S::suggestAnimatedEmoji, &S::setSuggestAnimatedEmoji);
	addField(fields, "cornerReaction", settings,
		&S::cornerReaction, &S::setCornerReaction);
	addField(fields, "cornerReply", settings,
		&S::cornerReply, &S::setCornerReply);
	addField(fields, "pullToNextChannel", settings,
		&S::pullToNextChannel, &S::setPullToNextChannel);
	addField(fields, "spellcheckerEnabled", settings,
		&S::spellcheckerEnabled, &S::setSpellcheckerEnabled);
	addField(fields, "autoDownloadDictionaries", settings,
		&S::autoDownloadDictionaries, &S::setAutoDownloadDictionaries);
	addField(fields, "mainMenuAccountsShown", settings,
		&S::mainMenuAccountsShown, &S::setMainMenuAccountsShown);
	addField(fields, "recordVideoMessages", settings,
		&S::recordVideoMessages, &S::setRecordVideoMessages);
	addField(fields, "notifyFromAll", settings,
		&S::notifyFromAll, &S::setNotifyFromAll);
	addField(fields, "systemDarkModeEnabled", settings,
		&S::systemDarkModeEnabled, &S::setSystemDarkModeEnabled);
	addField(fields, "systemAccentColorEnabled", settings,
		&S::systemAccentColorEnabled, &S::setSystemAccentColorEnabled,
		0, 0, true);
	addField(fields, "workMode", settings,
		&S::workMode, &S::setWorkMode, 0, 2);
	addField(fields, "closeBehavior", settings,
		&S::closeBehavior, &S::setCloseBehavior, 0, 2);
	addField(fields, "trayIconMonochrome", settings,
		&S::trayIconMonochrome, &S::setTrayIconMonochrome);
	addField(fields, "playerRepeatMode", settings,
		&S::playerRepeatMode, &S::setPlayerRepeatMode, 0, 2);
	addField(fields, "playerOrderMode", settings,
		&S::playerOrderMode, &S::setPlayerOrderMode, 0, 2);
	addField(fields, "hardwareAcceleratedVideo", settings,
		&S::hardwareAcceleratedVideo, &S::setHardwareAcceleratedVideo);
	addField(fields, "chatQuickAction", settings,
		&S::chatQuickAction, &S::setChatQuickAction, 0, 2);
	addField(fields, "translateButtonEnabled", settings,
		&S::translateButtonEnabled, &S::setTranslateButtonEnabled);
	addField(fields, "translateChatEnabled", settings,
		&S::translateChatEnabled, &S::setTranslateChatEnabled);
	addField(fields, "customFontFamily", settings,
		&S::customFontFamily, &S::setCustomFontFamily, 0, 0, true);
	addField(fields, "chatFiltersHorizontal", settings,
		&S::chatFiltersHorizontal, &S::setChatFiltersHorizontal);
	addField(fields, "videoPlaybackSpeed", settings,
		[](const S &s) { return s.videoPlaybackSpeed(); },
		&S::setVideoPlaybackSpeed, Media::kSpeedMin, Media::kSpeedMax);
	addField(fields, "voicePlaybackSpeed", settings,
		[](const S &s) { return s.voicePlaybackSpeed(); },
		&S::setVoicePlaybackSpeed, Media::kSpeedMin, Media::kSpeedMax);
	addField(fields, "audioPlaybackSpeed", settings,
		[](const S &s) { return s.audioPlaybackSpeed(); },
		&S::setAudioPlaybackSpeed, Media::kSpeedMin, Media::kSpeedMax);
	addSendFilesFields(fields, settings);
	return fields;
}

Fields customFields() {
	using S = ExtrasSettings;
	auto &settings = S::getInstance();
	auto fields = Fields();
	addField(fields, "saveDeletedMessages", settings,
		&S::saveDeletedMessages, &S::setSaveDeletedMessages);
	addField(fields, "saveMessagesHistory", settings,
		&S::saveMessagesHistory, &S::setSaveMessagesHistory);
	addField(fields, "saveForBots", settings,
		&S::saveForBots, &S::setSaveForBots);
	addField(fields, "filtersEnabled", settings,
		&S::filtersEnabled, &S::setFiltersEnabled);
	addField(fields, "filtersEnabledInChats", settings,
		&S::filtersEnabledInChats, &S::setFiltersEnabledInChats);
	addField(fields, "hideFromBlocked", settings,
		&S::hideFromBlocked, &S::setHideFromBlocked);
	addField(fields, "semiTransparentDeletedMessages", settings,
		&S::semiTransparentDeletedMessages, &S::setSemiTransparentDeletedMessages);
	addField(fields, "disableAds", settings,
		&S::disableAds, &S::setDisableAds);
	addField(fields, "disableStories", settings,
		&S::disableStories, &S::setDisableStories, 0, 0, true);
	addField(fields, "disableChatBackground", settings,
		&S::disableChatBackground, &S::setDisableChatBackground);
	addField(fields, "showBubbleOutline", settings,
		&S::showBubbleOutline, &S::setShowBubbleOutline);
	addField(fields, "disableBubbleShadow", settings,
		&S::disableBubbleShadow, &S::setDisableBubbleShadow);
	addField(fields, "hidePremiumStatuses", settings,
		&S::hidePremiumStatuses, &S::setHidePremiumStatuses);
	addField(fields, "hideProxySettingsIcon", settings,
		&S::hideProxySettingsIcon, &S::setHideProxySettingsIcon);
	addField(fields, "showDownloadsButtonInHeader", settings,
		&S::showDownloadsButtonInHeader, &S::setShowDownloadsButtonInHeader);
	addField(fields, "showOnlyAddedEmojisAndStickers", settings,
		&S::showOnlyAddedEmojisAndStickers, &S::setShowOnlyAddedEmojisAndStickers);
	addField(fields, "collapseSimilarChannels", settings,
		&S::collapseSimilarChannels, &S::setCollapseSimilarChannels);
	addField(fields, "hideSimilarChannels", settings,
		&S::hideSimilarChannels, &S::setHideSimilarChannels);
	addField(fields, "messageBubbleRadius", settings,
		&S::messageBubbleRadius, &S::setMessageBubbleRadius, Ui::kBubbleRadiusSliderMin, Ui::kBubbleRadiusSliderMax, true);
	addField(fields, "disableOpenLinkWarning", settings,
		&S::disableOpenLinkWarning, &S::setDisableOpenLinkWarning);
	addField(fields, "wideMultiplier", settings,
		&S::wideMultiplier, &S::setWideMultiplier, 0.5, 4.0, true);
	addField(fields, "messageStickerScale", settings,
		&S::messageStickerScale, &S::setMessageStickerScale, 0.5, 1.6);
	addField(fields, "stickerTimestampOnHover", settings,
		&S::stickerTimestampOnHover, &S::setStickerTimestampOnHover);
	addField(fields, "spoofWebviewAsAndroid", settings,
		&S::spoofWebviewAsAndroid, &S::setSpoofWebviewAsAndroid);
	addField(fields, "increaseWebviewHeight", settings,
		&S::increaseWebviewHeight, &S::setIncreaseWebviewHeight);
	addField(fields, "increaseWebviewWidth", settings,
		&S::increaseWebviewWidth, &S::setIncreaseWebviewWidth);
	addField(fields, "windowMaterial", settings,
		&S::windowMaterial, &S::setWindowMaterial, 0, 3);
	addField(fields, "horizontalTabStyle", settings,
		&S::horizontalTabStyle, &S::setHorizontalTabStyle, 0, 2);
	addField(fields, "removeMessageTail", settings,
		&S::removeMessageTail, &S::setRemoveMessageTail);
	addField(fields, "disableNotificationsDelay", settings,
		&S::disableNotificationsDelay, &S::setDisableNotificationsDelay);
	addField(fields, "showChannelReactions", settings,
		&S::showChannelReactions, &S::setShowChannelReactions);
	addField(fields, "showGroupReactions", settings,
		&S::showGroupReactions, &S::setShowGroupReactions);
	addField(fields, "showPrivateChatReactions", settings,
		&S::showPrivateChatReactions, &S::setShowPrivateChatReactions);
	addField(fields, "simpleQuotesAndReplies", settings,
		&S::simpleQuotesAndReplies, &S::setSimpleQuotesAndReplies);
	addField(fields, "hideFastShare", settings,
		&S::hideFastShare, &S::setHideFastShare);
	addField(fields, "replaceBottomInfoWithIcons", settings,
		&S::replaceBottomInfoWithIcons, &S::setReplaceBottomInfoWithIcons);
	addField(fields, "deletedMark", settings,
		&S::deletedMark, &S::setDeletedMark);
	addField(fields, "editedMark", settings,
		&S::editedMark, &S::setEditedMark);
	addField(fields, "unlimitedRecentStickers", settings,
		&S::unlimitedRecentStickers, &S::setUnlimitedRecentStickers);
	addField(fields, "showReactionsPanelInContextMenu", settings,
		&S::showReactionsPanelInContextMenu, &S::setShowReactionsPanelInContextMenu, 0, 2);
	addField(fields, "showViewsPanelInContextMenu", settings,
		&S::showViewsPanelInContextMenu, &S::setShowViewsPanelInContextMenu, 0, 2);
	addField(fields, "showUserMessagesInContextMenu", settings,
		&S::showUserMessagesInContextMenu, &S::setShowUserMessagesInContextMenu, 0, 2);
	addField(fields, "showMessageDetailsInContextMenu", settings,
		&S::showMessageDetailsInContextMenu, &S::setShowMessageDetailsInContextMenu, 0, 2);
	addField(fields, "showRepeatMessageInContextMenu", settings,
		&S::showRepeatMessageInContextMenu, &S::setShowRepeatMessageInContextMenu, 0, 2);
	addField(fields, "showAddFilterInContextMenu", settings,
		&S::showAddFilterInContextMenu, &S::setShowAddFilterInContextMenu, 0, 2);
	addField(fields, "showAttachButtonInMessageField", settings,
		&S::showAttachButtonInMessageField, &S::setShowAttachButtonInMessageField);
	addField(fields, "showEmojiButtonInMessageField", settings,
		&S::showEmojiButtonInMessageField, &S::setShowEmojiButtonInMessageField);
	addField(fields, "showMicrophoneButtonInMessageField", settings,
		&S::showMicrophoneButtonInMessageField, &S::setShowMicrophoneButtonInMessageField);
	addField(fields, "showAutoDeleteButtonInMessageField", settings,
		&S::showAutoDeleteButtonInMessageField, &S::setShowAutoDeleteButtonInMessageField);
	addField(fields, "showGiftButtonInMessageField", settings,
		&S::showGiftButtonInMessageField, &S::setShowGiftButtonInMessageField);
	addField(fields, "showAiEditorButtonInMessageField", settings,
		&S::showAiEditorButtonInMessageField, &S::setShowAiEditorButtonInMessageField);
	addField(fields, "showAttachPopup", settings,
		&S::showAttachPopup, &S::setShowAttachPopup);
	addField(fields, "showEmojiPopup", settings,
		&S::showEmojiPopup, &S::setShowEmojiPopup);
	addField(fields, "showMyProfileInDrawer", settings,
		&S::showMyProfileInDrawer, &S::setShowMyProfileInDrawer);
	addField(fields, "showBotsInDrawer", settings,
		&S::showBotsInDrawer, &S::setShowBotsInDrawer);
	addField(fields, "showNewGroupInDrawer", settings,
		&S::showNewGroupInDrawer, &S::setShowNewGroupInDrawer);
	addField(fields, "showNewChannelInDrawer", settings,
		&S::showNewChannelInDrawer, &S::setShowNewChannelInDrawer);
	addField(fields, "showContactsInDrawer", settings,
		&S::showContactsInDrawer, &S::setShowContactsInDrawer);
	addField(fields, "showCallsInDrawer", settings,
		&S::showCallsInDrawer, &S::setShowCallsInDrawer);
	addField(fields, "showSavedMessagesInDrawer", settings,
		&S::showSavedMessagesInDrawer, &S::setShowSavedMessagesInDrawer);
	addField(fields, "showArchiveInDrawer", settings,
		&S::showArchiveInDrawer, &S::setShowArchiveInDrawer);
	addField(fields, "showDonationDetailsInDrawer", settings,
		&S::showDonationDetailsInDrawer, &S::setShowDonationDetailsInDrawer);
	addField(fields, "showNightModeToggleInDrawer", settings,
		&S::showNightModeToggleInDrawer, &S::setShowNightModeToggleInDrawer);
	addField(fields, "showGhostToggleInDrawer", settings,
		&S::showGhostToggleInDrawer, &S::setShowGhostToggleInDrawer);
	addField(fields, "showStreamerToggleInDrawer", settings,
		&S::showStreamerToggleInDrawer, &S::setShowStreamerToggleInDrawer);
	addField(fields, "showGhostToggleInTray", settings,
		&S::showGhostToggleInTray, &S::setShowGhostToggleInTray);
	addField(fields, "showStreamerToggleInTray", settings,
		&S::showStreamerToggleInTray, &S::setShowStreamerToggleInTray);
	addField(fields, "monoFont", settings,
		&S::monoFont, &S::setMonoFont, 0, 0, true);
	addField(fields, "hideNotificationCounters", settings,
		&S::hideNotificationCounters, &S::setHideNotificationCounters);
	addField(fields, "hideNotificationBadge", settings,
		&S::hideNotificationBadge, &S::setHideNotificationBadge);
	addField(fields, "hideAllChatsFolder", settings,
		&S::hideAllChatsFolder, &S::setHideAllChatsFolder);
	addField(fields, "channelBottomButton", settings,
		&S::channelBottomButton, &S::setChannelBottomButton, 0, 2);
	addField(fields, "quickAdminShortcuts", settings,
		&S::quickAdminShortcuts, &S::setQuickAdminShortcuts);
	addField(fields, "disableGreetingSticker", settings,
		&S::disableGreetingSticker, &S::setDisableGreetingSticker);
	addField(fields, "useQuickForwardMenu", settings,
		&S::useQuickForwardMenu, &S::setUseQuickForwardMenu);
	addField(fields, "sendForwardFirst", settings,
		&S::sendForwardFirst, &S::setSendForwardFirst);
	addField(fields, "showPeerId", settings,
		&S::showPeerId, &S::setShowPeerId, 0, 2);
	addField(fields, "showMessageSeconds", settings,
		&S::showMessageSeconds, &S::setShowMessageSeconds);
	addField(fields, "showMessageId", settings,
		&S::showMessageId, &S::setShowMessageId);
	addField(fields, "showMessageShot", settings,
		&S::showMessageShot, &S::setShowMessageShot);
	addField(fields, "filterZalgo", settings,
		&S::filterZalgo, &S::setFilterZalgo, 0, 0, true);
	addField(fields, "autoSpaceSending", settings,
		&S::autoSpaceSending, &S::setAutoSpaceSending);
	addField(fields, "autoSpaceEditing", settings,
		&S::autoSpaceEditing, &S::setAutoSpaceEditing);
	addField(fields, "autoSpaceReceiving", settings,
		&S::autoSpaceReceiving, &S::setAutoSpaceReceiving, 0, 0, true);
	addField(fields, "stickerConfirmation", settings,
		&S::stickerConfirmation, &S::setStickerConfirmation);
	addField(fields, "gifConfirmation", settings,
		&S::gifConfirmation, &S::setGifConfirmation);
	addField(fields, "voiceConfirmation", settings,
		&S::voiceConfirmation, &S::setVoiceConfirmation);
	addField(fields, "roundConfirmation", settings,
		&S::roundConfirmation, &S::setRoundConfirmation);
	addField(fields, "translationProvider", settings,
		&S::translationProvider, &S::setTranslationProvider, 0, 3);
	addField(fields, "adaptiveCoverColor", settings,
		&S::adaptiveCoverColor, &S::setAdaptiveCoverColor);
	addField(fields, "improveLinkPreviews", settings,
		&S::improveLinkPreviews, &S::setImproveLinkPreviews);
	addField(fields, "avatarCorners", settings,
		&S::avatarCorners, &S::setAvatarCorners, 0, 23, true);
	addField(fields, "singleCornerRadius", settings,
		&S::singleCornerRadius, &S::setSingleCornerRadius);
	addField(fields, "streamerMode", settings,
		&S::streamerMode, &S::setStreamerMode);
	return fields;
}

Fields accountFields(not_null<Main::Session*> session) {
	using S = Main::SessionSettings;
	using namespace Data::AutoDownload;
	auto &settings = session->settings();
	auto fields = Fields();
	addField(fields, "archiveCollapsed", settings,
		&S::archiveCollapsed, &S::setArchiveCollapsed);
	addField(fields, "archiveInMainMenu", settings,
		&S::archiveInMainMenu, &S::setArchiveInMainMenu);
	addField(fields, "dialogsFiltersEnabled", settings,
		&S::dialogsFiltersEnabled, &S::setDialogsFiltersEnabled);
	addField(fields, "phoneNumberHidden", settings,
		&S::phoneNumberHidden, &S::setPhoneNumberHidden);
	const auto sources = std::array{
		std::pair{ "user", Source::User },
		std::pair{ "group", Source::Group },
		std::pair{ "channel", Source::Channel },
	};
	const auto types = std::array{
		std::pair{ "photo", Type::Photo },
		std::pair{ "autoPlayVideo", Type::AutoPlayVideo },
		std::pair{ "voiceMessage", Type::VoiceMessage },
		std::pair{ "autoPlayVideoMessage", Type::AutoPlayVideoMessage },
		std::pair{ "music", Type::Music },
		std::pair{ "autoPlayGIF", Type::AutoPlayGIF },
		std::pair{ "file", Type::File },
	};
	for (const auto &[sourceName, source] : sources) {
		for (const auto &[typeName, type] : types) {
			const auto key = std::string("autoDownload.")
				+ sourceName + '.' + typeName;
			fields.emplace(key, Field{
				.value = settings.autoDownload().bytesLimit(source, type),
				.set = [&settings, source, type](const Json &value) {
					settings.autoDownload().setBytesLimit(
						source, type, value.get<int64>());
				},
				.valid = [](const Json &value) {
					return value.is_number_integer()
						&& value.get<double>() >= 0
						&& value.get<double>() <= kMaxBytesLimit;
				},
			});
		}
	}
	return fields;
}

}
