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

void HistoryWidget::setFieldText(
		const TextWithTags &textWithTags,
		TextUpdateEvents events,
		FieldHistoryAction fieldHistoryAction) {
	_textUpdateEvents = events;
	_field->setTextWithTags(textWithTags, fieldHistoryAction);
	auto cursor = _field->textCursor();
	cursor.movePosition(QTextCursor::End);
	_field->setTextCursor(cursor);
	_textUpdateEvents = TextUpdateEvent::SaveDraft
		| TextUpdateEvent::SendTyping;

	checkCharsCount();

	if (_preview) {
		_preview->checkNow(false);
	}
}

void HistoryWidget::clearFieldText(
		TextUpdateEvents events,
		FieldHistoryAction fieldHistoryAction) {
	setFieldText(TextWithTags(), events, fieldHistoryAction);
}

void HistoryWidget::replyToMessage(FullReplyTo id) {
	if (const auto item = session().data().message(id.messageId)) {
		if (CanSendReply(item) && !base::IsCtrlPressed()) {
			replyToMessage(item, id);
		} else if (item->allowsForward()) {
			const auto show = controller()->uiShow();
			HistoryView::Controls::ShowReplyToChatBox(show, id);
		} else {
			controller()->showToast(
				tr::lng_error_cant_reply_other(tr::now));
		}
	}
}

void HistoryWidget::replyToMessage(
		not_null<HistoryItem*> item,
		FullReplyTo fields) {
	if (isJoinChannel()) {
		return;
	}
	fields.messageId = item->fullId();
	_processingReplyTo = fields;
	_processingReplyItem = item;
	processReply();
}

void HistoryWidget::processReply() {
	const auto processContinue = [=] {
		return crl::guard(_list, [=] {
			if (!_peer || !_processingReplyTo) {
				return;
			} else if (!_processingReplyItem) {
				_processingReplyItem = _peer->owner().message(
					_processingReplyTo.messageId);
				if (!_processingReplyItem) {
					_processingReplyTo = {};
				} else {
					processReply();
				}
			}
		});
	};
	const auto processCancel = [=] {
		_processingReplyTo = {};
		_processingReplyItem = nullptr;
	};

	if (!_peer || !_processingReplyTo) {
		return processCancel();
	}
	cancelSuggestPost();
	if (!_processingReplyItem) {
		session().api().requestMessageData(
			session().data().peer(_processingReplyTo.messageId.peer),
			_processingReplyTo.messageId.msg,
			processContinue());
		return;
#if 0 // Now we can "reply" to old legacy group messages.
	} else if (_processingReplyItem->history() == _migrated) {
		if (_processingReplyItem->isService()) {
			controller()->showToast(tr::lng_reply_cant(tr::now));
		} else {
			const auto itemId = _processingReplyItem->fullId();
			controller()->show(
				Ui::MakeConfirmBox({
					.text = tr::lng_reply_cant_forward(),
					.confirmed = crl::guard(this, [=] {
						controller()->content()->setForwardDraft(
							_history,
							{ .ids = { 1, itemId } });
					}),
					.confirmText = tr::lng_selected_forward(),
				}));
		}
		return processCancel();
#endif
	} else if (!_processingReplyItem->isRegular()
		&& !CanReplyToEphemeral(_processingReplyItem)) {
		return processCancel();
	} else if (const auto forum = _peer->forum()
		; forum && _processingReplyItem->history() == _history) {
		const auto topicRootId = _processingReplyItem->topicRootId();
		using namespace Data;
		if (forum->topicDeleted(topicRootId)
			&& !(topicRootId == ForumTopic::kGeneralId && _peer->isBot())) {
			return processCancel();
		} else if (const auto topic = forum->topicFor(topicRootId)) {
			if (!Data::CanSendAnything(topic)) {
				return processCancel();
			}
		} else {
			forum->requestTopic(topicRootId, processContinue());
		}
	} else if (!Data::CanSendAnything(_peer)) {
		return processCancel();
	}
	setReplyFieldsFromProcessing();
}

