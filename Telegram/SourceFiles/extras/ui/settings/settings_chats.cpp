#include "extras/ui/settings/settings_chats.h"

#include "lang_auto.h"
#include "extras/extras_settings.h"
#include "extras/ui/boxes/edit_mark_box.h"
#include "extras/ui/components/message_preview.h"
#include "extras/ui/components/sticker_preview.h"
#include "extras/ui/settings/extras_builder.h"
#include "extras/ui/settings/settings_extras_utils.h"
#include "extras/ui/settings/settings_main.h"
#include "chat_helpers/emoji_sets_manager.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_extras_icons.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <memory>

namespace Settings {

using namespace Builder;
using namespace ExtrasBuilder;

namespace {

struct PreviewState {
	MessagePreview *widget = nullptr;
};

void BuildStickersAndEmoji(SectionBuilder &builder, ExtrasSectionBuilder &extras) {
	builder.addSubsectionTitle(tr::lng_settings_stickers_emoji());
	const auto controller = builder.controller();
	builder.addButton({
		.id = u"extras/emojiPacks"_q,
		.title = tr::extras_EmojiPacks(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			controller->show(Box<Ui::Emoji::ManageSetsBox>(&controller->session()));
		},
		.keywords = tr::extras_EmojiPackSearchKeywords(tr::now).split(' '),
	});
	builder.addDividerText(tr::extras_EmojiPackDescription());

	extras.addSettingToggle({
		.id = u"extras/showOnlyAddedEmojisAndStickers"_q,
		.title = tr::extras_ShowOnlyAddedEmojisAndStickers(),
		.getter = &ExtrasSettings::showOnlyAddedEmojisAndStickers,
		.setter = &ExtrasSettings::setShowOnlyAddedEmojisAndStickers,
	});

	extras.addSettingToggle({
		.id = u"extras/unlimitedRecentStickers"_q,
		.altIds = { u"extras/recentStickersCount"_q },
		.title = tr::extras_SettingsUnlimitedRecentStickers(),
		.getter = &ExtrasSettings::unlimitedRecentStickers,
		.setter = &ExtrasSettings::setUnlimitedRecentStickers,
	});

	extras.addCollapsibleToggle({
		.id = u"extras/hideReactions"_q,
		.title = tr::extras_HideReactions(),
		.checkboxes = {
			NestedEntry{
				tr::extras_HideReactionsInChannels(tr::now),
				[] { return !ExtrasSettings::getInstance().showChannelReactions(); },
				[](bool v) { ExtrasSettings::getInstance().setShowChannelReactions(!v); }
			},
			NestedEntry{
				tr::extras_HideReactionsInGroups(tr::now),
				[] { return !ExtrasSettings::getInstance().showGroupReactions(); },
				[](bool v) { ExtrasSettings::getInstance().setShowGroupReactions(!v); }
			},
			NestedEntry{
				tr::extras_HideReactionsInPrivateChats(tr::now),
				[] { return !ExtrasSettings::getInstance().showPrivateChatReactions(); },
				[](bool v) { ExtrasSettings::getInstance().setShowPrivateChatReactions(!v); }
			}
		},
		.toggledWhenAll = false,
	});

	extras.addSectionDivider();
}

void buildMessageStickers(SectionBuilder &builder, ExtrasSectionBuilder &extras) {
	auto *settings = &ExtrasSettings::getInstance();

	extras.addSettingToggle({
		.id = u"extras/stickerTimestampOnHover"_q,
		.title = tr::extras_StickerTimestampOnHover(),
		.getter = &ExtrasSettings::stickerTimestampOnHover,
		.setter = &ExtrasSettings::setStickerTimestampOnHover,
	});

	constexpr auto kMessageStickerMinScale = 0.5;
	constexpr auto kMessageStickerScaleStep = 0.1;

	const auto scaleToIndex = [](double value, double min, double step) {
		return static_cast<int>(std::round((value - min) / step));
	};

	extras.addSlider({
		.id = u"extras/messageStickerScale"_q,
		.title = tr::extras_StickerSizeScale(),
		.steps = 12,
		.current = scaleToIndex(
			settings->messageStickerScale(),
			kMessageStickerMinScale,
			kMessageStickerScaleStep),
		.indexToValue = [](int index) { return index; },
		.onChanged = [=](int index) {
			ExtrasSettings::getInstance().setMessageStickerScale(
				kMessageStickerMinScale + index * kMessageStickerScaleStep);
		},
		.onFinalChanged = [=](int index) {
			ExtrasSettings::getInstance().setMessageStickerScale(
				kMessageStickerMinScale + index * kMessageStickerScaleStep);
		},
		.formatLabel = [=](int index) {
			return QString::number(
				kMessageStickerMinScale + index * kMessageStickerScaleStep,
				'f',
				1) + 'x';
		},
	});

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<StickerPreview>(ctx.container),
		};
	});

	extras.addSectionDivider();
}

