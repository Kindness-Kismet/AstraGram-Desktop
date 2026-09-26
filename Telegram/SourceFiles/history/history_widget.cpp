/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_widget.h"

#include "ayu/features/window_material/window_material.h"
#include "ui/chat/floating_bar.h"

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

HistoryWidget::HistoryWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Window::AbstractSectionWidget(
	parent,
	controller,
	ActivePeerValue(controller))
, _api(&controller->session().mtp())
, _updateEditTimeLeftDisplay([=] { updateField(); })
, _fieldBarCancel(this, st::historyReplyCancel)
, _topBars(std::make_unique<Ui::RpWidget>(this))
, _topBar(this, controller)
, _scroll(
	this,
	controller->chatStyle()->value(lifetime(), st::historyScroll))
, _updateHistoryItems([=] { updateHistoryItemsByTimer(); })
, _cornerButtons(
	_scroll.data(),
	controller->chatStyle(),
	static_cast<HistoryView::CornerButtonsDelegate*>(this))
, _pullToNext(std::make_unique<HistoryView::PullToNextChannel>(
	this,
	_scroll.data(),
	controller))
, _supportAutocomplete(session().supportMode()
	? object_ptr<Support::Autocomplete>(this, &session())
	: nullptr)
, _send(std::make_shared<Ui::SendButton>(this, st::historySend))
, _aiButton(Ui::CreateChild<HistoryView::Controls::ComposeAiButton>(
	this,
	st::historyAiComposeButton))
, _sendAsFile(Ui::CreateChild<Ui::IconButton>(
	this,
	st::historySendAsFileButton))
, _expand(Ui::CreateChild<Ui::IconButton>(
	this,
	st::historyExpandComposeButton))
, _discardRichDraft(Ui::CreateChild<Ui::IconButton>(
	this,
	st::historyDiscardRichDraftButton))
, _unblock(
	this,
	tr::lng_unblock_button(tr::now).toUpper(),
	st::historyUnblock)
, _botStart(
	this,
	tr::lng_bot_start(tr::now).toUpper(),
	st::historyComposeButton)
, _joinChannel(
	this,
	tr::lng_profile_join_channel(tr::now).toUpper(),
	st::historyComposeButton)
, _muteUnmute(
	this,
	tr::lng_channel_mute(tr::now).toUpper(),
	st::historyComposeButton)
, _discuss(this,
	tr::ayu_ChannelBottomButtonDiscuss(tr::now).toUpper(),
	st::historyComposeButton)
, _reportMessages(this, QString(), st::historyComposeButton)
, _attachToggle(this, st::historyAttach)
, _tabbedSelectorToggle(this, st::historyAttachEmoji)
, _botKeyboardShow(this, st::historyBotKeyboardShow)
, _botKeyboardHide(this, st::historyBotKeyboardHide)
, _botCommandStart(this, st::historyBotCommandStart)
, _voiceRecordBar(std::make_unique<VoiceRecordBar>(
	this,
	controller->uiShow(),
	_send,
	st::historySendSize.height()))
, _forwardPanel(std::make_unique<ForwardPanel>([=] { updateField(); }))
, _field(
	this,
	st::historyComposeField,
	Ui::InputField::Mode::MultiLine,
	tr::lng_message_ph())
, _richDraftPreview(std::make_unique<HistoryView::Controls::RichDraftPreview>(
	this,
	&session(),
	[=] {
		return controller->isGifPausedAtLeastFor(
			Window::GifPauseReason::Any);
	},
	[=] {
		if (_history) {
			Iv::Editor::ShowComposeBox(
				controller,
				_history->peer,
				prepareSendAction({}),
				sendMenuDetails());
		}
	},
	[=] {
		updateControlsGeometry();
	}))
, _kbScroll(this, st::botKbScroll)
, _keyboard(_kbScroll->setOwnedWidget(object_ptr<BotKeyboard>(
	controller,
	this)))
, _membersDropdownShowTimer([=] { showMembersDropdown(); })
, _highlighter(
	&session().data(),
	[=](const HistoryItem *item) { return item->mainView(); },
	[=](const HistoryView::Element *view) {
		session().data().requestViewRepaint(view);
	})
, _saveDraftTimer([=] { saveDraft(); })
, _saveCloudDraftTimer([=] { saveCloudDraft(); })
, _paidReactionToast(std::make_unique<HistoryView::PaidReactionToast>(
	this,
	&session().data(),
	rpl::single(0),
	[=](not_null<const HistoryView::Element*> view) {
		return _list && _list->itemTop(view) >= 0;
	}))