void HistoryWidget::setReplyFieldsFromProcessing() {
	if (!_processingReplyTo || !_processingReplyItem) {
		return;
	}

	if (_composeSearch) {
		_composeSearch->hideAnimated();
	}

	const auto id = base::take(_processingReplyTo);
	const auto item = base::take(_processingReplyItem);
	if (_editMsgId) {
		if (const auto localDraft = _history->localDraft({}, {})) {
			localDraft->reply = id;
			localDraft->suggest = SuggestOptions();
		} else {
			_history->setLocalDraft(std::make_unique<Data::Draft>(
				TextWithTags(),
				id,
				SuggestOptions(),
				MessageCursor(),
				Data::WebPageDraft()));
		}
	} else {
		_replyEditMsg = item;
		_replyTo = id;
		if (_replyTo) {
			if (const auto i = session().data().message(_replyTo.messageId)) {
				if (const auto media = i->media()) {
					using namespace SendMenu;
					const auto type = media->hasSpoiler()
						? Action{ .type = Action::Type::SpoilerOn }
						: Action{ .type = Action::Type::SpoilerOff };
					_mediaEditManager.apply(type);
				}
			}
		} else {
			_mediaEditManager.cancel();
		}
		cancelSuggestPost();
		updateReplyEditText(_replyEditMsg);
		updateCanSendMessage();
		updateBotKeyboard();
		updateReplyToName();
		updateControlsVisibility();
		updateControlsGeometry();
		updateField();
		refreshTopBarActiveChat();
	}

	saveDraftWithTextNow();
	setInnerFocus();
}

void HistoryWidget::editMessage(
		not_null<HistoryItem*> item,
		const TextSelection &selection) {
	if (Iv::Editor::ActivateEditWindowFor(&session(), item->fullId())) {
		return;
	}
	if (item->richPage()) {
		Iv::Editor::ShowEditBox(controller(), item);
		return;
	} else if (_chooseTheme) {
		toggleChooseChatTheme(_peer);
	} else if (_voiceRecordBar->isActive()) {
		controller()->showToast(tr::lng_edit_caption_voice(tr::now));
		return;
	} else if (const auto media = item->media()) {
		if (media->todolist()) {
			Window::PeerMenuEditTodoList(controller(), item);
			return;
		}
	}
	if (_composeSearch) {
		_composeSearch->hideAnimated();
	}

	if (isRecording()) {
		// Just fix some strange inconsistency.
		_send->clearState();
	}
	if (!_editMsgId) {
		const auto suggest = suggestOptions();
		if (_replyTo || suggest.exists || !_field->empty()) {
			_history->setLocalDraft(std::make_unique<Data::Draft>(
				_field,
				_replyTo,
				suggest,
				_preview->draft()));
		} else {
			_history->clearLocalDraft(MsgId(), PeerId());
		}
	}

	const auto editData = PrepareEditText(item);
	const auto cursor = MessageCursor {
		int(editData.text.size()),
		int(editData.text.size()),
		Ui::kQFixedMax
	};
	const auto previewDraft = Data::WebPageDraft::FromItem(item);
	_history->setLocalEditDraft(std::make_unique<Data::Draft>(
		editData,
		FullReplyTo{ item->fullId() },
		SuggestOptions(),
		cursor,
		previewDraft));
	applyDraft();

	updateBotKeyboard();

	if (fieldOrDisabledShown()) {
		_fieldBarCancel->show();
	}
	updateFieldPlaceholder();
	updateMouseTracking();
	updateReplyToName();
	updateControlsGeometry();
	updateField();
	SelectTextInFieldWithMargins(_field, selection);

	saveDraftWithTextNow();
	setInnerFocus();
}

void HistoryWidget::fillSenderUserpicMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<PeerData*> peer) {
	if (!_peer || _peer->isUser()) {
		// No need to offer View Profile / Send Message in private chat.
		return;
	}
	const auto inGroup = _peer && (_peer->isChat() || _peer->isMegagroup());
	Window::FillSenderUserpicMenu(
		controller(),
		peer,
		inGroup ? _peer : nullptr,
		(inGroup && _canSendTexts) ? _field.data() : nullptr,
		inGroup ? _peer->owner().history(_peer) : Dialogs::Key(),
		Ui::Menu::CreateAddActionCallback(menu));
}

void HistoryWidget::hidePinnedMessage() {
	Expects(_pinnedBar != nullptr);

	const auto id = _pinnedTracker->currentMessageId();
	if (!id.message) {
		return;
	}
	if (_peer->canPinMessages()) {
		Window::ToggleMessagePinned(controller(), id.message, false);
	} else {
		const auto callback = [=] {
			if (_pinnedTracker) {
				checkPinnedBarState();
			}
		};
		Window::HidePinnedBar(
			controller(),
			_peer,
			MsgId(0), // topicRootId
			PeerId(0), // monoforumPeerId
			crl::guard(this, callback));
	}
}

bool HistoryWidget::lastForceReplyReplied(const FullMsgId &replyTo) const {
	return _peer
		&& (replyTo.peer == _peer->id)
		&& _keyboard->forceReply()
		&& (_keyboard->forMsgId()
			== FullMsgId(_peer->id, _history->lastKeyboardId))
		&& _keyboard->forMsgId().msg == replyTo.msg;
}

