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
#include "ayu/ayu_settings.h"
#include "ayu/features/filters/filters_cache_controller.h"
#include "ayu/utils/telegram_helpers.h"
#include "ayu/features/message_shot/message_shot.h"
#include "ayu/features/forward/ayu_forward.h"
#include "ayu/features/auto_space/auto_space.h"
#include "boxes/abstract_box.h"
#include "history/history_widget_internal.h"

using namespace HistoryWidgetDetails;

void HistoryWidget::updateControlsVisibility() {
	const auto &settings = AyuSettings::getInstance();

	auto fieldDisabledRemoved = (_fieldDisabled != nullptr);
	auto fieldVisibilityChanged = false;
	const auto hideExtra = hideExtraButtons();
	const auto guard = gsl::finally([&] {
		if (fieldDisabledRemoved) {
			_fieldDisabled = nullptr;
		}
		updateComposeSurfaceVisibility();
	});
	const auto hideField = [&] {
		if (!_field->isHidden()) {
			if (Ui::InFocusChain(_field)) {
				setFocus();
			}
			_field->hide();
			fieldVisibilityChanged = true;
		}
	};
	const auto showField = [&] {
		if (_field->isHidden()) {
			_field->show();
			fieldVisibilityChanged = true;
		}
	};
	const auto hidePreview = [&] {
		if (!_richDraftPreview->isHidden()) {
			_richDraftPreview->hide();
			fieldVisibilityChanged = true;
		}
	};
	const auto showPreview = [&] {
		if (const auto draft = cloudDraft()) {
			_richDraftPreview->setDraft(*draft, Data::FileOriginCloudDraft{
				.peerId = _history->peer->id,
			});
		}
		if (_richDraftPreview->isHidden()) {
			_richDraftPreview->show();
			fieldVisibilityChanged = true;
		}
	};

	if (!_showAnimation) {
		_topShadow->setVisible(_peer != nullptr);
		_topBar->setVisible(_peer != nullptr);
	}
	_cornerButtons.updateJumpDownVisibility();
	_cornerButtons.updateUnreadThingsVisibility();
	if (!_history || _showAnimation) {
		hideChildWidgets();
		return;
	}

	if (_scroll->isHidden()) {
		_scroll->show();
	}
	_topBars->show();
	if (_sponsoredMessageBar && checkSponsoredMessageBarVisibility()) {
		_sponsoredMessageBar->toggle(true, anim::type::normal);
	}
	if (_paysStatus) {
		_paysStatus->show();
	}
	if (_contactStatus) {
		_contactStatus->show();
	}
	if (_businessBotStatus) {
		_businessBotStatus->show();
	}
	if (_subsectionTabs) {
		_subsectionTabs->show();
	}
	if (isChoosingTheme()
		|| (!editingMessage()
			&& (isSearching()
				|| isBlocked()
				|| isJoinChannel()
				|| isMuteUnmute()
				|| isBotStart()
				|| isReportMessages()))) {
		const auto toggle = [&](Ui::FlatButton *shown) {
			const auto toggleOne = [&](not_null<Ui::FlatButton*> button) {
				if (button.get() != shown) {
					button->hide();
				} else if (button->isHidden()) {
					button->clearState();
					button->show();
				}
			};
			toggleOne(_reportMessages);
			toggleOne(_joinChannel);
			toggleOne(_muteUnmute);
			toggleOne(_botStart);
			toggleOne(_unblock);
		};
		if (isChoosingTheme()) {
			_chooseTheme->show();
			setInnerFocus();
			toggle(nullptr);
		} else if (isReportMessages()) {
			toggle(_reportMessages);
		} else if (isBlocked()) {
			toggle(_unblock);
			_discuss->hide();
		} else if (isJoinChannel()) {
			toggle(_joinChannel);
			_discuss->hide();
		} else if (isMuteUnmute()) {
			toggle(_muteUnmute);
			if (hasDiscussionGroup()) {
				if (_discuss->isHidden()) {
					_discuss->clearState();
					_discuss->show();
				}
			} else {
				_discuss->hide();
			}
		} else if (isBotStart()) {
			toggle(_botStart);
			_discuss->hide();

			const auto startToken = _peer->asUser()->botInfo->startToken;
			if (!startToken.isEmpty()) {
				const auto shortened = startToken.left(20);
				const auto s = QString("%1 (%2)").arg(tr::lng_bot_start(tr::now).toUpper()).arg(shortened);
				_botStart->setText(s);
			} else {
				_botStart->setText(tr::lng_bot_start(tr::now).toUpper());
			}
		}
		_kbShown = false;
		if (_autocomplete) {
			_autocomplete->hide();
		}
		if (_supportAutocomplete) {
			_supportAutocomplete->hide();
		}
		_send->hide();
		if (_silent) {
			_silent->hide();
		}
		if (_scheduled) {
			_scheduled->hide();
		}
		if (_toggleSuggestPost) {
			_toggleSuggestPost->hide();
		}
		if (_giftToUser) {
			_giftToUser->hide();
		}
		if (_ttlInfo) {
			_ttlInfo->hide();
		}
		if (_sendAs) {
			_sendAs->hide();
		}
		_kbScroll->hide();
		_fieldBarCancel->hide();
		_attachToggle->hide();
		if (_replaceMedia) {
			_replaceMedia->hide();
		}
		_tabbedSelectorToggle->hide();
		_botKeyboardShow->hide();
		_botKeyboardHide->hide();
		_botCommandStart->hide();
		if (_botMenu.button) {
			_botMenu.button->hide();
		}
		if (_tabbedPanel) {
			_tabbedPanel->hide();
		}
		if (_voiceRecordBar) {
			_voiceRecordBar->hideFast();
		}
		if (_inlineResults) {
			_inlineResults->hide();
		}
		if (_sendRestriction) {
			_sendRestriction->hide();
		}
		hidePreview();
		hideField();
	} else if (editingMessage() || _canSendMessages) {
		if (_autocomplete) {
			_autocomplete->requestRefresh();
		}
		_unblock->hide();
		_botStart->hide();
		_joinChannel->hide();
		_muteUnmute->hide();
		_discuss->hide();
		_reportMessages->hide();
		_send->show();
		updateSendButtonType();

		if (_canSendTexts || _editMsgId) {
			const auto richDraft = !_voiceRecordBar->isActive()
				&& _canSendTexts
				&& shouldShowRichDraftPreview();
			if (richDraft) {
				showPreview();
				hideField();
			} else {
				hidePreview();
				showField();
			}
		} else {
			hidePreview();
			fieldDisabledRemoved = false;
			if (!_fieldDisabled) {
				_fieldDisabled = CreateDisabledFieldView(this, _peer);
				orderWidgets();
				updateControlsGeometry();
				update();
			}
			_fieldDisabled->show();
			hideField();
		}
		if (_kbShown) {
			_kbScroll->show();
			_tabbedSelectorToggle->hide();
			showKeyboardHideButton();
			_botKeyboardShow->hide();
			_botCommandStart->hide();
		} else if (_kbReplyTo) {
			_kbScroll->hide();
			SWITCH_BUTTON(_tabbedSelectorToggle, settings.showEmojiButtonInMessageField());
			_botKeyboardHide->hide();
			_botKeyboardShow->hide();
			_botCommandStart->hide();
		} else {
			_kbScroll->hide();
			SWITCH_BUTTON(_tabbedSelectorToggle, settings.showEmojiButtonInMessageField());
			_botKeyboardHide->hide();
			if (_keyboard->hasMarkup()) {
				_botKeyboardShow->show();
				_botCommandStart->hide();
			} else {
				_botKeyboardShow->hide();
				_botCommandStart->setVisible(_cmdStartShown && settings.showCommandsButtonInMessageField());
			}
		}
		if (_replaceMedia) {
			_replaceMedia->show();
			_attachToggle->hide();
		} else {
			SWITCH_BUTTON(_attachToggle, settings.showAttachButtonInMessageField());
		}
		if (_botMenu.button) {
			_botMenu.button->show();
		}
		if (_sendRestriction) {
			_sendRestriction->hide();
		}
		{
			auto rightButtonsChanged = false;
			if (_silent) {
				const auto was = _silent->isVisible();
				const auto now = (!_editMsgId) && (!hideExtra);
				if (was != now) {
					_silent->setVisible(now);
					rightButtonsChanged = true;
				}
			}
			if (_scheduled) {
				const auto was = _scheduled->isVisible();
				const auto now = (!_editMsgId) && (!hideExtra);
				if (was != now) {
					_scheduled->setVisible(now);
					rightButtonsChanged = true;
				}
			}
			if (_toggleSuggestPost) {
				const auto was = _toggleSuggestPost->isVisible();
				const auto now = !_suggestOptions;
				if (was != now) {
					_toggleSuggestPost->setVisible(now);
					rightButtonsChanged = true;
				}
			}
			if (_giftToUser) {
				const auto was = _giftToUser->isVisible();
				const auto now = (!_editMsgId)
					&& (!hideExtra)
					&& settings.showGiftButtonInMessageField();
				if (was != now) {
					_giftToUser->setVisible(now);
					rightButtonsChanged = true;
				}
			}
			if (_ttlInfo) {
				const auto was = _ttlInfo->isVisible();
				const auto now = (!_editMsgId)
					&& (!hideExtra)
					&& settings.showAutoDeleteButtonInMessageField();
				if (was != now) {
					_ttlInfo->setVisible(now);
					rightButtonsChanged = true;
				}
			}
			if (rightButtonsChanged) {
				updateFieldSize();
			}
		}
		if (_sendAs) {
			_sendAs->show();
		}
		updateFieldPlaceholder();

		if (_editMsgId
			|| _replyTo
			|| readyToForward()
			|| _previewDrawPreview
			|| _kbReplyTo
			|| _suggestOptions) {
			if (_fieldBarCancel->isHidden()) {
				_fieldBarCancel->show();
				updateControlsGeometry();
				update();
			}
		} else {
			_fieldBarCancel->hide();
		}
	} else {
		if (_autocomplete) {
			_autocomplete->hide();
		}
		if (_supportAutocomplete) {
			_supportAutocomplete->hide();
		}
		_send->hide();
		_unblock->hide();
		_botStart->hide();
		_joinChannel->hide();
		_muteUnmute->hide();
		_discuss->hide();
		_reportMessages->hide();
		_attachToggle->hide();
		if (_silent) {
			_silent->hide();
		}
		if (_scheduled) {
			_scheduled->hide();
		}
		if (_toggleSuggestPost) {
			_toggleSuggestPost->hide();
		}
		if (_giftToUser) {
			_giftToUser->hide();
		}
		if (_ttlInfo) {
			_ttlInfo->hide();
		}
		if (_sendAs) {
			_sendAs->hide();
		}
		if (_botMenu.button) {
			_botMenu.button->hide();
		}
		_kbScroll->hide();
		if (_replyTo || readyToForward() || _kbReplyTo) {
			if (_fieldBarCancel->isHidden()) {
				_fieldBarCancel->show();
				updateControlsGeometry();
				update();
			}
		} else {
			_fieldBarCancel->hide();
		}
		_tabbedSelectorToggle->hide();
		_botKeyboardShow->hide();
		_botKeyboardHide->hide();
		_botCommandStart->hide();
		if (_tabbedPanel) {
			_tabbedPanel->hide();
		}
		if (_voiceRecordBar) {
			_voiceRecordBar->hideFast();
		}
		if (_composeSearch) {
			_composeSearch->hideAnimated();
		}
		if (_inlineResults) {
			_inlineResults->hide();
		}
		if (_sendRestriction) {
			_sendRestriction->show();
		}
		_kbScroll->hide();
		hidePreview();
		hideField();
	}
	//checkTabbedSelectorToggleTooltip();
	if (fieldVisibilityChanged) {
		updateControlsGeometry();
		update();
	}
	updateAiButtonVisibility();
	updateSendAsFileVisibility();
	updateExpandButtonVisibility();
	updateDiscardRichDraftVisibility();
	updateMouseTracking();
}

