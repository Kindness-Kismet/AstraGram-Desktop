/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_widget.h"

#include "api/api_compose_with_ai.h"
#include "api/api_editing.h"
#include "api/api_bot.h"
#include "api/api_chat_participants.h"
#include "api/api_global_privacy.h"
#include "api/api_report.h"
#include "api/api_sending.h"
#include "api/api_send_progress.h"
#include "api/api_unread_things.h"
#include "base/random.h"
#include "boxes/compose_ai_box.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/delete_messages_box.h"
#include "boxes/send_credits_box.h"
#include "boxes/send_gif_with_caption_box.h"
#include "boxes/send_files_box.h"
#include "boxes/share_box.h"
#include "boxes/edit_caption_box.h"
#include "boxes/moderate_messages_box.h"
#include "boxes/premium_limits_box.h"
#include "boxes/premium_preview_box.h"
#include "boxes/star_gift_box.h"
#include "boxes/peers/edit_peer_permissions_box.h" // ShowAboutGigagroup.
#include "boxes/peers/edit_peer_requests_box.h"
#include "core/core_settings.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "history/view/history_view_draw_to_reply.h"
#include "history/view/controls/history_view_rich_draft_preview.h"
#include "ui/emoji_config.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/chat/choose_theme_controller.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/inner_dropdown.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/labels.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/message_sending_animation_controller.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/chat/message_bar.h"
#include "ui/chat/attach/attach_send_files_way.h"
#include "ui/chat/choose_send_as.h"
#include "ui/effects/spoiler_mess.h"
#include "ui/image/image.h"
#include "ui/painter.h"
#include "ui/chat/floating_bar.h"
#include "ui/rect.h"
#include "ui/power_saving.h"
#include "ui/controls/compose_ai_button_factory.h"
#include "ui/controls/emoji_button.h"
#include "ui/controls/send_button.h"
#include "ui/controls/send_as_button.h"
#include "ui/controls/silent_toggle.h"
#include "ui/screen_reader_mode.h"
#include "ui/ui_utility.h"
#include "inline_bots/inline_bot_result.h"
#include "base/event_filter.h"
#include "base/options.h"
#include "base/qt_signal_producer.h"
#include "base/qt/qt_key_modifiers.h"
#include "base/unixtime.h"
#include "base/call_delayed.h"
#include "data/business/data_shortcut_messages.h"
#include "data/components/credits.h"
#include "data/components/ephemeral_messages.h"
#include "data/components/recent_inline_bots.h"
#include "data/components/scheduled_messages.h"
#include "data/components/sponsored_messages.h"
#include "data/notify/data_notify_settings.h"
#include "data/data_ai_compose_tones.h"
#include "data/data_changes.h"
#include "data/data_drafts.h"
#include "data/data_session.h"
#include "data/data_todo_list.h"
#include "data/data_web_page.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_poll.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_user.h"
#include "data/data_chat_filters.h"
#include "data/data_file_origin.h"
#include "data/data_histories.h"
#include "data/data_group_call.h"
#include "data/data_message_reactions.h"
#include "data/data_peer_values.h" // Data::AmPremiumValue.
#include "data/data_premium_limits.h" // Data::PremiumLimits.
#include "data/stickers/data_stickers.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h" // GetErrorForSending.
#include "history/history_drag_area.h"
#include "history/history_inner_widget.h"
#include "history/history_item_components.h"
#include "history/history_streamed_drafts.h"
#include "history/history_unread_things.h"
#include "history/history_view_pull_to_next_channel.h"
#include "history/admin_log/history_admin_log_section.h"
#include "history/view/controls/compose_controls_common.h"
#include "history/view/controls/history_view_characters_limit.h"
#include "history/view/controls/history_view_compose_ai_button.h"
#include "history/view/controls/history_view_compose_ai_tooltip.h"
#include "history/view/controls/history_view_compose_search.h"
#include "history/view/controls/history_view_forward_panel.h"
#include "history/view/controls/history_view_draft_options.h"
#include "history/view/controls/history_view_suggest_options.h"
#include "history/view/controls/history_view_ttl_button.h"
#include "history/view/controls/history_view_voice_record_bar.h"
#include "history/view/controls/history_view_webpage_processor.h"
#include "history/view/reactions/history_view_reactions_button.h"
#include "history/view/history_view_chat_section.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_service_message.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_scheduled_section.h"
#include "history/view/history_view_schedule_box.h"
#include "history/view/history_view_top_bar_widget.h"
#include "history/view/history_view_contact_status.h"
#include "history/view/history_view_paid_reaction_toast.h"
#include "history/view/history_view_pinned_tracker.h"
#include "history/view/history_view_pinned_section.h"
#include "history/view/history_view_pinned_bar.h"
#include "history/view/history_view_group_call_bar.h"
#include "history/view/history_view_group_members_widget.h"
#include "history/view/history_view_item_preview.h"
#include "history/view/history_view_reply.h"
#include "history/view/history_view_requests_bar.h"
#include "history/view/history_view_self_forwards_tagger.h"
#include "history/view/history_view_sticker_toast.h"
#include "history/view/history_view_subsection_tabs.h"
#include "history/view/history_view_translate_bar.h"
#include "history/view/media/history_view_media.h"
#include "iv/editor/iv_editor_session.h"
#include "iv/iv_rich_message_serializer.h"
#include "iv/iv_rich_page.h"
#include "core/click_handler_types.h"
#include "chat_helpers/field_autocomplete.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "chat_helpers/tabbed_section.h"
#include "chat_helpers/bot_keyboard.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/rich_paste_toast.h"
#include "menu/menu_send.h"
#include "menu/menu_timecode_action.h"
#include "mtproto/mtproto_config.h"
#include "lang/lang_keys.h"
#include "settings/business/settings_quick_replies.h"
#include "settings/settings_credits_graphics.h"
#include "storage/localimageloader.h"
#include "storage/storage_account.h"
#include "storage/file_upload.h"
#include "storage/storage_folder_archive.h"
#include "storage/storage_media_prepare.h"
#include "media/audio/media_audio.h"
#include "media/audio/media_audio_capture.h"
#include "media/player/media_player_instance.h"
#include "core/application.h"
#include "apiwrap.h"
#include "base/qthelp_regex.h"
#include "ui/boxes/report_box_graphics.h"
#include "ui/chat/pinned_bar.h"
#include "ui/chat/group_call_bar.h"
#include "ui/chat/requests_bar.h"
#include "ui/chat/chat_theme.h"
#include "ui/chat/chat_style.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/popup_menu.h"
#include "ui/item_text_options.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "main/session/send_as_peers.h"
#include "webrtc/webrtc_environment.h"
#include "window/notifications_manager.h"
#include "window/window_adaptive.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "window/window_slide_animation.h"
#include "window/window_peer_menu.h"
#include "inline_bots/inline_results_widget.h"
#include "inline_bots/bot_attach_web_view.h"
#include "info/profile/info_profile_values.h" // SharedMediaCountValue.
#include "chat_helpers/emoji_suggestions_widget.h"
#include "core/shortcuts.h"
#include "core/ui_integration.h"
#include "support/support_common.h"
#include "support/support_autocomplete.h"
#include "support/support_helper.h"
#include "support/support_preload.h"
#include "dialogs/dialogs_key.h"
#include "calls/calls_instance.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_window.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
#include "styles/style_iv.h"
#include "styles/style_layers.h"

