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
#include "extras/ui/controls/attach_menu.h"
#include "extras/features/filters/filters_cache_controller.h"
#include "extras/utils/telegram_helpers.h"
#include "extras/features/message_shot/message_shot.h"
#include "extras/features/forward/extras_forward.h"
#include "extras/features/auto_space/auto_space.h"
#include "boxes/abstract_box.h"
#include "history/history_widget_internal.h"

using namespace HistoryWidgetDetails;

void HistoryWidget::fieldChanged() {
	if (_stashHintManager) {
		const auto save = bool(_textUpdateEvents & TextUpdateEvent::SaveDraft);
		const auto sendTyping = bool(_textUpdateEvents & TextUpdateEvent::SendTyping);
		_stashHintManager->trackChange(save && sendTyping);
	}
	const auto updateTyping = (_textUpdateEvents
		& TextUpdateEvent::SendTyping);

	InvokeQueued(this, [=] {
		updateInlineBotQuery();
		if (_history
			&& !_inlineBot
			&& !_editMsgId
			&& (!_autocomplete || !_autocomplete->stickersEmoji())
			&& updateTyping
			&& fieldHasSendText()
			&& !suppressSendAction()) {
			session().sendProgressManager().update(
				_history,
				Api::SendProgressType::Typing);
		}
	});

	checkCharsCount();

	updateSendButtonType();
	if (!fieldHasSendText()) {
		_fieldIsEmpty = true;
	} else if (_fieldIsEmpty) {
		_fieldIsEmpty = false;
		if (_kbShown && (!_kbReplyTo || !forceReplyPending())) {
			toggleKeyboard();
		}
	}
	if (refreshBotMenuButton()) {
		updateControlsVisibility();
		updateControlsGeometry();
	}
	updateAiButtonVisibility();
	updateSendAsFileVisibility();
	updateExpandButtonVisibility();

	_saveCloudDraftTimer.cancel();
	if (bypassNormalDraftHandling()) {
		return;
	}
	if (!_peer || !(_textUpdateEvents & TextUpdateEvent::SaveDraft)) {
		return;
	}

	_saveDraftText = true;
	saveDraft(true);
}

void HistoryWidget::saveDraftDelayed() {
	if (bypassNormalDraftHandling()) {
		cancelPendingDraftSaves();
		return;
	}
	if (!_peer || !(_textUpdateEvents & TextUpdateEvent::SaveDraft)) {
		return;
	}
	if (!_field->textCursor().position()
		&& !_field->textCursor().anchor()
		&& !_field->scrollTop().current()) {
		if (!session().local().hasDraftCursors(_peer->id)) {
			return;
		}
	}
	saveDraft(true);
}

void HistoryWidget::saveDraft(bool delayed) {
	if (!_peer) {
		return;
	} else if (delayed) {
		auto ms = crl::now();
		if (!_saveDraftStart) {
			_saveDraftStart = ms;
			return _saveDraftTimer.callOnce(kSaveDraftTimeout);
		} else if (ms - _saveDraftStart < kSaveDraftAnywayTimeout) {
			return _saveDraftTimer.callOnce(kSaveDraftTimeout);
		}
	}
	writeDrafts();
}

void HistoryWidget::saveFieldToHistoryLocalDraft() {
	if (bypassNormalDraftHandling()) {
		return;
	}
	if (!_history) {
		return;
	}

	const auto topicRootId = MsgId();
	const auto monoforumPeerId = PeerId();
	if (_editMsgId) {
		_history->setLocalEditDraft(std::make_unique<Data::Draft>(
			_field,
			FullReplyTo{
				.messageId = FullMsgId(_history->peer->id, _editMsgId),
				.topicRootId = topicRootId,
				.monoforumPeerId = monoforumPeerId,
			},
			suggestOptions(true),
			_preview->draft(),
			_saveEditMsgRequestId));
	} else if (shouldShowRichDraftPreview()) {
		_history->clearLocalDraft(topicRootId, monoforumPeerId);
		_history->clearLocalEditDraft(topicRootId, monoforumPeerId);
	} else {
		const auto suggest = suggestOptions();
		if (_replyTo || suggest.exists || !_field->empty()) {
			_history->setLocalDraft(std::make_unique<Data::Draft>(
				_field,
				_replyTo,
				suggest,
				_preview->draft()));
		} else {
			_history->clearLocalDraft(topicRootId, monoforumPeerId);
		}
		_history->clearLocalEditDraft(topicRootId, monoforumPeerId);
	}
}

