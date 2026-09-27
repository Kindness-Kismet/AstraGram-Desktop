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

void HistoryWidget::initVoiceRecordBar() {
	_voiceRecordBar->setStartRecordingFilter([=] {
		const auto error = [&]() -> Data::SendError {
			if (_peer) {
				if (const auto error = Data::RestrictionError(
						_peer,
						ChatRestriction::SendVoiceMessages)) {
					return error;
				}
			}
			return {};
		}();
		if (error) {
			Data::ShowSendErrorToast(controller(), _peer, error);
			return true;
		} else if (showSlowmodeError()) {
			return true;
		}
		return false;
	});
	_voiceRecordBar->setTTLFilter([=] {
		if (_editMsgId) {
			return false;
		} else if (const auto peer = _history ? _history->peer.get() : nullptr) {
			if (const auto user = peer->asUser()) {
				if (!user->isSelf() && !user->isBot()) {
					return true;
				}
			}
		}
		return false;
	});

	const auto applyLocalDraft = [=] {
		if (_history && _history->localDraft(MsgId(), PeerId())) {
			applyDraft();
		}
	};

	_voiceRecordBar->sendActionUpdates(
	) | rpl::on_next([=](const auto &data) {
		if (!_history) {
			return;
		} else if (data.progress >= 0 && suppressSendAction()) {
			return;
		}
		session().sendProgressManager().update(
			_history,
			data.type,
			data.progress);
	}, lifetime());

	_voiceRecordBar->sendVoiceRequests(
	) | rpl::on_next([=](const VoiceToSend &data) {
		sendVoice(data);
	}, lifetime());

	_voiceRecordBar->cancelRequests(
	) | rpl::on_next(applyLocalDraft, lifetime());

	_voiceRecordBar->lockShowStarts(
	) | rpl::on_next([=] {
		_cornerButtons.updateJumpDownVisibility();
		_cornerButtons.updateUnreadThingsVisibility();
	}, lifetime());

	_voiceRecordBar->errors(
	) | rpl::on_next([=](::Media::Capture::Error error) {
		using Error = ::Media::Capture::Error;
		switch (error) {
		case Error::AudioInit:
		case Error::AudioTimeout:
			controller()->showToast(tr::lng_record_audio_problem(tr::now));
			break;
		case Error::VideoInit:
		case Error::VideoTimeout:
			controller()->showToast(tr::lng_record_video_problem(tr::now));
			break;
		default:
			controller()->showToast(u"Unknown error."_q);
			break;
		}
	}, lifetime());

	_voiceRecordBar->updateSendButtonTypeRequests(
	) | rpl::on_next([=] {
		updateSendButtonType();
	}, lifetime());

	_voiceRecordBar->lockViewportEvents(
	) | rpl::on_next([=](not_null<QEvent*> e) {
		_scroll->viewportEvent(e);
	}, lifetime());

	_voiceRecordBar->recordingTipRequests(
	) | rpl::on_next([=] {
		Core::App().settings().setRecordVideoMessages(
			!Core::App().settings().recordVideoMessages());
		updateSendButtonType();
		switch (_send->type()) {
		case Ui::SendButton::Type::Record: {
			const auto can = Webrtc::RecordAvailability::VideoAndAudio;
			controller()->showToast((_recordAvailability == can)
				? tr::lng_record_voice_tip(tr::now)
				: tr::lng_record_hold_tip(tr::now));
		} break;
		case Ui::SendButton::Type::Round:
			controller()->showToast(tr::lng_record_video_tip(tr::now));
			break;
		}
	}, lifetime());

	_voiceRecordBar->recordingStateChanges(
	) | rpl::on_next([=](bool active) {
		_field->setDisabled(active);
		controller()->widget()->setInnerFocus();
		updateAiButtonVisibility();
		updateSendAsFileVisibility();
		updateExpandButtonVisibility();
	}, lifetime());

	_voiceRecordBar->hideFast();
}