void BuildGroupsAndChannels(SectionBuilder &builder, ExtrasSectionBuilder &extras) {
	auto *settings = &ExtrasSettings::getInstance();

	builder.addSubsectionTitle(tr::extras_CategoryChats());

	extras.addChooseButton({
		.id = u"extras/channelBottomButton"_q,
		.altIds = { u"extras/bottomButton"_q },
		.title = tr::extras_ChannelBottomButton(),
		.boxTitle = tr::extras_ChannelBottomButton(),
		.initialSelection = static_cast<int>(settings->channelBottomButton()),
		.options = {
			tr::extras_ChannelBottomButtonHide(tr::now),
			tr::extras_ChannelBottomButtonMute(tr::now),
			tr::extras_ChannelBottomButtonDiscuss(tr::now),
		},
		.setter = [](int index) {
			ExtrasSettings::getInstance().setChannelBottomButton(
				static_cast<ChannelBottomButton>(index));
		},
	});

	extras.addSettingToggle({
		.id = u"extras/quickAdminShortcuts"_q,
		.title = tr::extras_QuickAdminShortcuts(),
		.getter = &ExtrasSettings::quickAdminShortcuts,
		.setter = &ExtrasSettings::setQuickAdminShortcuts,
	});
	extras.addSettingToggle({
		.id = u"extras/disableGreetingSticker"_q,
		.title = tr::extras_DisableGreetingSticker(),
		.getter = &ExtrasSettings::disableGreetingSticker,
		.setter = &ExtrasSettings::setDisableGreetingSticker,
	});
	extras.addSettingToggle({
		.id = u"extras/useQuickForwardMenu"_q,
		.title = tr::extras_UseQuickForwardMenu(),
		.getter = &ExtrasSettings::useQuickForwardMenu,
		.setter = &ExtrasSettings::setUseQuickForwardMenu,
	});
	extras.addSettingToggle({
		.id = u"extras/sendForwardFirst"_q,
		.title = tr::extras_SendForwardFirst(),
		.getter = &ExtrasSettings::sendForwardFirst,
		.setter = &ExtrasSettings::setSendForwardFirst,
	});
	extras.addSettingToggle({
		.id = u"extras/showMessageShot"_q,
		.title = tr::extras_SettingsShowMessageShot(),
		.getter = &ExtrasSettings::showMessageShot,
		.setter = &ExtrasSettings::setShowMessageShot,
	});

	builder.addSkip();
	builder.addDividerText(tr::extras_SettingsShowMessageShotDescription());
	builder.addSkip();
}