Data::Draft *HistoryWidget::cloudDraft() const {
	return _history ? _history->cloudDraft(MsgId(), PeerId()) : nullptr;
}

std::shared_ptr<const Iv::RichPage> HistoryWidget::shownRichMessage() const {
	if (const auto draft = shouldShowRichDraftPreview() ? cloudDraft() : nullptr) {
		return draft->richMessage;
	}
	return nullptr;
}

bool HistoryWidget::isComposeBoxOpen() const {
	return _history
		&& Iv::Editor::IsComposeBoxOpen(
			&session(),
			_history->peer->id,
			MsgId(),
			PeerId());
}

bool HistoryWidget::hasEditDraft() const {
	return _history
		&& (_history->localEditDraft(MsgId(), PeerId()) != nullptr);
}

bool HistoryWidget::bypassNormalDraftHandling() const {
	return !_editMsgId
		&& !hasEditDraft()
		&& isComposeBoxOpen();
}

bool HistoryWidget::shouldShowRichDraftPreview() const {
	const auto draft = cloudDraft();
	return !_threadFieldVisible
		&& !_editMsgId
		&& draft
		&& draft->hasRichMessage();
}

void HistoryWidget::clearRichDraft() {
	if (!_history) {
		return;
	}
	const auto reply = _replyTo;
	clearFieldText();
	if (reply.messageId) {
		_history->setLocalDraft(std::make_unique<Data::Draft>(
			TextWithTags(),
			reply,
			SuggestOptions(),
			MessageCursor(),
			Data::WebPageDraft()));
	} else {
		_history->clearLocalDraft(MsgId(), PeerId());
	}
	_history->clearCloudDraft(MsgId(), PeerId());
	applyDraft(Ui::InputField::HistoryAction::NewEntry);
	updateControlsVisibility();
	updateControlsGeometry();
	auto draft = Data::Draft(
		TextWithTags(),
		reply,
		SuggestOptions(),
		MessageCursor(),
		Data::WebPageDraft());
	if (const auto cloudDraft = _history->createCloudDraft(
			MsgId(),
			PeerId(),
			&draft)) {
		session().api().saveDraftToCloud(
			not_null{ _history },
			*cloudDraft);
	}
}

void HistoryWidget::migrateFieldToRichEditor() {
	if (!_history) {
		return;
	}
	if (editingMessage()) {
		cancelEdit();
	} else {
		clearRichDraft();
	}
}

void HistoryWidget::fileChosen(ChatHelpers::FileChosen &&data) {
	controller()->hideLayer(anim::type::normal);
	if (const auto info = data.document->sticker()
		; info && info->setType == Data::StickersType::Emoji) {
		if (data.document->isPremiumEmoji()
			&& !session().premium()
			&& (!_peer
				|| !Data::AllowEmojiWithoutPremium(
					_peer,
					data.document))) {
			showPremiumToast(data.document);
		} else if (!_field->isHidden()) {
			Data::InsertCustomEmoji(_field.data(), data.document);
		}
	} else if (_history) {
		if (data.needsCaption) {
			const auto document = data.document;
			const auto from = data.messageSendingFrom;
			Ui::SendGifWithCaption(
				controller()->uiShow(),
				_field,
				document,
				_peer,
				sendMenuDetails(),
				crl::guard(this, [=](
						Api::SendOptions options,
						TextWithTags caption,
						Ui::PreparedList &&edited) {
					if (!edited.files.empty()) {
						sendingFilesConfirmed(
							Ui::MakeSingleFileBundle(std::move(edited)),
							options);
						return;
					}
					const auto effectiveFrom = options.scheduled
						? Ui::MessageSendingAnimationFrom()
						: from;
					controller()->sendingAnimation().appendSending(
						effectiveFrom);
					auto messageToSend = Api::MessageToSend(
						prepareSendAction(options));
					messageToSend.textWithTags = std::move(caption);
					sendExistingDocument(
						document,
						std::move(messageToSend),
						effectiveFrom.localId);
				}));
			return;
		}
		controller()->sendingAnimation().appendSending(
			data.messageSendingFrom);
		const auto localId = data.messageSendingFrom.localId;
		auto messageToSend = Api::MessageToSend(
			prepareSendAction(data.options));
		messageToSend.textWithTags = base::take(data.caption);
		sendExistingDocument(
			data.document,
			std::move(messageToSend),
			localId);
	}
}