void HistoryWidget::initAiButton() {
	_aiButton->hide();
	_aiButton->setAccessibleName(tr::lng_ai_compose_title(tr::now));
	_aiButton->setClickedCallback([=] {
		if (_aiTooltipManager) {
			_aiTooltipManager->hideAndRemember();
		}
		updateAiButtonVisibility();
		showAiComposeBox();
	});

	_aiTooltipManager = std::make_unique<HistoryView::Controls::AiTooltipManager>(
		this,
		_aiButton,
		tr::lng_ai_compose_tooltip(tr::rich),
		"ai_compose_tooltip_hidden"_cs,
		[=] { return width(); });
}

void HistoryWidget::initSendAsFileButton() {
	_sendAsFile->hide();
	_sendAsFile->setClickedCallback([=] {
		if (_sendAsFileTooltipManager) {
			_sendAsFileTooltipManager->hideAndRemember();
		}
		const auto cursor = _field->textCursor();
		const auto text = _field->getTextWithTags();
		sendTextAsFile(text.text, text, cursor.position(), cursor.anchor());
	});

	_sendAsFileTooltipManager = std::make_unique<HistoryView::Controls::AiTooltipManager>(
		this,
		_sendAsFile,
		tr::lng_send_as_file_tooltip(tr::rich),
		"send_as_file_tooltip_hidden"_cs,
		[=] { return width(); });
}

void HistoryWidget::initExpandButton() {
	_expand->hide();
	_expand->setAccessibleName(tr::lng_article_menu_item(tr::now));
	_expand->setClickedCallback([=] {
		showRichEditor();
	});
}

void HistoryWidget::offerRichPaste(not_null<const QMimeData*> data) {
	if (!_history || !canShowRichEditor() || editingMessage()) {
		return;
	}
	const auto decision = ChatHelpers::MimeDataRichPasteOffer(
		&session(),
		data);
	if (!decision) {
		return;
	}
	const auto copy = ChatHelpers::CloneMimeData(data);
	const auto was = _field->getTextWithTags();
	const auto cursor = _field->textCursor();
	const auto position = cursor.position();
	const auto anchor = cursor.anchor();
	crl::on_main(this, [=] {
		const auto now = _field->getTextWithTags();
		if (now == was) {
			return;
		}
		ChatHelpers::ShowRichPasteToast({
			.session = &session(),
			.parent = _scroll.data(),
			.cancel = _field->changes(),
			.offer = decision->offer,
			.action = crl::guard(this, [=] {
				const auto unchanged = (_field->getTextWithTags() == now);
				if (decision->offer == ChatHelpers::RichPasteOffer::Field) {
					if (!unchanged) {
						return;
					}
					const auto &markdown = decision->markdown;
					const auto from = std::min(position, anchor);
					_field->setTextWithTags(ChatHelpers::TextWithTagsReplaced(
						was,
						from,
						std::max(position, anchor),
						markdown));
					_field->setCursorPosition(
						from + int(markdown.text.size()));
					return;
				}
				if (unchanged) {
					_field->setTextWithTags(was);
					auto cursor = _field->textCursor();
					cursor.setPosition(anchor);
					if (position != anchor) {
						cursor.setPosition(position, QTextCursor::KeepAnchor);
					}
					_field->setTextCursor(cursor);
				}
				showRichEditorWithPaste(copy);
			}),
		});
	});
}

void HistoryWidget::showRichEditorWithPaste(
		std::shared_ptr<QMimeData> data) {
	_pendingRichPaste = std::move(data);
	showRichEditor();
	_pendingRichPaste = nullptr;
}

