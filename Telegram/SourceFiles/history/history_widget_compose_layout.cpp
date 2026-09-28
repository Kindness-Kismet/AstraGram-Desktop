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

void HistoryWidget::setGeometryWithTopMoved(
		const QRect &newGeometry,
		int topDelta) {
	_topDelta = topDelta;
	bool willBeResized = (size() != newGeometry.size());
	if (geometry() != newGeometry) {
		auto weak = base::make_weak(this);
		setGeometry(newGeometry);
		if (!weak) {
			return;
		}
	}
	if (!willBeResized) {
		resizeEvent(nullptr);
	}
	_topDelta = 0;
}

Dialogs::EntryState HistoryWidget::computeDialogsEntryState() const {
	return Dialogs::EntryState{
		.key = _history,
		.section = Dialogs::EntryState::Section::History,
		.currentReplyTo = replyTo(),
		.currentSuggest = suggestOptions(),
	};
}

void HistoryWidget::refreshJoinChannelText() {
	if (const auto channel = _peer ? _peer->asChannel() : nullptr) {
		_joinChannel->setText((channel->isBroadcast()
			? tr::lng_profile_join_channel(tr::now)
			: (channel->requestToJoin() && !channel->amCreator())
			? tr::lng_profile_apply_to_join_group(tr::now)
			: tr::lng_profile_join_group(tr::now)).toUpper());
	}
}

void HistoryWidget::refreshGiftToChannelShown() {
	if (!_giftToChannel || !_peer) {
		return;
	}
	// AyuGram: hide gift button almost everywhere
	// still accessible via the menu in peer window
	const auto channel = _peer->asChannel();
	_giftToChannel->setVisible(channel
		&& channel->isBroadcast()
		&& channel->stargiftsAvailable()
		&& isExteraPeer(getBareID(channel)));
}

void HistoryWidget::refreshDirectMessageShown() {
	if (!_directMessage || !_peer) {
		return;
	}
	const auto channel = _peer->asChannel();
	const auto monoforum = channel ? channel->broadcastMonoforum() : nullptr;
	const auto visible = monoforum && !monoforum->monoforumDisabled();
	_directMessage->setVisible(visible);
	if (visible) {
		using Flags = Data::Flags<ChannelDataFlags>;
		_directMessageLifetime = monoforum->flagsValue(
		) | rpl::skip(
			1
		) | rpl::on_next([=](Flags::Change change) {
			if (change.diff & ChannelDataFlag::MonoforumDisabled) {
				refreshDirectMessageShown();
			}
		});
	}
}

void HistoryWidget::refreshTopBarActiveChat() {
	const auto state = computeDialogsEntryState();
	_topBar->setActiveChat(state, _history->sendActionPainter());
	if (state.key) {
		controller()->setDialogsEntryState(state);
	}
}

void HistoryWidget::refreshTabbedPanel() {
	if (_peer && controller()->hasTabbedSelectorOwnership()) {
		createTabbedPanel();
	} else {
		setTabbedPanel(nullptr);
	}
}

bool HistoryWidget::searchInChatEmbedded(
		QString query,
		Dialogs::Key chat,
		PeerData *searchFrom) {
	const auto peer = chat.peer(); // windows todo
	const auto archiveWindow = (controller()->windowId().type
		== Window::SeparateType::Archive);
	if (!peer
		|| ((Window::SeparateId(peer) != controller()->windowId())
			&& !controller()->isPrimary()
			&& !archiveWindow)) {
		return false;
	} else if (_peer != peer) {
		const auto weak = base::make_weak(this);
		controller()->showPeerHistory(peer);
		if (!weak) {
			return false;
		}
	}
	if (_peer != peer) {
		return false;
	} else if (_composeSearch) {
		_composeSearch->setQuery(query);
		_composeSearch->setInnerFocus();
		return true;
	}
	switchToSearch(query);
	return true;
}

void HistoryWidget::switchToSearch(QString query) {
	const auto search = crl::guard(_list, [=] {
		if (!_peer) {
			return;
		}
		const auto update = [=] {
			updateControlsVisibility();
			updateBotKeyboard();
			updateFieldPlaceholder();

			updateControlsGeometry();
		};
		const auto from = (PeerData*)nullptr;
		_composeSearch = std::make_unique<HistoryView::ComposeSearch>(
			this,
			controller(),
			_history,
			from,
			query);

		update();
		setInnerFocus();

		using Activation = HistoryView::ComposeSearch::Activation;
		_composeSearch->activations(
		) | rpl::on_next([=](Activation activation) {
			const auto item = activation.item;
			auto params = ::Window::SectionShow(
				::Window::SectionShow::Way::ClearStack);
			params.highlight = Window::SearchHighlightId(activation.query);
			controller()->showPeerHistory(
				item->history()->peer->id,
				params,
				item->fullId().msg);
		}, _composeSearch->lifetime());

		_composeSearch->destroyRequests(
		) | rpl::take(1) | rpl::on_next([=] {
			_composeSearch = nullptr;

			update();
			setInnerFocus();
		}, _composeSearch->lifetime());
	});
	if (!preventsClose(search)) {
		search();
	}
}