void BuildMarks(
		SectionBuilder &builder,
		ExtrasSectionBuilder &extras,
		std::shared_ptr<PreviewState> previewState) {
	auto *settings = &ExtrasSettings::getInstance();
	const auto controller = builder.controller();

	builder.addSubsectionTitle(tr::lng_settings_messages());

	builder.add([=](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		auto preview = object_ptr<MessagePreview>(ctx.container, controller);
		previewState->widget = preview.data();
		return {
			.widget = std::move(preview),
		};
	});

	extras.addSettingToggle({
		.id = u"extras/replaceBottomInfoWithIcons"_q,
		.altIds = { u"extras/replaceEditedWithIcon"_q },
		.title = tr::extras_ReplaceMarksWithIcons(),
		.getter = &ExtrasSettings::replaceBottomInfoWithIcons,
		.setter = &ExtrasSettings::setReplaceBottomInfoWithIcons,
	});

	builder.scope([&] {
		builder.addButton({
			.id = u"extras/deletedMark"_q,
			.title = tr::extras_DeletedMarkText(),
			.st = &st::settingsButtonNoIcon,
			.label = ExtrasSettings::getInstance().deletedMarkValue(),
			.onClick = [=] {
				auto box = Box<EditMarkBox>(
					tr::extras_DeletedMarkText(),
					settings->deletedMark(),
					QString("🧹"),
					[=](const QString &value) {
						ExtrasSettings::getInstance().setDeletedMark(value);
					});
				Ui::show(std::move(box));
			},
		});

		builder.addButton({
			.id = u"extras/editedMark"_q,
			.title = tr::extras_EditedMarkText(),
			.st = &st::settingsButtonNoIcon,
			.label = ExtrasSettings::getInstance().editedMarkValue(),
			.onClick = [=] {
				auto box = Box<EditMarkBox>(
					tr::extras_EditedMarkText(),
					settings->editedMark(),
					tr::lng_edited(tr::now),
					[=](const QString &value) {
						ExtrasSettings::getInstance().setEditedMark(value);
					});
				Ui::show(std::move(box));
			},
		});
	}, ExtrasSettings::getInstance().replaceBottomInfoWithIconsValue()
		| rpl::map([](bool v) { return !v; }));

	extras.addSettingToggle({
		.id = u"extras/removeMessageTail"_q,
		.title = tr::extras_RemoveMessageTail(),
		.getter = &ExtrasSettings::removeMessageTail,
		.setter = &ExtrasSettings::setRemoveMessageTail,
	});

	extras.addSettingToggle({
		.id = u"extras/hideFastShare"_q,
		.altIds = { u"extras/hideShareButton"_q },
		.title = tr::extras_HideShareButton(),
		.getter = &ExtrasSettings::hideFastShare,
		.setter = &ExtrasSettings::setHideFastShare,
	});
	extras.addSettingToggle({
		.id = u"extras/simpleQuotesAndReplies"_q,
		.altIds = { u"extras/disableColorfulReplies"_q, u"extras/replyElements"_q },
		.title = tr::extras_SimpleQuotesAndReplies(),
		.getter = &ExtrasSettings::simpleQuotesAndReplies,
		.setter = &ExtrasSettings::setSimpleQuotesAndReplies,
	});

	extras.addSettingToggle({
		.id = u"extras/semiTransparentDeletedMessages"_q,
		.altIds = { u"extras/translucentDeletedMessages"_q },
		.title = tr::extras_SemiTransparentDeletedMessages(),
		.getter = &ExtrasSettings::semiTransparentDeletedMessages,
		.setter = &ExtrasSettings::setSemiTransparentDeletedMessages,
	});

	extras.addSectionDivider();
}