#include <QtGui/QWindow>
#include <QtCore/QMimeData>

// AyuGram includes
#include "extras/extras_settings.h"
#include "extras/features/filters/filters_cache_controller.h"
#include "extras/utils/telegram_helpers.h"
#include "extras/features/message_shot/message_shot.h"
#include "extras/features/forward/extras_forward.h"
#include "extras/features/auto_space/auto_space.h"
#include "boxes/abstract_box.h"
#include "history/history_widget_internal.h"

using namespace HistoryWidgetDetails;

void HistoryWidget::cancelInlineBot() {
	const auto &textWithTags = _field->getTextWithTags();
	if (textWithTags.text.size() > _inlineBotUsername.size() + 2) {
		setFieldText(
			{ '@' + _inlineBotUsername + ' ', TextWithTags::Tags() },
			TextUpdateEvent::SaveDraft,
			Ui::InputField::HistoryAction::NewEntry);
	} else {
		clearFieldText(
			TextUpdateEvent::SaveDraft,
			Ui::InputField::HistoryAction::NewEntry);
	}
}

void HistoryWidget::windowIsVisibleChanged() {
	InvokeQueued(this, [=] {
		preloadHistoryIfNeeded();
	});
}

TextWithEntities HistoryWidget::prepareTextForEditMsg() const {
	const auto textWithTags = _field->getTextWithAppliedMarkdown();
	const auto prepareFlags = Ui::ItemTextOptions(
		_history,
		session().user()).flags;
	auto left = TextWithEntities {
		textWithTags.text,
		TextUtilities::ConvertTextTagsToEntities(textWithTags.tags) };
	TextUtilities::PrepareForSending(left, prepareFlags);
	// extras: 编辑时在中英文间自动插空格
	if (ExtrasSettings::getInstance().autoSpaceEditing()) {
		Extras::AutoSpace::processText(left);
	}
	return left;
}

void HistoryWidget::setupSendMenu(
		not_null<Ui::RpWidget*> button,
		Fn<void(SendMenu::Action, SendMenu::Details)> action) {
	using namespace SendMenu;
	SetupMenuAndShortcuts(
		button,
		controller()->uiShow(),
		[=] { return sendButtonMenuDetails(); },
		[=](Action value, Details details) {
			if (value.type == ActionType::CaptionUp
				|| value.type == ActionType::CaptionDown
				|| value.type == ActionType::SpoilerOn
				|| value.type == ActionType::SpoilerOff
				|| value.type == ActionType::EditCover
				|| value.type == ActionType::RemoveCover) {
				_mediaEditManager.apply(value, controller()->uiShow());
			} else {
				action(value, details);
			}
		});
}

void HistoryWidget::showAiComposeBox() {
	const auto text = prepareTextForEditMsg();
	if (text.text.isEmpty()) {
		return;
	}
	auto send = Fn<void(TextWithEntities &&, Api::SendOptions, Fn<void()>)>(
		nullptr);
	auto setupMenu = Fn<void(
		not_null<Ui::RpWidget*>,
		Fn<void(Api::SendOptions)>)>(nullptr);
	if (_list && canSendAiComposeDirect()) {
		send = crl::guard(_list, [=](
				TextWithEntities result,
				Api::SendOptions options,
				Fn<void()> done) {
			sendWithTextOverride(std::move(result), options, std::move(done));
		});
		setupMenu = crl::guard(_list, [=](
				not_null<Ui::RpWidget*> button,
				Fn<void(Api::SendOptions)> sendCallback) {
			setupSendMenu(
				button,
				SendMenu::DefaultCallback(
					controller()->uiShow(),
					sendCallback));
		});
	}
	HistoryView::Controls::ShowComposeAiBox(controller()->uiShow(), {
		.session = &session(),
		.text = text,
		.chatStyle = _fieldChatStyle,
		.apply = crl::guard(this, [=](const TextWithEntities &result) {
			const auto action = Ui::InputField::HistoryAction::NewEntry;
			setFieldText({
				result.text,
				TextUtilities::ConvertEntitiesToTextTags(result.entities),
			}, TextUpdateEvent::SaveDraft, action);
		}),
		.send = std::move(send),
		.setupMenu = std::move(setupMenu),
	});
}

void HistoryWidget::triggerAiApplyInPlace() {
	Api::TriggerAiApplyInPlace(
		&session(),
		controller()->uiShow(),
		this,
		_field,
		prepareTextForEditMsg(),
		crl::guard(this, [=](TextWithTags textWithTags, int cursor) {
			setFieldText(
				textWithTags,
				TextUpdateEvent::SaveDraft,
				Ui::InputField::HistoryAction::NewEntry);
			_field->setCursorPosition(cursor);
		}));
}