, _topShadow(this) {
	setAcceptDrops(true);
	setVisualTabOrder(true);
	_field->setObjectName(u"messageInput"_q);
	_scroll->setObjectName(u"historyScroll"_q);
	_send->setObjectName(u"sendButton"_q);
	_attachToggle->setObjectName(u"compose.attach"_q);
	_tabbedSelectorToggle->setObjectName(u"compose.emoji"_q);

	// The controls inside these are created in an order of their own - the
	// top bar's selection buttons start with the one placed last, the bars
	// are raised over each other in the reverse of their visual order, and
	// the gift button of the mute bar is set up before the one placed to
	// its left. The outer ordering above sees each of them as a single
	// child and keeps whatever order they have, so they arrange themselves.
	_topBar->setVisualTabOrder(true);
	_topBars->setVisualTabOrder(true);
	_muteUnmute->setVisualTabOrder(true);

	session().downloaderTaskFinished() | rpl::on_next([=] {
		update();
	}, lifetime());

	_scroll->setHandleTouch(false);
	_scroll->lockWheelDirection();
	_scroll->setCrossAxisWheelProcess([=](QPoint delta, Qt::ScrollPhase phase) {
		return _list && _list->consumeScrollAction(delta, phase);
	});
	_scroll->scrolls() | rpl::on_next([=] {
		handleScroll();
	}, lifetime());
	_scroll->setOverscrollTypes(
		Ui::ElasticScroll::OverscrollType::Real,
		Ui::ElasticScroll::OverscrollType::Real);
	_scroll->setOverscrollBg(QColor(0, 0, 0, 0));
	_scroll->setOverscrollEdges(
		[=] { return historyLoadedAtTop(); },
		[=] { return historyLoadedAtBottom(); });
	_scroll->geometryChanged(
	) | rpl::on_next(crl::guard(_list, [=] {
		_list->onParentGeometryChanged();
	}), lifetime());

	_scroll->setBottomContentRequest([=] {
		if (!_history
			|| _firstLoadRequest
			|| !_history->loadedAtBottom()) {
			return false;
		}
		using Result = Data::SponsoredMessages::AppendResult;
		const auto tryToAppend = [=] {
			return session().sponsoredMessages().append(_history);
		};
		const auto result = tryToAppend();
		if (result == Result::MediaLoading
			&& !_historySponsoredPreloading) {
			session().downloaderTaskFinished(
			) | rpl::on_next([=] {
				if (tryToAppend() != Result::MediaLoading) {
					_historySponsoredPreloading.destroy();
				}
			}, _historySponsoredPreloading);
		}
		return (result == Result::Appended);
	});

	_fieldBarCancel->addClickHandler([=] { cancelFieldAreaState(); });
	_send->addClickHandler([=] { sendButtonClicked(); });

	Iv::Editor::SetupSendLockBadge(
		_send.get(),
		st::ivComposeSendLockBadgePosition,
		_sendLockBadge.events());

	_mediaEditManager.updateRequests() | rpl::on_next([this] {
		updateOverStates(mapFromGlobal(QCursor::pos()));
		updateField();
	}, lifetime());

	setupSendMenu(_send.get(), [=](SendMenu::Action action, SendMenu::Details) {
		if (action.type == SendMenu::ActionType::Send) {
			send(action.options);
		} else {
			sendScheduled(action.options);
		}
	});
	_unblock->addClickHandler([=] { unblockUser(); });
	for (const auto &[button, name] : {
			std::pair{ _unblock.data(), u"chatAction.unblock"_q },
			std::pair{ _botStart.data(), u"chatAction.start"_q },
			std::pair{ _joinChannel.data(), u"chatAction.join"_q },
			std::pair{ _muteUnmute.data(), u"chatAction.notify"_q },
			std::pair{ _discuss.data(), u"chatAction.discuss"_q },
			std::pair{ _reportMessages.data(), u"chatAction.report"_q } }) {
		button->setObjectName(name);
		Ui::ApplyChatControlSurface(button, st::historyComposeCapsuleRadius);
	}
	_botStart->setAcceptBoth(true);
	_botStart->clicks() | rpl::on_next(
		[=](Qt::MouseButton button)
		{
			if (button == Qt::LeftButton) {
				sendBotStartCommand();
			} else if (button == Qt::RightButton && isBotStart() && !_peer->asUser()->botInfo->startToken.isEmpty()) {
				_peer->asUser()->botInfo->startToken = QString();
				session().changes().peerUpdated(
					_peer,
					Data::PeerUpdate::Flag::BotStartToken);
			}
		},
		_botStart->lifetime());
	_joinChannel->addClickHandler([=] { joinChannel(); });
	_muteUnmute->addClickHandler([=] { toggleMuteUnmute(); });
	_discuss->addClickHandler([=] { goToDiscussionGroup(); });
	setupGiftToChannelButton();
	setupDirectMessageButton();
	_reportMessages->addClickHandler([=] { reportSelectedMessages(); });
	_field->submits(
	) | rpl::on_next([=](Qt::KeyboardModifiers modifiers) {
		sendWithModifiers(modifiers);
	}, _field->lifetime());
	_field->cancelled(
	) | rpl::on_next([=] {
		if (_peer && _peer->amMonoforumAdmin()) {
			QWidget::setEnabled(false);
			crl::on_main([=] {
				QWidget::setEnabled(true);
				QWidget::setFocus();
			});
		}
		escape();
	}, _field->lifetime());
	_field->tabbed(
	) | rpl::on_next([=](not_null<Ui::InputField::TabbedRequest*> request) {
		if (_supportAutocomplete) {
			_supportAutocomplete->activate(_field.data());
			request->handled = true;
		}
	}, _field->lifetime());
	_field->heightChanges(
	) | rpl::on_next([=] {
		fieldResized();
	}, _field->lifetime());
	_field->focusedChanges(
	) | rpl::filter(rpl::mappers::_1) | rpl::on_next([=] {
		fieldFocused();
	}, _field->lifetime());
	_field->changes(
	) | rpl::on_next([=] {
		fieldChanged();
	}, _field->lifetime());
	Data::AmPremiumValue(&session()) | rpl::on_next([=] {
		checkCharsLimitation();
		updateAiButtonVisibility();
		updateSendAsFileVisibility();
		updateExpandButtonVisibility();
		updateSendButtonType();
	}, lifetime());
#ifdef Q_OS_MAC
	// Removed an ability to insert text from the menu bar
	// when the field is hidden.
	_field->shownValue(
	) | rpl::on_next([=](bool shown) {
		_field->setEnabled(shown);
	}, _field->lifetime());
#endif // Q_OS_MAC
	controller->widget()->shownValue(
	) | rpl::skip(1) | rpl::on_next([=] {
		windowIsVisibleChanged();
	}, lifetime());

	initTabbedSelector();

	_attachToggle->setClickedCallback([=] {
		const auto toggle = _attachBotsMenu && _attachBotsMenu->isHidden();
		base::call_delayed(st::historyAttach.ripple.hideDuration, this, [=] {
			if (_attachBotsMenu && toggle) {
				_attachBotsMenu->showAnimated();
			} else {
				chooseAttach();
				if (_attachBotsMenu) {
					_attachBotsMenu->hideAnimated();
				}
			}
		});
	});

	const auto rawTextEdit = _field->rawTextEdit().get();
	rpl::merge(
		_field->scrollTop().changes() | rpl::to_empty,
		base::qt_signal_producer(
			rawTextEdit,
			&QTextEdit::cursorPositionChanged)
	) | rpl::on_next([=] {
		saveDraftDelayed();
	}, _field->lifetime());

	_fieldBarCancel->hide();

	_topBar->hide();
	_scroll->hide();
	_kbScroll->hide();

	controller->chatStyle()->paletteChanged(
	) | rpl::on_next([=] {
		_scroll->updateBars();
	}, lifetime());

	_forwardPanel->itemsUpdated(
	) | rpl::on_next([=] {
		updateControlsVisibility();
		updateControlsGeometry();
	}, lifetime());

	_fieldChatStyle = InitMessageField(controller, _field, [=](
			not_null<DocumentData*> document) {
		if (_peer && Data::AllowEmojiWithoutPremium(_peer, document)) {
			return true;
		}
		showPremiumToast(document);
		return false;
	});
	CenterMessageFieldText(_field);
	InitMessageFieldFade(_field, st::historyComposeField.textBg);
	AyuFeatures::WindowMaterial::changes(this) | rpl::on_next([=] {
		if (_showAnimation) {
			showFinished();
		}
	}, lifetime());

	setupFastButtonMode();
	initAiButton();
	initSendAsFileButton();
	initExpandButton();
	initDiscardRichDraftButton();

	_fieldCharsCountManager.limitExceeds(
	) | rpl::on_next([=] {
		const auto &settings = AyuSettings::getInstance();
		const auto hide = _fieldCharsCountManager.isLimitExceeded();
		if (_silent) {
			_silent->setVisible(!hide);
		}
		if (_ttlInfo) {
			_ttlInfo->setVisible(!hide && settings.showAutoDeleteButtonInMessageField());
		}
		if (_giftToUser) {
			_giftToUser->setVisible(!hide && settings.showGiftButtonInMessageField());
		}
		if (_scheduled) {
			_scheduled->setVisible(!hide);
		}
		updateFieldSize();
		moveFieldControls();
	}, lifetime());

	_send->widthValue() | rpl::skip(1) | rpl::on_next([=] {
		updateFieldSize();
		moveFieldControls();
	}, _send->lifetime());

	_keyboard->sendCommandRequests(
	) | rpl::on_next([=](Bot::SendCommandRequest r) {
		sendBotCommand(r);
	}, lifetime());

	if (_supportAutocomplete) {
		supportInitAutocomplete();
	}
	_field->rawTextEdit()->installEventFilter(this);
	_field->setMimeDataHook(WrappedMessageFieldMimeHook([=](
			not_null<const QMimeData*> data,
			Ui::InputField::MimeAction action) {
		const auto pasteResult = Ui::CheckLargeTextPaste(
			&session(),
			_field,
			data);
		if (pasteResult.exceeds) {
			if (action == Ui::InputField::MimeAction::Check) {
				return true;
			}
			const auto text = _field->getTextWithTags();
			const auto cursor = _field->textCursor();
			sendTextAsFile(
				pasteResult.resultingText,
				text,
				cursor.position(),
				cursor.anchor());
			return true;
		}
		if (action == Ui::InputField::MimeAction::Check) {
			return canSendFiles(data);
		} else if (action == Ui::InputField::MimeAction::Insert) {
			if (confirmSendingFiles(
					data,
					std::nullopt,
					Core::ReadMimeText(data))) {
				return true;
			}
			offerRichPaste(data);
			return false;
		}
		Unexpected("action in MimeData hook.");
	}, _field));

	updateFieldSubmitSettings();

	_field->hide();
	_richDraftPreview->hide();
	_send->hide();
	_unblock->hide();
	_botStart->hide();
	_joinChannel->hide();
	_muteUnmute->hide();
	_discuss->hide();
	_reportMessages->hide();

	initVoiceRecordBar();

	_attachToggle->hide();
	_tabbedSelectorToggle->hide();
	_botKeyboardShow->hide();
	_botKeyboardHide->hide();
	_botCommandStart->hide();

	session().attachWebView().requestBots();
	rpl::merge(
		session().attachWebView().attachBotsUpdates(),
		session().changes().peerUpdates(
			Data::PeerUpdate::Flag::Rights
			| Data::PeerUpdate::Flag::StarsPerMessage
		) | rpl::filter([=](const Data::PeerUpdate &update) {
			return update.peer == _peer;
		}) | rpl::to_empty
	) | rpl::on_next([=] {
		refreshAttachBotsMenu();
	}, lifetime());

	_botKeyboardShow->addClickHandler([=] { toggleKeyboard(); });
	_botKeyboardHide->addClickHandler([=] { toggleKeyboard(); });
	_botCommandStart->addClickHandler([=] { startBotCommand(); });

	_topShadow->hide();

	_attachDragAreas = DragArea::SetupDragAreaToContainer(
		this,
		crl::guard(this, [=](not_null<const QMimeData*> d) {
			if (!_peer || isRecording()) {
				return false;
			}
			const auto topic = resolveReplyToTopic();
			return topic
				? Data::CanSendAnyOf(topic, Data::FilesSendRestrictions())
				: Data::CanSendAnyOf(_peer, Data::FilesSendRestrictions());
		}),
		crl::guard(this, [=](bool f) { _field->setAcceptDrops(f); }),
		crl::guard(this, [=] { updateControlsGeometry(); }),
		nullptr,
		crl::guard(this, [=] { return (_editMsgId != 0); }));
	_attachDragAreas.document->setDroppedCallback([=](const QMimeData *data) {
		confirmSendingFiles(data, false);
		Window::ActivateWindow(controller);
	});
	_attachDragAreas.photo->setDroppedCallback([=](const QMimeData *data) {
		confirmSendingFiles(data, true);
		Window::ActivateWindow(controller);
	});
	_attachDragAreas.photo->setArchiveDroppedCallback([=](
			const QMimeData *data) {
		const auto urls = Core::ReadMimeUrls(data);
		if (!urls.isEmpty()) {
			auto list = Ui::PreparedList();
			list.files.push_back(Storage::PrepareFilesArchive(urls));
			confirmSendingFiles(std::move(list), QString());
		}
		Window::ActivateWindow(controller);
	});

	Core::App().mediaDevices().recordAvailabilityValue(
	) | rpl::on_next([=](Webrtc::RecordAvailability value) {
		_recordAvailability = value;
		if (_list) {
			updateSendButtonType();
		}
	}, lifetime());

	session().data().newItemAdded(
	) | rpl::on_next([=](not_null<HistoryItem*> item) {
		newItemAdded(item);
	}, lifetime());

	session().data().historyChanged(
	) | rpl::on_next([=](not_null<History*> history) {
		handleHistoryChange(history);
	}, lifetime());

	session().data().viewResizeRequest(
	) | rpl::on_next([=](not_null<HistoryView::Element*> view) {
		const auto item = view->data();
		const auto history = item->history();
		if (item->mainView() == view
			&& (history == _history || history == _migrated)) {
			updateHistoryGeometry();
		}
	}, lifetime());
	session().data().viewHeightAdjusted(
	) | rpl::on_next([=](Data::Session::ViewHeightAdjusted data) {
		const auto item = data.view->data();
		const auto history = item->history();
		if (item->mainView() == data.view
			&& (history == _history || history == _migrated)) {
			updateHistoryGeometry();
		}
	}, lifetime());

	session().data().itemShowHighlightRequest(
	) | rpl::on_next([=](not_null<HistoryItem*> item) {
		const auto history = item->history();
		if (history == _history || history == _migrated) {
			if (item->mainView()) {
				enqueueMessageHighlight({ item });
				animatedScrollToItem(item->id);
			}
		}
	}, lifetime());

	session().data().itemDataChanges(
	) | rpl::filter([=](not_null<HistoryItem*> item) {
		return !_list && (item->mainView() != nullptr);
	}) | rpl::on_next([=](not_null<HistoryItem*> item) {
		item->mainView()->itemDataChanged();
	}, lifetime());

	rpl::merge(
		session().changes().peerUpdates(
			Data::PeerUpdate::Flag::IsBlocked
		) | rpl::to_empty,
		FiltersCacheController::updates()
	) | rpl::on_next(
		[=]
		{
			crl::on_main(
				this,
				[=]
				{
					if (_history) {
						_history->forceFullResize();
						if (_migrated) {
							_migrated->forceFullResize();
						}
						updateHistoryGeometry();
						update();

						for (const auto &item : _history->blocks) {
							if (!item) {
								continue;
							}
							for (const auto &msg : item->messages) {
								if (!msg) {
									continue;
								}

								_history->owner().requestViewResize(msg.get());
								_history->owner().requestItemViewRefresh(msg->data());
							}
						}
					}
				});
		},
		lifetime());

	Core::App().settings().largeEmojiChanges(
	) | rpl::on_next([=] {
		crl::on_main(this, [=] {
			updateHistoryGeometry();
		});
	}, lifetime());
	Core::App().settings().sendSubmitWayValue(
	) | rpl::on_next([=] {
		crl::on_main(this, [=] {
			updateFieldSubmitSettings();
		});
	}, lifetime());

	session().data().channelDifferenceTooLong(
	) | rpl::filter([=](not_null<ChannelData*> channel) {
		return _peer == channel.get();
	}) | rpl::on_next([=] {
		_cornerButtons.updateJumpDownVisibility();
		preloadHistoryIfNeeded();
	}, lifetime());

	session().data().userIsBotChanges(
	) | rpl::filter([=](not_null<UserData*> user) {
		return (_peer == user.get());
	}) | rpl::on_next([=](not_null<UserData*> user) {
		_list->refreshAboutView();
		_list->updateBotInfo();
		updateControlsVisibility();
		updateControlsGeometry();
	}, lifetime());

	session().data().botCommandsChanges(
	) | rpl::filter([=](not_null<PeerData*> peer) {
		return _peer && (_peer == peer);
	}) | rpl::on_next([=] {
		if (updateCmdStartShown()) {
			updateControlsVisibility();
			updateControlsGeometry();
		}
	}, lifetime());

	using EntryUpdateFlag = Data::EntryUpdate::Flag;
	session().changes().entryUpdates(
		EntryUpdateFlag::HasPinnedMessages
		| EntryUpdateFlag::ForwardDraft
	) | rpl::on_next([=](const Data::EntryUpdate &update) {
		if (_pinnedTracker
			&& (update.flags & EntryUpdateFlag::HasPinnedMessages)
			&& ((update.entry.get() == _history)
				|| (update.entry.get() == _migrated))) {
			checkPinnedBarState();
		}
		if (update.flags & EntryUpdateFlag::ForwardDraft) {
			updateForwarding();
		}
	}, lifetime());

	using HistoryUpdateFlag = Data::HistoryUpdate::Flag;
	session().changes().historyUpdates(
		HistoryUpdateFlag::MessageSent
		| HistoryUpdateFlag::BotKeyboard
		| HistoryUpdateFlag::CloudDraft
		| HistoryUpdateFlag::UnreadMentions
		| HistoryUpdateFlag::UnreadReactions
		| HistoryUpdateFlag::UnreadPollVotes
		| HistoryUpdateFlag::UnreadView
		| HistoryUpdateFlag::TopPromoted
		| HistoryUpdateFlag::ClientSideMessages
		| HistoryUpdateFlag::StreamedDrafts
	) | rpl::filter([=](const Data::HistoryUpdate &update) {
		return (_history == update.history.get());
	}) | rpl::on_next([=](const Data::HistoryUpdate &update) {
		const auto flags = update.flags;
		if (flags & HistoryUpdateFlag::MessageSent) {
			synteticScrollToY(_scroll->scrollTopMax());
		}
		if (flags & HistoryUpdateFlag::BotKeyboard) {
			updateBotKeyboard(update.history);
		}
		if (flags & HistoryUpdateFlag::CloudDraft) {
			applyCloudDraft(update.history);
		}
		if (flags & (HistoryUpdateFlag::ClientSideMessages
			| HistoryUpdateFlag::StreamedDrafts)) {
			updateSendButtonType();
		}
		if ((flags & HistoryUpdateFlag::UnreadMentions)
			|| (flags & HistoryUpdateFlag::UnreadReactions)
			|| (flags & HistoryUpdateFlag::UnreadPollVotes)) {
			_cornerButtons.updateUnreadThingsVisibility();
		}
		if (flags & HistoryUpdateFlag::UnreadView) {
			unreadCountUpdated();
		}
		if (flags & HistoryUpdateFlag::TopPromoted) {
			updateHistoryGeometry();
			updateControlsVisibility();
			updateControlsGeometry();
			this->update();
		}
	}, lifetime());

	rpl::merge(
		AyuSettings::getInstance().showAttachButtonInMessageFieldChanges() | rpl::to_empty,
		AyuSettings::getInstance().showCommandsButtonInMessageFieldChanges() | rpl::to_empty,
		AyuSettings::getInstance().showEmojiButtonInMessageFieldChanges() | rpl::to_empty,
		AyuSettings::getInstance().showMicrophoneButtonInMessageFieldChanges() | rpl::to_empty,
		AyuSettings::getInstance().showAutoDeleteButtonInMessageFieldChanges() | rpl::to_empty,
		AyuSettings::getInstance().showGiftButtonInMessageFieldChanges() | rpl::to_empty,
		AyuSettings::getInstance().showAiEditorButtonInMessageFieldChanges() | rpl::to_empty,
		base::options::lookup<bool>(Ui::kOptionHideAiButton).changes(),
		session().data().aiComposeTones().updated(),
		AyuSettings::getInstance().showAttachPopupChanges() | rpl::to_empty,
		AyuSettings::getInstance().showEmojiPopupChanges() | rpl::to_empty,
		AyuSettings::getInstance().channelBottomButtonChanges() | rpl::to_empty,
		AyuSettings::getInstance().removeMessageTailChanges() | rpl::to_empty
	) | rpl::on_next([=] {
		refreshSendGiftToggle();
		refreshAttachBotsMenu();
		updateHistoryGeometry();
		updateControlsVisibility();
		updateControlsGeometry();
		this->update();
	}, lifetime());

	AyuSettings::getInstance().translationProviderChanges(
	) | rpl::on_next([=](TranslationProvider) {
		if (_history) {
			for (const auto &block : _history->blocks) {
				for (const auto &view : block->messages) {
					const auto item = view->data();
					if (item->Has<HistoryMessageTranslation>()) {
						item->removeTranslationBit();
						_history->owner().requestItemTextRefresh(item);
					}
				}
			}
		}
	}, lifetime());

	using MessageUpdateFlag = Data::MessageUpdate::Flag;
	session().changes().messageUpdates(
		MessageUpdateFlag::Destroyed
		| MessageUpdateFlag::Edited
		| MessageUpdateFlag::ReplyMarkup
		| MessageUpdateFlag::BotCallbackSent
	) | rpl::on_next([=](const Data::MessageUpdate &update) {
		const auto flags = update.flags;
		if (flags & MessageUpdateFlag::Destroyed) {
			itemRemoved(update.item);
			return;
		}
		if (flags & MessageUpdateFlag::Edited) {
			itemEdited(update.item);
		}
		if (flags & MessageUpdateFlag::ReplyMarkup) {
			if (_keyboard->forMsgId() == update.item->fullId()) {
				updateBotKeyboard(update.item->history(), true);
			}
		}
		if (flags & MessageUpdateFlag::BotCallbackSent) {
			botCallbackSent(update.item);
		}
	}, lifetime());

	session().changes().realtimeMessageUpdates(
		MessageUpdateFlag::NewUnreadReaction
	) | rpl::on_next([=](const Data::MessageUpdate &update) {
		maybeMarkReactionsRead(update.item);
	}, lifetime());

	session().data().sentToScheduled(
	) | rpl::on_next([=](const Data::SentToScheduled &value) {
		const auto history = value.history;
		if (history == _history) {
			const auto id = value.scheduledId;
			crl::on_main(this, [=] {
				if (history == _history) {
					controller->showSection(
						std::make_shared<HistoryView::ScheduledMemento>(
							history,
							id));
				}
			});
			return;
		}
	}, lifetime());

	session().data().sentFromScheduled(
	) | rpl::on_next([=](const Data::SentFromScheduled &value) {
		if (value.item->awaitingVideoProcessing()
			&& !_sentFromScheduledTip
			&& HistoryView::ShowScheduledVideoPublished(
				controller,
				value,
				crl::guard(this, [=] { _sentFromScheduledTip = false; }))) {
			_sentFromScheduledTip = true;
		}
	}, lifetime());

	using MediaSwitch = Media::Player::Instance::Switch;
	Media::Player::instance()->switchToNextEvents(
	) | rpl::filter([=](const MediaSwitch &pair) {
		return (pair.from.type() == AudioMsgId::Type::Voice);
	}) | rpl::on_next([=](const MediaSwitch &pair) {
		scrollToCurrentVoiceMessage(pair.from.contextId(), pair.to);
	}, lifetime());

	session().user()->flagsValue(
	) | rpl::on_next([=](UserData::Flags::Change change) {
		if (change.diff & UserData::Flag::Premium) {
			if (const auto user = _peer ? _peer->asUser() : nullptr) {
				if (user->requiresPremiumToWrite()) {
					handlePeerUpdate();
				}
			}
		}
	}, lifetime());

	using PeerUpdateFlag = Data::PeerUpdate::Flag;
	session().changes().peerUpdates(
		PeerUpdateFlag::Rights
		| PeerUpdateFlag::Migration
		| PeerUpdateFlag::UnavailableReason
		| PeerUpdateFlag::IsBlocked
		| PeerUpdateFlag::Admins
		| PeerUpdateFlag::Members
		| PeerUpdateFlag::OnlineStatus
		| PeerUpdateFlag::Notifications
		| PeerUpdateFlag::ChannelAmIn
		| PeerUpdateFlag::DiscussionLink
		| PeerUpdateFlag::Slowmode
		| PeerUpdateFlag::BotStartToken
		| PeerUpdateFlag::MessagesTTL
		| PeerUpdateFlag::ChatThemeToken
		| PeerUpdateFlag::FullInfo
		| PeerUpdateFlag::ManagedBot
		| PeerUpdateFlag::StarsPerMessage
		| PeerUpdateFlag::GiftSettings
	) | rpl::filter([=](const Data::PeerUpdate &update) {
		if (update.peer.get() == _peer) {
			return true;
		} else if (update.peer->isSelf()
			&& (update.flags & PeerUpdateFlag::GiftSettings)) {
			refreshSendGiftToggle();
			updateControlsVisibility();
			updateControlsGeometry();
		}
		return false;
	}) | rpl::map([](const Data::PeerUpdate &update) {
		return update.flags;
	}) | rpl::on_next([=](Data::PeerUpdate::Flags flags) {
		if (flags & PeerUpdateFlag::Rights) {
			updateFieldPlaceholder();
			updateSendButtonType();
			_preview->checkNow(false);

			const auto was = (_sendAs != nullptr);
			refreshSendAsToggle();
			if (was != (_sendAs != nullptr)) {
				updateControlsVisibility();
				updateControlsGeometry();
				orderWidgets();
			}
		}
		if (flags & PeerUpdateFlag::Migration) {
			handlePeerMigration();
		}
		if (flags & PeerUpdateFlag::Notifications) {
			updateNotifyControls();
		}
		if (flags & PeerUpdateFlag::UnavailableReason) {
			const auto unavailable = _peer->computeUnavailableReason();
			if (!unavailable.isEmpty()) {
				const auto account = not_null(&_peer->account());
				closeCurrent();
				if (const auto primary = Core::App().windowFor(account)) {
					primary->showToast(unavailable);
				}
				return;
			}
		}
		if (flags & PeerUpdateFlag::StarsPerMessage) {
			updateFieldPlaceholder();
			updateSendButtonType();
		}
		if (flags & PeerUpdateFlag::GiftSettings) {
			refreshSendGiftToggle();
		}
		if (flags & (PeerUpdateFlag::BotStartToken
			| PeerUpdateFlag::GiftSettings)) {
			updateControlsVisibility();
			updateControlsGeometry();
		}
		if (flags & PeerUpdateFlag::Slowmode) {
			updateSendButtonType();
		}
		if ((flags & PeerUpdateFlag::ManagedBot) && _list) {
			_list->refreshAboutView();
			_list->updateBotInfo();
		}
		if (flags & (PeerUpdateFlag::IsBlocked
			| PeerUpdateFlag::Admins
			| PeerUpdateFlag::Members
			| PeerUpdateFlag::OnlineStatus
			| PeerUpdateFlag::Rights
			| PeerUpdateFlag::ChannelAmIn
			| PeerUpdateFlag::DiscussionLink)) {
			handlePeerUpdate();
		}
		if (flags & PeerUpdateFlag::MessagesTTL) {
			checkMessagesTTL();
		}
		if ((flags & PeerUpdateFlag::ChatThemeToken) && _list) {
			const auto emoji = _peer->themeToken();
			if (Data::CloudThemes::TestingColors() && !emoji.isEmpty()) {
				_peer->owner().cloudThemes().themeForTokenValue(
					emoji
				) | rpl::filter_optional(
				) | rpl::take(
					1
				) | rpl::on_next([=](const Data::CloudTheme &theme) {
					const auto &themes = _peer->owner().cloudThemes();
					const auto text = themes.prepareTestingLink(theme);
					if (!text.isEmpty()) {
						_field->setText(text);
					}
				}, _list->lifetime());
			}
		}
		if (flags & PeerUpdateFlag::FullInfo) {
			fullInfoUpdated();
			updateSendButtonType();
			if (_peer->starsPerMessageChecked()) {
				session().credits().load();
			} else if (const auto channel = _peer->asChannel()) {
				if (channel->allowedReactions().paidEnabled) {
					session().credits().load();
				}
			}
		}
	}, lifetime());

	using Type = Data::DefaultNotify;
	rpl::merge(
		session().data().notifySettings().defaultUpdates(Type::User),
		session().data().notifySettings().defaultUpdates(Type::Group),
		session().data().notifySettings().defaultUpdates(Type::Broadcast)
	) | rpl::on_next([=] {
		updateNotifyControls();
	}, lifetime());

	session().data().itemVisibilityQueries(
	) | rpl::filter([=](
			const Data::Session::ItemVisibilityQuery &query) {
		return !_showAnimation
			&& (_history == query.item->history())
			&& (query.item->mainView() != nullptr)
			&& isVisible();
	}) | rpl::on_next([=](
			const Data::Session::ItemVisibilityQuery &query) {
		if (const auto view = query.item->mainView()) {
			auto top = _list->itemTop(view);
			if (top >= 0) {
				if (top + view->height() > visibleScrollTop()
					&& top < visibleScrollBottom()) {
					*query.isVisible = true;
				}
			}
		}
	}, lifetime());

	_topBar->membersShowAreaActive(
	) | rpl::on_next([=](bool active) {
		setMembersShowAreaActive(active);
	}, _topBar->lifetime());
	_topBar->forwardSelectionRequest(
	) | rpl::on_next([=] {
		forwardSelected();
	}, _topBar->lifetime());
	_topBar->noQuoteSelectionRequest(
	) | rpl::on_next([=] {
		forwardNoQuoteSelected();
	}, _topBar->lifetime());
	_topBar->deleteSelectionRequest(
	) | rpl::on_next([=] {
		confirmDeleteSelected();
	}, _topBar->lifetime());
	_topBar->messageShotSelectionRequest(
	) | rpl::on_next([=] {
		messageShotSelected();
	}, _topBar->lifetime());
	_topBar->clearSelectionRequest(
	) | rpl::on_next([=] {
		clearSelected();
	}, _topBar->lifetime());
	_topBar->cancelChooseForReportRequest(
	) | rpl::on_next([=] {
		setChooseReportMessagesDetails({}, nullptr);
	}, _topBar->lifetime());
	_topBar->searchRequest(
	) | rpl::on_next([=] {
		if (_history) {
			controller->searchInChat(_history);
		}
	}, _topBar->lifetime());

	session().api().sendActions(
	) | rpl::filter([=](const Api::SendAction &action) {
		if (_creatingBotTopic
			&& action.history == _creatingBotTopic->owningHistory()
			&& action.replyTo.topicRootId == _creatingBotTopic->rootId()) {
			// Guard 'this' (the call reads _creatingBotTopic) and re-check
			// the topic: it may be gone or already handled by another call.
			const auto weak = base::make_weak(_creatingBotTopic);
			Ui::PostponeCall(this, [=] {
				using namespace HistoryView;
				const auto topic = base::take(_creatingBotTopic);
				if (!topic || topic != weak.get()) {
					return;
				}
				controller->showSection(
					std::make_shared<ChatMemento>(ChatViewId{
						.history = topic->owningHistory(),
						.repliesRootId = topic->rootId(),
					}),
					Window::SectionShow::Way::ClearStack);
			});
			return false;
		}
		return (action.history == _history);
	}) | rpl::on_next([=](const Api::SendAction &action) {
		const auto lastKeyboardUsed = lastForceReplyReplied(
			action.replyTo.messageId);
		if (action.replaceMediaOf) {
		} else if (action.options.scheduled) {
			cancelReplyOrSuggest(lastKeyboardUsed);
			const auto &ghost = AyuSettings::ghost(&controller->session());
			if (!ghost.isUseScheduledMessages()) {
				crl::on_main(this, [=, history = action.history]
				{
					controller->showSection(
						std::make_shared<HistoryView::ScheduledMemento>(history));
				});
			}
		} else {
			fastShowAtEnd(action.history);
			if (!_justMarkingAsRead
				&& cancelReplyOrSuggest(lastKeyboardUsed)
				&& !action.clearDraft) {
				saveCloudDraft();
			}
		}
		if (action.options.handleSupportSwitch) {
			handleSupportSwitch(action.history);
		}
	}, lifetime());

	_selfForwardsTagger = std::make_unique<HistoryView::SelfForwardsTagger>(
		controller,
		this,
		[=] { return _list; },
		_scroll.data(),
		[=] { return _history; });

	if (session().supportMode()) {
		session().data().chatListEntryRefreshes(
		) | rpl::on_next([=] {
			crl::on_main(this, [=] { checkSupportPreload(true); });
		}, lifetime());
	}

	Core::App().materializeLocalDraftsRequests(
	) | rpl::on_next([=] {
		saveFieldToHistoryLocalDraft();
	}, lifetime());

	setupScheduledToggle();
	setupSendAsToggle();
	orderWidgets();
	setupShortcuts();

	_attachToggle->setAccessibleName(tr::lng_attach(tr::now));
	_tabbedSelectorToggle->setAccessibleName(tr::lng_emoji_sticker_gif(tr::now));
	_botKeyboardShow->setAccessibleName(tr::lng_bot_keyboard_show(tr::now));
	_botKeyboardHide->setAccessibleName(tr::lng_bot_keyboard_hide(tr::now));
	_botCommandStart->setAccessibleName(tr::lng_bot_commands_start(tr::now));
	_fieldBarCancel->setAccessibleName(tr::lng_cancel(tr::now));

}