bool HistoryWidget::processChosenSticker(ChatHelpers::FileChosen &&chosen) {
	if (!_peer) {
		return false;
	}
	fileChosen(std::move(chosen));
	return true;
}

void HistoryWidget::saveCloudDraft() {
	if (bypassNormalDraftHandling()) {
		_saveCloudDraftTimer.cancel();
		return;
	}
	controller()->session().api().saveCurrentDraftToCloud();
}

void HistoryWidget::writeDraftTexts() {
	Expects(_history != nullptr);

	session().local().writeDrafts(_history);
	if (_migrated) {
		_migrated->clearDrafts();
		session().local().writeDrafts(_migrated);
	}
}

void HistoryWidget::writeDraftCursors() {
	Expects(_history != nullptr);

	session().local().writeDraftCursors(_history);
	if (_migrated) {
		_migrated->clearDrafts();
		session().local().writeDraftCursors(_migrated);
	}
}

void HistoryWidget::writeDrafts() {
	if (bypassNormalDraftHandling()) {
		cancelPendingDraftSaves();
		return;
	}
	const auto save = (_history != nullptr) && (_saveDraftStart > 0);
	_saveDraftStart = 0;
	_saveDraftTimer.cancel();
	if (save) {
		if (_saveDraftText) {
			writeDraftTexts();
		}
		writeDraftCursors();
	}
	_saveDraftText = false;

	if (!_editMsgId && !_inlineBot) {
		_saveCloudDraftTimer.callOnce(kSaveCloudDraftIdleTimeout);
	}
}

void HistoryWidget::cancelPendingDraftSaves() {
	_saveDraftStart = 0;
	_saveDraftText = false;
	_saveDraftTimer.cancel();
	_saveCloudDraftTimer.cancel();
}

bool HistoryWidget::isRecording() const {
	return _voiceRecordBar->isRecording();
}

void HistoryWidget::activate() {
	if (_history) {
		if (!_historyInited) {
			updateHistoryGeometry(true);
		} else if (hasPendingResizedItems()) {
			updateHistoryGeometry();
		}
	}
	controller()->widget()->setInnerFocus();
}

void HistoryWidget::setInnerFocus() {
	if (_list) {
		if (isSearching() && !_nonEmptySelection) {
			_composeSearch->setInnerFocus();
		} else if (isChoosingTheme()) {
			_chooseTheme->setFocus();
		} else if (_showAnimation
			|| _nonEmptySelection
			|| (_list && _list->wasSelectedText())
			|| isRecording()
			|| isJoinChannel()
			|| isBotStart()
			|| isBlocked()
			|| !_richDraftPreview->isHidden()
			|| (!_canSendTexts && !_editMsgId)) {
			if (_scroll->isHidden()) {
				setFocus();
			} else {
				_list->setFocus();
			}
		} else {
			_field->setFocus();
		}
	} else if (_scroll->isHidden()) {
		setFocus();
	}
}

bool HistoryWidget::notify_switchInlineBotButtonReceived(
		const QString &query,
		UserData *samePeerBot,
		MsgId samePeerReplyTo) {
	if (samePeerBot) {
		const auto to = controller()->dialogsEntryStateCurrent();
		if (!to.key.owningHistory()) {
			return false;
		}
		controller()->switchInlineQuery(to, samePeerBot, query);
		return true;
	} else if (const auto bot = _peer ? _peer->asUser() : nullptr) {
		const auto to = bot->isBot()
			? bot->botInfo->inlineReturnTo
			: Dialogs::EntryState();
		if (!to.key.owningHistory()) {
			return false;
		}
		bot->botInfo->inlineReturnTo = Dialogs::EntryState();
		controller()->switchInlineQuery(to, bot, query);
		return true;
	}
	return false;
}

void HistoryWidget::tryProcessKeyInput(not_null<QKeyEvent*> e) {
	e->accept();
	keyPressEvent(e);
	if (!e->isAccepted()
		&& _canSendTexts
		&& _field->isVisible()
		&& !e->text().isEmpty()) {
		_field->setFocusFast();
		QCoreApplication::sendEvent(_field->rawTextEdit(), e);
	}
}