void HistoryWidget::showAboutTopPromotion() {
	Expects(_history != nullptr);
	Expects(_list != nullptr);

	if (!_history->useTopPromotion() || _history->topPromotionAboutShown()) {
		return;
	}
	_history->markTopPromotionAboutShown();
	const auto type = _history->topPromotionType();
	const auto custom = type.isEmpty()
		? QString()
		: Lang::GetNonDefaultValue(kPsaAboutPrefix + type.toUtf8());
	const auto text = type.isEmpty()
		? tr::lng_proxy_sponsor_about(tr::now, tr::rich)
		: custom.isEmpty()
		? tr::lng_about_psa_default(tr::now, tr::rich)
		: tr::rich(custom);
	showInfoTooltip(text, nullptr);
}

void HistoryWidget::updateMouseTracking() {
	const auto trackMouse = !_fieldBarCancel->isHidden();
	setMouseTracking(trackMouse);
}

void HistoryWidget::destroyUnreadBar() {
	if (_history) _history->destroyUnreadBar();
	if (_migrated) _migrated->destroyUnreadBar();
}

void HistoryWidget::destroyUnreadBarOnClose() {
	if (!_history || !_historyInited) {
		return;
	} else if (_scroll->scrollTop() >= _scroll->scrollTopMax()) {
		destroyUnreadBar();
		return;
	}
	const auto top = unreadBarTop();
	if (top && *top < visibleScrollTop()) {
		destroyUnreadBar();
		return;
	}
}