void HistoryWidget::saveEditMessage(Api::SendOptions options) {
	Expects(_history != nullptr);

	if (_saveEditMsgRequestId) {
		return;
	} else if (_mediaEditManager.videoCoverUploading()) {
		return;
	}

	const auto item = session().data().message(_history->peer, _editMsgId);
	if (!item) {
		cancelEdit();
		return;
	}
	const auto webPageDraft = _preview->draft();
	const auto sending = prepareTextForEditMsg();

	const auto hasMediaWithCaption = item
		&& item->media()
		&& item->media()->allowsEditCaption();
	if (sending.text.isEmpty()
		&& (webPageDraft.removed
			|| webPageDraft.url.isEmpty()
			|| !webPageDraft.manual)
		&& !hasMediaWithCaption) {
		if (item->computeSuggestionActions() == SuggestionActions::None) {
			controller()->show(Box<DeleteMessagesBox>(item));
		}
		return;
	} else {
		const auto limits = Data::PremiumLimits(&session());
		const auto maxTextSize = hasMediaWithCaption
			? limits.captionLengthCurrent()
			: limits.messageLengthCurrent();
		const auto remove = _fieldCharsCountManager.count() - maxTextSize;
		if (remove > 0) {
			controller()->showToast(
				tr::lng_edit_limit_reached(tr::now, lt_count, remove));
#ifndef _DEBUG
			return;
#else
			if (!base::IsCtrlPressed()) {
				return;
			}
#endif
		}
	}

	const auto weak = base::make_weak(this);
	const auto history = _history;

	const auto done = [=](mtpRequestId requestId) {
		crl::guard(weak, [=] {
			if (requestId == _saveEditMsgRequestId) {
				_saveEditMsgRequestId = 0;
				cancelEdit();
			}
		})();
		if (const auto editDraft = history->localEditDraft({}, {})) {
			if (editDraft->saveRequestId == requestId) {
				history->clearLocalEditDraft(MsgId(), PeerId());
				history->session().local().writeDrafts(history);
			}
		}
	};

	const auto fail = [=](const QString &error, mtpRequestId requestId) {
		if (const auto editDraft = history->localEditDraft({}, {})) {
			if (editDraft->saveRequestId == requestId) {
				editDraft->saveRequestId = 0;
			}
		}
		crl::guard(weak, [=] {
			if (requestId == _saveEditMsgRequestId) {
				_saveEditMsgRequestId = 0;
			}
			if (ranges::contains(Api::kDefaultEditMessagesErrors, error)) {
				controller()->showToast(tr::lng_edit_error(tr::now));
			} else if (error == u"MESSAGE_NOT_MODIFIED"_q) {
				cancelEdit();
			} else if (error == u"MESSAGE_EMPTY"_q) {
				_field->selectAll();
				setInnerFocus();
			} else {
				controller()->showToast(tr::lng_edit_error(tr::now));
			}
			update();
		})();
	};

	options.invertCaption = _mediaEditManager.invertCaption();
	options.suggest = suggestOptions(true);

	if (item->computeSuggestionActions()
		== SuggestionActions::AcceptAndDecline) {
		const auto withPaymentApproved = [=](int approved) {
			auto copy = options;
			copy.starsApproved = approved;
			saveEditMessage(copy);
		};
		const auto checked = checkSendPayment(
			1 + int(_forwardPanel->items().size()),
			options,
			withPaymentApproved);
		if (!checked) {
			return;
		}
	}

	_saveEditMsgRequestId = Api::EditTextMessage(
		item,
		sending,
		webPageDraft,
		options,
		done,
		fail,
		_mediaEditManager.spoilered(),
		_mediaEditManager.videoCover());
}

void HistoryWidget::hideChildWidgets() {
	if (Ui::InFocusChain(this)) {
		// Removing focus from list clears selected and updates top bar.
		setFocus();
	}
	if (_tabbedPanel) {
		_tabbedPanel->hideFast();
	}
	if (_sponsoredMessageBar) {
		_sponsoredMessageBar->toggle(false, anim::type::instant);
	}
	_topBars->hide();
	if (_subsectionTabs) {
		_subsectionTabs->hide();
	}
	if (_voiceRecordBar) {
		_voiceRecordBar->hideFast();
	}
	if (_composeSearch) {
		_composeSearch->hideAnimated();
	}
	if (_chooseTheme) {
		_chooseTheme->hide();
	}
	_richDraftPreview->hide();
	if (_paysStatus) {
		_paysStatus->hide();
	}
	if (_contactStatus) {
		_contactStatus->hide();
	}
	if (_businessBotStatus) {
		_businessBotStatus->hide();
	}
	hideChildren();
}

void HistoryWidget::hideSelectorControlsAnimated() {
	if (_autocomplete) {
		_autocomplete->hideAnimated();
	}
	if (_supportAutocomplete) {
		_supportAutocomplete->hide();
	}
	if (_tabbedPanel) {
		_tabbedPanel->hideAnimated();
	}
	if (_inlineResults) {
		_inlineResults->hideAnimated();
	}
}

Api::SendAction HistoryWidget::prepareSendAction(
		Api::SendOptions options) {
	auto result = Api::SendAction(_history, options);
	result.replyTo = replyTo();

	if (const auto forum = _history->asForum()) {
		if (forum->bot() && Data::IsBotUserCreatesTopics(_history->peer)) {
			const auto readyRootId = [&]() -> MsgId {
				if (const auto id = result.replyTo.messageId) {
					if (const auto item = session().data().message(id)) {
						return item->topicRootId();
					}
				}
				return {};
			}();
			if (readyRootId) {
				result.replyTo.topicRootId = readyRootId;
			} else {
				if (!_creatingBotTopic) {
					_creatingBotTopic = forum->reserveNewBotTopic();
					auto draft = _history->forwardDraft(MsgId(0), PeerId());
					if (!draft.ids.empty()) {
						_history->setForwardDraft(MsgId(0), PeerId(), {});
						_history->setForwardDraft(
							_creatingBotTopic->rootId(),
							PeerId(),
							std::move(draft));
					}
				}
				result = Api::SendAction(_creatingBotTopic, options);
				result.replyTo.topicRootId = _creatingBotTopic->rootId();
			}
		}
	}

	result.options.suggest = suggestOptions();
	result.options.sendAs = _sendAs
		? _history->session().sendAsPeers().resolveChosen(
			_history->peer).get()
		: nullptr;
	result.clearDraft = !isComposeBoxOpen();
	return result;
}

void HistoryWidget::sendVoice(const VoiceToSend &data) {
	if (!canWriteMessage() || data.bytes.isEmpty() || !_history) {
		return;
	}

	const auto withPaymentApproved = [=](int approved) {
		auto copy = data;
		copy.options.starsApproved = approved;
		sendVoice(copy);
	};
	auto action = prepareSendAction(data.options);
	const auto checked = checkSendPayment(
		1 + int(_forwardPanel->items().size()),
		action.options,
		withPaymentApproved);
	if (!checked) {
		return;
	}

	session().api().sendVoiceMessage(
		data.bytes,
		data.waveform,
		data.duration,
		data.video,
		action);
	_voiceRecordBar->clearListenState();
}

