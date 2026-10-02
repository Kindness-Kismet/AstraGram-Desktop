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

void HistoryWidget::showAnimated(
		Window::SlideDirection direction,
		const Window::SectionSlideParams &params) {
	validateSubsectionTabs();

	_showAnimation = nullptr;

	// If we show pinned bar here, we don't want it to change the
	// calculated and prepared scrollTop of the messages history.
	_preserveScrollTop = true;
	show();
	_topBar->finishAnimating();
	_cornerButtons.finishAnimations();
	if (_pinnedBar) {
		_pinnedBar->finishAnimating();
	}
	if (_translateBar) {
		_translateBar->finishAnimating();
	}
	if (_groupCallBar) {
		_groupCallBar->finishAnimating();
	}
	if (_requestsBar) {
		_requestsBar->finishAnimating();
	}
	const auto fromBottom = (direction == Window::SlideDirection::FromBottom);
	_topShadow->setVisible(fromBottom
		? params.withTopBarShadow
		: !params.withTopBarShadow);
	_preserveScrollTop = false;
	_stickerToast = nullptr;

	auto newContentCache = Ui::GrabWidget(this);

	hideChildWidgets();
	if (params.withTopBarShadow && !fromBottom) {
		_topShadow->show();
	}

	if (_history && !fromBottom) {
		_topBar->show();
		_topBar->setAnimatingMode(true);
	}

	_showAnimation = std::make_unique<Window::SlideAnimation>();
	_showAnimation->setDirection(direction);
	_showAnimation->setRepaintCallback([=] { update(); });
	_showAnimation->setFinishedCallback([=] { showFinished(); });
	_showAnimation->setPixmaps(params.oldContentCache, newContentCache);
	_showAnimation->start();

	activate();
}

void HistoryWidget::showFast() {
	validateSubsectionTabs();
	show();
}

void HistoryWidget::showFinished() {
	_cornerButtons.finishAnimations();
	if (_pinnedBar) {
		_pinnedBar->finishAnimating();
	}
	if (_translateBar) {
		_translateBar->finishAnimating();
	}
	if (_groupCallBar) {
		_groupCallBar->finishAnimating();
	}
	if (_requestsBar) {
		_requestsBar->finishAnimating();
	}
	_showAnimation = nullptr;
	doneShow();
	synteticScrollToY(_scroll->scrollTop());
}

void HistoryWidget::doneShow() {
	_topBar->setAnimatingMode(false);
	updateCanSendMessage();
	updateBotKeyboard();
	updateControlsVisibility();
	if (!_historyInited) {
		updateHistoryGeometry(true);
	} else {
		handlePendingHistoryUpdate();
	}
	// If we show pinned bar here, we don't want it to change the
	// calculated and prepared scrollTop of the messages history.
	_preserveScrollTop = true;
	preloadHistoryIfNeeded();
	updatePinnedViewer();
	if (_pinnedBar) {
		_pinnedBar->finishAnimating();
	}
	checkSponsoredMessageBar();
	if (_sponsoredMessageBar) {
		_sponsoredMessageBar->finishAnimating();
	}
	if (_translateBar) {
		_translateBar->finishAnimating();
	}
	if (_groupCallBar) {
		_groupCallBar->finishAnimating();
	}
	if (_requestsBar) {
		_requestsBar->finishAnimating();
	}
	checkActivation();
	controller()->widget()->setInnerFocus();
	_preserveScrollTop = false;
	checkSuggestToGigagroup();

	if (_history) {
		_history->saveMeAsActiveSubsectionThread();
	}
}

void HistoryWidget::cornerButtonsShowAtPosition(
		Data::MessagePosition position) {
	if (!_peer) {
		return;
	} else if (position == Data::UnreadMessagePosition) {
		DEBUG_LOG(("JumpToEnd(%1, %2, %3): Show at unread requested."
			).arg(_history->peer->name()
			).arg(_history->inboxReadTillId().bare
			).arg(Logs::b(_history->loadedAtBottom())));
		showHistory(_peer->id, ShowAtUnreadMsgId);
	} else if (_peer && position.fullId.peer == _peer->id) {
		showHistory(_peer->id, position.fullId.msg);
	} else if (_migrated && position.fullId.peer == _migrated->peer->id) {
		showHistory(_peer->id, -position.fullId.msg);
	}
}

Data::Thread *HistoryWidget::cornerButtonsThread() {
	return _history;
}

FullMsgId HistoryWidget::cornerButtonsCurrentId() {
	return (_migrated && _showAtMsgId < 0)
		? FullMsgId(_migrated->peer->id, -_showAtMsgId)
		: (_history && _showAtMsgId > 0)
		? FullMsgId(_history->peer->id, _showAtMsgId)
		: FullMsgId();
}

void HistoryWidget::leaveEventHook(QEvent *e) {
	if (hasMouseTracking()) {
		clearOverStates();
	}
}

void HistoryWidget::mouseMoveEvent(QMouseEvent *e) {
	const auto pos = e ? e->pos() : mapFromGlobal(QCursor::pos());
	updateOverStates(pos);
}

