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
#include <QtGui/QPainterPath>

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

void HistoryWidget::drawField(Painter &p, const QRect &rect) {
	_repaintFieldScheduled = false;

	auto backy = _field->y() - st::historySendPadding;
	auto backh = fieldHeight() + 2 * st::historySendPadding;
	auto hasForward = readyToForward();
	auto drawMsgText = (_editMsgId || _replyTo) ? _replyEditMsg : _kbReplyTo;
	if (_editMsgId
		|| _replyTo
		|| hasForward
		|| _kbReplyTo
		|| _previewDrawPreview
		|| _suggestOptions) {
		backy -= st::historyReplyHeight;
		backh += st::historyReplyHeight;
	}
	p.setInactive(
		controller()->isGifPausedAtLeastFor(Window::GifPauseReason::Any));
	// 无壁纸时用独立底色区分输入区，四周保留悬浮间距。
	const auto flatBackground = AyuSettings::getInstance().disableChatBackground();
	const auto capsuleMargin = st::historyComposeCapsuleMargin;
	const auto capsuleRect = myrtlrect(
		capsuleMargin,
		backy,
		width() - 2 * capsuleMargin,
		backh);
	const auto halfStroke = st::lineWidth / 2.;
	const auto capsuleOutline = QRectF(capsuleRect).adjusted(
		halfStroke, halfStroke, -halfStroke, -halfStroke);
	// 同步限制两个方向的半径，避免单行高度被截成椭圆弧。
	const auto capsuleRadius = std::min(
		qreal(st::historyComposeCapsuleRadius),
		capsuleOutline.height() / 2.);
	{
		auto hq = PainterHighQualityEnabler(p);
		auto path = QPainterPath();
		path.addRoundedRect(capsuleOutline, capsuleRadius, capsuleRadius);
		p.save();
		p.setClipPath(path, Qt::IntersectClip);
		paintFrostedBackground(p, capsuleRect, (flatBackground
			? st::windowBgOver
			: st::historyComposeAreaBg)->c);
		p.restore();
		p.setPen(QPen((flatBackground
			? st::filterInputBorderFg
			: st::windowDividerFg)->c, st::lineWidth));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(capsuleOutline, capsuleRadius, capsuleRadius);
	}
	// 回复/编辑/转发条的内容整体右移,落进胶囊内部
	p.translate(capsuleMargin, 0);
	const auto fullWidth = width() - 2 * capsuleMargin;

	const auto media = (!_previewDrawPreview && drawMsgText)
		? drawMsgText->media()
		: nullptr;
	const auto poll = media ? media->poll() : nullptr;
	const auto pollAnswer = poll
		? poll->answerByOption(_replyTo.pollOption)
		: nullptr;
	const auto pollMediaPtr = pollAnswer
		? &pollAnswer->media
		: (poll && _replyTo.pollOption.isEmpty())
		? &poll->attachedMedia
		: nullptr;
	const auto pollMediaHasPreview = pollMediaPtr
		&& (pollMediaPtr->photo || pollMediaPtr->document);
	const auto hasPreview = pollMediaHasPreview
		|| (media && media->hasReplyPreview());
	const auto preview = _mediaEditManager
		? _mediaEditManager.mediaPreview()
		: pollMediaHasPreview
		? (pollMediaPtr->photo
			? pollMediaPtr->photo->getReplyPreview(drawMsgText)
			: pollMediaPtr->document->getReplyPreview(drawMsgText))
		: (media && media->hasReplyPreview())
		? media->replyPreview()
		: nullptr;
	const auto spoilered = _mediaEditManager.spoilered();
	if (!spoilered) {
		_replySpoiler = nullptr;
	} else if (!_replySpoiler) {
		_replySpoiler = std::make_unique<Ui::SpoilerAnimation>([=] {
			updateField();
		});
	}

	if (_previewDrawPreview) {
		st::historyLinkIcon.paint(
			p,
			st::historyReplyIconPosition + QPoint(0, backy),
			fullWidth);
		const auto textTop = backy + st::msgReplyPadding.top();
		auto previewLeft = st::historyReplySkip;

		const auto to = QRect(
			previewLeft,
			backy + (st::historyReplyHeight - st::historyReplyPreview) / 2,
			st::historyReplyPreview,
			st::historyReplyPreview);
		if (_previewDrawPreview(p, to)) {
			previewLeft += st::historyReplyPreview + st::msgReplyBarSkip;
		}
		p.setPen(st::historyReplyNameFg);
		const auto elidedWidth = fullWidth
			- previewLeft
			- _fieldBarCancel->width()
			- st::msgReplyPadding.right();

		_previewTitle.drawElided(
			p,
			previewLeft,
			textTop,
			elidedWidth);
		p.setPen(st::historyComposeAreaFg);
		_previewDescription.drawElided(
			p,
			previewLeft,
			textTop + st::msgServiceNameFont->height,
			elidedWidth);
	} else if (_editMsgId || _replyTo || (!hasForward && _kbReplyTo)) {
		const auto now = crl::now();
		const auto paused = p.inactive();
		const auto pausedSpoiler = paused || On(PowerSaving::kChatSpoiler);
		auto replyLeft = st::historyReplySkip;
		if (_suggestOptions) {
			_suggestOptions->paintIcon(p, 0, backy, fullWidth);
		} else {
			(_editMsgId
				? st::historyEditIcon
				: (_replyTo && !_replyTo.quote.empty())
				? st::historyQuoteIcon
				: st::historyReplyIcon).paint(
					p,
					st::historyReplyIconPosition + QPoint(0, backy),
					fullWidth);
		}
		if (drawMsgText) {
			if (hasPreview) {
				if (preview) {
					const auto overEdit = _photoEditMedia
						? _inPhotoEditOver.value(_inPhotoEdit ? 1. : 0.)
						: 0.;
					auto to = QRect(
						replyLeft,
						(st::historyReplyHeight - st::historyReplyPreview) / 2
							+ backy,
						st::historyReplyPreview,
						st::historyReplyPreview);
					p.drawPixmap(to.x(), to.y(), preview->pixSingle(
						preview->size() / style::DevicePixelRatio(),
						{
							.options = Images::Option::RoundSmall,
							.outer = to.size(),
						}));
					if (_replySpoiler) {
						if (overEdit > 0.) {
							p.setOpacity(1. - overEdit);
						}
						Ui::FillSpoilerRect(
							p,
							to,
							Ui::DefaultImageSpoiler().frame(
								_replySpoiler->index(now, pausedSpoiler)));
					}
					if (overEdit > 0.) {
						p.setOpacity(overEdit);
						p.fillRect(to, st::historyEditMediaBg);
						st::historyEditMedia.paintInCenter(p, to);
						p.setOpacity(1.);
					}
					_mediaEditManager.paintCoverUpload(p, to);
				}
				replyLeft += st::historyReplyPreview + st::msgReplyBarSkip;
			}
			if (_suggestOptions) {
				_suggestOptions->paintLines(p, replyLeft, backy, fullWidth);
			} else {
				p.setPen(st::historyReplyNameFg);
				if (_editMsgId) {
					paintEditHeader(p, rect, replyLeft, backy);
				} else {
					_replyToName.drawElided(
						p,
						replyLeft,
						backy + st::msgReplyPadding.top(),
						fullWidth
							- replyLeft
							- _fieldBarCancel->width()
							- st::msgReplyPadding.right());
				}
				p.setPen(st::historyComposeAreaFg);
				_replyEditMsgText.draw(p, {
					.position = QPoint(
						replyLeft,
						st::msgReplyPadding.top()
							+ st::msgServiceNameFont->height
							+ backy),
					.availableWidth = fullWidth
						- replyLeft
						- _fieldBarCancel->width()
						- st::msgReplyPadding.right(),
					.palette = &st::historyComposeAreaPalette,
					.spoiler = Ui::Text::DefaultSpoilerCache(),
					.now = now,
					.pausedEmoji = paused || On(PowerSaving::kEmojiChat),
					.pausedSpoiler = pausedSpoiler,
					.elisionLines = 1,
				});
			}
		} else {
			p.setFont(st::msgDateFont);
			p.setPen(st::historyComposeAreaFgService);
			p.drawText(
				replyLeft,
				backy
					+ (st::historyReplyHeight - st::msgDateFont->height) / 2
					+ st::msgDateFont->ascent,
				st::msgDateFont->elided(
					tr::lng_profile_loading(tr::now),
					fullWidth
						- replyLeft
						- _fieldBarCancel->width()
						- st::msgReplyPadding.right()));
		}
	} else if (hasForward) {
		st::historyForwardIcon.paint(
			p,
			st::historyReplyIconPosition + QPoint(0, backy), fullWidth);
		const auto x = st::historyReplySkip;
		const auto available = fullWidth
			- x
			- _fieldBarCancel->width()
			- st::msgReplyPadding.right();
		_forwardPanel->paint(p, x, backy, available, fullWidth);
	} else if (_suggestOptions) {
		_suggestOptions->paintBar(p, 0, backy, fullWidth);
	}
}