void HistoryWidget::send(Api::SendOptions options) {
	if (!_history) {
		return;
	} else if (_editMsgId) {
		saveEditMessage({});
		return;
	} else if (const auto page = shownRichMessage()) {
		sendRichDraft(page, options);
		return;
	}
	if (!options.scheduled) {
		auto action = Api::SendAction(_history, options);
		action.replyTo = replyTo();
		auto message = Api::MessageToSend(std::move(action));
		message.textWithTags = _field->getTextWithAppliedMarkdown();
		if (!session().ephemeralMessages().wouldSend(message)
			&& showSlowmodeError()) {
			return;
		}
	}
	if (_voiceRecordBar->isListenState()) {
		_voiceRecordBar->requestToSendWithOptions(options);
		return;
	}

	sendTextWithTags(
		_field->getTextWithAppliedMarkdown(),
		true,
		options,
		nullptr);
}

void HistoryWidget::sendRichDraft(
		std::shared_ptr<const Iv::RichPage> page,
		Api::SendOptions options) {
	if (!page) {
		return;
	}
	const auto ephemeral = session().ephemeralMessages()
		.isEphemeralBotReply(replyTo().messageId);
	if (ephemeral && options.scheduled) {
		controller()->showToast(tr::lng_ephemeral_cant_schedule(tr::now));
		return;
	}
	if (!options.scheduled) {
		_cornerButtons.clearReplyReturns();
		if (!ephemeral && showSlowmodeError()) {
			return;
		}
	}
	if (!session().premium()
		&& Iv::RichPageUsesPremiumFormatting(*page)) {
		if (Iv::RichPageIsFlattenSafe(*page)) {
			const auto weak = base::make_weak(this);
			Iv::Editor::OfferRichMessagePremiumChoice(
				controller()->uiShow(),
				&session(),
				*page,
				[=] {
					if (const auto strong = weak.get()) {
						strong->sendRichDraftWithoutFormatting(
							page,
							options);
					}
				});
		} else {
			Iv::Editor::ShowRichMessagesPremiumToast(
				controller()->uiShow());
		}
		return;
	}

	auto action = prepareSendAction(options);
	auto withPaymentApproved = Fn<void(int)>();
	if (!options.scheduled) {
		withPaymentApproved = [=](int approved) {
			auto copy = options;
			copy.starsApproved = approved;
			sendRichDraft(page, copy);
		};
	}
	if (showSendRichDraftError(
			options.scheduled != 0,
			std::move(withPaymentApproved),
			action.options,
			ephemeral)) {
		return;
	}

	const auto serialized = Iv::SerializeInputRichMessage(
		&session(),
		*page,
		Iv::SerializeInputRichMessageMode::FinalSubmit);
	if (serialized.status == Iv::SerializeInputRichMessageStatus::EmptyContent) {
		controller()->showToast(tr::lng_article_submit_empty(tr::now));
		return;
	} else if (serialized.status != Iv::SerializeInputRichMessageStatus::Success
		|| !serialized.value) {
		controller()->showToast(tr::lng_attach_failed(tr::now));
		return;
	}

	session().api().sendRichMessage(
		page,
		*serialized.value,
		action);

	clearFieldText();
	if (_preview) {
		_preview->apply({ .removed = true });
	}
	saveDraftWithTextNow();
	if (session().supportMode()) {
		refreshBotMenuButton();
		updateControlsVisibility();
		updateControlsGeometry();
	} else {
		applyCloudDraft(_history);
	}

	hideSelectorControlsAnimated();
	setInnerFocus();

	if (!_keyboard->hasMarkup() && _keyboard->forceReply() && !_kbReplyTo) {
		toggleKeyboard();
	}
	session().sendProgressManager().update(
		_history,
		Api::SendProgressType::Typing,
		-1);
}

void HistoryWidget::sendRichDraftWithoutFormatting(
		std::shared_ptr<const Iv::RichPage> page,
		Api::SendOptions options) {
	if (!page || !_history) {
		return;
	}
	const auto flattened = Iv::FlattenRichPageToSimpleText(*page);
	sendTextWithTags(
		{
			flattened.text,
			TextUtilities::ConvertEntitiesToTextTags(flattened.entities),
		},
		false,
		options,
		nullptr);
	applyCloudDraft(_history);
}

void HistoryWidget::sendTextWithTags(
		TextWithTags textWithTags,
		bool useWebPageDraft,
		Api::SendOptions options,
		Fn<void()> done) {
	if (!options.scheduled) {
		_cornerButtons.clearReplyReturns();
	}

	auto message = Api::MessageToSend(prepareSendAction(options));
	message.textWithTags = textWithTags;
	if (useWebPageDraft && _preview) {
		message.webPage = _preview->draft();
	}
	const auto ephemeral = session().ephemeralMessages().wouldSend(message);
	if (options.scheduled && ephemeral) {
		controller()->showToast(tr::lng_ephemeral_cant_schedule(tr::now));
		return;
	}

	const auto ignoreSlowmodeCountdown = (options.scheduled != 0);
	const auto withPaymentApproved = [=](int approved) {
		auto copy = options;
		copy.starsApproved = approved;
		sendTextWithTags(textWithTags, useWebPageDraft, copy, done);
	};
	if (showSendMessageError(
			message.textWithTags,
			ignoreSlowmodeCountdown,
			withPaymentApproved,
			message.action.options,
			ephemeral)) {
		return;
	}

	const auto nextLocalMessageId = session().data().nextLocalMessageId();
	const auto hasText = !message.textWithTags.text.trimmed().isEmpty();

	if (hasText
		&& message.webPage.url.isEmpty()
		&& (_field->document()->size().height() <= _field->height())) {
		controller()->sendingAnimation().appendSending({
			.type = Ui::MessageSendingAnimationFrom::Type::Text,
			.localId = nextLocalMessageId,
			.globalStartGeometry = _field->mapToGlobal(
				Rect(_field->size()).translated(
					0, MessageFieldCenteringMargins(_field.data()).top())),
		});
	}

	// Just a flag not to drop reply info if we're not sending anything.
	_justMarkingAsRead = !hasText
		&& message.webPage.url.isEmpty();
	session().api().sendMessage(std::move(message), nextLocalMessageId);
	_justMarkingAsRead = false;

	clearFieldText();
	if (_preview) {
		_preview->apply({ .removed = true });
	}
	saveDraftWithTextNow();

	hideSelectorControlsAnimated();

	setInnerFocus();

	if (!_keyboard->hasMarkup() && _keyboard->forceReply() && !_kbReplyTo) {
		toggleKeyboard();
	}
	session().changes().historyUpdated(
		_history,
		(options.scheduled
			? Data::HistoryUpdate::Flag::ScheduledSent
			: Data::HistoryUpdate::Flag::MessageSent));
	if (done) {
		done();
	}
}