void HistoryWidget::setupShortcuts() {
	Shortcuts::Requests(
	) | rpl::filter([=] {
		return _history
			&& Ui::AppInFocus()
			&& Ui::InFocusChain(this)
			&& !controller()->isLayerShown()
			&& window()->isActiveWindow();
	}) | rpl::on_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;
		request->check(Command::Search, 1) && request->handle([=] {
			controller()->searchInChat(_history);
			return true;
		});
		request->check(Command::ShowChatMenu, 1) && request->handle([=] {
			Window::ActivateWindow(controller());
			_topBar->showPeerMenu();
			return true;
		});
		_canSendMessages
			&& request->check(Command::ShowScheduled, 1)
			&& request->handle([=] {
				using Scheduled = HistoryView::ScheduledMemento;
				controller()->showSection(
					std::make_shared<Scheduled>(_history));
				return true;
			});
		_canSendTexts
			&& _field->isVisible()
			&& request->check(Command::ComposeAiApplyInPlace, 1)
			&& request->handle([=] {
				triggerAiApplyInPlace();
				return true;
			});
		canShowRichEditor()
			&& request->check(Command::ShowRichEditor, 1)
			&& request->handle([=] {
				showRichEditor();
				return true;
			});
		_preview
			&& (_previewDrawPreview || _preview->draft().removed)
			&& request->check(Command::ToggleWebPagePreview, 1)
			&& request->handle([=] {
				if (_previewDrawPreview) {
					_preview->apply({ .removed = true });
				} else {
					_preview->apply({}, true);
				}
				return true;
			});
		_stash
			&& _stash->canExchange()
			&& request->check(Command::StashMessage, 1)
			&& request->handle([=] {
				_stash->exchange();
				return true;
			});
		const auto record = recordMenuOptions();
		if (showRecordButton()
			&& _canSendMessages
			&& _joinChannel->isHidden()
			&& !_composeSearch) {
			const auto isVoice = record.voice
				&& request->check(Command::RecordVoice, 1);
			const auto isRound = !isVoice
				&& record.round
				&& request->check(Command::RecordRound, 1);
			(isVoice || isRound) && request->handle([=] {
				startRecordFromMenu(isRound);
				return true;
			});
		}
		const auto channel = _peer ? _peer->asChannel() : nullptr;
		const auto hasRecentActions = channel
			&& (channel->hasAdminRights() || channel->amCreator());
		if (hasRecentActions) {
			request->check(Command::ShowAdminLog, 1) && request->handle([=] {
				controller()->showSection(
					std::make_shared<AdminLog::SectionMemento>(channel));
				return true;
			});
		}
		if (session().supportMode()) {
			request->check(
				Command::SupportToggleMuted
			) && request->handle([=] {
				toggleMuteUnmute();
				return true;
			});
		}
	}, lifetime());
}

void HistoryWidget::setupComposeStash() {
	using namespace HistoryView::Controls;
	_stash = std::make_unique<StashManager>(StashManagerDescriptor{
		.session = &session(),
		.buttons = &_cornerButtons,
		.show = controller()->uiShow(),
		.history = [=] { return _history; },
		.key = [] { return Data::DraftKey::Local(MsgId(), PeerId()); },
		.allowed = [=] { return canUseComposeStash(); },
		.hasContent = [=] { return hasStashableContent(); },
		.canSendTexts = [=] { return _canSendTexts; },
		.take = [=] { return takeComposeStash(); },
		.apply = [=](Data::ComposeStash &&stash) {
			applyComposeStash(std::move(stash));
		},
		.suggest = [=] { return suggestOptions(); },
		.clearComposer = [=] {
			cancelReplyOrSuggest();
			updateForwarding();
			saveDraftWithTextNow();
		},
		.openFiles = [=](Ui::PreparedList &list) -> SendFilesBox* {
			if (showSendingFilesError(list)) {
				return nullptr;
			}
			return confirmSendingFiles(std::move(list), QString())
				? _sendFilesBox.data()
				: nullptr;
		},
		.filesError = [=](const Ui::PreparedList &list) {
			return showSendingFilesError(list);
		},
		.menuDetails = [=] { return sendMenuDetails(); },
		.send = [=](Api::SendOptions options) { send(options); },
	});
}