void HistoryWidget::newItemAdded(not_null<HistoryItem*> item) {
	if (_history != item->history()
		|| !_historyInited
		|| item->isScheduled()) {
		return;
	}
	if (item->isSponsored()) {
		if (const auto view = item->mainView()) {
			view->resizeGetHeight(width());
			updateHistoryGeometry(
				false,
				true,
				{ ScrollChangeNoJumpToBottom, 0 });
		}
		return;
	}

	// If we get here in non-resized state we can't rely on results of
	// markingMessagesRead() and mark chat as read.
	// If we receive N messages being not at bottom:
	// - on first message we set unreadcount += 1, firstUnreadMessage.
	// - on second we get wrong markingMessagesRead() and read both.
	session().data().sendHistoryChangeNotifications();

	if (item->isSending()) {
		synteticScrollToY(_scroll->scrollTopMax());
	} else if (_scroll->scrollTop() < _scroll->scrollTopMax()) {
		return;
	}
	if (item->showNotification()) {
		destroyUnreadBar();
		if (markingMessagesRead()) {
			if (_list && item->hasUnwatchedEffect()) {
				_list->startEffectOnRead(item);
			}
			if (item->isUnreadMention() && !item->isUnreadMedia()) {
				session().api().markContentsRead(item);
			}
			session().data().histories().readInboxOnNewMessage(item);

			// Also clear possible scheduled messages notifications.
			// Side-effect: Also clears all notifications from forum topics.
			Core::App().notifications().clearFromHistory(_history);
		}
	}
	const auto view = item->mainView();
	if (!view) {
		return;
	} else if (anim::Disabled()) {
		if (!On(PowerSaving::kChatBackground)) {
			// Strange case of disabled animations, but enabled bg rotation.
			if (item->out() || _history->peer->isSelf()) {
				_list->theme()->rotateComplexGradientBackground();
			}
		}
		return;
	}
	const auto streamed = item->history()->streamedDraftsIfExists();
	if (!streamed || !streamed->hasFor(item)) {
		_itemRevealPending.emplace(item);
	}
}