void BuildWideMessagesMultiplier(
		SectionBuilder &builder,
		ExtrasSectionBuilder &extras,
		std::shared_ptr<PreviewState> previewState) {
	auto *settings = &ExtrasSettings::getInstance();

	constexpr auto kMinSize = 1.00;
	constexpr auto kStep = 0.05;

	const auto valueToIndex = [=](double value) {
		return static_cast<int>(std::round((value - kMinSize) / kStep));
	};

	const auto controller = builder.controller();
	extras.addSlider({
		.id = u"extras/messageBubbleRadius"_q,
		.title = tr::extras_MessageBubbleRadius(),
		.steps = 17,
		.current = settings->messageBubbleRadius(),
		.indexToValue = [](int index) { return index; },
		.onChanged = [=](int index) {
			if (previewState->widget) {
				previewState->widget->setBubbleRadius(index);
			}
		},
		.onFinalChanged = [=](int index) {
			if (previewState->widget) {
				previewState->widget->setBubbleRadius(index);
			}
			ExtrasSettings::getInstance().setMessageBubbleRadius(index);
			ShowRestartPrompt(controller);
		},
		.formatLabel = [](int index) {
			return QString::number(index);
		},
	});

	extras.addSectionDivider();

	extras.addSlider({
		.id = u"extras/wideMultiplier"_q,
		.title = tr::extras_SettingsWideMultiplier(),
		.steps = 61, // (4.00 - 1.00) / 0.05 + 1
		.current = valueToIndex(settings->wideMultiplier()),
		.indexToValue = [](int index) { return index; },
		.onChanged = nullptr,
		.onFinalChanged = [=](int index) {
			ExtrasSettings::getInstance().setWideMultiplier(
				kMinSize + index * kStep);
			ShowRestartPrompt(controller);
		},
		.formatLabel = [=](int index) {
			return QString::number(kMinSize + index * kStep, 'f', 2);
		},
	});

	builder.addSkip();
	builder.addDividerText(tr::extras_SettingsWideMultiplierDescription());
	builder.addSkip();
}

void BuildContextMenuElements(SectionBuilder &builder, ExtrasSectionBuilder &extras) {
	auto *settings = &ExtrasSettings::getInstance();

	builder.addSubsectionTitle(tr::extras_ContextMenuElementsHeader());

	const auto options = std::vector{
		tr::extras_SettingsContextMenuItemHidden(tr::now),
		tr::extras_SettingsContextMenuItemShown(tr::now),
		tr::extras_SettingsContextMenuItemExtended(tr::now),
	};

	extras.addChooseButton({
		.id = u"extras/showReactionsPanelInContextMenu"_q,
		.title = tr::extras_SettingsContextMenuReactionsPanel(),
		.boxTitle = tr::extras_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showReactionsPanelInContextMenu()),
		.options = options,
		.setter = [](int i) { ExtrasSettings::getInstance().setShowReactionsPanelInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::menuIconReactions },
	});
	extras.addChooseButton({
		.id = u"extras/showViewsPanelInContextMenu"_q,
		.title = tr::extras_SettingsContextMenuViewsPanel(),
		.boxTitle = tr::extras_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showViewsPanelInContextMenu()),
		.options = options,
		.setter = [](int i) { ExtrasSettings::getInstance().setShowViewsPanelInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::menuIconShowInChat },
	});
	extras.addChooseButton({
		.id = u"extras/showUserMessagesInContextMenu"_q,
		.title = tr::extras_UserMessagesMenuText(),
		.boxTitle = tr::extras_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showUserMessagesInContextMenu()),
		.options = options,
		.setter = [](int i) { ExtrasSettings::getInstance().setShowUserMessagesInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::menuIconTTL },
	});
	extras.addChooseButton({
		.id = u"extras/showMessageDetailsInContextMenu"_q,
		.title = tr::extras_MessageDetailsPC(),
		.boxTitle = tr::extras_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showMessageDetailsInContextMenu()),
		.options = options,
		.setter = [](int i) { ExtrasSettings::getInstance().setShowMessageDetailsInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::menuIconInfo },
	});
	extras.addChooseButton({
		.id = u"extras/showRepeatMessageInContextMenu"_q,
		.title = tr::extras_RepeatMessage(),
		.boxTitle = tr::extras_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showRepeatMessageInContextMenu()),
		.options = options,
		.setter = [](int i) { ExtrasSettings::getInstance().setShowRepeatMessageInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::extrasRepeatMenuIcon },
	});
	if (settings->filtersEnabled()) {
		extras.addChooseButton({
			.id = u"extras/showAddFilterInContextMenu"_q,
			.title = tr::extras_RegexFilterQuickAdd(),
			.boxTitle = tr::extras_SettingsContextMenuTitle(),
			.initialSelection = static_cast<int>(settings->showAddFilterInContextMenu()),
			.options = options,
			.setter = [](int i) { ExtrasSettings::getInstance().setShowAddFilterInContextMenu(static_cast<ContextMenuVisibility>(i)); },
			.icon = { &st::menuIconAddToFolder },
		});
	}

	builder.addSkip();
	builder.addDividerText(tr::extras_SettingsContextMenuDescription());
	builder.addSkip();
}