HistoryWidget::~HistoryWidget() {
	if (_history) {
		// Saving a draft on account switching.
		saveFieldToHistoryLocalDraft();
		session().api().saveDraftToCloudDelayed(_history);
		setHistory(nullptr);

		session().data().itemVisibilitiesUpdated();
	}
	// Destroy the list while our own children are still alive: ~HistoryInner
	// destroys the about view item, which fires itemRemoved() and reenters
	// updateTopBarSelection(). Left to ~QWidget's deleteChildren() that
	// happens after _topBar, an earlier child, was already deleted.
	_list = nullptr;
	_scroll->takeWidget<HistoryInner>().destroy();

	_subsectionTabsLifetime.destroy();
	_subsectionTopicsLifetime.destroy();
	_subsectionTabs = nullptr;
	setTabbedPanel(nullptr);
}

namespace HistoryWidgetDetails {

[[nodiscard]] rpl::producer<PeerData*> ActivePeerValue(
		not_null<Window::SessionController*> controller) {
	return controller->activeChatValue(
	) | rpl::map([](Dialogs::Key key) {
		const auto history = key.history();
		return history ? history->peer.get() : nullptr;
	});
}

[[nodiscard]] QString FirstEmoji(const QString &s) {
	const auto begin = s.data();
	const auto end = begin + s.size();
	for (auto ch = begin; ch != end; ch++) {
		auto length = 0;
		if (const auto e = Ui::Emoji::Find(ch, end, &length)) {
			return e->text();
		}
	}
	return QString();
}

} // namespace HistoryWidgetDetails