void HistoryWidget::sendWithTextOverride(
		TextWithEntities text,
		Api::SendOptions options,
		Fn<void()> done) {
	if (!canSendAiComposeDirect()) {
		return;
	}
	const auto useWebPageDraft = (text.text == prepareTextForEditMsg().text);
	sendTextWithTags({
		text.text,
		TextUtilities::ConvertEntitiesToTextTags(text.entities),
	}, useWebPageDraft, options, std::move(done));
}

void HistoryWidget::sendWithModifiers(Qt::KeyboardModifiers modifiers) {
	send({ .handleSupportSwitch = Support::HandleSwitch(modifiers) });
}

void HistoryWidget::sendScheduled(Api::SendOptions initialOptions) {
	if (!_list) {
		return;
	}
	const auto ignoreSlowmodeCountdown = true;
	if (shownRichMessage()
		? showSendRichDraftError(ignoreSlowmodeCountdown)
		: showSendMessageError(
			_field->getTextWithAppliedMarkdown(),
			ignoreSlowmodeCountdown)) {
		return;
	}
	controller()->show(
		HistoryView::PrepareScheduleBox(
			_list,
			controller()->uiShow(),
			sendButtonDefaultDetails(),
			[=](Api::SendOptions options) { send(options); },
			initialOptions));
}

SendMenu::Details HistoryWidget::sendMenuDetails() const {
	const auto ephemeralReply = session().ephemeralMessages()
		.isEphemeralBotReply(replyTo().messageId);
	const auto type = (!_peer || ephemeralReply)
		? SendMenu::Type::Disabled
		: _peer->starsPerMessageChecked()
		? SendMenu::Type::SilentOnly
		: _peer->isSelf()
		? SendMenu::Type::Reminder
		: HistoryView::CanScheduleUntilOnline(_peer)
		? SendMenu::Type::ScheduledToUser
		: SendMenu::Type::Scheduled;
	const auto effectAllowed = _peer && _peer->isUser();
	return {
		.type = type,
		.barePeerId = _peer ? _peer->id.value : 0,
		.effectAllowed = effectAllowed,
	};
}

SendMenu::Details HistoryWidget::saveMenuDetails() const {
	return (_editMsgId && _replyEditMsg && !_replyEditMsg->richPage())
		? _mediaEditManager.sendMenuDetails(fieldHasSendText())
		: SendMenu::Details();
}

SendMenu::Details HistoryWidget::sendButtonDefaultDetails() const {
	auto result = sendMenuDetails();
	if (!hasSendableContent() && !_previewDrawPreview) {
		result.effectAllowed = false;
	}
	return result;
}

void HistoryWidget::unblockUser() {
	if (const auto user = _peer ? _peer->asUser() : nullptr) {
		const auto show = controller()->uiShow();
		Window::PeerMenuUnblockUserWithBotRestart(show, user);
	} else {
		updateControlsVisibility();
	}
}

void HistoryWidget::sendBotStartCommand() {
	if (!_peer
		|| !_peer->isUser()
		|| !_peer->asUser()->isBot()
		|| !_canSendMessages) {
		updateControlsVisibility();
		return;
	}
	session().api().sendBotStart(controller()->uiShow(), _peer->asUser());
	updateControlsVisibility();
	updateControlsGeometry();
}

void HistoryWidget::joinChannel() {
	if (!_peer || !_peer->isChannel() || !isJoinChannel()) {
		updateControlsVisibility();
		return;
	}
	session().api().joinChannel(_peer->asChannel());
}

void HistoryWidget::toggleMuteUnmute() {
	const auto wasMuted = _history->muted();
	const auto muteForSeconds = Data::MuteValue{
		.unmute = wasMuted,
		.forever = !wasMuted,
	};
	session().data().notifySettings().update(_peer, muteForSeconds);
}

void HistoryWidget::goToDiscussionGroup() {
	const auto channel = _peer ? _peer->asChannel() : nullptr;
	const auto chat = channel ? channel->discussionLink() : nullptr;
	if (!chat) {
		return;
	}
	controller()->showPeerHistory(chat, Window::SectionShow::Way::Forward);
}

bool HistoryWidget::hasDiscussionGroup() const {
	const auto &settings = ExtrasSettings::getInstance();
	if (settings.channelBottomButton() != ChannelBottomButton::DiscussWithFallback) {
		return false;
	}

	const auto channel = _peer ? _peer->asChannel() : nullptr;
	return channel
		&& channel->isBroadcast()
		&& (channel->flags() & ChannelDataFlag::HasLink);
}

void HistoryWidget::reportSelectedMessages() {
	if (!_list || !_chooseForReport || !_list->getSelectionState().count) {
		return;
	}
	const auto ids = _list->getSelectedItems();
	const auto done = _chooseForReport->callback;
	clearSelected();
	controller()->clearChooseReportMessages();
	if (done) {
		done(ranges::views::all(
			ids
		) | ranges::views::transform(&FullMsgId::msg) | ranges::to_vector);
	}
}

History *HistoryWidget::history() const {
	return _history;
}

PeerData *HistoryWidget::peer() const {
	return _peer;
}

// Sometimes _showAtMsgId is set directly.
void HistoryWidget::setMsgId(
		MsgId showAtMsgId,
		const Window::SectionShow &params) {
	_showAtMsgParams = params;
	if (_showAtMsgId != showAtMsgId) {
		_showAtMsgId = showAtMsgId;
		if (_history) {
			controller()->setActiveChatEntry({
				_history,
				FullMsgId(_history->peer->id, _showAtMsgId) });
		}
	}
}

MsgId HistoryWidget::msgId() const {
	return _showAtMsgId;
}

bool HistoryWidget::checkSendPayment(
		int messagesCount,
		Api::SendOptions options,
		Fn<void(int)> withPaymentApproved) {
	return _peer
		&& _sendPayment.check(
			controller(),
			_peer,
			options,
			messagesCount,
			std::move(withPaymentApproved));
}

void HistoryWidget::checkSuggestToGigagroup() {
	const auto group = _peer ? _peer->asMegagroup() : nullptr;
	if (!group || !group->owner().suggestToGigagroup(group)) {
		return;
	}
	InvokeQueued(_list, [=] {
		if (!controller()->isLayerShown()) {
			group->owner().setSuggestToGigagroup(group, false);
			group->session().api().request(MTPhelp_DismissSuggestion(
				group->input(),
				MTP_string("convert_to_gigagroup")
			)).send();
			controller()->show(Box([=](not_null<Ui::GenericBox*> box) {
				box->setTitle(tr::lng_gigagroup_suggest_title());
				box->addRow(
					object_ptr<Ui::FlatLabel>(
						box,
						tr::lng_gigagroup_suggest_text(
						) | rpl::map(tr::rich),
						st::infoAboutGigagroup));
				box->addButton(
					tr::lng_gigagroup_suggest_more(),
					AboutGigagroupCallback(group, controller()));
				box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
			}));
		}
	});
}