bool HistoryWidget::canUseComposeStash() const {
	return canWriteMessage()
		&& !_editMsgId
		&& !_chooseTheme
		&& !_voiceRecordBar->isActive();
}

bool HistoryWidget::hasStashableContent() const {
	return _history
		&& (!_field->empty()
			|| _replyTo
			|| _suggestOptions
			|| readyToForward()
			|| shownRichMessage());
}

std::unique_ptr<Data::ComposeStash> HistoryWidget::takeComposeStash() {
	Expects(_history != nullptr);

	if (!hasStashableContent()) {
		return nullptr;
	}
	auto result = std::make_unique<Data::ComposeStash>();
	if (const auto draft = shownRichMessage() ? cloudDraft() : nullptr) {
		result->draft = *draft;
		result->draft.saveRequestId = 0;
		clearRichDraft();
	} else {
		result->draft = Data::Draft(
			_field,
			_replyTo,
			suggestOptions(),
			_preview ? _preview->draft() : Data::WebPageDraft());
		clearFieldText();
		if (_preview) {
			_preview->apply({ .removed = true });
		}
	}
	cancelReplyOrSuggest();
	result->forward = _history->forwardDraft(MsgId(), PeerId());
	if (!result->forward.ids.empty()) {
		_history->setForwardDraft(MsgId(), PeerId(), {});
		updateForwarding();
	}
	saveDraftWithTextNow();
	saveCloudDraft();
	if (_stashHintManager) {
		_stashHintManager->markUsed();
	}
	return result;
}

void HistoryWidget::applyComposeStash(Data::ComposeStash &&stash) {
	Expects(_history != nullptr);

	if (stash.draft.hasRichMessage()) {
		_history->clearLocalDraft(MsgId(), PeerId());
		const auto cloud = _history->createCloudDraft(
			MsgId(),
			PeerId(),
			&stash.draft);
		applyDraft();
		if (cloud) {
			session().api().saveDraftToCloud(not_null{ _history }, *cloud);
		}
	} else {
		const auto reply = stash.draft.reply;
		_history->setDraft(
			Data::DraftKey::Local(MsgId(), PeerId()),
			std::make_unique<Data::Draft>(std::move(stash.draft)));
		if (!applyDraft() && reply) {
			replyToMessage(reply);
		}
		saveDraftWithTextNow();
		saveCloudDraft();
	}
	if (!stash.forward.ids.empty()) {
		_history->setForwardDraft(
			MsgId(),
			PeerId(),
			std::move(stash.forward));
		updateForwarding();
	}
	if (_stashHintManager) {
		_stashHintManager->markUsed();
	}
}

void HistoryWidget::setupGiftToChannelButton() {
	_giftToChannel = Ui::CreateChild<Ui::IconButton>(
		_muteUnmute.data(),
		st::historyGiftToChannel);
	_giftToChannel->setAccessibleName(tr::lng_gift_channel_title(tr::now));
	rpl::merge(_muteUnmute->widthValue(), _joinChannel->widthValue()
	) | rpl::on_next([=] {
		_giftToChannel->moveToRight(0, 0);
	}, _giftToChannel->lifetime());
	_giftToChannel->setClickedCallback([=] {
		Ui::ShowStarGiftBox(controller(), _peer);
	});
	rpl::combine(
		_muteUnmute->shownValue(),
		_joinChannel->shownValue()
	) | rpl::on_next([=](bool muteUnmute, bool joinChannel) {
		const auto newParent = (muteUnmute && !joinChannel)
			? _muteUnmute.data()
			: (joinChannel && !muteUnmute)
			? _joinChannel.data()
			: nullptr;
		if (newParent) {
			_giftToChannel->setParent(newParent);
			_giftToChannel->moveToRight(0, 0);
			refreshGiftToChannelShown();
		}
	}, _giftToChannel->lifetime());
}