bool HistoryWidget::lastForceReplyReplied() const {
	return _peer
		&& _keyboard->forceReply()
		&& _keyboard->forMsgId() == replyTo().messageId
		&& (_keyboard->forMsgId()
			== FullMsgId(_peer->id, _history->lastKeyboardId));
}

bool HistoryWidget::cancelReplyOrSuggest(bool lastKeyboardUsed) {
	const auto ok1 = cancelReply(lastKeyboardUsed);
	const auto ok2 = cancelSuggestPost();
	return ok1 || ok2;
}

bool HistoryWidget::cancelReply(bool lastKeyboardUsed) {
	bool wasReply = false;
	if (_replyTo) {
		wasReply = true;

		_processingReplyItem = _replyEditMsg = nullptr;
		_processingReplyTo = _replyTo = FullReplyTo();
		mouseMoveEvent(0);
		if (!readyToForward()
			&& !_previewDrawPreview
			&& !_kbReplyTo
			&& !_suggestOptions) {
			_fieldBarCancel->hide();
			updateMouseTracking();
		}
		updateBotKeyboard();
		refreshTopBarActiveChat();
		updateCanSendMessage();
		updateControlsVisibility();
		updateControlsGeometry();
		update();
	}
	if (const auto localDraft
			= (_history ? _history->localDraft({}, {}) : nullptr)) {
		if (localDraft->reply) {
			if (localDraft->textWithTags.text.isEmpty()) {
				_history->clearLocalDraft(MsgId(), PeerId());
			} else {
				localDraft->reply = {};
			}
		}
	}
	if (wasReply) {
		saveDraftWithTextNow();
	}
	if (!_editMsgId
		&& _keyboard->forceReply()
		&& lastKeyboardUsed) {
		if (!_keyboard->hasMarkup()) {
			if (_kbReplyTo) {
				toggleKeyboard(false);
			}
		} else if (_history) {
			_history->lastKeyboardUsed = true;
			if (_keyboard->singleUse() && _kbShown) {
				toggleKeyboard(false);
			} else if (base::take(_kbReplyTo)) {
				if (!readyToForward()
					&& !_previewDrawPreview
					&& !_replyTo
					&& !_suggestOptions) {
					_fieldBarCancel->hide();
					updateMouseTracking();
				}
				updateControlsGeometry();
				update();
			}
		}
	}
	return wasReply;
}

int HistoryWidget::countMembersDropdownHeightMax() const {
	auto result = height()
		- rect::m::sum::v(st::membersInnerDropdown.padding);
	result -= _tabbedSelectorToggle->height();
	accumulate_min(result, st::membersInnerHeightMax);
	return result;
}

void HistoryWidget::cancelEdit() {
	if (!_editMsgId) {
		return;
	}

	_canReplaceMedia = _canAddMedia = false;
	_photoEditMedia = nullptr;
	updateReplaceMediaButton();
	_replyEditMsg = nullptr;
	setEditMsgId(0);
	_history->clearLocalEditDraft(MsgId(), PeerId());
	cancelSuggestPost();
	applyDraft();

	if (_saveEditMsgRequestId) {
		_history->session().api().request(_saveEditMsgRequestId).cancel();
		_saveEditMsgRequestId = 0;
	}

	saveDraftWithTextNow();

	mouseMoveEvent(nullptr);
	if (!readyToForward()
		&& !_previewDrawPreview
		&& !replyTo()
		&& !_suggestOptions) {
		_fieldBarCancel->hide();
		updateMouseTracking();
	}

	auto old = _textUpdateEvents;
	_textUpdateEvents = 0;
	fieldChanged();
	_textUpdateEvents = old;

	updateControlsVisibility();
	updateBotKeyboard();
	updateFieldPlaceholder();

	updateControlsGeometry();
	update();
}

void HistoryWidget::cancelFieldAreaState() {
	controller()->hideLayer();
	if (_previewDrawPreview) {
		_preview->apply({ .removed = true });
	} else if (_editMsgId) {
		cancelEdit();
	} else if (_replyTo) {
		cancelReply();
	} else if (readyToForward()) {
		_history->setForwardDraft(MsgId(), PeerId(), {});
	} else if (_kbReplyTo) {
		toggleKeyboard();
	} else if (_suggestOptions) {
		cancelSuggestPost();
	}
}

bool HistoryWidget::cancelSuggestPost() {
	if (!_suggestOptions) {
		return false;
	}
	_suggestOptions = nullptr;
	updateControlsVisibility();
	updateControlsGeometry();
	saveDraftWithTextNow();
	return true;
}