bool HistoryWidget::kbWasHidden() const {
	return _history
		&& (_keyboard->forMsgId()
			== FullMsgId(
				_history->peer->id,
				_history->lastKeyboardHiddenId));
}

bool HistoryWidget::forceReplyPending() const {
	return _keyboard->forceReply()
		&& (!_history
			|| !_history->lastKeyboardUsed
			|| (_keyboard->forMsgId()
				!= FullMsgId(_history->peer->id, _history->lastKeyboardId)));
}

void HistoryWidget::showKeyboardHideButton() {
	_botKeyboardHide->setVisible(!_peer->isUser()
		|| !_keyboard->persistent());
}

void HistoryWidget::toggleKeyboard(bool manual) {
	const auto &settings = AyuSettings::getInstance();

	const auto fieldEnabled = canWriteMessage() && !_showAnimation;
	if (_kbShown || _kbReplyTo) {
		_botKeyboardHide->hide();
		if (_kbShown) {
			if (fieldEnabled) {
				_botKeyboardShow->show();
			}
			if (manual && _history) {
				_history->lastKeyboardHiddenId = _keyboard->forMsgId().msg;
			}

			_kbScroll->hide();
			_kbShown = false;

			_field->setMaxHeight(computeMaxFieldHeight());

			_kbReplyTo = nullptr;
			if (!readyToForward()
				&& !_previewDrawPreview
				&& !_editMsgId
				&& !_replyTo
				&& !_suggestOptions) {
				_fieldBarCancel->hide();
				updateMouseTracking();
			}
		} else {
			if (_history) {
				_history->clearLastKeyboard();
			} else {
				updateBotKeyboard();
			}
		}
	} else if (!_keyboard->hasMarkup() && _keyboard->forceReply()) {
		_botKeyboardHide->hide();
		_botKeyboardShow->hide();
		_kbScroll->hide();
		_kbShown = false;

		_field->setMaxHeight(computeMaxFieldHeight());

		_kbReplyTo = (false
			|| _peer->isChat()
			|| _peer->isChannel()
			|| _keyboard->forceReply())
			? session().data().message(_keyboard->forMsgId())
			: nullptr;
		if (_kbReplyTo && !_editMsgId && !_replyTo && fieldEnabled) {
			updateReplyToName();
			updateReplyEditText(_kbReplyTo);
		}
		if (manual && _history) {
			_history->lastKeyboardHiddenId = 0;
		}
	} else if (fieldEnabled) {
		showKeyboardHideButton();
		_botKeyboardShow->hide();
		_kbScroll->show();
		_kbShown = true;

		const auto maxheight = computeMaxFieldHeight();
		const auto kbheight = std::min(
			_keyboard->height(),
			maxheight - (maxheight / 2));
		_field->setMaxHeight(maxheight - kbheight);

		_kbReplyTo = (false
			|| _peer->isChat()
			|| _peer->isChannel()
			|| forceReplyPending())
			? session().data().message(_keyboard->forMsgId())
			: nullptr;
		if (_kbReplyTo && !_editMsgId && !_replyTo) {
			updateReplyToName();
			updateReplyEditText(_kbReplyTo);
		}
		if (manual && _history) {
			_history->lastKeyboardHiddenId = 0;
		}
	}
	updateControlsGeometry();
	updateAiButtonVisibility();
	updateSendAsFileVisibility();
	updateExpandButtonVisibility();
	updateFieldPlaceholder();
	SWITCH_BUTTON(_tabbedSelectorToggle, _botKeyboardHide->isHidden()
		&& canWriteMessage()
		&& !_showAnimation && settings.showEmojiButtonInMessageField());
	updateField();
}

void HistoryWidget::setMembersShowAreaActive(bool active) {
	if (!active) {
		_membersDropdownShowTimer.cancel();
	}
	if (active && _peer && (_peer->isChat() || _peer->isMegagroup())) {
		if (_membersDropdown) {
			_membersDropdown->otherEnter();
		} else if (!_membersDropdownShowTimer.isActive()) {
			_membersDropdownShowTimer.callOnce(kShowMembersDropdownTimeoutMs);
		}
	} else if (_membersDropdown) {
		_membersDropdown->otherLeave();
	}
}