void HistoryWidget::paintEditHeader(
		Painter &p,
		const QRect &rect,
		int left,
		int top) const {
	if (!rect.intersects(
			myrtlrect(left, top, width() - left, st::normalFont->height))) {
		return;
	}

	p.setFont(st::msgServiceNameFont);
	p.drawTextLeft(
		left,
		top + st::msgReplyPadding.top(),
		width(),
		tr::lng_edit_message(tr::now));

	if (!_replyEditMsg
		|| _replyEditMsg->history()->peer->canEditMessagesIndefinitely()) {
		return;
	}

	auto editTimeLeftText = QString();
	auto updateIn = int(-1);
	auto timeSinceMessage = ItemDateTime(_replyEditMsg).msecsTo(
		QDateTime::currentDateTime());
	auto editTimeLeft = (session().serverConfig().editTimeLimit * 1000LL)
		- timeSinceMessage;
	if (editTimeLeft < 2) {
		editTimeLeftText = u"0:00"_q;
	} else if (editTimeLeft > kDisplayEditTimeWarningMs) {
		updateIn = static_cast<int>(std::min(
			editTimeLeft - kDisplayEditTimeWarningMs,
			qint64(kFullDayInMs)));
	} else {
		updateIn = static_cast<int>(editTimeLeft % 1000);
		if (!updateIn) {
			updateIn = 1000;
		}
		++updateIn;

		editTimeLeft = (editTimeLeft - 1) / 1000; // seconds
		editTimeLeftText = u"%1:%2"_q
			.arg(editTimeLeft / 60)
			.arg(editTimeLeft % 60, 2, 10, QChar('0'));
	}

	// Restart timer only if we are sure that we've painted the whole timer.
	if (rect.contains(
			myrtlrect(left, top, width() - left, st::normalFont->height))
		&& (updateIn > 0)) {
		_updateEditTimeLeftDisplay.callOnce(updateIn);
	}

	if (!editTimeLeftText.isEmpty()) {
		p.setFont(st::normalFont);
		p.setPen(st::historyComposeAreaFgService);
		p.drawText(
			left
				+ st::msgServiceNameFont->width(tr::lng_edit_message(tr::now))
				+ st::normalFont->spacew,
			top + st::msgReplyPadding.top() + st::msgServiceNameFont->ascent,
			editTimeLeftText);
	}
}