void BuildMessageFieldElements(SectionBuilder &builder, ExtrasSectionBuilder &extras) {
	builder.addSubsectionTitle(tr::extras_MessageFieldElementsHeader());

	extras.addSettingToggle({
		.id = u"extras/showSendAsButtonInMessageField"_q,
		.title = tr::extras_MessageFieldElementSendAs(),
		.getter = &ExtrasSettings::showSendAsButtonInMessageField,
		.setter = &ExtrasSettings::setShowSendAsButtonInMessageField,
		.icon = { &st::menuIconChannel },
	});
	builder.addDividerText(tr::extras_MessageFieldElementSendAsDescription());

	extras.addSettingToggle({
		.id = u"extras/showAttachButtonInMessageField"_q,
		.title = tr::extras_MessageFieldElementAttach(),
		.getter = &ExtrasSettings::showAttachButtonInMessageField,
		.setter = &ExtrasSettings::setShowAttachButtonInMessageField,
		.icon = { &st::messageFieldAttachIcon },
	});
	extras.addSettingToggle({
		.id = u"extras/showAutoDeleteButtonInMessageField"_q,
		.title = tr::extras_MessageFieldElementTTL(),
		.getter = &ExtrasSettings::showAutoDeleteButtonInMessageField,
		.setter = &ExtrasSettings::setShowAutoDeleteButtonInMessageField,
		.icon = { &st::messageFieldTTLIcon },
	});
	extras.addSettingToggle({
		.id = u"extras/showEmojiButtonInMessageField"_q,
		.title = tr::extras_MessageFieldElementEmoji(),
		.getter = &ExtrasSettings::showEmojiButtonInMessageField,
		.setter = &ExtrasSettings::setShowEmojiButtonInMessageField,
		.icon = { &st::messageFieldEmojiIcon },
	});
	extras.addSettingToggle({
		.id = u"extras/showGiftButtonInMessageField"_q,
		.title = tr::lng_profile_action_short_gift(),
		.getter = &ExtrasSettings::showGiftButtonInMessageField,
		.setter = &ExtrasSettings::setShowGiftButtonInMessageField,
		.icon = { &st::messageFieldGiftIcon },
	});
	extras.addSettingToggle({
		.id = u"extras/showAiEditorButtonInMessageField"_q,
		.title = tr::lng_ai_compose_title(),
		.getter = &ExtrasSettings::showAiEditorButtonInMessageField,
		.setter = &ExtrasSettings::setShowAiEditorButtonInMessageField,
		.icon = { &st::messageFieldCocoonAiIcon },
	});

	extras.addSectionDivider();
}