void HistoryWidget::showRichEditor() {
	if (!_history) {
		return;
	}
	const auto window = controller();
	if (editingMessage()) {
		const auto item = session().data().message(
			_history->peer,
			_editMsgId);
		if (item) {
			Iv::Editor::ShowEditFromFieldBox(
				window,
				item,
				prepareSendAction({}),
				_field->getTextWithAppliedMarkdown(),
				crl::guard(this, [=] {
					cancelEdit();
				}));
		}
		return;
	}
	using Options = Iv::Editor::ComposeBoxOptions;
	const auto support = session().supportMode();
	auto options = Options();
	options.initialPaste = _pendingRichPaste;
	if (support) {
		options.scope = Options::Scope::Detached;
		options.returnText = crl::guard(this, [=](TextWithTags text) {
			setFieldText(text);
		});
	}
	Iv::Editor::ShowComposeBox(
		window,
		_history->peer,
		prepareSendAction({}),
		sendMenuDetails(),
		_field->getTextWithAppliedMarkdown(),
		crl::guard(this, [=] {
			if (support) {
				migrateSupportFieldToRichEditor();
			} else {
				migrateFieldToRichEditor();
			}
		}),
		std::move(options));
}

void HistoryWidget::migrateSupportFieldToRichEditor() {
	if (!_history) {
		return;
	}
	clearFieldText();
	_history->clearLocalDraft(MsgId(), PeerId());
}

void HistoryWidget::sendTextAsFile(
		const QString &fileText,
		TextWithTags restoreText,
		int restorePosition,
		int restoreAnchor) {
	auto result = Ui::PrepareTextAsFile(fileText);

	_field->setTextWithTags({});

	auto box = Box<SendFilesBox>(
		controller(),
		std::move(result),
		TextWithTags{},
		_peer,
		Api::SendType::Normal,
		sendMenuDetails());
	box->setReplyTo(replyTo());
	box->setConfirmedCallback(crl::guard(this, [=](
			std::shared_ptr<Ui::PreparedBundle> bundle,
			Api::SendOptions options,
			FullReplyTo currentReplyTo) {
		if (!currentReplyTo.messageId && replyTo().messageId) {
			cancelReply();
		}
		sendingFilesConfirmed(std::move(bundle), options);
	}));
	box->setCancelledCallback(crl::guard(this, [=] {
		_field->setTextWithTags(restoreText);
		auto cursor = _field->textCursor();
		cursor.setPosition(restoreAnchor);
		if (restorePosition != restoreAnchor) {
			cursor.setPosition(restorePosition, QTextCursor::KeepAnchor);
		}
		_field->setTextCursor(cursor);
	}));

	Window::ActivateWindow(controller());
	controller()->show(std::move(box));
}

void HistoryWidget::initTabbedSelector() {
	refreshTabbedPanel();

	_tabbedSelectorToggle->addClickHandler([=] {
		if (_tabbedPanel && _tabbedPanel->isHidden()) {
			_tabbedPanel->showAnimated();
		} else {
			toggleTabbedSelectorMode();
		}
	});

	const auto selector = controller()->tabbedSelector();

	base::install_event_filter(this, selector, [=](not_null<QEvent*> e) {
		if (_tabbedPanel && e->type() == QEvent::ParentChange) {
			setTabbedPanel(nullptr);
		}
		return base::EventFilterResult::Continue;
	});

	auto filter = rpl::filter([=] {
		return !isHidden();
	});
	using Selector = TabbedSelector;

	selector->emojiChosen(
	) | rpl::filter([=] {
		return !isHidden() && !_field->isHidden();
	}) | rpl::on_next([=](ChatHelpers::EmojiChosen data) {
		Ui::InsertEmojiAtCursor(_field->textCursor(), data.emoji);
	}, lifetime());

	rpl::merge(
		selector->fileChosen() | filter,
		selector->customEmojiChosen() | filter,
		controller()->stickerOrEmojiChosen() | filter
	) | rpl::on_next([=](ChatHelpers::FileChosen &&data) {
		fileChosen(std::move(data));
	}, lifetime());

	selector->photoChosen(
	) | filter | rpl::on_next([=](ChatHelpers::PhotoChosen data) {
		sendExistingPhoto(data.photo, data.options);
	}, lifetime());

	selector->inlineResultChosen(
	) | filter | rpl::filter([=](const ChatHelpers::InlineChosen &data) {
		if (!data.recipientOverride) {
			return true;
		} else if (data.recipientOverride != _peer) {
			showHistory(data.recipientOverride->id, ShowAtTheEndMsgId);
		}
		return (data.recipientOverride == _peer);
	}) | rpl::on_next([=](ChatHelpers::InlineChosen data) {
		sendInlineResult(data);
	}, lifetime());

	selector->contextMenuRequested(
	) | filter | rpl::on_next([=] {
		selector->showMenuWithDetails(sendMenuDetails());
	}, lifetime());

	selector->choosingStickerUpdated(
	) | rpl::on_next([=](const Selector::Action &data) {
		if (!_history) {
			return;
		}
		const auto type = Api::SendProgressType::ChooseSticker;
		if (data != Selector::Action::Cancel) {
			if (!suppressSendAction()) {
				session().sendProgressManager().update(_history, type);
			}
		} else {
			session().sendProgressManager().cancel(_history, type);
		}
	}, lifetime());
}