void HistoryWidget::setupDirectMessageButton() {
	_directMessage = Ui::CreateChild<Ui::IconButton>(
		_muteUnmute.data(),
		st::historyDirectMessage);
		_directMessage->setAccessibleName(tr::lng_profile_direct_messages(tr::now));
	rpl::merge(_muteUnmute->widthValue(), _joinChannel->widthValue()
	) | rpl::on_next([=] {
		_directMessage->moveToLeft(0, 0);
	}, _directMessage->lifetime());
	_directMessage->setClickedCallback([=] {
		if (const auto channel = _peer ? _peer->asChannel() : nullptr) {
			if (channel->invitePeekExpires()) {
				controller()->showToast(
					tr::lng_channel_invite_private(tr::now));
			} else if (const auto monoforum = channel->monoforumLink()) {
				controller()->showPeerHistory(
					monoforum,
					Window::SectionShow::Way::Forward);
			}
		}
	});
	rpl::combine(
		_muteUnmute->shownValue(),
		_joinChannel->shownValue()
	) | rpl::on_next([=](bool muteUnmute, bool joinChannel) {
		const auto newParent = (muteUnmute && !joinChannel)
			? _muteUnmute.data()
			: (joinChannel && !muteUnmute)
			? _joinChannel.data()
			: nullptr;
		if (newParent) {
			_directMessage->setParent(newParent);
			_directMessage->moveToLeft(0, 0);
			refreshDirectMessageShown();
		}
	}, _directMessage->lifetime());
}

void HistoryWidget::pushReplyReturn(not_null<HistoryItem*> item) {
	if (item->history() != _history && item->history() != _migrated) {
		return;
	}
	_cornerButtons.pushReplyReturn(item);
	updateControlsVisibility();
}

QVector<FullMsgId> HistoryWidget::replyReturns() const {
	return _cornerButtons.replyReturns();
}

void HistoryWidget::setReplyReturns(
		PeerId peer,
		QVector<FullMsgId> replyReturns) {
	if (!_peer || _peer->id != peer) {
		return;
	}
	_cornerButtons.setReplyReturns(std::move(replyReturns));
}

void HistoryWidget::fastShowAtEnd(not_null<History*> history) {
	if (_history != history) {
		return;
	}

	clearAllLoadRequests();
	setMsgId(ShowAtUnreadMsgId);
	_pinnedClickedId = FullMsgId();
	_minPinnedId = std::nullopt;
	if (_history->isReadyFor(_showAtMsgId)) {
		_history->forgetScrollState();
		if (_migrated) {
			_migrated->forgetScrollState();
		}
		historyLoaded();
	} else {
		firstLoadMessages();
		doneShow();
	}
}