bool HistoryWidget::paintShowAnimationFrame() {
	if (_showAnimation) {
		auto p = QPainter(this);
		_showAnimation->paintContents(p);
		return true;
	}
	return false;
}

void HistoryWidget::paintEvent(QPaintEvent *e) {
	if (paintShowAnimationFrame()
		|| controller()->contentOverlapped(this, e)) {
		return;
	}
	if (hasPendingResizedItems()) {
		updateListSize();
	}

	Window::SectionWidget::PaintBackground(
		controller(),
		controller()->currentChatTheme(),
		this,
		e->rect());
	invalidateFrostedBackground(e->rect());

	Painter p(this);
	const auto clip = e->rect();
	if (_list) {
		const auto restrictionHidden = fieldOrDisabledShown()
			|| isRecording();
		if (restrictionHidden
			|| replyTo()
			|| readyToForward()
			|| _kbShown
			|| _suggestOptions) {
			if (!isSearching() && _composeSurface->isHidden()) {
				drawField(p, clip);
			}
		}
	} else {
		const auto w = 0
			+ st::msgServiceFont->width(tr::lng_willbe_history(tr::now))
			+ st::msgPadding.left()
			+ st::msgPadding.right();
		const auto h = st::msgServiceFont->height
			+ st::msgServicePadding.top()
			+ st::msgServicePadding.bottom();
		const auto tr = QRect(
			(width() - w) / 2,
			st::msgServiceMargin.top() + (height()
				- fieldHeight()
				- 2 * st::historySendPadding
				- h
				- st::msgServiceMargin.top()
				- st::msgServiceMargin.bottom()) / 2,
			w,
			h);
		const auto st = controller()->chatStyle();
		HistoryView::ServiceMessagePainter::PaintBubble(p, st, tr);

		p.setPen(st->msgServiceFg());
		p.setFont(st::msgServiceFont->f);
		p.drawTextLeft(
			tr.left() + st::msgPadding.left(),
			tr.top() + st::msgServicePadding.top(),
			width(),
			tr::lng_willbe_history(tr::now));
	}
}

QPoint HistoryWidget::clampMousePosition(QPoint point) {
	if (point.x() < 0) {
		point.setX(0);
	} else if (point.x() >= _scroll->width()) {
		point.setX(_scroll->width() - 1);
	}
	if (point.y() < _scroll->scrollTop()) {
		point.setY(_scroll->scrollTop());
	} else if (point.y() >= visibleScrollBottom()) {
		point.setY(visibleScrollBottom() - 1);
	}
	return point;
}

bool HistoryWidget::touchScroll(const QPoint &delta) {
	const auto scTop = _scroll->scrollTop();
	const auto scMax = _scroll->scrollTopMax();
	const auto scNew = std::clamp(scTop - delta.y(), 0, scMax);
	if (scNew == scTop) {
		return false;
	}
	_scroll->scrollToY(scNew);
	return true;
}

void HistoryWidget::synteticScrollToY(int y) {
	_synteticScrollEvent = true;
	if (_scroll->scrollTop() == y) {
		visibleAreaUpdated();
	} else {
		_scroll->scrollToY(y);
	}
	_synteticScrollEvent = false;
}