void HistoryWidget::supportInitAutocomplete() {
	_supportAutocomplete->hide();

	_supportAutocomplete->insertRequests(
	) | rpl::on_next([=](const QString &text) {
		supportInsertText(text);
	}, _supportAutocomplete->lifetime());

	_supportAutocomplete->shareContactRequests(
	) | rpl::on_next([=](const Support::Contact &contact) {
		supportShareContact(contact);
	}, _supportAutocomplete->lifetime());
}

void HistoryWidget::supportInsertText(const QString &text) {
	_field->setFocus();
	_field->textCursor().insertText(text);
	_field->ensureCursorVisible();
}

void HistoryWidget::supportShareContact(Support::Contact contact) {
	if (!_history) {
		return;
	}
	supportInsertText(contact.comment);
	contact.comment = _field->getLastText();

	const auto submit = [=](Qt::KeyboardModifiers modifiers) {
		const auto history = _history;
		if (!history) {
			return;
		}
		auto options = Api::SendOptions{
			.sendAs = prepareSendAction({}).options.sendAs,
		};
		auto action = Api::SendAction(history);
		send(options);
		options.handleSupportSwitch = Support::HandleSwitch(modifiers);
		action.options = options;
		session().api().shareContact(
			contact.phone,
			contact.firstName,
			contact.lastName,
			action);
	};
	const auto box = controller()->show(Box<Support::ConfirmContactBox>(
		controller(),
		_history,
		contact,
		crl::guard(this, submit)));
	box->boxClosing(
	) | rpl::on_next([=] {
		_field->document()->undo();
	}, lifetime());
}

void HistoryWidget::initFieldAutocomplete() {
	_emojiSuggestions = nullptr;
	_autocomplete = nullptr;
	if (!_peer) {
		return;
	}
	const auto processShortcut = [=](QString shortcut) {
		if (!_peer) {
			return;
		}
		const auto messages = &_peer->owner().shortcutMessages();
		const auto shortcutId = messages->lookupShortcutId(shortcut);
		if (shortcut.isEmpty()) {
			controller()->showSettings(Settings::QuickRepliesId());
		} else if (!_peer->session().premium()) {
			ShowPremiumPreviewToBuy(
				controller(),
				PremiumFeature::QuickReplies);
		} else if (shortcutId) {
			session().api().sendShortcutMessages(_peer, shortcutId);
			session().api().finishForwarding(prepareSendAction({}));
			setFieldText(_field->getTextWithTagsPart(
				_field->textCursor().position()));
		}
	};
	ChatHelpers::InitFieldAutocomplete(_autocomplete, {
		.parent = this,
		.show = controller()->uiShow(),
		.field = _field.data(),
		.peer = _peer,
		.features = [=] {
			auto result = ChatHelpers::ComposeFeatures();
			if (_showAnimation
				|| isChoosingTheme()
				|| (_inlineBot && !_inlineLookingUpBot)) {
				result.autocompleteMentions = false;
				result.autocompleteHashtags = false;
				result.autocompleteCommands = false;
			}
			if (_editMsgId) {
				result.autocompleteCommands = false;
				result.suggestStickersByEmoji = false;
			}
			return result;
		},
		.sendMenuDetails = [=] { return sendMenuDetails(); },
		.stickerChoosing = [=] {
			if (_history && !suppressSendAction()) {
				session().sendProgressManager().update(
					_history,
					Api::SendProgressType::ChooseSticker);
			}
		},
		.stickerChosen = [=](ChatHelpers::FileChosen &&data) {
			fileChosen(std::move(data));
		},
		.setText = [=](TextWithTags text) { if (_peer) setFieldText(text); },
		.sendBotCommand = [=](QString command) {
			if (_peer) {
				sendBotCommand({ _peer, command, FullMsgId(), replyTo() });
				session().api().finishForwarding(prepareSendAction({}));
			}
		},
		.processShortcut = processShortcut,
		.moderateKeyActivateCallback = [=](int key) {
			const auto context = [=](FullMsgId itemId) {
				return _list->prepareClickContext(Qt::LeftButton, itemId);
			};
			return !_keyboard->isHidden() && _keyboard->moderateKeyActivate(
				key,
				context);
		},
	});
	const auto allow = [=](const auto&) {
		return _peer->isSelf();
	};
	_emojiSuggestions.reset(Ui::Emoji::SuggestionsController::Init(
		this,
		_field,
		&controller()->session(),
		{ .suggestCustomEmoji = true, .allowCustomWithoutPremium = allow }));
}