void HistoryWidget::finishAnimating() {
	if (!_showAnimation) {
		return;
	}
	_showAnimation = nullptr;
	_topShadow->setVisible(_peer != nullptr);
	_topBar->setVisible(_peer != nullptr);
	_cornerButtons.finishAnimations();
}

void HistoryWidget::chooseAttach(
		std::optional<bool> overrideSendImagesAsPhotos) {
	if (_editMsgId) {
		controller()->showToast(tr::lng_edit_caption_attach(tr::now));
		return;
	}

	if (!_peer || !_canSendMessages) {
		return;
	}
	if (!session().ephemeralMessages().isEphemeralBotReply(
			replyTo().messageId)) {
		if (const auto error = Data::AnyFileRestrictionError(_peer)) {
			Data::ShowSendErrorToast(controller(), _peer, error);
			return;
		} else if (showSlowmodeError()) {
			return;
		}
	}

	const auto filter = (overrideSendImagesAsPhotos == true)
		? FileDialog::PhotoVideoFilesFilter()
		: FileDialog::AllOrImagesFilter();

	const auto callbackOnResult = crl::guard(this, [=](
			FileDialog::OpenResult &&result) {
		if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
			return;
		}

		if (!result.remoteContent.isEmpty()) {
			auto read = Images::Read({
				.content = result.remoteContent,
			});
			if (!read.image.isNull() && !read.animated) {
				confirmSendingFiles(
					std::move(read.image),
					std::move(result.remoteContent),
					overrideSendImagesAsPhotos);
			} else {
				uploadFile(result.remoteContent, SendMediaType::File);
			}
		} else {
			const auto premium = controller()->session().user()->isPremium();
			auto list = Storage::PrepareMediaList(
				result.paths,
				st::sendMediaPreviewSize,
				premium);
			list.overrideSendImagesAsPhotos = overrideSendImagesAsPhotos;
			confirmSendingFiles(std::move(list));
		}
	});
	FileDialog::GetOpenPaths(
		this,
		tr::lng_choose_files(tr::now),
		filter,
		callbackOnResult,
		nullptr);
}

void HistoryWidget::sendButtonClicked() {
	const auto type = _send->type();
	if (type == Ui::SendButton::Type::Cancel) {
		cancelInlineBot();
	} else if (type == Ui::SendButton::Type::Stop) {
		stopStreamedDraft();
	} else if (type != Ui::SendButton::Type::Record
		&& type != Ui::SendButton::Type::Round) {
		send({});
	}
}

void HistoryWidget::stopStreamedDraft() {
	if (const auto streamed = _history
			? _history->streamedDraftsIfExists()
			: nullptr) {
		streamed->requestStop(MsgId(0));
	}
	updateSendButtonType();
}

bool HistoryWidget::showSendingFilesError(
		const Ui::PreparedList &list) const {
	const auto show = controller()->uiShow();
	const auto ephemeralReply = session().ephemeralMessages()
		.isEphemeralBotReply(replyTo().messageId);
	return Data::ShowSendError(
		show,
		_peer,
		list,
		std::nullopt,
		false,
		ephemeralReply);
}

bool HistoryWidget::showSendingFilesError(
		const Ui::PreparedBundle &bundle) const {
	const auto ephemeralReply = session().ephemeralMessages()
		.isEphemeralBotReply(replyTo().messageId);
	return Data::ShowSendError(
		controller()->uiShow(),
		_peer,
		bundle,
		false,
		ephemeralReply);
}

MsgId HistoryWidget::resolveReplyToTopicRootId() {
	Expects(_peer != nullptr);

	const auto replyToInfo = replyTo();
	const auto replyToMessage = (replyToInfo.messageId.peer == _peer->id)
		? session().data().message(replyToInfo.messageId)
		: nullptr;
	const auto result = replyToMessage
		? replyToMessage->topicRootId()
		: replyToInfo.topicRootId;
	if (result
		&& _peer->isForum()
		&& !_peer->forumTopicFor(result)
		&& _topicsRequested.emplace(result).second) {
		_peer->forum()->requestTopic(result, crl::guard(_list, [=] {
			updateCanSendMessage();
			updateFieldPlaceholder();
			_topicsRequested.remove(result);
		}));
	}
	return result;
}

Data::ForumTopic *HistoryWidget::resolveReplyToTopic() {
	return _peer
		? _peer->forumTopicFor(resolveReplyToTopicRootId())
		: nullptr;
}

bool HistoryWidget::showSendMessageError(
		const TextWithTags &textWithTags,
		bool ignoreSlowmodeCountdown,
		Fn<void(int starsApproved)> withPaymentApproved,
		Api::SendOptions options,
		bool ephemeral) {
	if (!_canSendMessages) {
		return false;
	}
	const auto topicRootId = resolveReplyToTopicRootId();
	auto request = SendingErrorRequest{
		.topicRootId = topicRootId,
		.forward = &_forwardPanel->items(),
		.text = &textWithTags,
		.ignoreSlowmodeCountdown = ignoreSlowmodeCountdown,
		.ignoreRestrictions = ephemeral,
	};
	request.messagesCount = ComputeSendingMessagesCount(_history, request);
	const auto error = GetErrorForSending(_peer, request);
	if (error) {
		Data::ShowSendErrorToast(controller(), _peer, error);
		return true;
	}

	return withPaymentApproved
		&& !ephemeral
		&& !checkSendPayment(
			request.messagesCount,
			options,
			withPaymentApproved);
}

bool HistoryWidget::showSendRichDraftError(
		bool ignoreSlowmodeCountdown,
		Fn<void(int starsApproved)> withPaymentApproved,
		Api::SendOptions options,
		bool ephemeral) {
	if (!_canSendMessages || !_history || !_peer) {
		return false;
	}
	const auto topicRootId = resolveReplyToTopicRootId();
	auto request = SendingErrorRequest{
		.topicRootId = topicRootId,
		.forward = &_forwardPanel->items(),
		.messagesCount = 1,
		.ignoreSlowmodeCountdown = ignoreSlowmodeCountdown,
		.richMessage = true,
		.ignoreRestrictions = ephemeral,
	};
	request.messagesCount = ComputeSendingMessagesCount(_history, request);
	const auto error = GetErrorForSending(_peer, request);
	if (error) {
		Data::ShowSendErrorToast(controller(), _peer, error);
		return true;
	}

	return withPaymentApproved
		&& !ephemeral
		&& !checkSendPayment(
			request.messagesCount,
			options,
			withPaymentApproved);
}