void HistoryWidget::showMembersDropdown() {
	if (!_peer) {
		return;
	}
	if (!_membersDropdown) {
		_membersDropdown.create(this, st::membersInnerDropdown);
		_membersDropdown->setOwnedWidget(
			object_ptr<HistoryView::GroupMembersWidget>(this, controller(), _peer));
		_membersDropdown->resizeToWidth(st::membersInnerWidth);

		_membersDropdown->setMaxHeight(countMembersDropdownHeightMax());
		_membersDropdown->moveToLeft(0, _topBar->height());
		_membersDropdown->setHiddenCallback([this] {
			_membersDropdown.destroyDelayed();
		});
	}
	_membersDropdown->otherEnter();
}

bool HistoryWidget::pushTabbedSelectorToThirdSection(
		not_null<Data::Thread*> thread,
		const Window::SectionShow &params) {
	if (!_tabbedPanel) {
		return true;
	} else if (!Data::CanSendAnyOf(
			thread,
			Data::TabbedPanelSendRestrictions())) {
		Core::App().settings().setTabbedReplacedWithInfo(true);
		controller()->showPeerInfo(thread, params.withThirdColumn());
		return false;
	}
	Core::App().settings().setTabbedReplacedWithInfo(false);
	controller()->resizeForThirdSection();
	controller()->showSection(
		std::make_shared<ChatHelpers::TabbedMemento>(),
		params.withThirdColumn());
	return true;
}

bool HistoryWidget::returnTabbedSelector() {
	createTabbedPanel();
	moveFieldControls();
	return true;
}

void HistoryWidget::createTabbedPanel() {
	setTabbedPanel(std::make_unique<TabbedPanel>(
		this,
		controller(),
		controller()->tabbedSelector()));
}

void HistoryWidget::setTabbedPanel(std::unique_ptr<TabbedPanel> panel) {
	_tabbedPanel = std::move(panel);
	if (const auto raw = _tabbedPanel.get()) {
		_tabbedSelectorToggle->installEventFilter(raw);
		_tabbedSelectorToggle->setColorOverrides(nullptr, nullptr, nullptr);
	} else {
		_tabbedSelectorToggle->setColorOverrides(
			&st::historyAttachEmojiActive,
			&st::historyRecordVoiceFgActive,
			&st::historyRecordVoiceRippleBgActive);
	}
}

bool HistoryWidget::preventsClose(Fn<void()> &&continueCallback) const {
	if (_voiceRecordBar->isActive()) {
		_voiceRecordBar->showDiscardBox(std::move(continueCallback));
		return true;
	}
	return false;
}

void HistoryWidget::toggleTabbedSelectorMode() {
	if (!_history) {
		return;
	}
	if (_tabbedPanel) {
		if (controller()->canShowThirdSection()
			&& !controller()->adaptive().isOneColumn()) {
			Core::App().settings().setTabbedSelectorSectionEnabled(true);
			Core::App().saveSettingsDelayed();
			pushTabbedSelectorToThirdSection(
				_history,
				Window::SectionShow::Way::ClearStack);
		} else {
			_tabbedPanel->toggleAnimated();
		}
	} else {
		controller()->closeThirdSection();
	}
}

void HistoryWidget::recountChatWidth() {
	const auto layout = (width() < st::adaptiveChatWideWidth)
		? Window::Adaptive::ChatLayout::Normal
		: Window::Adaptive::ChatLayout::Wide;
	controller()->adaptive().setChatLayout(layout);
}

int HistoryWidget::fieldHeight() const {
	if (!_richDraftPreview->isHidden()) {
		return _richDraftPreview->height();
	}
	return (_canSendTexts || _editMsgId)
		? _field->height()
		: (st::historySendSize.height() - 2 * st::historySendPadding);
}

bool HistoryWidget::fieldOrDisabledShown() const {
	return !_field->isHidden() || !_richDraftPreview->isHidden() || _fieldDisabled;
}

bool HistoryWidget::fieldHasSendText() const {
	return !_field->isHidden() && HasSendText(_field);
}

bool HistoryWidget::hasSendableContent() const {
	return fieldHasSendText() || shouldShowRichDraftPreview();
}

bool HistoryWidget::hideExtraButtons() const {
	return _fieldCharsCountManager.isLimitExceeded()
		|| shouldShowRichDraftPreview();
}

bool HistoryWidget::hasEnoughLinesForAi() const {
	return _history
		&& !_voiceRecordBar->isActive()
		&& Ui::HasEnoughLinesForAi(&session(), _field);
}

bool HistoryWidget::hasEnoughLinesForExpand() const {
	return _history
		&& !_voiceRecordBar->isActive()
		&& Ui::HasEnoughLinesForExpand(_field);
}

bool HistoryWidget::textExceedsMaxSize() const {
	return _history
		&& !_voiceRecordBar->isActive()
		&& (_field->getLastText().size()
			> Data::PremiumLimits(&session()).messageLengthCurrent());
}