InlineBotQuery HistoryWidget::parseInlineBotQuery() const {
	return (isChoosingTheme() || _editMsgId)
		? InlineBotQuery()
		: ParseInlineBotQuery(&session(), _field);
}

void HistoryWidget::updateInlineBotQuery() {
	if (!_history) {
		return;
	}
	const auto query = parseInlineBotQuery();
	if (_inlineBotUsername != query.username) {
		_inlineBotUsername = query.username;
		if (_inlineBotResolveRequestId) {
			_api.request(_inlineBotResolveRequestId).cancel();
			_inlineBotResolveRequestId = 0;
		}
		if (query.lookingUpBot) {
			_inlineBot = nullptr;
			_inlineLookingUpBot = true;
			const auto username = _inlineBotUsername;
			_inlineBotResolveRequestId = _api.request(
				MTPcontacts_ResolveUsername(
					MTP_flags(0),
					MTP_string(username),
					MTP_string())
			).done([=](const MTPcontacts_ResolvedPeer &result) {
				const auto &data = result.data();
				const auto resolvedBot = [&]() -> UserData* {
					if (const auto user = session().data().processUsers(
							data.vusers())) {
						if (user->isBot()
							&& !user->botInfo->inlinePlaceholder.isEmpty()) {
							return user;
						}
					}
					return nullptr;
				}();
				session().data().processChats(data.vchats());

				_inlineBotResolveRequestId = 0;
				const auto query = parseInlineBotQuery();
				if (_inlineBotUsername == query.username) {
					applyInlineBotQuery(
						query.lookingUpBot ? resolvedBot : query.bot,
						query.query);
				} else {
					clearInlineBot();
				}
			}).fail([=](const MTP::Error &error) {
				_inlineBotResolveRequestId = 0;
				if (username == _inlineBotUsername) {
					clearInlineBot();
				}
			}).send();
		} else {
			applyInlineBotQuery(query.bot, query.query);
		}
	} else if (query.lookingUpBot) {
		if (!_inlineLookingUpBot) {
			applyInlineBotQuery(_inlineBot, query.query);
		}
	} else {
		applyInlineBotQuery(query.bot, query.query);
	}
}