bool HistoryWidget::confirmSendingFiles(const QStringList &files) {
	return confirmSendingFiles(files, QString());
}

bool HistoryWidget::confirmSendingFiles(not_null<const QMimeData*> data) {
	return confirmSendingFiles(data, std::nullopt);
}

bool HistoryWidget::confirmSendingFiles(
		const QStringList &files,
		const QString &insertTextOnCancel) {
	const auto premium = controller()->session().user()->isPremium();
	return confirmSendingFiles(
		Storage::PrepareMediaList(files, st::sendMediaPreviewSize, premium),
		insertTextOnCancel);
}

bool HistoryWidget::confirmSendingFiles(
		Ui::PreparedList &&list,
		const QString &insertTextOnCancel) {
	if (_editMsgId) {
		if (_canReplaceMedia || _canAddMedia) {
			EditCaptionBox::StartMediaReplace(
				controller(),
				{ _history->peer->id, _editMsgId },
				std::move(list),
				_field->getTextWithTags(),
				suggestOptions(),
				_mediaEditManager.spoilered(),
				_mediaEditManager.invertCaption(),
				crl::guard(_list, [=] { cancelEdit(); }));
			return true;
		}
		controller()->showToast(tr::lng_edit_caption_attach(tr::now));
		return false;
	} else if (!_peer || showSendingFilesError(list)) {
		return false;
	}

	const auto cursor = _field->textCursor();
	const auto position = cursor.position();
	const auto anchor = cursor.anchor();
	const auto text = _field->getTextWithTags();
	auto box = Box<SendFilesBox>(
		controller(),
		std::move(list),
		text,
		_peer,
		Api::SendType::Normal,
		sendMenuDetails(),
		[=](const TextWithTags &text) { _field->setTextWithTags(text); });
	box->setReplyTo(replyTo());
	_sendFilesBox = box.data();
	_field->setTextWithTags({});
	box->setConfirmedCallback(crl::guard(this, [=](
			std::shared_ptr<Ui::PreparedBundle> bundle,
			Api::SendOptions options,
			FullReplyTo currentReplyTo) {
		if (!currentReplyTo.messageId && replyTo().messageId) {
			cancelReply();
		}
		sendingFilesConfirmed(std::move(bundle), options);
	}));
	box->setStashCallbacks(
		crl::guard(this, [=] { return _stash->canTakeFromBox(); }),
		crl::guard(this, [=](SendFilesStashed &&stashed) {
			_stash->takeFromBox(std::move(stashed));
		}));
	box->setCancelledCallback(crl::guard(this, [=] {
		_field->setTextWithTags(text);
		auto cursor = _field->textCursor();
		cursor.setPosition(anchor);
		if (position != anchor) {
			cursor.setPosition(position, QTextCursor::KeepAnchor);
		}
		_field->setTextCursor(cursor);
		if (Ui::InsertTextOnImageCancel(insertTextOnCancel)) {
			_field->textCursor().insertText(insertTextOnCancel);
		}
	}));
	box->takeTextWithTagsRequests() | rpl::on_next([=](TextWithTags &&text) {
		_field->setTextWithTags(std::move(text));
	}, box->lifetime());

	Window::ActivateWindow(controller());
	controller()->show(std::move(box));

	return true;
}

void HistoryWidget::sendingFilesConfirmed(
		std::shared_ptr<Ui::PreparedBundle> bundle,
		Api::SendOptions options) {
	if (!_peer || showSendingFilesError(*bundle)) {
		return;
	}
	const auto ephemeralReply = session().ephemeralMessages()
		.isEphemeralBotReply(replyTo().messageId);
	if (bundle->totalCount > 1 && ephemeralReply) {
		controller()->showToast(
			tr::lng_ephemeral_reply_single_message(tr::now));
		return;
	}

	const auto compress = bundle->way.sendImagesAsPhotos();
	const auto type = compress ? SendMediaType::Photo : SendMediaType::File;
	auto action = prepareSendAction(options);
	action.clearDraft = false;

	if (!ephemeralReply) {
		const auto withPaymentApproved = [=](int approved) {
			auto copy = options;
			copy.starsApproved = approved;
			sendingFilesConfirmed(bundle, copy);
		};
		const auto checked = checkSendPayment(
			bundle->totalCount,
			action.options,
			withPaymentApproved);
		if (!checked) {
			return;
		}
	}

	auto &api = session().api();
	for (auto &group : bundle->groups) {
		const auto album = (group.type != Ui::AlbumType::None)
			? std::make_shared<SendingAlbum>()
			: nullptr;
		api.sendFiles(std::move(group.list), type, album, action);
	}
}

bool HistoryWidget::confirmSendingFiles(
		QImage &&image,
		QByteArray &&content,
		std::optional<bool> overrideSendImagesAsPhotos,
		const QString &insertTextOnCancel) {
	if (image.isNull()) {
		return false;
	}

	auto list = Storage::PrepareMediaFromImage(
		std::move(image),
		std::move(content),
		st::sendMediaPreviewSize);
	list.overrideSendImagesAsPhotos = overrideSendImagesAsPhotos;
	return confirmSendingFiles(std::move(list), insertTextOnCancel);
}

bool HistoryWidget::canSendFiles(not_null<const QMimeData*> data) const {
	if (!canWriteMessage()) {
		return false;
	} else if (data->hasImage()) {
		return true;
	} else if (const auto urls = Core::ReadMimeUrls(data); !urls.empty()) {
		if (ranges::all_of(urls, &QUrl::isLocalFile)) {
			return true;
		}
	}
	return false;
}