void HistoryWidget::updateAiButtonVisibility() {
	const auto hidden = !hasEnoughLinesForAi()
		|| !_send->isVisible()
		|| !_field->isVisible();
	if (_aiButton->isHidden() == hidden) {
		return;
	}
	const auto shown = !hidden;
	_aiButton->setVisible(shown);
	if (shown) {
		updateAiButtonGeometry();
	}
	if (_aiTooltipManager) {
		_aiTooltipManager->updateVisibility(shown);
	}
}

bool HistoryWidget::canShowRichEditor() const {
	return _history
		&& _send->isVisible()
		&& _field->isVisible()
		&& !_voiceRecordBar->isActive()
		&& (editingMessage() || _canSendTexts)
		&& (!textExceedsMaxSize() || editingMessage())
		&& !(_editMsgId
			&& _replyEditMsg
			&& _replyEditMsg->media()
			&& !_replyEditMsg->media()->webpage())
		&& Iv::Editor::CanAuthorRichMessages(&session());
}

void HistoryWidget::updateExpandButtonVisibility() {
	const auto hidden = !canShowRichEditor() || !hasEnoughLinesForExpand();
	if (_expand->isHidden() != hidden) {
		_expand->setVisible(!hidden);
	}
	updateExpandButtonGeometry();
}

void HistoryWidget::updateExpandButtonGeometry() {
	if (_expand->isHidden()) {
		return;
	}
	const auto x = _send->x() + _send->width() - _expand->width();
	_expand->move(QPoint(x, _field->y()) + st::historyAiComposeButtonPosition);
}

void HistoryWidget::initDiscardRichDraftButton() {
	_discardRichDraft->hide();
	_richDraftPreview->shownValue(
	) | rpl::on_next([=] {
		updateDiscardRichDraftVisibility();
	}, lifetime());
	_discardRichDraft->setAccessibleName(
		tr::lng_record_lock_discard(tr::now));
	_discardRichDraft->setClickedCallback([=] {
		if (!shouldShowRichDraftPreview()) {
			return;
		} else if (base::IsCtrlPressed()) {
			clearRichDraft();
			return;
		}
		controller()->show(Ui::MakeConfirmBox({
			.text = tr::lng_iv_editor_discard_draft_sure(tr::now),
			.confirmed = crl::guard(this, [=](Fn<void()> close) {
				clearRichDraft();
				close();
			}),
			.confirmText = tr::lng_record_lock_discard(),
			.confirmStyle = &st::attentionBoxButton,
		}));
	});
}

void HistoryWidget::updateDiscardRichDraftVisibility() {
	const auto top = _richDraftPreview->y()
		+ st::historyAiComposeButtonPosition.y();
	const auto hidden = _richDraftPreview->isHidden()
		|| !_send->isVisible()
		|| _voiceRecordBar->isActive()
		|| (top + _discardRichDraft->height() > _send->y());
	if (_discardRichDraft->isHidden() != hidden) {
		_discardRichDraft->setVisible(!hidden);
	}
	updateDiscardRichDraftGeometry();
}

void HistoryWidget::updateDiscardRichDraftGeometry() {
	if (_discardRichDraft->isHidden()) {
		return;
	}
	const auto anchor = _attachToggle->geometry();
	const auto x = anchor.x()
		+ (anchor.width() - _discardRichDraft->width()) / 2;
	const auto y = _richDraftPreview->y()
		+ st::historyAiComposeButtonPosition.y();
	_discardRichDraft->move(x, y);
}

void HistoryWidget::updateAiButtonGeometry() {
	if (_aiButton->isHidden()) {
		return;
	}
	const auto x = _attachToggle->x() - st::historyAiComposeButtonPosition.x();
	const auto y = _field->y() + st::historyAiComposeButtonPosition.y();
	_aiButton->move(x, y);
	if (_aiTooltipManager) {
		_aiTooltipManager->updateGeometry();
	}
}

void HistoryWidget::updateSendAsFileVisibility() {
	const auto hidden = !textExceedsMaxSize()
		|| _send->isHidden()
		|| _field->isHidden()
		|| editingMessage();
	if (_sendAsFile->isHidden() == hidden) {
		return;
	}
	_sendAsFile->setVisible(!hidden);
	if (!hidden) {
		updateSendAsFileGeometry();
	}
	if (_sendAsFileTooltipManager) {
		_sendAsFileTooltipManager->updateVisibility(!hidden);
	}
}

void HistoryWidget::updateSendAsFileGeometry() {
	if (_sendAsFile->isHidden()) {
		return;
	}
	const auto x = _attachToggle->x() - st::historyAiComposeButtonPosition.x();
	const auto y = _field->y() + st::historyAiComposeButtonPosition.y();
	_sendAsFile->move(x, y);
	if (_sendAsFileTooltipManager) {
		_sendAsFileTooltipManager->updateGeometry();
	}
}