void HistoryWidget::applyInlineBotQuery(UserData *bot, const QString &query) {
	if (bot) {
		if (_inlineBot != bot) {
			_inlineBot = bot;
			_inlineLookingUpBot = false;
			inlineBotChanged();
		}
		if (!_inlineResults) {
			_inlineResults.create(this, controller());
			_inlineResults->setResultSelectedCallback([=](
					InlineBots::ResultSelected result) {
				if (result.open) {
					const auto request = result.result->openRequest();
					const auto showDrawButton = canWriteMessage();
					if (const auto photo = request.photo()) {
						controller()->openPhoto(
							photo,
							{ .showDrawButton = showDrawButton });
					} else if (const auto document = request.document()) {
						controller()->openDocument(
							document,
							false,
							{ .showDrawButton = showDrawButton });
					}
				} else {
					sendInlineResult(result);
				}
			});
			_inlineResults->setSendMenuDetails([=] {
				return sendMenuDetails();
			});
			_inlineResults->requesting(
			) | rpl::on_next([=](bool requesting) {
				_tabbedSelectorToggle->setLoading(requesting);
			}, _inlineResults->lifetime());
			updateControlsGeometry();
			orderWidgets();
		}
		_inlineResults->queryInlineBot(_inlineBot, _peer, query);
		if (_autocomplete) {
			_autocomplete->hideAnimated();
		}
	} else {
		clearInlineBot();
	}
}

void HistoryWidget::orderWidgets() {
	_fieldBarCancel->raise();
	_voiceRecordBar->raise();
	_send->raise();
	_aiButton->raise();
	_sendAsFile->raise();
	_expand->raise();
	_richDraftPreview->raise();
	_discardRichDraft->raise();
	_topBars->raise();
	if (_businessBotStatus) {
		_businessBotStatus->bar().raise();
	}
	if (_contactStatus) {
		_contactStatus->bar().raise();
	}
	if (_paysStatus) {
		_paysStatus->bar().raise();
	}
	if (_translateBar) {
		_translateBar->raise();
	}
	if (_sponsoredMessageBar) {
		_sponsoredMessageBar->raise();
	}
	if (_pinnedBar) {
		_pinnedBar->raise();
	}
	if (_requestsBar) {
		_requestsBar->raise();
	}
	if (_groupCallBar) {
		_groupCallBar->raise();
	}
	if (_chooseTheme) {
		_chooseTheme->raise();
	}
	if (_subsectionTabs) {
		_subsectionTabs->raise();
	}
	_topShadow->raise();
	if (_autocomplete) {
		_autocomplete->raise();
	}
	if (_membersDropdown) {
		_membersDropdown->raise();
	}
	if (_inlineResults) {
		_inlineResults->raise();
	}
	if (_tabbedPanel) {
		_tabbedPanel->raise();
	}
	if (_emojiSuggestions) {
		_emojiSuggestions->raise();
	}
	if (_attachBotsMenu) {
		_attachBotsMenu->raise();
	}
	if (_aiTooltipManager) {
		_aiTooltipManager->raise();
	}
	if (_sendAsFileTooltipManager) {
		_sendAsFileTooltipManager->raise();
	}
	_attachDragAreas.document->raise();
	_attachDragAreas.photo->raise();
}

void HistoryWidget::toggleChooseChatTheme(
		not_null<PeerData*> peer,
		std::optional<bool> show) {
	const auto update = [=] {
		updateInlineBotQuery();
		updateControlsGeometry();
		updateControlsVisibility();
	};
	if (peer.get() != _peer) {
		return;
	} else if (_chooseTheme) {
		if (isChoosingTheme() && !show.value_or(false)) {
			const auto was = base::take(_chooseTheme);
			if (Ui::InFocusChain(this)) {
				setInnerFocus();
			}
			update();
		}
		return;
	} else if (!show.value_or(true)) {
		return;
	} else if (_voiceRecordBar->isActive()) {
		controller()->showToast(tr::lng_chat_theme_cant_voice(tr::now));
		return;
	}
	_chooseTheme = std::make_unique<Ui::ChooseThemeController>(
		this,
		controller(),
		peer);
	_chooseTheme->shouldBeShownValue(
	) | rpl::on_next(update, _chooseTheme->lifetime());
}

Ui::ChatTheme *HistoryWidget::customChatTheme() const {
	return _list ? _list->theme().get() : nullptr;
}

bool HistoryWidget::suppressSendAction() const {
	if (!_history) {
		return false;
	}
	const auto &ephemeral = session().ephemeralMessages();
	return ephemeral.isEphemeralBotReply(replyTo().messageId)
		|| (_peer && ephemeral.hasEphemeralCommand(
			_peer,
			_field->getLastText()));
}