void HistoryWidget::fullInfoUpdated() {
	auto refresh = false;
	if (_list) {
		if (updateCanSendMessage()) {
			refresh = true;
		}
		if (_autocomplete) {
			_autocomplete->requestRefresh();
		}
		_list->refreshAboutView();
		_list->updateBotInfo();

		handlePeerUpdate();
		checkSuggestToGigagroup();

		const auto hasNonEmpty = _history->findFirstNonEmpty();
		const auto readyForBotStart = hasNonEmpty
			|| (_history->loadedAtTop() && _history->loadedAtBottom());
		if (readyForBotStart && clearMaybeSendStart() && hasNonEmpty) {
			sendBotStartCommand();
		}
		refreshGiftToChannelShown();
		refreshDirectMessageShown();
	}
	if (updateCmdStartShown()) {
		refresh = true;
	} else if (!_scroll->isHidden() && _unblock->isHidden() == isBlocked()) {
		refresh = true;
	}
	if (_history
			&& HistoryView::SubsectionTabs::UsedFor(_history)
			&& !_subsectionTabs) {
		validateSubsectionTabs();
	}
	if (refresh) {
		updateControlsVisibility();
		updateControlsGeometry();
	}
}

void HistoryWidget::handlePeerUpdate() {
	bool resize = false;
	updateSendRestriction();
	updateHistoryGeometry();
	if (_peer->isChat() && _peer->asChat()->noParticipantInfo()) {
		session().api().requestFullPeer(_peer);
	} else if (_peer->isUser()
		&& ((_peer->asUser()->blockStatus() == UserData::BlockStatus::Unknown)
			|| (_peer->asUser()->callsStatus()
				== UserData::CallsStatus::Unknown))) {
		session().api().requestFullPeer(_peer);
	} else if (auto channel = _peer->asMegagroup()) {
		if (channel->mgInfo->botStatus == Data::BotStatus::Unknown) {
			session().api().chatParticipants().requestBots(channel);
		}
		if (!channel->mgInfo->adminsLoaded) {
			session().api().chatParticipants().requestAdmins(channel);
		}
	}
	if (!_showAnimation) {
		const auto blockChanged = (_unblock->isHidden() == isBlocked());
		if (blockChanged
			|| ((!isBlocked() && _joinChannel->isHidden() == isJoinChannel())
				|| (isMuteUnmute() && _discuss->isHidden() == hasDiscussionGroup()))) {
			resize = true;
		}
		if (updateCanSendMessage()) {
			resize = true;
		}
		if (blockChanged) {
			_list->refreshAboutView(true);
			_list->updateBotInfo();
		}
		updateControlsVisibility();
		if (resize) {
			updateControlsGeometry();
		}
	}
}

bool HistoryWidget::updateCanSendMessage() {
	if (!_peer) {
		return false;
	}
	const auto topic = resolveReplyToTopic();
	const auto allWithoutPolls = Data::AllSendRestrictions()
		& ~ChatRestriction::SendPolls;
	const auto onlyReplies = _peer->amMonoforumAdmin();
	const auto restrictedOnlyReplies = onlyReplies
		&& (!_replyTo.messageId || _replyTo.messageId.peer != _peer->id);
	const auto newCanSendMessages = restrictedOnlyReplies
		? false
		: topic
		? Data::CanSendAnyOf(topic, allWithoutPolls)
		: Data::CanSendAnyOf(_peer, allWithoutPolls);
	const auto newCanSendTexts = restrictedOnlyReplies
		? false
		: topic
		? Data::CanSend(topic, ChatRestriction::SendOther)
		: Data::CanSend(_peer, ChatRestriction::SendOther);
	if (_canSendMessages == newCanSendMessages
		&& _canSendTexts == newCanSendTexts) {
		return false;
	}
	_canSendMessages = newCanSendMessages;
	_canSendTexts = newCanSendTexts;
	if (!_canSendMessages) {
		cancelReplyOrSuggest();
	}
	refreshSuggestPostToggle();
	refreshScheduledToggle();
	refreshSendGiftToggle();
	refreshSilentToggle();
	return true;
}

void HistoryWidget::messageDataReceived(
		not_null<PeerData*> peer,
		MsgId msgId) {
	if (!_peer || _peer != peer || !msgId) {
		return;
	} else if (_editMsgId == msgId
		|| (_replyTo.messageId == FullMsgId(peer->id, msgId))) {
		updateReplyEditTexts(true);
		if (_editMsgId == msgId) {
			_preview->setDisabled(_editMsgId
				&& _replyEditMsg
				&& _replyEditMsg->media()
				&& !_replyEditMsg->media()->webpage());
		}
	}
}