void HistoryWidget::maybeMarkReactionsRead(not_null<HistoryItem*> item) {
	if (!_historyInited || !_list) {
		return;
	}
	const auto view = item->mainView();
	const auto itemTop = _list->itemTop(view);
	if (itemTop <= 0 || !markingContentsRead()) {
		return;
	}
	const auto reactionCenter
		= view->reactionButtonParameters({}, {}).center.y();
	const auto visibleTop = visibleScrollTop();
	const auto visibleBottom = visibleScrollBottom();
	if (itemTop + reactionCenter < visibleTop
		|| itemTop + view->height() > visibleBottom) {
		return;
	}
	session().api().markContentsRead(item);
}

bool HistoryWidget::handleDrawToReplyRequest(
		Data::DrawToReplyRequest request) {
	if (!_peer || request.messageId.peer != _peer->id) {
		return false;
	}
	auto image = HistoryView::ResolveDrawToReplyImage(
		&session().data(),
		request);
	if (image.isNull()) {
		return false;
	}
	const auto replyTo = request.messageId;
	HistoryView::OpenDrawToReplyEditor(
		controller(),
		std::move(image),
		crl::guard(this, [=](QImage &&result) {
			if (result.isNull()) {
				return;
			}
			if (replyTo) {
				replyToMessage({ .messageId = replyTo });
			}
			auto list = Storage::PrepareMediaFromImage(
				std::move(result),
				QByteArray(),
				st::sendMediaPreviewSize);
			confirmSendingFiles(std::move(list));
		}));
	return true;
}