void HistoryWidget::updateOverStates(QPoint pos) {
	const auto isReadyToForward = readyToForward();
	const auto detailsRect = QRect(
		0,
		_field->y() - st::historySendPadding - st::historyReplyHeight,
		width() - _fieldBarCancel->width(),
		st::historyReplyHeight);
	const auto hasWebPage = !!_previewDrawPreview;
	const auto inDetails = detailsRect.contains(pos)
		&& (_editMsgId
			|| replyTo()
			|| isReadyToForward
			|| hasWebPage
			|| _suggestOptions);
	const auto inPhotoEdit = inDetails
		&& _photoEditMedia
		&& QRect(
			detailsRect.x() + st::historyReplySkip,
			(detailsRect.y()
				+ (detailsRect.height() - st::historyReplyPreview) / 2),
			st::historyReplyPreview,
			st::historyReplyPreview).contains(pos);
	const auto inClickable = inDetails;
	if (_inPhotoEdit != inPhotoEdit) {
		_inPhotoEdit = inPhotoEdit;
		if (_photoEditMedia) {
			_inPhotoEditOver.start(
				[=] { updateField(); },
				_inPhotoEdit ? 0. : 1.,
				_inPhotoEdit ? 1. : 0.,
				st::defaultMessageBar.duration);
		} else {
			_inPhotoEditOver.stop();
		}
	}
	_inDetails = inDetails && !inPhotoEdit;
	if (inClickable != _inClickable) {
		_inClickable = inClickable;
		setCursor(_inClickable ? style::cur_pointer : style::cur_default);
	}
}

void HistoryWidget::clearOverStates() {
	if (_inPhotoEdit) {
		_inPhotoEdit = false;
		if (_photoEditMedia) {
			_inPhotoEditOver.start(
				[=] { updateField(); },
				1.,
				0.,
				st::defaultMessageBar.duration);
		} else {
			_inPhotoEditOver.stop();
		}
	}
	_inDetails = false;
	if (_inClickable) {
		_inClickable = false;
		setCursor(style::cur_default);
	}
}

void HistoryWidget::leaveToChildEvent(QEvent *e, QWidget *child) {
// e -- from enterEvent() of child RpWidget
	if (hasMouseTracking()) {
		clearOverStates();
	}
}

void HistoryWidget::mouseReleaseEvent(QMouseEvent *e) {
}

void HistoryWidget::sendBotCommand(const Bot::SendCommandRequest &request) {
	sendBotCommand(request, {});
}