void HistoryWidget::moveFieldControls() {
	const auto &settings = AyuSettings::getInstance();

	auto keyboardHeight = 0;
	auto bottom = height();
	auto maxKeyboardHeight = computeMaxFieldHeight() - fieldHeight();
	_keyboard->resizeToWidth(width(), maxKeyboardHeight);
	if (_kbShown) {
		keyboardHeight = std::min(_keyboard->height(), maxKeyboardHeight);
		bottom -= keyboardHeight;
		_kbScroll->setGeometryToLeft(0, bottom, width(), keyboardHeight);
	}
	// 胶囊底部留边,键盘弹出时悬在键盘上方
	bottom -= st::historyComposeCapsuleMargin;

// (_botMenu.button) (_attachToggle|_replaceMedia) (_sendAs) ---- _inlineResults ------------------------------ _tabbedPanel ------ _fieldBarCancel
// (_attachDocument|_attachPhoto) _field (_ttlInfo) (_scheduled) (_giftToUser) (_silent|_cmdStart|_kbShow) (_toggleSuggestPost) (_kbHide|_tabbedSelectorToggle) _send
// (_botStart|_unblock|_joinChannel|_muteUnmute|_reportMessages)

	auto buttonsBottom = bottom - _attachToggle->height();
	auto left = st::historyComposeCapsuleMargin + st::historyComposeCapsulePadding;
	if (_botMenu.button) {
		const auto skip = st::historyBotMenuSkip;
		_botMenu.button->moveToLeft(left + skip, buttonsBottom + skip);
		left += skip + _botMenu.button->width();
	}
	if (_replaceMedia) {
		_replaceMedia->moveToLeft(left, buttonsBottom);
	}
	if (settings.showAttachButtonInMessageField()) {
		_attachToggle->moveToLeft(left, buttonsBottom);
		left += _attachToggle->width();
	}
	if (_sendAs) {
		_sendAs->moveToLeft(left, buttonsBottom);
		left += _sendAs->width();
	}
	const auto fieldTop = bottom - fieldHeight() - st::historySendPadding;
	_field->moveToLeft(left, fieldTop);
	_richDraftPreview->moveToLeft(left, fieldTop);
	if (_fieldDisabled) {
		_fieldDisabled->moveToLeft(
			left,
			bottom - fieldHeight() - st::historySendPadding);
	}
	auto right = st::historyComposeCapsuleMargin + st::historyComposeCapsulePadding;
	// 发送圆与胶囊右端同心：圆的右侧留白等于底部留白。
	const auto sendInset = (st::historySend.inner.height
		- st::historySend.inner.width) / 2;
	_send->moveToRight(
		st::historyComposeCapsuleMargin + sendInset,
		buttonsBottom);
	right += _send->width();
	_voiceRecordBar->moveToLeft(0, bottom - _voiceRecordBar->height());
	_tabbedSelectorToggle->moveToRight(right, buttonsBottom);
	_botKeyboardHide->moveToRight(right, buttonsBottom);
	right += settings.showEmojiButtonInMessageField() || !_botKeyboardHide->isHidden() ? _botKeyboardHide->width() : 0;
	_botKeyboardShow->moveToRight(right, buttonsBottom);
	if (_silent) {
		_silent->moveToRight(right, buttonsBottom);
	}
	const auto kbShowShown = _history && !_kbShown && _keyboard->hasMarkup();
	if (kbShowShown || _silent) {
		right += _botKeyboardShow->width();
	}
	if (_toggleSuggestPost) {
		_toggleSuggestPost->moveToRight(right, buttonsBottom);
		right += _toggleSuggestPost->width();
	}
	if (_giftToUser) {
		_giftToUser->moveToRight(right, buttonsBottom);
		right += _giftToUser->width();
	}
	if (_scheduled) {
		_scheduled->moveToRight(right, buttonsBottom);
		right += _scheduled->width();
	}
	if (_ttlInfo) {
		_ttlInfo->move(width() - right - _ttlInfo->width(), buttonsBottom);
	}
	updateAiButtonGeometry();
	updateSendAsFileGeometry();
	updateExpandButtonGeometry();
	updateDiscardRichDraftGeometry();

	_fieldBarCancel->moveToRight(
		st::historyComposeCapsuleMargin + st::historyComposeCapsulePadding,
		_field->y() - st::historySendPadding - _fieldBarCancel->height());
	if (_inlineResults) {
		_inlineResults->moveBottom(_field->y() - st::historySendPadding);
	}
	if (_tabbedPanel) {
		_tabbedPanel->moveBottomRight(buttonsBottom, width());
	}
	if (_attachBotsMenu) {
		_attachBotsMenu->moveToLeft(
			0,
			buttonsBottom - _attachBotsMenu->height());
	}

	const auto actionButtonRect = myrtlrect(
		st::historyComposeCapsuleMargin,
		bottom - _botStart->height(),
		width() - 2 * st::historyComposeCapsuleMargin,
		_botStart->height());
	_botStart->setGeometry(actionButtonRect);
	_unblock->setGeometry(actionButtonRect);
	_joinChannel->setGeometry(actionButtonRect);
	_muteUnmute->setGeometry(actionButtonRect);
	_discuss->setGeometry(actionButtonRect);
	_reportMessages->setGeometry(actionButtonRect);
	if (_sendRestriction) {
		_sendRestriction->setGeometry(actionButtonRect);
	}
}