void HistoryWidget::unreadCountUpdated() {
	if (_history->unreadMark() || (_migrated && _migrated->unreadMark())) {
		crl::on_main(this, [=, history = _history] {
			if (history == _history) {
				closeCurrent();
			}
		});
	} else {
		const auto hideCounter = _history->isForum()
			|| !_history->trackUnreadMessages();
		_cornerButtons.updateJumpDownVisibility(hideCounter
			? 0
			: _history->amMonoforumAdmin()
			? _history->chatListUnreadState().messages
			: _history->chatListBadgesState().unreadCounter);
	}
}

void HistoryWidget::closeCurrent() {
	if (controller()->isPrimary()) {
		controller()->showBackFromStack();
	} else {
		controller()->window().close();
	}
}

void HistoryWidget::messagesFailed(const MTP::Error &error, int requestId) {
	if (error.type() == u"CHANNEL_PRIVATE"_q
		&& _peer->isChannel()
		&& _peer->asChannel()->invitePeekExpires()) {
		_peer->asChannel()->privateErrorReceived();
	} else if (error.type() == u"CHANNEL_PRIVATE"_q
		|| error.type() == u"CHANNEL_PUBLIC_GROUP_NA"_q
		|| error.type() == u"USER_BANNED_IN_CHANNEL"_q) {
		auto was = _peer;
		closeCurrent();
		const auto wasAccount = not_null(&was->account());
		if (const auto primary = Core::App().windowFor(wasAccount)) {
			primary->showToast(was->isMegagroup()
				? tr::lng_group_not_accessible(tr::now)
				: tr::lng_channel_not_accessible(tr::now));
		}
		return;
	}

	LOG(("RPC Error: %1 %2: %3").arg(
		QString::number(error.code()),
		error.type(),
		error.description()));

	if (_preloadRequest == requestId) {
		_preloadRequest = 0;
	} else if (_preloadDownRequest == requestId) {
		_preloadDownRequest = 0;
	} else if (_firstLoadRequest == requestId) {
		_firstLoadRequest = 0;
		closeCurrent();
	} else if (_delayedShowAtRequest == requestId) {
		_delayedShowAtRequest = 0;
	}
}