void HistoryWidget::sendBotCommand(
		const Bot::SendCommandRequest &request,
		Api::SendOptions options) {
	// replyTo != 0 from ReplyKeyboardMarkup, == 0 from command links
	if (_peer != request.peer.get()) {
		return;
	}

	const auto action = prepareSendAction(options);

	// 'bot' may be nullptr in case of sending from FieldAutocomplete.
	const auto toSend = (request.replyTo/* || !bot*/)
		? request.command
		: Bot::WrapCommandInChat(_peer, request.command, request.context);

	auto message = Api::MessageToSend(action);
	message.textWithTags = { toSend, TextWithTags::Tags() };
	message.action.replyTo = request.replyTo
		? ((!_peer->isUser()/* && (botStatus == 0 || botStatus == 2)*/)
			? request.replyTo
			: replyTo())
		: FullReplyTo();

	const auto ephemeral = session().ephemeralMessages().wouldSend(message);
	if (!ephemeral && showSlowmodeError()) {
		return;
	}
	if (!ephemeral) {
		const auto withPaymentApproved = [=](int approved) {
			auto copy = options;
			copy.starsApproved = approved;
			sendBotCommand(request, copy);
		};
		const auto checked = checkSendPayment(
			1,
			action.options,
			withPaymentApproved);
		if (!checked) {
			return;
		}
	}

	const auto forMsgId = _keyboard->forMsgId();
	const auto lastKeyboardUsed = (forMsgId == request.replyTo.messageId)
		&& (forMsgId == FullMsgId(_peer->id, _history->lastKeyboardId));

	session().api().sendMessage(std::move(message));
	if (request.replyTo) {
		if (_replyTo == request.replyTo) {
			cancelReply();
			saveCloudDraft();
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

	setInnerFocus();
}

void HistoryWidget::hideSingleUseKeyboard(FullMsgId replyToId) {
	if (!_peer || _peer->id != replyToId.peer) {
		return;
	}

	const auto lastKeyboardUsed = (_keyboard->forMsgId() == replyToId)
		&& (_keyboard->forMsgId()
			== FullMsgId(_peer->id, _history->lastKeyboardId));
	if (replyToId) {
		if (_replyTo.messageId == replyToId) {
			cancelReply();
			saveCloudDraft();
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
}

bool HistoryWidget::insertBotCommand(const QString &cmd) {
	if (!_canSendTexts) {
		return false;
	}

	const auto insertingInlineBot = !cmd.isEmpty() && (cmd.at(0) == '@');
	auto toInsert = cmd;
	if (!toInsert.isEmpty() && !insertingInlineBot) {
		auto bot = (PeerData*)(_peer->asUser());
		if (!bot) {
			if (const auto link = HistoryView::Element::HoveredLink()) {
				bot = link->data()->fromOriginal().get();
			}
		}
		if (bot && (!bot->isUser() || !bot->asUser()->isBot())) {
			bot = nullptr;
		}
		const auto username = bot ? bot->asUser()->username() : QString();
		const auto botStatus = _peer->isChat()
			? _peer->asChat()->botStatus
			: _peer->isMegagroup()
			? _peer->asChannel()->mgInfo->botStatus
			: Data::BotStatus::NoBots;
		if ((toInsert.indexOf('@') < 0)
			&& !username.isEmpty()
			&& (botStatus != Data::BotStatus::NoBots)) {
			toInsert += '@' + username;
		}
	}
	toInsert += ' ';

	if (!insertingInlineBot) {
		const auto &textWithTags = _field->getTextWithTags();
		auto textWithTagsToSet = TextWithTags();
		const auto m = QRegularExpression(
			u"^/[A-Za-z_0-9]{0,64}(@[A-Za-z_0-9]{0,32})?(\\s|$)"_q).match(
				textWithTags.text);
		textWithTagsToSet = m.hasMatch()
			? _field->getTextWithTagsPart(m.capturedLength())
			: textWithTags;
		textWithTagsToSet.text = toInsert + textWithTagsToSet.text;
		for (auto &tag : textWithTagsToSet.tags) {
			tag.offset += toInsert.size();
		}
		_field->setTextWithTags(textWithTagsToSet);

		auto cur = QTextCursor(_field->textCursor());
		cur.movePosition(QTextCursor::End);
		_field->setTextCursor(cur);
	} else {
		setFieldText(
			{ toInsert, TextWithTags::Tags() },
			TextUpdateEvent::SaveDraft,
			Ui::InputField::HistoryAction::NewEntry);
		setInnerFocus();
		return true;
	}
	return false;
}

void HistoryWidget::insertTextAtCursor(const QString &text) {
	if (!_canSendTexts) {
		return;
	}
	Menu::InsertTextAtCursor(_field, text);
}

bool HistoryWidget::eventFilter(QObject *obj, QEvent *e) {
	if (e->type() == QEvent::KeyPress) {
		const auto k = static_cast<QKeyEvent*>(e);
		if ((k->modifiers() & kCommonModifiers) == Qt::ControlModifier) {
			if (k->key() == Qt::Key_Up) {
#ifdef Q_OS_MAC
				// Cmd + Up is used instead of Home.
				if (fieldHasSendText()
					&& !base::options::value<bool>(
						HistoryView::Controls::kOptionMacCmdReplyImmediately)) {
					return false;
				}
#endif
				return replyToPreviousMessage();
			} else if (k->key() == Qt::Key_Down) {
#ifdef Q_OS_MAC
				// Cmd + Down is used instead of End.
				if (fieldHasSendText()
					&& !base::options::value<bool>(
						HistoryView::Controls::kOptionMacCmdReplyImmediately)) {
					return false;
				}
#endif
				return replyToNextMessage();
			}
		}
	}
	return RpWidget::eventFilter(obj, e);
}

bool HistoryWidget::floatPlayerHandleWheelEvent(QEvent *e) {
	return _peer ? _scroll->viewportEvent(e) : false;
}

QRect HistoryWidget::floatPlayerAvailableRect() {
	return _peer ? mapToGlobal(visibleScrollGeometry()) : mapToGlobal(rect());
}

bool HistoryWidget::readyToForward() const {
	return _canSendMessages && !_forwardPanel->empty();
}

bool HistoryWidget::hasSilentToggle() const {
	return _peer
		&& _peer->isBroadcast()
		&& Data::CanSendAnything(_peer)
		&& !session().data().notifySettings().silentPostsUnknown(_peer);
}

void HistoryWidget::handleSupportSwitch(not_null<History*> updated) {
	if (_history != updated || !session().supportMode()) {
		return;
	}

	const auto setting = session().settings().supportSwitch();
	if (auto method = Support::GetSwitchMethod(setting)) {
		crl::on_main(this, std::move(method));
	}
}

bool HistoryWidget::isBotStart() const {
	const auto user = _peer ? _peer->asUser() : nullptr;
	if (!user
		|| !user->isBot()
		|| !_canSendMessages) {
		return false;
	} else if (!user->botInfo->startToken.isEmpty()) {
		return true;
	} else if (_history->isEmpty() && !_history->lastMessage()) {
		return true;
	}
	return false;
}

bool HistoryWidget::isReportMessages() const {
	return _peer && _chooseForReport && _chooseForReport->active;
}

bool HistoryWidget::isBlocked() const {
	return _peer && _peer->isUser() && _peer->asUser()->isBlocked();
}

bool HistoryWidget::isJoinChannel() const {
	if (const auto channel = _peer ? _peer->asChannel() : nullptr) {
		return !channel->amIn() && !channel->isMonoforum();
	}
	return false;
}

bool HistoryWidget::isChoosingTheme() const {
	return _chooseTheme && _chooseTheme->shouldBeShown();
}

bool HistoryWidget::isMuteUnmute() const {
	const auto &settings = ExtrasSettings::getInstance();
	if (settings.channelBottomButton() == ChannelBottomButton::Hidden) {
		return false;
	}

	return _peer
		&& ((_peer->isBroadcast() && !_peer->asChannel()->canPostMessages())
			|| (_peer->isGigagroup() && !Data::CanSendAnything(_peer))
			|| _peer->isRepliesChat()
			|| _peer->isVerifyCodes());
}

bool HistoryWidget::isSearching() const {
	return _composeSearch != nullptr;
}

auto HistoryWidget::computeSendButtonType() const {
	using Type = Ui::SendButton::Type;

	if (_editMsgId) {
		return Type::Save;
	} else if (_isInlineBot) {
		return Type::Cancel;
	} else if (showRecordButton()) {
		const auto both = Webrtc::RecordAvailability::VideoAndAudio;
		const auto video = Core::App().settings().recordVideoMessages();
		return (video && _recordAvailability == both)
			? Type::Round
			: Type::Record;
	}
	return Type::Send;
}

bool HistoryWidget::canSendAiComposeDirect() const {
	using Type = Ui::SendButton::Type;
	return _history
		&& _peer
		&& (computeSendButtonType() == Type::Send)
		&& !_peer->slowmodeSecondsLeft()
		&& !(_peer->slowmodeApplied() && _history->latestSendingMessage())
		&& !_peer->starsPerMessageChecked();
}

SendMenu::Details HistoryWidget::sendButtonMenuDetails() const {
	using Type = Ui::SendButton::Type;
	if (showStopButton()) {
		return {};
	}
	const auto type = computeSendButtonType();
	if (type == Type::Save) {
		return saveMenuDetails();
	} else if (type != Type::Send) {
		return {};
	}
	return sendButtonDefaultDetails();
}

bool HistoryWidget::showRecordButton() const {
	// 录制从加号菜单发起，发送键只在录制条出现期间显示录制状态。
	return !_voiceRecordBar->isHidden()
		&& !_voiceRecordBar->isListenState();
}

bool HistoryWidget::showInlineBotCancel() const {
	return _inlineBot && !_inlineLookingUpBot;
}

bool HistoryWidget::showStopButton() const {
	using Type = Ui::SendButton::Type;

	const auto type = computeSendButtonType();
	if ((_send->isDown() && _send->type() != Type::Stop)
		|| !_voiceRecordBar->isHidden()
		|| type == Type::Save
		|| type == Type::Cancel) {
		return false;
	}
	const auto streamed = _history
		? _history->streamedDraftsIfExists()
		: nullptr;
	return streamed && streamed->stoppableFor(MsgId(0));
}

void HistoryWidget::updateSendButtonType() {
	using Type = Ui::SendButton::Type;

	const auto type = showStopButton()
		? Type::Stop
		: computeSendButtonType();
	const auto forbidden = [&] {
		if (type != Type::Record && type != Type::Round) {
			return false;
		}
		if (!_peer) {
			return false;
		}
		const auto restriction = (type == Type::Record)
			? ChatRestriction::SendVoiceMessages
			: ChatRestriction::SendVideoMessages;
		return !!Data::RestrictionError(_peer, restriction);
	}();
	// This logic is duplicated in ChatWidget.
	const auto disabledBySlowmode = _peer
		&& _peer->slowmodeApplied()
		&& (_history->latestSendingMessage() != nullptr);
	const auto delay = [&] {
		return (type != Type::Cancel
			&& type != Type::Save
			&& type != Type::Stop
			&& _peer)
			? _peer->slowmodeSecondsLeft()
			: 0;
	}();
	const auto ephemeralReply = session().ephemeralMessages()
		.isEphemeralBotReply(replyTo().messageId);
	const auto perMessage = (_peer && !ephemeralReply)
		? _peer->starsPerMessageChecked()
		: 0;
	const auto richPage = shownRichMessage();
	const auto richMessage = (richPage != nullptr);
	_sendLockBadge.fire(richMessage
		&& !session().premium()
		&& Iv::RichPageUsesPremiumFormatting(*richPage));
	const auto messages = !_peer
		? 0
		: _voiceRecordBar->isListenState()
		? 1
		: ComputeSendingMessagesCount(_history, {
			.forward = &_forwardPanel->items(),
			.text = richMessage ? nullptr : &_field->getTextWithTags(),
			.richMessage = richMessage,
		});
	const auto stars = perMessage ? (perMessage * messages) : 0;
	_send->setState({
		.type = (delay > 0) ? Type::Slowmode : type,
		.slowmodeDelay = delay,
		.starsToSend = stars,
		.forbidden = forbidden,
	});
	_send->setDisabled(disabledBySlowmode
		&& (type == Type::Send
			|| type == Type::Record
			|| type == Type::Round));

	if (delay != 0) {
		base::call_delayed(
			kRefreshSlowmodeLabelTimeout,
			this,
			[=] { updateSendButtonType(); });
	}
}

bool HistoryWidget::refreshBotMenuButton() {
	const auto bot = (_peer && _peer->isUser() && _peer->asUser()->isBot())
		? _peer->asUser()
		: nullptr;
	constexpr auto kSmallMenuAfter = 10;
	auto buttonChanged = false;
	if (!bot
		|| (bot->botInfo->botMenuButtonUrl.isEmpty()
			&& bot->botInfo->commands.empty())) {
		buttonChanged = (_botMenu.button != nullptr);
		_botMenu.button.destroy();
	} else if (!_botMenu.button) {
		buttonChanged = true;
		_botMenu.text = bot->botInfo->botMenuButtonText;
		_botMenu.small = (_fieldCharsCountManager.count() > kSmallMenuAfter);
		if (_botMenu.small) {
			if (const auto e = FirstEmoji(_botMenu.text); !e.isEmpty()) {
				_botMenu.text = e;
			}
		}
		_botMenu.button.create(
			this,
			(_botMenu.text.isEmpty()
				? tr::lng_bot_menu_button()
				: rpl::single(_botMenu.text)),
			st::historyBotMenuButton);
		orderWidgets();

		_botMenu.button->setFullRadius(true);
		_botMenu.button->setClickedCallback([=] {
			const auto user = _peer ? _peer->asUser() : nullptr;
			const auto bot = (user && user->isBot()) ? user : nullptr;
			if (bot && !bot->botInfo->botMenuButtonUrl.isEmpty()) {
				session().attachWebView().open({
					.bot = bot,
					.context = { .controller = controller() },
					.button = {
						.url = bot->botInfo->botMenuButtonUrl.toUtf8(),
					},
					.source = InlineBots::WebViewSourceBotMenu(),
				});
			} else if (_autocomplete && !_autocomplete->isHidden()) {
				_autocomplete->hideAnimated();
			} else if (_autocomplete) {
				_autocomplete->showFiltered(_peer, "/", true);
			}
		});
		_botMenu.button->widthValue(
		) | rpl::on_next([=](int width) {
			if (width > st::historyBotMenuMaxWidth) {
				_botMenu.button->setFullWidth(st::historyBotMenuMaxWidth);
			} else {
				updateFieldSize();
			}
		}, _botMenu.button->lifetime());
	}
	const auto textSmall = _fieldCharsCountManager.count() > kSmallMenuAfter;
	const auto textChanged = _botMenu.button
		&& ((_botMenu.text != bot->botInfo->botMenuButtonText)
			|| (_botMenu.small != textSmall));
	if (textChanged) {
		_botMenu.text = bot->botInfo->botMenuButtonText;
		if ((_botMenu.small = textSmall)) {
			if (const auto e = FirstEmoji(_botMenu.text); !e.isEmpty()) {
				_botMenu.text = e;
			}
		}
		_botMenu.button->setText(_botMenu.text.isEmpty()
			? tr::lng_bot_menu_button()
			: rpl::single(_botMenu.text));
	}
	return buttonChanged || textChanged;
}

void HistoryWidget::mousePressEvent(QMouseEvent *e) {
	if (!_list) {
		// Remove focus from the chats list search.
		setFocus();

		// Set it back to the chats list so that typing filter chats.
		controller()->widget()->setInnerFocus();
		return;
	}
	const auto isReadyToForward = readyToForward();
	if (_editMsgId
		&& (_inDetails || _inPhotoEdit)
		&& (e->button() == Qt::RightButton)) {
		_mediaEditManager.showMenu(
			_list,
			[=] { mouseMoveEvent(nullptr); },
			fieldHasSendText(),
			controller()->uiShow());
	} else if (_inPhotoEdit && _photoEditMedia) {
		EditCaptionBox::StartPhotoEdit(
			controller(),
			_photoEditMedia,
			{ _history->peer->id, _editMsgId },
			_field->getTextWithTags(),
			suggestOptions(),
			_mediaEditManager.spoilered(),
			_mediaEditManager.invertCaption(),
			crl::guard(_list, [=] { cancelEdit(); }));
	} else if (!_inDetails) {
		return;
	} else if (_previewDrawPreview) {
		editDraftOptions();
	} else if (_editMsgId) {
		if (_suggestOptions) {
			_suggestOptions->edit();
		} else {
			controller()->showPeerHistory(
				_peer,
				Window::SectionShow::Way::Forward,
				_editMsgId);
		}
	} else if (_replyTo
		&& ((e->modifiers() & Qt::ControlModifier)
			|| (e->button() != Qt::LeftButton))) {
		jumpToReply(_replyTo);
	} else if (_replyTo
		|| (isReadyToForward && e->button() == Qt::LeftButton)) {
		editDraftOptions();
	} else if (isReadyToForward) {
		_forwardPanel->editToNextOption();
	} else if (_kbReplyTo) {
		controller()->showPeerHistory(
			_kbReplyTo->history()->peer->id,
			Window::SectionShow::Way::Forward,
			_kbReplyTo->id);
	} else if (_suggestOptions) {
		_suggestOptions->edit();
	}
}

void HistoryWidget::editDraftOptions() {
	Expects(_history != nullptr);

	const auto history = _history;
	const auto reply = _replyTo;
	const auto suggest = suggestOptions();
	const auto webpage = _preview->draft();
	const auto forward = _forwardPanel->draft();

	const auto done = [=](
			FullReplyTo replyTo,
			Data::WebPageDraft webpage,
			Data::ForwardDraft forward) {
		if (replyTo) {
			replyToMessage(replyTo);
		} else {
			cancelReply();
		}
		history->setForwardDraft(MsgId(), PeerId(), std::move(forward));
		_preview->apply(webpage);
	};
	const auto replyToId = reply.messageId;
	const auto highlight = crl::guard(this, [=](FullReplyTo to) {
		jumpToReply(to);
	});

	using namespace HistoryView::Controls;
	EditDraftOptions({
		.show = controller()->uiShow(),
		.history = history,
		.draft = Data::Draft(_field, reply, suggest, _preview->draft()),
		.usedLink = _preview->link(),
		.forward = _forwardPanel->draft(),
		.links = _preview->links(),
		.resolver = _preview->resolver(),
		.done = done,
		.highlight = highlight,
		.clearOldDraft = [=] {
			ClearDraftReplyTo(history, MsgId(), PeerId(), replyToId);
		},
	});
}

void HistoryWidget::jumpToReply(FullReplyTo to) {
	if (const auto item = session().data().message(to.messageId)) {
		JumpToMessageClickHandler(item, {}, to.highlight())->onClick({});
	}
}

void HistoryWidget::keyPressEvent(QKeyEvent *e) {
	if (!_history) return;

	const auto scroll = [&] {
		stopScrollAnimation();
		_scroll->keyPressEvent(e);
	};
	const auto commonModifiers = e->modifiers() & kCommonModifiers;
	if (e->key() == Qt::Key_Escape) {
		if (hasFocus()) {
			escape();
		} else {
			e->ignore();
		}
	} else if (e->key() == Qt::Key_Back) {
		_cancelRequests.fire({});
	} else if (e->key() == Qt::Key_PageDown) {
		scroll();
	} else if (e->key() == Qt::Key_PageUp) {
		scroll();
	} else if (e->key() == Qt::Key_Down && !commonModifiers) {
		scroll();
	} else if (e->key() == Qt::Key_Up && !commonModifiers) {
		if (!_field->empty()
			|| !canWriteMessage()
			|| _editMsgId
			|| _replyTo) {
			scroll();
		} else {
			const auto last = _history->lastMessage();
			if (last && last->isLocal()) {
				if (last->media() && last->media()->allowsEdit()) {
					if (const auto view = last->mainView()) {
						controller()->show(Box(Ui::EditCaptionBox, view));
					}
				} else {
					scroll();
				}
				return;
			}
			const auto item = _history
				? _history->lastEditableMessage()
				: nullptr;
			if (item) {
				editMessage(item, {});
			} else {
				scroll();
			}
		}
	} else if (e->key() == Qt::Key_Up
		&& commonModifiers == Qt::ControlModifier) {
		if (!replyToPreviousMessage()) {
			e->ignore();
		}
	} else if (e->key() == Qt::Key_Down
		&& commonModifiers == Qt::ControlModifier) {
		if (!replyToNextMessage()) {
			e->ignore();
		}
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		if (!_botStart->isHidden()) {
			sendBotStartCommand();
		}
		if (!_canSendMessages) {
			const auto submitting = Ui::InputField::ShouldSubmit(
				Core::App().settings().sendSubmitWay(),
				e->modifiers());
			if (submitting) {
				sendWithModifiers(e->modifiers());
			}
		}
	} else if ((e->key() == Qt::Key_O)
		&& (e->modifiers() == Qt::ControlModifier)) {
		chooseAttach();
	} else {
		e->ignore();
	}
}

void HistoryWidget::handlePeerMigration() {
	const auto current = _peer->migrateToOrMe();
	const auto chat = current->migrateFrom();
	if (!chat) {
		return;
	}
	const auto channel = current->asChannel();
	Assert(channel != nullptr);

	if (_peer != channel) {
		showHistory(
			channel->id,
			(_showAtMsgId > 0) ? (-_showAtMsgId) : _showAtMsgId);
		channel->session().api().chatParticipants().requestCountDelayed(
			channel);
	} else {
		_migrated = _history->migrateFrom();
		_list->notifyMigrateUpdated();
		setupPinnedTracker();
		setupGroupCallBar();
		setupRequestsBar();
		updateHistoryGeometry();
	}
	const auto from = chat->owner().historyLoaded(chat);
	const auto to = channel->owner().historyLoaded(channel);
	if (from
		&& to
		&& !from->isEmpty()
		&& (!from->loadedAtBottom() || !to->loadedAtTop())) {
		from->clear(History::ClearType::Unload);
	}
}

HistoryItem *HistoryWidget::lookupReplyNavItem(FullMsgId itemId) const {
	if (!_history) {
		return nullptr;
	} else if (const auto regular = session().data().message(itemId)) {
		return regular;
	}
	for (const auto &item : _history->clientSideMessages()) {
		if (item->fullId() == itemId) {
			return item;
		}
	}
	return nullptr;
}

bool HistoryWidget::replyToPreviousMessage() {
	if (!_history
		|| _editMsgId
		|| _history->isForum()
		|| (_replyTo && _replyTo.messageId.peer != _history->peer->id)) {
		return false;
	}
	const auto fullId = FullMsgId(
		_history->peer->id,
		(_field->isVisible()
			? _replyTo.messageId.msg
			: _highlighter.latestSingleHighlightedMsgId()));
	const auto skipLocal = [](HistoryView::Element *from) {
		auto view = from;
		while (view
			&& view->data()->isLocal()
			&& !CanReplyToEphemeral(view->data())) {
			view = view->previousDisplayedInBlocks();
		}
		return view;
	};
	if (const auto item = lookupReplyNavItem(fullId)) {
		if (const auto view = item->mainView()) {
			if (const auto target = skipLocal(
					view->previousDisplayedInBlocks())) {
				const auto previous = target->data();
				controller()->showMessage(previous);
				if (_field->isVisible()) {
					replyToMessage(previous);
				}
				return true;
			}
		}
	} else if (const auto target = skipLocal(
			_history->findLastDisplayed())) {
		const auto previous = target->data();
		controller()->showMessage(previous);
		if (_field->isVisible()) {
			replyToMessage(previous);
		}
		return true;
	}
	return false;
}

bool HistoryWidget::replyToNextMessage() {
	if (!_history
		|| _editMsgId
		|| _history->isForum()
		|| (_replyTo && _replyTo.messageId.peer != _history->peer->id)) {
		return false;
	}
	const auto fullId = FullMsgId(
		_history->peer->id,
		(_field->isVisible()
			? _replyTo.messageId.msg
			: _highlighter.latestSingleHighlightedMsgId()));
	if (const auto item = lookupReplyNavItem(fullId)) {
		if (const auto view = item->mainView()) {
			auto next = view->nextDisplayedInBlocks();
			while (next
				&& next->data()->isLocal()
				&& !CanReplyToEphemeral(next->data())) {
				next = next->nextDisplayedInBlocks();
			}
			if (next) {
				const auto item = next->data();
				controller()->showMessage(item);
				if (_field->isVisible()) {
					replyToMessage(item);
				}
			} else {
				_highlighter.clear();
				cancelReply(false);
			}
			return true;
		}
	}
	return false;
}

void HistoryWidget::forwardSelected() {
	if (!_list) {
		return;
	}
	const auto weak = base::make_weak(this);
	Window::ShowNewForwardMessagesBox(
		controller(),
		_list->getSelectedForwardItems(),
		false,
		false,
		[=] {
			if (const auto strong = weak.get()) {
				strong->clearSelected();
			}
		});
}

void HistoryWidget::forwardNoQuoteSelected() {
	if (!_list) {
		return;
	}
	const auto weak = base::make_weak(this);
	Window::ShowNewForwardMessagesBox(
		controller(),
		_list->getSelectedForwardItems(),
		true,
		false,
		[=] {
			if (const auto strong = weak.get()) {
				strong->clearSelected();
			}
		});
}

void HistoryWidget::forwardNoCaptionSelected() {
	if (!_list) {
		return;
	}
	const auto weak = base::make_weak(this);
	Window::ShowNewForwardMessagesBox(
		controller(),
		_list->getSelectedForwardItems(),
		false,
		true,
		[=] {
			if (const auto strong = weak.get()) {
				strong->clearSelected();
			}
		});
}

void HistoryWidget::confirmDeleteSelected() {
	if (!_list) return;

	auto ids = _list->getSelectedItems();
	auto ephemeral = _list->getSelectedEphemeral();
	if (ids.empty()) {
		if (!ephemeral.empty()) {
			ConfirmDeleteSelectedEphemeral(
				controller()->uiShow(),
				std::move(ephemeral),
				crl::guard(this, [=] { clearSelected(); }));
		}
		return;
	}
	for (const auto &item : ephemeral) {
		ids.push_back(item->fullId());
	}
	const auto items = session().data().idsToItems(ids);
	if (ephemeral.empty() && CanCreateModerateMessagesBox(items)) {
		const auto opt = DefaultModerateMessagesBoxOptions();
		controller()->show(Box(
			CreateModerateMessagesBox,
			ModerateMessagesBoxEntry{ .items = items },
			crl::guard(this, [=] { clearSelected(); }),
			opt));
	} else {
		auto box = Box<DeleteMessagesBox>(&session(), std::move(ids));
		box->setDeleteConfirmedCallback(crl::guard(this, [=] {
			clearSelected();
		}));
		controller()->show(std::move(box));
	}
}

void HistoryWidget::messageShotSelected() {
	if (!_list) {
		return;
	}

	ExtrasFeatures::MessageShot::Wrapper(
		_list.data(),
		[=] { clearSelected(); });
}

void HistoryWidget::escape() {
	if (_composeSearch) {
		if (_nonEmptySelection) {
			clearSelected();
		} else {
			_composeSearch->hideAnimated();
		}
	} else if (_chooseForReport) {
		controller()->clearChooseReportMessages();
	} else if (_nonEmptySelection && _list) {
		clearSelected();
	} else if (_isInlineBot) {
		cancelInlineBot();
	} else if (_editMsgId) {
		if (_replyEditMsg
			&& EditTextChanged(_replyEditMsg, _field->getTextWithTags())) {
			controller()->show(Ui::MakeConfirmBox({
				.text = tr::lng_cancel_edit_post_sure(),
				.confirmed = crl::guard(this, [this](Fn<void()> &&close) {
					if (_editMsgId) {
						cancelEdit();
						close();
					}
				}),
				.confirmText = tr::lng_cancel_edit_post_yes(),
				.cancelText = tr::lng_cancel_edit_post_no(),
			}));
		} else {
			cancelEdit();
		}
	} else if (readyToForward() && _history) {
		_history->setForwardDraft(MsgId(), PeerId(), {});
	} else if (_autocomplete && !_autocomplete->isHidden()) {
		_autocomplete->hideAnimated();
	} else if ((_replyTo || _suggestOptions)
		&& _field->getTextWithTags().empty()) {
		cancelReplyOrSuggest();
	} else if (auto &voice = _voiceRecordBar; voice->isActive()) {
		voice->showDiscardBox(nullptr, anim::type::normal);
	} else {
		_cancelRequests.fire({});
	}
}

void HistoryWidget::clearSelected() {
	if (_list) {
		_list->clearSelected();
	}
}

HistoryItem *HistoryWidget::getItemFromHistoryOrMigrated(
		MsgId genericMsgId) const {
	return (genericMsgId < 0 && -genericMsgId < ServerMaxMsgId && _migrated)
		? session().data().message(_migrated->peer, -genericMsgId)
		: _peer
		? session().data().message(_peer, genericMsgId)
		: nullptr;
}

MessageIdsList HistoryWidget::getSelectedItems() const {
	return _list ? _list->getSelectedItems() : MessageIdsList();
}

void HistoryWidget::updateTopBarChooseForReport() {
	if (_chooseForReport && _chooseForReport->active) {
		_topBar->showChooseMessagesForReport(
			_chooseForReport->reportInput);
	} else {
		_topBar->clearChooseMessagesForReport();
	}
	updateTopBarSelection();
	updateControlsVisibility();
	updateControlsGeometry();
}

void HistoryWidget::updateTopBarSelection() {
	if (!_list) {
		_topBar->showSelected(HistoryView::TopBarWidget::SelectedState {});
		return;
	}

	auto selectedState = _list->getSelectionState();
	_nonEmptySelection = (selectedState.count > 0)
		|| selectedState.textSelected;
	_topBar->showSelected(selectedState);

	if ((selectedState.count > 0) && _composeSearch) {
		_composeSearch->hideAnimated();
	}

	const auto transparent = Qt::WA_TransparentForMouseEvents;
	if (selectedState.count == 0) {
		_reportMessages->clearState();
		_reportMessages->setAttribute(transparent);
		_reportMessages->setColorOverride(st::windowSubTextFg->c);
	} else if (_reportMessages->testAttribute(transparent)) {
		_reportMessages->setAttribute(transparent, false);
		_reportMessages->setColorOverride(std::nullopt);
	}
	_reportMessages->setText(selectedState.count
		? tr::lng_report_messages_count(
			tr::now,
			lt_count,
			selectedState.count,
			tr::upper)
		: tr::lng_report_messages_none(tr::now, tr::upper));
	updateControlsVisibility();
	updateHistoryGeometry();
	if (!controller()->isLayerShown()
		&& !Core::App().passcodeLocked()) {
		if (isSearching() && !_nonEmptySelection) {
			_composeSearch->setInnerFocus();
		} else if (_nonEmptySelection
			|| (_list && _list->wasSelectedText())
			|| isRecording()
			|| isBotStart()
			|| isBlocked()
			|| !_richDraftPreview->isHidden()
			|| (!_canSendTexts && !_editMsgId)
			|| (_list
				&& _list->hasFocus()
				&& Ui::ScreenReaderModeActive())) {
			_list->setFocus();
		} else {
			_field->setFocus();
		}
	}
	_topBar->update();
	update();
}
