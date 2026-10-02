#ifdef _DEBUG
#include "extras/debug/commands/settings_registry.h"

#include "extras/extras_settings.h"
#include "main/main_session.h"

namespace ExtrasDebug::Commands {
namespace {

void addGhost(SettingsMap &entries, const QString &prefix, GhostModeAccountSettings &settings) {
	addSetting(entries, prefix + u"sendReadMessages"_q, [&] { return settings.sendReadMessages(); }, settings, &GhostModeAccountSettings::setSendReadMessages);
	addSetting(entries, prefix + u"sendReadStories"_q, [&] { return settings.sendReadStories(); }, settings, &GhostModeAccountSettings::setSendReadStories);
	addSetting(entries, prefix + u"sendOnlinePackets"_q, [&] { return settings.sendOnlinePackets(); }, settings, &GhostModeAccountSettings::setSendOnlinePackets);
	addSetting(entries, prefix + u"sendUploadProgress"_q, [&] { return settings.sendUploadProgress(); }, settings, &GhostModeAccountSettings::setSendUploadProgress);
	addSetting(entries, prefix + u"sendOfflinePacketAfterOnline"_q, [&] { return settings.sendOfflinePacketAfterOnline(); }, settings, &GhostModeAccountSettings::setSendOfflinePacketAfterOnline);
	addSetting(entries, prefix + u"markReadAfterAction"_q, [&] { return settings.markReadAfterAction(); }, settings, &GhostModeAccountSettings::setMarkReadAfterAction);
	addSetting(entries, prefix + u"useScheduledMessages"_q, [&] { return settings.useScheduledMessages(); }, settings, &GhostModeAccountSettings::setUseScheduledMessages);
	addSetting(entries, prefix + u"sendWithoutSound"_q, [&] { return settings.sendWithoutSound(); }, settings, &GhostModeAccountSettings::setSendWithoutSound);
	addSetting(entries, prefix + u"suggestGhostModeBeforeViewingStory"_q, [&] { return settings.suggestGhostModeBeforeViewingStory(); }, settings, &GhostModeAccountSettings::setSuggestGhostModeBeforeViewingStory);
	addSetting(entries, prefix + u"sendReadMessagesLocked"_q, [&] { return settings.sendReadMessagesLocked(); }, settings, &GhostModeAccountSettings::setSendReadMessagesLocked);
	addSetting(entries, prefix + u"sendReadStoriesLocked"_q, [&] { return settings.sendReadStoriesLocked(); }, settings, &GhostModeAccountSettings::setSendReadStoriesLocked);
	addSetting(entries, prefix + u"sendOnlinePacketsLocked"_q, [&] { return settings.sendOnlinePacketsLocked(); }, settings, &GhostModeAccountSettings::setSendOnlinePacketsLocked);
	addSetting(entries, prefix + u"sendUploadProgressLocked"_q, [&] { return settings.sendUploadProgressLocked(); }, settings, &GhostModeAccountSettings::setSendUploadProgressLocked);
	addSetting(entries, prefix + u"sendOfflinePacketAfterOnlineLocked"_q, [&] { return settings.sendOfflinePacketAfterOnlineLocked(); }, settings, &GhostModeAccountSettings::setSendOfflinePacketAfterOnlineLocked);
	addSetting(entries, prefix + u"enabled"_q,
		[&] { return settings.isGhostModeActive(); }, settings,
		&GhostModeAccountSettings::setGhostModeEnabled);
}

void addShot(SettingsMap &entries, MessageShotSettings &settings) {
	addSetting(entries, u"messageShotSettings.showBackground"_q, [&] { return settings.showBackground(); }, settings, &MessageShotSettings::setShowBackground);
	addSetting(entries, u"messageShotSettings.showDate"_q, [&] { return settings.showDate(); }, settings, &MessageShotSettings::setShowDate);
	addSetting(entries, u"messageShotSettings.showReactions"_q, [&] { return settings.showReactions(); }, settings, &MessageShotSettings::setShowReactions);
	addSetting(entries, u"messageShotSettings.showHeaderDecorations"_q, [&] { return settings.showHeaderDecorations(); }, settings, &MessageShotSettings::setShowHeaderDecorations);
	addSetting(entries, u"messageShotSettings.showColorfulReplies"_q, [&] { return settings.showColorfulReplies(); }, settings, &MessageShotSettings::setShowColorfulReplies);
	addSetting(entries, u"messageShotSettings.revealSpoilers"_q, [&] { return settings.revealSpoilers(); }, settings, &MessageShotSettings::setRevealSpoilers);
	const auto snapshot = Json(settings);
	for (const auto &[key, value] : snapshot.items()) {
		if (entries.contains(u"messageShotSettings."_q + QString::fromStdString(key))) continue;
		entries.emplace(u"messageShotSettings."_q + QString::fromStdString(key), SettingEntry{
			[&settings, key] { return Json(settings).at(key); },
			[&settings, key](const Json &value) {
				auto updated = Json(settings);
				const auto &before = updated.at(key);
				if (before.is_string() != value.is_string()
					|| (before.is_number_unsigned() && (!value.is_number_integer() || value.get<long double>() < 0))
					|| (before.is_number_integer() && !value.is_number_integer())) {
					return Result::Err(u"message shot theme field type mismatch"_q);
				}
				updated[key] = value;
				if (before.is_number_unsigned()) {
					uint64 parsed = 0;
					if (const auto error = readSetting(value, parsed); !error.isEmpty()) return Result::Err(error);
					if (key == "embeddedThemeAccentColor" && parsed > std::numeric_limits<uint32>::max()) {
						return Result::Err(u"theme accent color out of range"_q);
					}
				}
				if (key.starts_with("embedded")) {
					int type = 0;
					if (const auto error = readSetting(updated.at("embeddedThemeType"), type); !error.isEmpty()) return Result::Err(error);
					if (type < -1 || type > 3) return Result::Err(u"invalid embedded theme index"_q);
					settings.setEmbeddedTheme(type, updated.at("embeddedThemeAccentColor").get<uint32>());
				} else {
					settings.setCloudTheme(
						updated.at("cloudThemeAccountId").get<uint64>(),
						updated.at("cloudThemeId").get<uint64>(),
						updated.at("cloudThemeAccessHash").get<uint64>(),
						updated.at("cloudThemeDocumentId").get<uint64>(),
						updated.at("cloudThemeTitle").get<QString>());
				}
				return Result::Ok();
			},
		});
	}
}

} // namespace

void addExtrasSettings(SettingsMap &entries) {
	auto &settings = ExtrasSettings::getInstance();
	addSetting(entries, u"useGlobalGhostMode"_q, [&] { return settings.useGlobalGhostMode(); }, settings, &ExtrasSettings::setUseGlobalGhostMode);
	addSetting(entries, u"saveDeletedMessages"_q, [&] { return settings.saveDeletedMessages(); }, settings, &ExtrasSettings::setSaveDeletedMessages);
	addSetting(entries, u"saveMessagesHistory"_q, [&] { return settings.saveMessagesHistory(); }, settings, &ExtrasSettings::setSaveMessagesHistory);
	addSetting(entries, u"saveForBots"_q, [&] { return settings.saveForBots(); }, settings, &ExtrasSettings::setSaveForBots);
	addSetting(entries, u"filtersEnabled"_q, [&] { return settings.filtersEnabled(); }, settings, &ExtrasSettings::setFiltersEnabled);
	addSetting(entries, u"filtersEnabledInChats"_q, [&] { return settings.filtersEnabledInChats(); }, settings, &ExtrasSettings::setFiltersEnabledInChats);
	addSetting(entries, u"hideFromBlocked"_q, [&] { return settings.hideFromBlocked(); }, settings, &ExtrasSettings::setHideFromBlocked);
	addSetting(entries, u"semiTransparentDeletedMessages"_q, [&] { return settings.semiTransparentDeletedMessages(); }, settings, &ExtrasSettings::setSemiTransparentDeletedMessages);
	addSetting(entries, u"disableAds"_q, [&] { return settings.disableAds(); }, settings, &ExtrasSettings::setDisableAds);
	addSetting(entries, u"disableStories"_q, [&] { return settings.disableStories(); }, settings, &ExtrasSettings::setDisableStories);
	addSetting(entries, u"disableChatBackground"_q, [&] { return settings.disableChatBackground(); }, settings, &ExtrasSettings::setDisableChatBackground);
	addSetting(entries, u"showBubbleOutline"_q, [&] { return settings.showBubbleOutline(); }, settings, &ExtrasSettings::setShowBubbleOutline);
	addSetting(entries, u"hidePremiumStatuses"_q, [&] { return settings.hidePremiumStatuses(); }, settings, &ExtrasSettings::setHidePremiumStatuses);
	addSetting(entries, u"hideProxySettingsIcon"_q, [&] { return settings.hideProxySettingsIcon(); }, settings, &ExtrasSettings::setHideProxySettingsIcon);
	addSetting(entries, u"showDownloadsButtonInHeader"_q, [&] { return settings.showDownloadsButtonInHeader(); }, settings, &ExtrasSettings::setShowDownloadsButtonInHeader);
	addSetting(entries, u"showFps"_q, [&] { return settings.showFps(); }, settings, &ExtrasSettings::setShowFps);
	addSetting(entries, u"showOnlyAddedEmojisAndStickers"_q, [&] { return settings.showOnlyAddedEmojisAndStickers(); }, settings, &ExtrasSettings::setShowOnlyAddedEmojisAndStickers);
	addSetting(entries, u"collapseSimilarChannels"_q, [&] { return settings.collapseSimilarChannels(); }, settings, &ExtrasSettings::setCollapseSimilarChannels);
	addSetting(entries, u"hideSimilarChannels"_q, [&] { return settings.hideSimilarChannels(); }, settings, &ExtrasSettings::setHideSimilarChannels);
	addSetting(entries, u"messageBubbleRadius"_q, [&] { return settings.messageBubbleRadius(); }, settings, &ExtrasSettings::setMessageBubbleRadius);
	addSetting(entries, u"disableOpenLinkWarning"_q, [&] { return settings.disableOpenLinkWarning(); }, settings, &ExtrasSettings::setDisableOpenLinkWarning);
	addSetting(entries, u"wideMultiplier"_q, [&] { return settings.wideMultiplier(); }, settings, &ExtrasSettings::setWideMultiplier);
	addSetting(entries, u"messageStickerScale"_q, [&] { return settings.messageStickerScale(); }, settings, &ExtrasSettings::setMessageStickerScale);
	addSetting(entries, u"spoofWebviewAsAndroid"_q, [&] { return settings.spoofWebviewAsAndroid(); }, settings, &ExtrasSettings::setSpoofWebviewAsAndroid);
	addSetting(entries, u"increaseWebviewHeight"_q, [&] { return settings.increaseWebviewHeight(); }, settings, &ExtrasSettings::setIncreaseWebviewHeight);
	addSetting(entries, u"increaseWebviewWidth"_q, [&] { return settings.increaseWebviewWidth(); }, settings, &ExtrasSettings::setIncreaseWebviewWidth);
	addSetting(entries, u"windowMaterial"_q, [&] { return settings.windowMaterial(); }, settings, &ExtrasSettings::setWindowMaterial);
	addSetting(entries, u"removeMessageTail"_q, [&] { return settings.removeMessageTail(); }, settings, &ExtrasSettings::setRemoveMessageTail);
	addSetting(entries, u"disableNotificationsDelay"_q, [&] { return settings.disableNotificationsDelay(); }, settings, &ExtrasSettings::setDisableNotificationsDelay);
	addSetting(entries, u"localPremium"_q, [&] { return settings.localPremium(); }, settings, &ExtrasSettings::setLocalPremium);
	addSetting(entries, u"devFeaturesEnabled"_q, [&] { return settings.devFeaturesEnabled(); }, settings, &ExtrasSettings::setDevFeaturesEnabled);
	addSetting(entries, u"showChannelReactions"_q, [&] { return settings.showChannelReactions(); }, settings, &ExtrasSettings::setShowChannelReactions);
	addSetting(entries, u"showGroupReactions"_q, [&] { return settings.showGroupReactions(); }, settings, &ExtrasSettings::setShowGroupReactions);
	addSetting(entries, u"showPrivateChatReactions"_q, [&] { return settings.showPrivateChatReactions(); }, settings, &ExtrasSettings::setShowPrivateChatReactions);
	addSetting(entries, u"appIcon"_q, [&] { return settings.appIcon(); }, settings, &ExtrasSettings::setAppIcon);
	addSetting(entries, u"simpleQuotesAndReplies"_q, [&] { return settings.simpleQuotesAndReplies(); }, settings, &ExtrasSettings::setSimpleQuotesAndReplies);
	addSetting(entries, u"hideFastShare"_q, [&] { return settings.hideFastShare(); }, settings, &ExtrasSettings::setHideFastShare);
	addSetting(entries, u"replaceBottomInfoWithIcons"_q, [&] { return settings.replaceBottomInfoWithIcons(); }, settings, &ExtrasSettings::setReplaceBottomInfoWithIcons);
	addSetting(entries, u"deletedMark"_q, [&] { return settings.deletedMark(); }, settings, &ExtrasSettings::setDeletedMark);
	addSetting(entries, u"editedMark"_q, [&] { return settings.editedMark(); }, settings, &ExtrasSettings::setEditedMark);
	addSetting(entries, u"unlimitedRecentStickers"_q, [&] { return settings.unlimitedRecentStickers(); }, settings, &ExtrasSettings::setUnlimitedRecentStickers);
	addSetting(entries, u"showReactionsPanelInContextMenu"_q, [&] { return settings.showReactionsPanelInContextMenu(); }, settings, &ExtrasSettings::setShowReactionsPanelInContextMenu);
	addSetting(entries, u"showViewsPanelInContextMenu"_q, [&] { return settings.showViewsPanelInContextMenu(); }, settings, &ExtrasSettings::setShowViewsPanelInContextMenu);
	addSetting(entries, u"showHideMessageInContextMenu"_q, [&] { return settings.showHideMessageInContextMenu(); }, settings, &ExtrasSettings::setShowHideMessageInContextMenu);
	addSetting(entries, u"showUserMessagesInContextMenu"_q, [&] { return settings.showUserMessagesInContextMenu(); }, settings, &ExtrasSettings::setShowUserMessagesInContextMenu);
	addSetting(entries, u"showMessageDetailsInContextMenu"_q, [&] { return settings.showMessageDetailsInContextMenu(); }, settings, &ExtrasSettings::setShowMessageDetailsInContextMenu);
	addSetting(entries, u"showRepeatMessageInContextMenu"_q, [&] { return settings.showRepeatMessageInContextMenu(); }, settings, &ExtrasSettings::setShowRepeatMessageInContextMenu);
	addSetting(entries, u"showAddFilterInContextMenu"_q, [&] { return settings.showAddFilterInContextMenu(); }, settings, &ExtrasSettings::setShowAddFilterInContextMenu);
	addSetting(entries, u"showAttachButtonInMessageField"_q, [&] { return settings.showAttachButtonInMessageField(); }, settings, &ExtrasSettings::setShowAttachButtonInMessageField);
	addSetting(entries, u"showEmojiButtonInMessageField"_q, [&] { return settings.showEmojiButtonInMessageField(); }, settings, &ExtrasSettings::setShowEmojiButtonInMessageField);
	addSetting(entries, u"showMicrophoneButtonInMessageField"_q, [&] { return settings.showMicrophoneButtonInMessageField(); }, settings, &ExtrasSettings::setShowMicrophoneButtonInMessageField);
	addSetting(entries, u"showAutoDeleteButtonInMessageField"_q, [&] { return settings.showAutoDeleteButtonInMessageField(); }, settings, &ExtrasSettings::setShowAutoDeleteButtonInMessageField);
	addSetting(entries, u"showGiftButtonInMessageField"_q, [&] { return settings.showGiftButtonInMessageField(); }, settings, &ExtrasSettings::setShowGiftButtonInMessageField);
	addSetting(entries, u"showAiEditorButtonInMessageField"_q, [&] { return settings.showAiEditorButtonInMessageField(); }, settings, &ExtrasSettings::setShowAiEditorButtonInMessageField);
	addSetting(entries, u"showAttachPopup"_q, [&] { return settings.showAttachPopup(); }, settings, &ExtrasSettings::setShowAttachPopup);
	addSetting(entries, u"showEmojiPopup"_q, [&] { return settings.showEmojiPopup(); }, settings, &ExtrasSettings::setShowEmojiPopup);
	addSetting(entries, u"showMyProfileInDrawer"_q, [&] { return settings.showMyProfileInDrawer(); }, settings, &ExtrasSettings::setShowMyProfileInDrawer);
	addSetting(entries, u"showBotsInDrawer"_q, [&] { return settings.showBotsInDrawer(); }, settings, &ExtrasSettings::setShowBotsInDrawer);
	addSetting(entries, u"showNewGroupInDrawer"_q, [&] { return settings.showNewGroupInDrawer(); }, settings, &ExtrasSettings::setShowNewGroupInDrawer);
	addSetting(entries, u"showNewChannelInDrawer"_q, [&] { return settings.showNewChannelInDrawer(); }, settings, &ExtrasSettings::setShowNewChannelInDrawer);
	addSetting(entries, u"showContactsInDrawer"_q, [&] { return settings.showContactsInDrawer(); }, settings, &ExtrasSettings::setShowContactsInDrawer);
	addSetting(entries, u"showCallsInDrawer"_q, [&] { return settings.showCallsInDrawer(); }, settings, &ExtrasSettings::setShowCallsInDrawer);
	addSetting(entries, u"showSavedMessagesInDrawer"_q, [&] { return settings.showSavedMessagesInDrawer(); }, settings, &ExtrasSettings::setShowSavedMessagesInDrawer);
	addSetting(entries, u"showArchiveInDrawer"_q, [&] { return settings.showArchiveInDrawer(); }, settings, &ExtrasSettings::setShowArchiveInDrawer);
	addSetting(entries, u"showDonationDetailsInDrawer"_q, [&] { return settings.showDonationDetailsInDrawer(); }, settings, &ExtrasSettings::setShowDonationDetailsInDrawer);
	addSetting(entries, u"showNightModeToggleInDrawer"_q, [&] { return settings.showNightModeToggleInDrawer(); }, settings, &ExtrasSettings::setShowNightModeToggleInDrawer);
	addSetting(entries, u"showGhostToggleInDrawer"_q, [&] { return settings.showGhostToggleInDrawer(); }, settings, &ExtrasSettings::setShowGhostToggleInDrawer);
	addSetting(entries, u"showStreamerToggleInDrawer"_q, [&] { return settings.showStreamerToggleInDrawer(); }, settings, &ExtrasSettings::setShowStreamerToggleInDrawer);
	addSetting(entries, u"showGhostToggleInTray"_q, [&] { return settings.showGhostToggleInTray(); }, settings, &ExtrasSettings::setShowGhostToggleInTray);
	addSetting(entries, u"showStreamerToggleInTray"_q, [&] { return settings.showStreamerToggleInTray(); }, settings, &ExtrasSettings::setShowStreamerToggleInTray);
	addSetting(entries, u"monoFont"_q, [&] { return settings.monoFont(); }, settings, &ExtrasSettings::setMonoFont);
	addSetting(entries, u"hideNotificationCounters"_q, [&] { return settings.hideNotificationCounters(); }, settings, &ExtrasSettings::setHideNotificationCounters);
	addSetting(entries, u"hideNotificationBadge"_q, [&] { return settings.hideNotificationBadge(); }, settings, &ExtrasSettings::setHideNotificationBadge);
	addSetting(entries, u"hideAllChatsFolder"_q, [&] { return settings.hideAllChatsFolder(); }, settings, &ExtrasSettings::setHideAllChatsFolder);
	addSetting(entries, u"channelBottomButton"_q, [&] { return settings.channelBottomButton(); }, settings, &ExtrasSettings::setChannelBottomButton);
	addSetting(entries, u"quickAdminShortcuts"_q, [&] { return settings.quickAdminShortcuts(); }, settings, &ExtrasSettings::setQuickAdminShortcuts);
	addSetting(entries, u"disableGreetingSticker"_q, [&] { return settings.disableGreetingSticker(); }, settings, &ExtrasSettings::setDisableGreetingSticker);
	addSetting(entries, u"useQuickForwardMenu"_q, [&] { return settings.useQuickForwardMenu(); }, settings, &ExtrasSettings::setUseQuickForwardMenu);
	addSetting(entries, u"sendForwardFirst"_q, [&] { return settings.sendForwardFirst(); }, settings, &ExtrasSettings::setSendForwardFirst);
	addSetting(entries, u"showPeerId"_q, [&] { return settings.showPeerId(); }, settings, &ExtrasSettings::setShowPeerId);
	addSetting(entries, u"showMessageSeconds"_q, [&] { return settings.showMessageSeconds(); }, settings, &ExtrasSettings::setShowMessageSeconds);
	addSetting(entries, u"showMessageId"_q, [&] { return settings.showMessageId(); }, settings, &ExtrasSettings::setShowMessageId);
	addSetting(entries, u"showMessageShot"_q, [&] { return settings.showMessageShot(); }, settings, &ExtrasSettings::setShowMessageShot);
	addSetting(entries, u"filterZalgo"_q, [&] { return settings.filterZalgo(); }, settings, &ExtrasSettings::setFilterZalgo);
	addSetting(entries, u"autoSpaceSending"_q, [&] { return settings.autoSpaceSending(); }, settings, &ExtrasSettings::setAutoSpaceSending);
	addSetting(entries, u"autoSpaceEditing"_q, [&] { return settings.autoSpaceEditing(); }, settings, &ExtrasSettings::setAutoSpaceEditing);
	addSetting(entries, u"autoSpaceReceiving"_q, [&] { return settings.autoSpaceReceiving(); }, settings, &ExtrasSettings::setAutoSpaceReceiving);
	addSetting(entries, u"stickerConfirmation"_q, [&] { return settings.stickerConfirmation(); }, settings, &ExtrasSettings::setStickerConfirmation);
	addSetting(entries, u"gifConfirmation"_q, [&] { return settings.gifConfirmation(); }, settings, &ExtrasSettings::setGifConfirmation);
	addSetting(entries, u"voiceConfirmation"_q, [&] { return settings.voiceConfirmation(); }, settings, &ExtrasSettings::setVoiceConfirmation);
	addSetting(entries, u"roundConfirmation"_q, [&] { return settings.roundConfirmation(); }, settings, &ExtrasSettings::setRoundConfirmation);
	addSetting(entries, u"translationProvider"_q, [&] { return settings.translationProvider(); }, settings, &ExtrasSettings::setTranslationProvider);
	addSetting(entries, u"adaptiveCoverColor"_q, [&] { return settings.adaptiveCoverColor(); }, settings, &ExtrasSettings::setAdaptiveCoverColor);
	addSetting(entries, u"improveLinkPreviews"_q, [&] { return settings.improveLinkPreviews(); }, settings, &ExtrasSettings::setImproveLinkPreviews);
	addSetting(entries, u"avatarCorners"_q, [&] { return settings.avatarCorners(); }, settings, &ExtrasSettings::setAvatarCorners);
	addSetting(entries, u"singleCornerRadius"_q, [&] { return settings.singleCornerRadius(); }, settings, &ExtrasSettings::setSingleCornerRadius);
	addSetting(entries, u"streamerMode"_q, [&] { return settings.streamerMode(); }, settings, &ExtrasSettings::setStreamerMode);
	entries.emplace(u"shadowBanIds"_q, SettingEntry{
		[&] { return Json(settings.shadowBanIds()); },
		[&](const Json &value) {
			if (!value.is_array()) return Result::Err(u"expected an account id array"_q);
			std::unordered_set<int64> next;
			for (const auto &element : value) {
				int64 id = 0;
				if (const auto error = readSetting(element, id); !error.isEmpty()) return Result::Err(error);
				next.insert(id);
			}
			const auto previous = settings.shadowBanIds();
			for (const auto id : previous) if (!next.contains(id)) settings.removeShadowBan(id);
			for (const auto id : next) settings.addShadowBan(id);
			return Result::Ok();
		},
	});
	addShot(entries, settings.messageShotSettings());
	addGhost(entries, u"ghost."_q, ExtrasSettings::ghost());
	const auto all = Json(settings);
	for (const auto &[key, value] : all.at("ghostModeSettings").items()) {
		addGhost(entries, u"ghostModeSettings."_q + QString::fromStdString(key) + u"."_q,
			ExtrasSettings::ghostForDebug(QString::fromStdString(key).toULongLong()));
	}
	if (const auto session = ActiveSession()) {
		const auto id = session->userId().bare;
		addGhost(entries, u"ghostModeSettings.%1."_q.arg(id), ExtrasSettings::ghostForDebug(id));
	}
}

} // namespace ExtrasDebug::Commands
#endif