bool HistoryWidget::applyDraft(FieldHistoryAction fieldHistoryAction) {
	if (bypassNormalDraftHandling()) {
		clearFieldText(0, fieldHistoryAction);
		if (_preview) {
			_preview->apply({ .removed = true });
		}
		refreshBotMenuButton();
		updateControlsVisibility();
		updateControlsGeometry();
		return true;
	}
	InvokeQueued(this, [=] {
		if (_autocomplete) {
			_autocomplete->requestStickersUpdate();
		}
	});

	const auto editDraft = _history
		? _history->localEditDraft(MsgId(), PeerId())
		: nullptr;
	const auto richDraft = (!editDraft && shouldShowRichDraftPreview())
		? cloudDraft()
		: nullptr;
	const auto draft = editDraft
		? editDraft
		: _history
		? _history->localDraft(MsgId(), PeerId())
		: nullptr;
	auto fieldAvailable = canWriteMessage();
	const auto editMsgId = editDraft ? editDraft->reply.messageId.msg : 0;
	if (_voiceRecordBar->isActive()
		|| (!_canSendTexts && !editMsgId && !richDraft)) {
		if (!_canSendTexts) {
			clearFieldText(0, fieldHistoryAction);
		}
		return false;
	}

	if (richDraft) {
		_textUpdateEvents = 0;
		clearFieldText(0, fieldHistoryAction);
		if ((_replyTo != richDraft->reply)
			|| (_replyTo && !_replyEditMsg)) {
			_replyTo = richDraft->reply;
			_replyEditMsg = _replyTo
				? session().data().message(_replyTo.messageId)
				: nullptr;
			if (_replyEditMsg) {
				updateReplyEditText(_replyEditMsg);
				updateReplyToName();
			} else if (_replyTo) {
				requestMessageData(_replyTo.messageId.msg);
			}
		}
		_processingReplyItem = nullptr;
		_processingReplyTo = _replyTo;
		setEditMsgId(0);
		cancelSuggestPost();
		_mediaEditManager.cancel();
		_canReplaceMedia = _canAddMedia = false;
		if (_preview) {
			_preview->apply({ .removed = true });
			_preview->setDisabled(false);
		}
		_textUpdateEvents = TextUpdateEvent::SaveDraft
			| TextUpdateEvent::SendTyping;
		refreshBotMenuButton();
		updateControlsVisibility();
		updateControlsGeometry();
		refreshTopBarActiveChat();
		return true;
	}

	if (!draft || (!editDraft && !fieldAvailable)) {
		const auto fieldWillBeHiddenAfterEdit = (!fieldAvailable
			&& _editMsgId != 0);
		clearFieldText(0, fieldHistoryAction);
		setInnerFocus();
		_processingReplyItem = _replyEditMsg = nullptr;
		_processingReplyTo = _replyTo = FullReplyTo();
		setEditMsgId(0);
		if (_preview) {
			_preview->apply({ .removed = true });
		}
		if (fieldWillBeHiddenAfterEdit) {
			updateControlsVisibility();
			updateControlsGeometry();
		}
		refreshTopBarActiveChat();
		return true;
	}

	_textUpdateEvents = 0;
	setFieldText(draft->textWithTags, 0, fieldHistoryAction);
	setInnerFocus();
	draft->cursor.applyTo(_field);
	_textUpdateEvents = TextUpdateEvent::SaveDraft
		| TextUpdateEvent::SendTyping;

	_processingReplyItem = _replyEditMsg = nullptr;
	_processingReplyTo = _replyTo = FullReplyTo();
	setEditMsgId(editMsgId);
	refreshBotMenuButton();
	updateControlsVisibility();
	updateControlsGeometry();
	refreshTopBarActiveChat();
	if (_editMsgId) {
		updateReplyEditTexts();
		if (!_replyEditMsg) {
			requestMessageData(_editMsgId);
		}
		updateExpandButtonVisibility();
		if (editDraft && editDraft->suggest) {
			using namespace HistoryView;
			applySuggestOptions(editDraft->suggest, SuggestMode::Change);
		} else {
			cancelSuggestPost();
		}
	} else {
		const auto draft = _history->localDraft(MsgId(), PeerId());
		_processingReplyTo = draft ? draft->reply : FullReplyTo();
		if (_processingReplyTo) {
			_processingReplyItem = session().data().message(
				_processingReplyTo.messageId);
		} else if (draft && draft->suggest) {
			using namespace HistoryView;
			applySuggestOptions(draft->suggest, SuggestMode::New);
		}
		processReply();
	}

	if (_preview) {
		_preview->setDisabled(_editMsgId
			&& _replyEditMsg
			&& _replyEditMsg->media()
			&& !_replyEditMsg->media()->webpage());
		if (!_editMsgId) {
			_preview->apply(draft->webpage, true);
		} else if (!_replyEditMsg
			|| !_replyEditMsg->media()
			|| _replyEditMsg->media()->webpage()) {
			_preview->apply(draft->webpage, false);
		}
	}
	return true;
}

void HistoryWidget::applyCloudDraft(History *history) {
	Expects(!session().supportMode());

	if (_history == history
		&& !_editMsgId
		&& !bypassNormalDraftHandling()) {
		applyDraft(Ui::InputField::HistoryAction::NewEntry);

		updateControlsVisibility();
		updateControlsGeometry();
	}
}

bool HistoryWidget::insideJumpToEndInsteadOfToUnread() const {
	Expects(_history != nullptr);

	if (session().supportMode() || !_history->trackUnreadMessages()) {
		return true;
	} else if (!_historyInited) {
		return false;
	}
	_history->calculateFirstUnreadMessage();
	const auto unread = _history->firstUnreadMessage();
	const auto visibleBottom = visibleScrollBottom();
	DEBUG_LOG(("JumpToEnd(%1, %2, %3): "
		"unread: %4, top: %5, visibleBottom: %6."
		).arg(_history->peer->name()
		).arg(_history->inboxReadTillId().bare
		).arg(Logs::b(_history->loadedAtBottom())
		).arg(unread ? unread->data()->id.bare : 0
		).arg(unread ? _list->itemTop(unread) : -1
		).arg(visibleBottom));
	return unread && _list->itemTop(unread) <= visibleBottom;
}