void HistoryWidget::updateFieldSize() {
	const auto &settings = AyuSettings::getInstance();

	const auto kbShowShown = _history && !_kbShown && _keyboard->hasMarkup();
	auto fieldWidth = width()
		- (settings.showAttachButtonInMessageField() ? _attachToggle->width() : 0)
		- 2 * (st::historyComposeCapsuleMargin + st::historyComposeCapsulePadding)
		- _send->width()
		- (settings.showEmojiButtonInMessageField() ? _tabbedSelectorToggle->width() : 0);
	if (_botMenu.button) {
		fieldWidth -= st::historyBotMenuSkip + _botMenu.button->width();
	}
	if (_sendAs) {
		fieldWidth -= _sendAs->width();
	}
	if (kbShowShown) {
		fieldWidth -= _botKeyboardShow->width();
	}
	if (_silent && !_silent->isHidden()) {
		fieldWidth -= _silent->width();
	}
	if (_toggleSuggestPost && !_toggleSuggestPost->isHidden()) {
		fieldWidth -= _toggleSuggestPost->width();
	}
	if (_giftToUser && !_giftToUser->isHidden()) {
		fieldWidth -= _giftToUser->width();
	}
	if (_scheduled && !_scheduled->isHidden()) {
		fieldWidth -= _scheduled->width();
	}
	if (_ttlInfo && _ttlInfo->isVisible() && settings.showAutoDeleteButtonInMessageField()) {
		fieldWidth -= _ttlInfo->width();
	}

	if (_fieldDisabled) {
		_fieldDisabled->resize(width(), st::historySendSize.height());
	}
	if (_field->width() != fieldWidth) {
		_field->resize(fieldWidth, _field->height());
	}
	[[maybe_unused]] const auto previewHeight = _richDraftPreview->resizeGetHeight(
		fieldWidth,
		st::historyComposeField.heightMin,
		computeMaxFieldHeight());
	moveFieldControls();
}

void HistoryWidget::clearInlineBot() {
	if (_inlineBot || _inlineLookingUpBot) {
		_inlineBot = nullptr;
		_inlineLookingUpBot = false;
		inlineBotChanged();
		_field->finishAnimating();
	}
	if (_inlineResults) {
		_inlineResults->clearInlineBot();
	}
	if (_autocomplete) {
		_autocomplete->requestRefresh();
	}
}

void HistoryWidget::inlineBotChanged() {
	bool isInlineBot = showInlineBotCancel();
	if (_isInlineBot != isInlineBot) {
		_isInlineBot = isInlineBot;
		updateFieldPlaceholder();
		updateFieldSubmitSettings();
		updateControlsVisibility();
	}
}

void HistoryWidget::fieldResized() {
	moveFieldControls();
	updateAiButtonVisibility();
	updateSendAsFileVisibility();
	updateExpandButtonVisibility();
	updateHistoryGeometry();
	updateField();
}

void HistoryWidget::fieldFocused() {
	if (_list) {
		_list->clearSelected(true);
		_list->hideElementOverlay();
	}
}