void HistoryWidget::updateReplyEditText(not_null<HistoryItem*> item) {
	const auto context = Core::TextContext({
		.session = &session(),
		.repaint = [=] { updateField(); },
	});
	const auto text = [&] {
		const auto media = (_replyTo.todoItemId
				|| !_replyTo.pollOption.isEmpty())
			? item->media()
			: nullptr;
		if (const auto todolist = media ? media->todolist() : nullptr) {
			const auto i = ranges::find(
				todolist->items,
				_replyTo.todoItemId,
				&TodoListItem::id);
			if (i != end(todolist->items)) {
				return i->text;
			}
		}
		if (const auto poll = media ? media->poll() : nullptr) {
			if (const auto answer = poll->answerByOption(
					_replyTo.pollOption)) {
				return answer->text;
			}
		}
		return (_editMsgId || _replyTo.quote.empty())
			? item->inReplyText()
			: _replyTo.quote;
	}();
	_replyEditMsgText.setMarkedText(
		st::defaultTextStyle,
		text,
		Ui::DialogTextOptions(),
		context);
	if (fieldOrDisabledShown() || isRecording()) {
		_fieldBarCancel->show();
		updateMouseTracking();
	}
}

void HistoryWidget::updateReplyEditTexts(bool force) {
	if (!force) {
		if (_replyEditMsg || (!_editMsgId && !_replyTo)) {
			return;
		}
	}
	if (!_replyEditMsg && _peer) {
		_replyEditMsg = session().data().message(
			_editMsgId ? _peer->id : _replyTo.messageId.peer,
			_editMsgId ? _editMsgId : _replyTo.messageId.msg);
		if (!_editMsgId) {
			updateFieldPlaceholder();
		}
	}
	if (_replyEditMsg) {
		const auto richPage = _replyEditMsg->richPage();
		const auto editMedia = _editMsgId
			? _replyEditMsg->media()
			: nullptr;
		if (_editMsgId && _replyEditMsg && !richPage) {
			_mediaEditManager.start(_replyEditMsg);
		} else {
			_mediaEditManager.cancel();
		}
		_canReplaceMedia = _editMsgId
			&& !richPage
			&& _replyEditMsg->allowsEditMedia();
		if (_canReplaceMedia && editMedia && editMedia->allowsEditMedia()) {
			_canAddMedia = false;
		} else {
			_canAddMedia = base::take(_canReplaceMedia);
		}
		if (_canReplaceMedia || _canAddMedia) {
			// Invalidate the button, maybe icon has changed.
			_replaceMedia.destroy();
		}
		_photoEditMedia = (_canReplaceMedia
			&& editMedia->photo()
			&& !editMedia->photo()->isNull())
			? editMedia->photo()->createMediaView()
			: nullptr;
		if (_photoEditMedia) {
			_photoEditMedia->wanted(
				Data::PhotoSize::Large,
				_replyEditMsg->fullId());
		}
		if (updateReplaceMediaButton()) {
			updateControlsVisibility();
			updateControlsGeometry();
		}
		updateReplyEditText(_replyEditMsg);
		updateBotKeyboard();
		updateReplyToName();
		updateField();
	} else if (force) {
		if (_editMsgId) {
			cancelEdit();
		} else {
			cancelReply();
		}
	}
}

void HistoryWidget::updateForwarding() {
	_forwardPanel->update(_history, _history
		? _history->resolveForwardDraft(MsgId(), PeerId())
		: Data::ResolvedForwardDraft());
	updateControlsVisibility();
	updateControlsGeometry();
}

void HistoryWidget::updateReplyToName() {
	if (!_history || _editMsgId) {
		return;
	} else if (!_replyEditMsg && (_replyTo || !_kbReplyTo)) {
		return;
	}
	const auto context = Core::TextContext({
		.session = &_history->session(),
		.customEmojiLoopLimit = 1,
	});
	const auto to = _replyEditMsg ? _replyEditMsg : _kbReplyTo;
	_replyToName.setMarkedText(
		st::fwdTextStyle,
		HistoryView::Reply::ComposePreviewName(_history, to, _replyTo),
		Ui::NameTextOptions(),
		context);
}

void HistoryWidget::updateField() {
	if (_repaintFieldScheduled) {
		return;
	}
	_repaintFieldScheduled = true;
	_composeSurface->update();
	const auto fieldAreaTop = visibleScrollGeometry().bottom() + 1;
	rtlupdate(0, fieldAreaTop, width(), height() - fieldAreaTop);
}