bool HistoryWidget::confirmSendingFiles(
		not_null<const QMimeData*> data,
		std::optional<bool> overrideSendImagesAsPhotos,
		const QString &insertTextOnCancel) {
	if (!canWriteMessage()) {
		if (_composeSearch) {
			_composeSearch->hideAnimated();
		} else {
			return false;
		}
	}

	const auto hasImage = data->hasImage();
	const auto premium = controller()->session().user()->isPremium();

	if (const auto urls = Core::ReadMimeUrls(data); !urls.empty()) {
		const auto folder = Storage::SingleFolderPath(urls);
		if (!folder.isEmpty()) {
			if (overrideSendImagesAsPhotos == false && !_editMsgId) {
				const auto files = Storage::FolderFilesForSending(folder);
				if (!files.isEmpty()) {
					auto list = Storage::PrepareMediaList(
						files,
						st::sendMediaPreviewSize,
						premium);
					confirmSendingFiles(std::move(list), QString());
				}
			} else {
				auto list = Ui::PreparedList();
				list.files.push_back(Storage::PrepareFolderArchive(folder));
				confirmSendingFiles(std::move(list), QString());
			}
			return true;
		}
		if (overrideSendImagesAsPhotos == true
			&& (Storage::ComputeMimeDataState(data)
				== Storage::MimeDataState::FilesArchive)) {
			auto list = Ui::PreparedList();
			list.files.push_back(Storage::PrepareFilesArchive(urls));
			confirmSendingFiles(std::move(list), QString());
			return true;
		}
		auto list = Storage::PrepareMediaList(
			urls,
			st::sendMediaPreviewSize,
			premium);
		if (list.error != Ui::PreparedList::Error::NonLocalUrl) {
			if (list.error == Ui::PreparedList::Error::None
				|| !hasImage) {
				const auto emptyTextOnCancel = QString();
				list.overrideSendImagesAsPhotos = overrideSendImagesAsPhotos;
				confirmSendingFiles(std::move(list), emptyTextOnCancel);
				return true;
			}
		}
	}

	if (auto read = Core::ReadMimeImage(data)) {
		confirmSendingFiles(
			std::move(read.image),
			std::move(read.content),
			overrideSendImagesAsPhotos,
			insertTextOnCancel);
		return true;
	}
	return false;
}

void HistoryWidget::uploadFile(
		const QByteArray &fileContent,
		SendMediaType type) {
	if (!canWriteMessage()) return;

	session().api().sendFile(fileContent, type, prepareSendAction({}));
}

bool HistoryWidget::showSlowmodeError() {
	const auto text = [&] {
		if (const auto left = _peer->slowmodeSecondsLeft()) {
			return tr::lng_slowmode_enabled(
				tr::now,
				lt_left,
				Ui::FormatDurationWordsSlowmode(left));
		} else if (_peer->slowmodeApplied()) {
			if (const auto item = _history->latestSendingMessage()) {
				if (item->mainView()) {
					animatedScrollToItem(item->id);
					enqueueMessageHighlight({ item });
				}
				return tr::lng_slowmode_no_many(tr::now);
			}
		}
		return QString();
	}();
	if (text.isEmpty()) {
		return false;
	}
	controller()->showToast(text);
	return true;
}

void HistoryWidget::sendInlineResult(InlineBots::ResultSelected result) {
	if (!_peer || !_canSendMessages) {
		return;
	} else if (showSlowmodeError()) {
		return;
	} else if (const auto error = result.result->getErrorOnSend(_history)) {
		Data::ShowSendErrorToast(controller(), _peer, error);
		return;
	} else if (ShowEphemeralReplyTextOnlyError(
			controller()->uiShow(),
			&session(),
			replyTo().messageId)) {
		return;
	}

	const auto withPaymentApproved = [=](int approved) {
		auto copy = result;
		copy.options.starsApproved = approved;
		sendInlineResult(copy);
	};

	auto action = prepareSendAction(result.options);
	action.generateLocal = true;

	const auto checked = checkSendPayment(
		1,
		action.options,
		withPaymentApproved);
	if (!checked) {
		return;
	}

	controller()->sendingAnimation().appendSending(
		result.messageSendingFrom);
	session().api().sendInlineResult(
		result.bot,
		result.result.get(),
		action,
		result.messageSendingFrom.localId);

	clearFieldText();
	saveDraftWithTextNow();

	session().recentInlineBots().bump(result.bot);

	hideSelectorControlsAnimated();

	setInnerFocus();
}

bool HistoryWidget::sendExistingDocument(
		not_null<DocumentData*> document,
		Api::MessageToSend messageToSend,
		std::optional<MsgId> localId) {
	const auto ephemeralReply = session().ephemeralMessages()
		.isEphemeralBotReply(messageToSend.action.replyTo.messageId);
	const auto error = (_peer && !ephemeralReply)
		? Data::RestrictionError(_peer, ChatRestriction::SendStickers)
		: Data::SendError();
	if (error) {
		Data::ShowSendErrorToast(controller(), _peer, error);
		return false;
	} else if (!_peer
		|| !_canSendMessages
		|| (!ephemeralReply && showSlowmodeError())
		|| ShowSendPremiumError(controller(), document)) {
		return false;
	}
	if (!ephemeralReply) {
		const auto withPaymentApproved = [=](int approved) {
			auto copy = messageToSend;
			copy.action.options.starsApproved = approved;
			sendExistingDocument(document, std::move(copy), localId);
		};
		const auto checked = checkSendPayment(
			1,
			messageToSend.action.options,
			withPaymentApproved);
		if (!checked) {
			return false;
		}
	}

	Api::SendExistingDocument(
		std::move(messageToSend),
		document,
		localId);

	if (_autocomplete && _autocomplete->stickersShown()) {
		clearFieldText();
		//saveDraftWithTextNow();

		// won't be needed if SendInlineBotResult will clear the cloud draft
		saveCloudDraft();
	}

	hideSelectorControlsAnimated();

	setInnerFocus();
	return true;
}

bool HistoryWidget::sendExistingPhoto(
		not_null<PhotoData*> photo,
		Api::SendOptions options) {
	const auto ephemeralReply = session().ephemeralMessages()
		.isEphemeralBotReply(replyTo().messageId);
	const auto error = (_peer && !ephemeralReply)
		? Data::RestrictionError(_peer, ChatRestriction::SendPhotos)
		: Data::SendError();
	if (error) {
		Data::ShowSendErrorToast(controller(), _peer, error);
		return false;
	} else if (!_peer || !_canSendMessages) {
		return false;
	} else if (!ephemeralReply && showSlowmodeError()) {
		return false;
	}
	const auto action = prepareSendAction(options);

	if (!ephemeralReply) {
		const auto withPaymentApproved = [=](int approved) {
			auto copy = options;
			copy.starsApproved = approved;
			sendExistingPhoto(photo, copy);
		};
		const auto checked = checkSendPayment(
			1,
			action.options,
			withPaymentApproved);
		if (!checked) {
			return false;
		}
	}

	Api::SendExistingPhoto(Api::MessageToSend(action), photo);

	hideSelectorControlsAnimated();

	setInnerFocus();
	return true;
}