void BuildAttachMenuElements(SectionBuilder &builder, ExtrasSectionBuilder &extras) {
	builder.addSubsectionTitle(tr::extras_AttachMenuElementsHeader());

	extras.addSettingToggle({
		.id = u"extras/showPhotoInAttachMenu"_q,
		.title = tr::lng_attach_photo_or_video(),
		.getter = &ExtrasSettings::showPhotoInAttachMenu,
		.setter = &ExtrasSettings::setShowPhotoInAttachMenu,
		.icon = { &st::menuIconPhoto },
	});
	extras.addSettingToggle({
		.id = u"extras/showFileInAttachMenu"_q,
		.title = tr::lng_attach_document(),
		.getter = &ExtrasSettings::showFileInAttachMenu,
		.setter = &ExtrasSettings::setShowFileInAttachMenu,
		.icon = { &st::menuIconFile },
	});
	extras.addSettingToggle({
		.id = u"extras/showPollInAttachMenu"_q,
		.title = tr::lng_polls_menu_item(),
		.getter = &ExtrasSettings::showPollInAttachMenu,
		.setter = &ExtrasSettings::setShowPollInAttachMenu,
		.icon = { &st::menuIconCreatePoll },
	});
	extras.addSettingToggle({
		.id = u"extras/showTodoListInAttachMenu"_q,
		.title = tr::lng_todo_menu_item(),
		.getter = &ExtrasSettings::showTodoListInAttachMenu,
		.setter = &ExtrasSettings::setShowTodoListInAttachMenu,
		.icon = { &st::menuIconCreateTodoList },
	});
	extras.addSettingToggle({
		.id = u"extras/showArticleInAttachMenu"_q,
		.title = tr::lng_article_menu_item(),
		.getter = &ExtrasSettings::showArticleInAttachMenu,
		.setter = &ExtrasSettings::setShowArticleInAttachMenu,
		.icon = { &st::menuIconArticle },
	});
	extras.addSettingToggle({
		.id = u"extras/showLocationInAttachMenu"_q,
		.title = tr::lng_maps_point(),
		.getter = &ExtrasSettings::showLocationInAttachMenu,
		.setter = &ExtrasSettings::setShowLocationInAttachMenu,
		.icon = { &st::menuIconAddress },
	});
	extras.addSettingToggle({
		.id = u"extras/showMusicInAttachMenu"_q,
		.title = tr::lng_all_music(),
		.getter = &ExtrasSettings::showMusicInAttachMenu,
		.setter = &ExtrasSettings::setShowMusicInAttachMenu,
		.icon = { &st::menuIconSoundOn },
	});
	extras.addSettingToggle({
		.id = u"extras/showRecordMessageInAttachMenu"_q,
		.title = tr::extras_RecordMessage(),
		.getter = &ExtrasSettings::showRecordMessageInAttachMenu,
		.setter = &ExtrasSettings::setShowRecordMessageInAttachMenu,
		.icon = { &st::extrasRecordMessageIcon },
	});

	builder.addSkip();
	builder.addDividerText(tr::extras_AttachMenuElementsDescription());
	builder.addSkip();
}

void BuildMessageFieldPopups(SectionBuilder &builder, ExtrasSectionBuilder &extras) {
	builder.addSubsectionTitle(tr::extras_MessageFieldPopupsHeader());

	extras.addSettingToggle({
		.id = u"extras/showAttachPopup"_q,
		.title = tr::extras_MessageFieldElementAttach(),
		.getter = &ExtrasSettings::showAttachPopup,
		.setter = &ExtrasSettings::setShowAttachPopup,
		.icon = { &st::messageFieldAttachIcon },
	});
	extras.addSettingToggle({
		.id = u"extras/showEmojiPopup"_q,
		.title = tr::extras_MessageFieldElementEmoji(),
		.getter = &ExtrasSettings::showEmojiPopup,
		.setter = &ExtrasSettings::setShowEmojiPopup,
		.icon = { &st::messageFieldEmojiIcon },
	});
}

const auto kMeta = BuildHelper({
	.id = ExtrasChats::Id(),
	.parentId = ExtrasMain::Id(),
	.title = &tr::extras_CategoryChats,
	.icon = &st::menuIconChatBubble,
}, [](SectionBuilder &builder) {
	auto extras = ExtrasSectionBuilder(builder);
	const auto previewState = std::make_shared<PreviewState>();

	builder.addSkip();
	BuildStickersAndEmoji(builder, extras);
	buildMessageStickers(builder, extras);
	BuildGroupsAndChannels(builder, extras);
	BuildMarks(builder, extras, previewState);
	BuildWideMessagesMultiplier(builder, extras, previewState);
	BuildContextMenuElements(builder, extras);
	BuildMessageFieldElements(builder, extras);
	BuildAttachMenuElements(builder, extras);
	BuildMessageFieldPopups(builder, extras);
	builder.addSkip();
});

} // namespace

rpl::producer<QString> ExtrasChats::title() {
	return tr::extras_CategoryChats();
}

ExtrasChats::ExtrasChats(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void ExtrasChats::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type ExtrasChatsId() {
	return ExtrasChats::Id();
}

} // namespace Settings