void HistoryWidget::messagesReceived(
		not_null<PeerData*> peer,
		const MTPmessages_Messages &messages,
		int requestId) {
	// Expects(_history != nullptr);
	if (!_history || !_peer) {
		return; // AyuGram: fix crash when using `saveDeletedMessages`
	}

	const auto toMigrated = (peer == _peer->migrateFrom());
	if (peer != _peer && !toMigrated) {
		if (_preloadRequest == requestId) {
			_preloadRequest = 0;
		} else if (_preloadDownRequest == requestId) {
			_preloadDownRequest = 0;
		} else if (_firstLoadRequest == requestId) {
			_firstLoadRequest = 0;
		} else if (_delayedShowAtRequest == requestId) {
			_delayedShowAtRequest = 0;
		}
		return;
	}

	auto count = 0;
	const QVector<MTPMessage> emptyList, *histList = &emptyList;
	switch (messages.type()) {
	case mtpc_messages_messages: {
		auto &d(messages.c_messages_messages());
		_history->owner().processUsers(d.vusers());
		_history->owner().processChats(d.vchats());
		peer->processTopics(d.vtopics());
		histList = &d.vmessages().v;
		count = histList->size();
	} break;
	case mtpc_messages_messagesSlice: {
		auto &d(messages.c_messages_messagesSlice());
		_history->owner().processUsers(d.vusers());
		_history->owner().processChats(d.vchats());
		peer->processTopics(d.vtopics());
		histList = &d.vmessages().v;
		count = d.vcount().v;
	} break;
	case mtpc_messages_channelMessages: {
		auto &d(messages.c_messages_channelMessages());
		_history->owner().processUsers(d.vusers());
		_history->owner().processChats(d.vchats());
		if (const auto channel = peer->asChannel()) {
			channel->ptsReceived(d.vpts().v);
		} else {
			LOG(("API Error: received messages.channelMessages when "
				"no channel was passed! (HistoryWidget::messagesReceived)"));
		}
		peer->processTopics(d.vtopics());
		histList = &d.vmessages().v;
		count = d.vcount().v;
	} break;
	case mtpc_messages_messagesNotModified: {
		LOG(("API Error: received messages.messagesNotModified! "
			"(HistoryWidget::messagesReceived)"));
	} break;
	}

	if (_preloadRequest == requestId) {
		addMessagesToFront(peer, *histList);
		_preloadRequest = 0;
		preloadHistoryIfNeeded();
	} else if (_preloadDownRequest == requestId) {
		addMessagesToBack(peer, *histList);
		_preloadDownRequest = 0;
		preloadHistoryIfNeeded();
		if (_history->loadedAtBottom()) {
			checkActivation();
		}
	} else if (_firstLoadRequest == requestId) {
		if (toMigrated) {
			_history->clear(History::ClearType::Unload);
		} else if (_migrated) {
			_migrated->clear(History::ClearType::Unload);
		}
		addMessagesToFront(peer, *histList);
		_firstLoadRequest = 0;
		if (_firstLoadFromTheStart && !toMigrated) {
			_history->markLoadedAtTop();
		}
		if (_history->loadedAtTop() && _history->isEmpty() && count > 0) {
			firstLoadMessages();
			return;
		}

		historyLoaded();
		injectSponsoredMessages();
	} else if (_delayedShowAtRequest == requestId) {
		if (toMigrated) {
			_history->clear(History::ClearType::Unload);
		} else if (_migrated) {
			_migrated->clear(History::ClearType::Unload);
		}

		clearAllLoadRequests();
		_firstLoadRequest = -1; // hack - don't updateListSize yet
		_history->getReadyFor(_delayedShowAtMsgId);
		if (_history->isEmpty()) {
			addMessagesToFront(peer, *histList);
			if (_firstLoadFromTheStart && !toMigrated) {
				_history->markLoadedAtTop();
			}
		}
		_firstLoadRequest = 0;

		if (_history->loadedAtTop()
			&& _history->isEmpty()
			&& count > 0) {
			firstLoadMessages();
			return;
		}
		const auto skipId = (_migrated && _delayedShowAtMsgId < 0)
			? FullMsgId(_migrated->peer->id, -_delayedShowAtMsgId)
			: (_delayedShowAtMsgId > 0)
			? FullMsgId(_history->peer->id, _delayedShowAtMsgId)
			: FullMsgId();
		if (skipId) {
			_cornerButtons.skipReplyReturn(skipId);
		}

		_delayedShowAtRequest = 0;
		setMsgId(_delayedShowAtMsgId, _delayedShowAtMsgParams);
		historyLoaded();
	}
	if (session().supportMode()) {
		crl::on_main(this, [=] { checkSupportPreload(); });
	}
}

void HistoryWidget::historyLoaded() {
	_historyInited = false;
	doneShow();
}

bool HistoryWidget::clearMaybeSendStart() {
	if (!_showAndMaybeSendStart || !_history) {
		return false;
	} else if (!_history->peer->isFullLoaded()) {
		_history->peer->updateFull();
		return false;
	}
	_showAndMaybeSendStart = false;
	if (const auto user = _history ? _history->peer->asUser() : nullptr) {
		if (user->blockStatus() == PeerData::BlockStatus::NotBlocked) {
			if (const auto info = user->botInfo.get()) {
				if (!info->startToken.isEmpty()) {
					return true;
				}
			}
		}
	}
	return false;
}

void HistoryWidget::windowShown() {
	updateControlsGeometry();
}

bool HistoryWidget::markingMessagesRead() const {
	return markingContentsRead() && !session().supportMode();
}

bool HistoryWidget::markingContentsRead() const {
	return _history
		&& _list
		&& _historyInited
		&& !_firstLoadRequest
		&& !_delayedShowAtRequest
		&& !_showAnimation
		&& controller()->widget()->markingAsRead();
}

void HistoryWidget::checkActivation() {
	if (_list) {
		_list->checkActivation();
	}
}