void HistoryWidget::updateFieldPlaceholder() {
	_voiceRecordBar->setPauseInsteadSend(_history
		&& _history->peer->starsPerMessageChecked() > 0);

	if (!_editMsgId && _inlineBot && !_inlineLookingUpBot) {
		_field->setPlaceholder(
			rpl::single(_inlineBot->botInfo->inlinePlaceholder.mid(1)),
			_inlineBotUsername.size() + 2);
		return;
	}

	const auto ephemeralReply = session().ephemeralMessages()
		.isEphemeralBotReply(replyTo().messageId);
	_field->setPlaceholder([&]() -> rpl::producer<QString> {
		const auto peer = _history ? _history->peer.get() : nullptr;
		if (_editMsgId) {
			return tr::lng_edit_message_text();
		} else if (!peer) {
			return tr::lng_message_ph();
		} else if ((_kbShown || _keyboard->forceReply())
			&& !_keyboard->placeholder().isEmpty()) {
			return rpl::single(_keyboard->placeholder());
		} else if (const auto stars = ephemeralReply
			? 0
			: peer->starsPerMessageChecked()) {
			return tr::lng_message_stars_ph(
				lt_count,
				rpl::single(stars * 1.));
		} else if (const auto channel = peer->asChannel()) {
			const auto topic = resolveReplyToTopic();
			const auto topicRootId = topic
				? topic->rootId()
				: channel->forum()
				? resolveReplyToTopicRootId()
				: MsgId();
			if (topicRootId) {
				auto title = rpl::single(topic
					? topic->title()
					: (topicRootId == Data::ForumTopic::kGeneralId)
					? u"General"_q
					: u"Topic"_q
				) | rpl::then(session().changes().topicUpdates(
					Data::TopicUpdate::Flag::Title
				) | rpl::filter([=](const Data::TopicUpdate &update) {
					return (update.topic->peer() == channel)
						&& (update.topic->rootId() == topicRootId);
				}) | rpl::map([=](const Data::TopicUpdate &update) {
					return update.topic->title();
				}));
				const auto phrase = replyTo().messageId
					? tr::lng_forum_reply_in
					: tr::lng_forum_message_in;
				return phrase(lt_topic, std::move(title));
			} else if (channel->isBroadcast()) {
				return session().data().notifySettings().silentPosts(channel)
					? tr::lng_broadcast_silent_ph()
					: tr::lng_broadcast_ph();
			} else if (channel->adminRights() & ChatAdminRight::Anonymous) {
				return tr::lng_send_anonymous_ph();
			} else {
				return tr::lng_message_ph();
			}
		} else if (const auto user = peer->asUser()) {
			if (const auto &info = user->botInfo) {
				if (info->forum() && !info->userCreatesTopics) {
					return tr::lng_bot_off_thread_ph();
				}
			}
			return tr::lng_message_ph();
		} else {
			return tr::lng_message_ph();
		}
	}());
	updateSendButtonType();
}

void HistoryWidget::updateBotKeyboard(History *h, bool force) {
	if (h && h != _history && h != _migrated) {
		return;
	}

	const auto &settings = AyuSettings::getInstance();

	const auto wasVisible = _kbShown || _kbReplyTo;
	const auto wasMsgId = _keyboard->forMsgId();
	auto changed = false;
	if ((_replyTo && !_replyEditMsg) || _editMsgId || !_history) {
		changed = _keyboard->updateMarkup(nullptr, force);
	} else if (_replyTo && _replyEditMsg) {
		changed = _keyboard->updateMarkup(_replyEditMsg, force);
	} else {
		const auto keyboardItem = _history->lastKeyboardId
			? session().data().message(
				_history->peer,
				_history->lastKeyboardId)
			: nullptr;
		changed = _keyboard->updateMarkup(keyboardItem, force);
	}
	const auto controlsChanged = refreshBotMenuButton();
	if (!changed) {
		if (controlsChanged) {
			updateControlsGeometry();
		}
		return;
	} else if (_keyboard->forMsgId() != wasMsgId) {
		_kbScroll->scrollTo({ 0, 0 });
	}

	const auto hasMarkup = _keyboard->hasMarkup();
	const auto forceReply = _keyboard->forceReply()
		&& (!_replyTo || !_replyEditMsg);
	if (hasMarkup || forceReply) {
		if (_keyboard->singleUse()
			&& _keyboard->hasMarkup()
			&& (_keyboard->forMsgId()
				== FullMsgId(_history->peer->id, _history->lastKeyboardId))
			&& _history->lastKeyboardUsed) {
			_history->lastKeyboardHiddenId = _history->lastKeyboardId;
		}
		if (!isSearching()
			&& !isBotStart()
			&& !isBlocked()
			&& _canSendMessages
			&& (wasVisible
				|| (_replyTo && _replyEditMsg)
				|| (!hasSendableContent() && !kbWasHidden()))) {
			if (!_showAnimation) {
				if (hasMarkup) {
					_kbScroll->show();
					_tabbedSelectorToggle->hide();
					showKeyboardHideButton();
				} else {
					_kbScroll->hide();
					SWITCH_BUTTON(_tabbedSelectorToggle, settings.showEmojiButtonInMessageField());
					_botKeyboardHide->hide();
				}
				_botKeyboardShow->hide();
			}
			const auto maxheight = computeMaxFieldHeight();
			const auto kbheight = hasMarkup
				? std::min(_keyboard->height(), maxheight - (maxheight / 2))
				: 0;
			_field->setMaxHeight(maxheight - kbheight);
			_kbShown = hasMarkup;
			_kbReplyTo = (_peer->isChat()
					|| _peer->isChannel()
					|| forceReplyPending())
				? session().data().message(_keyboard->forMsgId())
				: nullptr;
			if (_kbReplyTo && !_replyTo) {
				updateReplyToName();
				updateReplyEditText(_kbReplyTo);
			}
		} else {
			if (!_showAnimation) {
				_kbScroll->hide();
				SWITCH_BUTTON(_tabbedSelectorToggle, settings.showEmojiButtonInMessageField());
				_botKeyboardHide->hide();
				_botKeyboardShow->show();
			}
			_field->setMaxHeight(computeMaxFieldHeight());
			_kbShown = false;
			_kbReplyTo = nullptr;
			if (!readyToForward()
				&& !_previewDrawPreview
				&& !_replyTo
				&& !_suggestOptions) {
				_fieldBarCancel->hide();
				updateMouseTracking();
			}
		}
	} else {
		if (!_scroll->isHidden()) {
			_kbScroll->hide();
			//SWITCH_BUTTON(_tabbedSelectorToggle, settings.showEmojiButtonInMessageField);
			_tabbedSelectorToggle->show();
			_botKeyboardHide->hide();
			_botKeyboardShow->hide();
		}
		_field->setMaxHeight(computeMaxFieldHeight());
		_kbShown = false;
		_kbReplyTo = nullptr;
		if (!readyToForward()
			&& !_previewDrawPreview
			&& !_replyTo
			&& !_editMsgId
			&& !_suggestOptions) {
			_fieldBarCancel->hide();
			updateMouseTracking();
		}
	}
	refreshTopBarActiveChat();
	updateFieldPlaceholder();
	updateControlsGeometry();
	update();
}

void HistoryWidget::botCallbackSent(not_null<HistoryItem*> item) {
	if (!item->isRegular() || _peer != item->history()->peer) {
		return;
	}

	const auto keyId = _keyboard->forMsgId();
	const auto lastKeyboardUsed = (keyId == FullMsgId(_peer->id, item->id))
		&& (keyId == FullMsgId(_peer->id, _history->lastKeyboardId));

	session().data().requestItemRepaint(item);

	if (_replyTo.messageId == item->fullId()) {
		cancelReply();
	}
	if (_keyboard->singleUse()
		&& _keyboard->hasMarkup()
		&& lastKeyboardUsed) {
		if (_kbShown) {
			toggleKeyboard(false);
		}
		_history->lastKeyboardUsed = true;
	}
}

int HistoryWidget::computeMaxFieldHeight() const {
	const auto available = height()
		- _topBar->height()
		- (_paysStatus ? _paysStatus->bar().height() : 0)
		- (_contactStatus ? _contactStatus->bar().height() : 0)
		- (_businessBotStatus ? _businessBotStatus->bar().height() : 0)
		- (_sponsoredMessageBar ? _sponsoredMessageBar->height() : 0)
		- (_pinnedBar ? _pinnedBar->height() : 0)
		- (_groupCallBar ? _groupCallBar->height() : 0)
		- (_requestsBar ? _requestsBar->height() : 0)
		- ((_editMsgId
			|| replyTo()
			|| readyToForward()
			|| _previewDrawPreview)
			? st::historyReplyHeight
			: 0)
		- (2 * st::historySendPadding)
		- st::historyReplyHeight; // at least this height for history.
	return std::min(st::historyComposeFieldMaxHeight, available);
}

bool HistoryWidget::cornerButtonsIgnoreVisibility() {
	return _showAnimation != nullptr;
}

std::optional<bool> HistoryWidget::cornerButtonsDownShown() {
	if (!_list || _firstLoadRequest) {
		return false;
	}
	if (_voiceRecordBar->isLockPresent()
		|| _voiceRecordBar->isTTLButtonShown()) {
		return false;
	}
	if (!_history->loadedAtBottom() || _cornerButtons.replyReturn()) {
		return true;
	}
	const auto top = _scroll->scrollTop() + st::historyToDownShownAfter;
	if (top < _scroll->scrollTopMax()) {
		return true;
	}

	const auto haveUnreadBelowBottom = [&](History *history) {
		if (!_list
			|| !history
			|| history->unreadCount() <= 0
			|| !history->trackUnreadMessages()) {
			return false;
		}
		const auto unread = history->firstUnreadMessage();
		if (!unread) {
			return false;
		}
		const auto top = _list->itemTop(unread);
		return (top >= visibleScrollBottom());
	};
	if (haveUnreadBelowBottom(_history)
		|| haveUnreadBelowBottom(_migrated)) {
		return true;
	}
	return false;
}

bool HistoryWidget::cornerButtonsUnreadMayBeShown() {
	return !_firstLoadRequest && !_voiceRecordBar->isLockPresent();
}

bool HistoryWidget::cornerButtonsHas(HistoryView::CornerButtonType type) {
	return true;
}
