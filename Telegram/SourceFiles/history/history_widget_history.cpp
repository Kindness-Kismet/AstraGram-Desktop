/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_widget.h"
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

void HistoryWidget::scrollToCurrentVoiceMessage(
		FullMsgId fromId,
		FullMsgId toId) {
	if (crl::now() <= _lastUserScrolled + kScrollToVoiceAfterScrolledMs) {
		return;
	}
	if (!_list) {
		return;
	}

	auto from = session().data().message(fromId);
	auto to = session().data().message(toId);
	if (!from || !to) {
		return;
	}

	// If history has pending resize items, the scrollTopItem won't be updated.
	// And the scrollTop will be reset back to scrollTopItem + scrollTopOffset.
	handlePendingHistoryUpdate();

	if (const auto toView = to->mainView()) {
		auto toTop = _list->itemTop(toView);
		if (toTop >= 0 && !isItemCompletelyHidden(from)) {
			auto scrollTop = visibleScrollTop();
			auto scrollBottom = visibleScrollBottom();
			auto toBottom = toTop + toView->height();
			if ((toTop < scrollTop && toBottom < scrollBottom)
				|| (toTop > scrollTop && toBottom > scrollBottom)) {
				animatedScrollToItem(to->id);
			}
		}
	}
}

void HistoryWidget::animatedScrollToItem(MsgId msgId) {
	Expects(_history != nullptr);

	if (hasPendingResizedItems()) {
		updateListSize();
	}

	auto to = session().data().message(_history->peer, msgId);
	if (_list->itemTop(to) < 0) {
		return;
	}

	auto scrollTo = std::clamp(
		physicalScrollTop(itemTopForHighlight(to->mainView())),
		0,
		_scroll->scrollTopMax());
	animatedScrollToY(scrollTo, to);
}

void HistoryWidget::animatedScrollToY(int scrollTo, HistoryItem *attachTo) {
	Expects(_history != nullptr);

	if (hasPendingResizedItems()) {
		updateListSize();
	}

	// Attach our scroll animation to some item.
	auto itemTop = _list->itemTop(attachTo);
	auto scrollTop = _scroll->scrollTop();
	if (itemTop < 0 && !_history->isEmpty()) {
		attachTo = _history->blocks.back()->messages.back()->data();
		itemTop = _list->itemTop(attachTo);
	}
	if (itemTop < 0 || (scrollTop == scrollTo)) {
		synteticScrollToY(scrollTo);
		return;
	}

	_scrollToAnimation.stop();
	auto maxAnimatedDelta = _scroll->height();
	auto transition = anim::sineInOut;
	if (scrollTo > scrollTop + maxAnimatedDelta) {
		scrollTop = scrollTo - maxAnimatedDelta;
		synteticScrollToY(scrollTop);
		transition = anim::easeOutCubic;
	} else if (scrollTo + maxAnimatedDelta < scrollTop) {
		scrollTop = scrollTo + maxAnimatedDelta;
		synteticScrollToY(scrollTop);
		transition = anim::easeOutCubic;
	} else {
		// In local showHistory() we forget current scroll state,
		// so we need to restore it synchronously, otherwise we may
		// jump to the bottom of history in some updateHistoryGeometry() call.
		synteticScrollToY(scrollTop);
	}
	const auto itemId = attachTo->fullId();
	const auto relativeFrom = scrollTop - itemTop;
	const auto relativeTo = scrollTo - itemTop;
	_scrollToAnimation.start(
		[=] { scrollToAnimationCallback(itemId, relativeTo); },
		relativeFrom,
		relativeTo,
		st::slideDuration,
		anim::sineInOut);
}

void HistoryWidget::scrollToAnimationCallback(
		FullMsgId attachToId,
		int relativeTo) {
	auto itemTop = _list->itemTop(session().data().message(attachToId));
	if (itemTop < 0) {
		_scrollToAnimation.stop();
	} else {
		const auto value = _scrollToAnimation.value(relativeTo);
		synteticScrollToY(int(base::SafeRound(value)) + itemTop);
	}
	if (!_scrollToAnimation.animating()) {
		preloadHistoryByScroll();
		checkReplyReturns();
	}
}

void HistoryWidget::enqueueMessageHighlight(
		const HistoryView::SelectedQuote &quote) {
	_highlighter.enqueue(quote);
}

Ui::ChatPaintHighlight HistoryWidget::itemHighlight(
		not_null<const HistoryItem*> item) const {
	return _highlighter.state(item);
}

int HistoryWidget::itemTopForHighlight(
		not_null<HistoryView::Element*> view) const {
	if (const auto group = session().data().groups().find(view->data())) {
		if (const auto leader = group->items.front()->mainView()) {
			view = leader;
		}
	}
	const auto itemTop = _list->itemTop(view);
	Assert(itemTop >= 0);

	const auto item = view->data();
	const auto unwatchedEffect = item->hasUnwatchedEffect();
	const auto showReactions = item->hasUnreadReaction() || unwatchedEffect;
	const auto reactionCenter = showReactions
		? view->reactionButtonParameters({}, {}).center.y()
		: -1;

	const auto visibleAreaHeight = visibleScrollHeight();
	const auto viewHeight = view->height();
	const auto heightLeft = (visibleAreaHeight - viewHeight);
	if (heightLeft >= 0) {
		return std::max(itemTop - (heightLeft / 2), 0);
	}
	const auto highlight = itemHighlight(item);
	if (const auto range = HistoryView::FindHighlightYRange(
			view,
			highlight)) {
		return HistoryView::AdjustScrollForRange(
			itemTop,
			visibleAreaHeight,
			range);
	} else if (reactionCenter >= 0) {
		const auto maxSize = st::reactionInlineImage;

		// Show message right till the bottom.
		const auto forBottom = itemTop + viewHeight - visibleAreaHeight;

		// Show message bottom and some space below for the effect.
		const auto bottomResult = forBottom + maxSize;

		// Show the reaction button center in the middle.
		const auto byReactionResult = itemTop
			+ reactionCenter
			- visibleAreaHeight / 2;

		// Show the reaction center and some space above it for the effect.
		const auto maxAllowed = itemTop + reactionCenter - 2 * maxSize;
		return std::max(
			std::min(maxAllowed, std::max(bottomResult, byReactionResult)),
			0);
	}
	return itemTop;
}

void HistoryWidget::firstLoadMessages() {
	if (!_history || _firstLoadRequest) {
		return;
	}

	auto from = _history;
	auto offsetId = MsgId();
	auto offset = 0;
	auto loadCount = kMessagesPerPage;
	_firstLoadFromTheStart = false;
	if (_showAtMsgId == ShowAtUnreadMsgId) {
		if (const auto around = _migrated ? _migrated->loadAroundId() : 0) {
			_history->getReadyFor(_showAtMsgId);
			from = _migrated;
			offset = -loadCount / 2;
			offsetId = around;
		} else if (const auto around = _history->loadAroundId()) {
			_history->getReadyFor(_showAtMsgId);
			offset = -loadCount / 2;
			offsetId = around;
			_firstLoadFromTheStart = (around == 1);
		} else {
			_history->getReadyFor(ShowAtTheEndMsgId);
		}
	} else if (_showAtMsgId == ShowAtTheEndMsgId) {
		_history->getReadyFor(_showAtMsgId);
		loadCount = kMessagesPerPageFirst;
	} else if (_showAtMsgId > 0) {
		_history->getReadyFor(_showAtMsgId);
		offset = -loadCount / 2;
		offsetId = _showAtMsgId;
	} else if (_showAtMsgId < 0 && _history->peer->isChannel()) {
		if (_showAtMsgId < 0 && -_showAtMsgId < ServerMaxMsgId && _migrated) {
			_history->getReadyFor(_showAtMsgId);
			from = _migrated;
			offset = -loadCount / 2;
			offsetId = -_showAtMsgId;
		} else if (_showAtMsgId == SwitchAtTopMsgId) {
			_history->getReadyFor(_showAtMsgId);
		}
	}

	const auto offsetDate = 0;
	const auto maxId = 0;
	const auto minId = 0;
	const auto historyHash = uint64(0);

	const auto history = from;
	const auto type = Data::Histories::RequestType::History;
	auto &histories = history->owner().histories();
	_firstLoadRequest = histories.sendRequest(history, type, [=](
			Fn<void()> finish) {
		return history->session().api().request(MTPmessages_GetHistory(
			history->peer->input(),
			MTP_int(offsetId),
			MTP_int(offsetDate),
			MTP_int(offset),
			MTP_int(loadCount),
			MTP_int(maxId),
			MTP_int(minId),
			MTP_long(historyHash)
		)).done([=](const MTPmessages_Messages &result) {
			messagesReceived(history->peer, result, _firstLoadRequest);
			finish();
		}).fail([=](const MTP::Error &error) {
			messagesFailed(error, _firstLoadRequest);
			finish();
		}).send();
	});
}

void HistoryWidget::loadMessages() {
	if (!_history || _preloadRequest) {
		return;
	}

	if (_history->isEmpty() && _migrated && _migrated->isEmpty()) {
		return firstLoadMessages();
	}

	auto loadMigrated = _migrated
		&& (_history->isEmpty()
			|| _history->loadedAtTop()
			|| (!_migrated->isEmpty() && !_migrated->loadedAtBottom()));
	const auto from = loadMigrated ? _migrated : _history;
	if (from->loadedAtTop()) {
		return;
	}

	const auto offsetId = from->minMsgId();
	const auto addOffset = 0;
	const auto loadCount = offsetId
		? kMessagesPerPage
		: kMessagesPerPageFirst;
	const auto offsetDate = 0;
	const auto maxId = 0;
	const auto minId = 0;
	const auto historyHash = uint64(0);

	DEBUG_LOG(("JumpToEnd(%1, %2, %3): Loading up before %4."
		).arg(_history->peer->name()
		).arg(_history->inboxReadTillId().bare
		).arg(Logs::b(_history->loadedAtBottom())
		).arg(offsetId.bare));

	const auto history = from;
	const auto type = Data::Histories::RequestType::History;
	auto &histories = history->owner().histories();
	_preloadRequest = histories.sendRequest(history, type, [=](
			Fn<void()> finish) {
		return history->session().api().request(MTPmessages_GetHistory(
			history->peer->input(),
			MTP_int(offsetId),
			MTP_int(offsetDate),
			MTP_int(addOffset),
			MTP_int(loadCount),
			MTP_int(maxId),
			MTP_int(minId),
			MTP_long(historyHash)
		)).done([=](const MTPmessages_Messages &result) {
			messagesReceived(history->peer, result, _preloadRequest);
			finish();
		}).fail([=](const MTP::Error &error) {
			messagesFailed(error, _preloadRequest);
			finish();
		}).send();
	});
}

bool HistoryWidget::historyLoadedAtTop() const {
	if (!_history) {
		return true;
	} else if (_firstLoadRequest) {
		return false;
	}
	const auto loadMigrated = _migrated
		&& (_history->isEmpty()
			|| _history->loadedAtTop()
			|| (!_migrated->isEmpty() && !_migrated->loadedAtBottom()));
	const auto from = loadMigrated ? _migrated : _history;
	return from->loadedAtTop();
}

bool HistoryWidget::historyLoadedAtBottom() const {
	// While the first load request is pending the (visible) scroll area
	// shows a blank list, but getReadyFor() may have already marked the
	// history as loaded at bottom before any server page arrived. Report
	// unloaded edges for that interval: it keeps the elastic overscroll
	// (and the pull-to-next-channel gesture riding on it) away from the
	// blank list until the requested messages are actually shown.
	if (!_history) {
		return true;
	} else if (_firstLoadRequest) {
		return false;
	}
	const auto loadMigrated = _migrated
		&& !(_migrated->isEmpty()
			|| _migrated->loadedAtBottom()
			|| (!_history->isEmpty() && !_history->loadedAtTop()));
	const auto from = loadMigrated ? _migrated : _history;
	return from->loadedAtBottom();
}

void HistoryWidget::loadMessagesDown() {
	if (!_history || _preloadDownRequest) {
		return;
	}

	if (_history->isEmpty() && _migrated && _migrated->isEmpty()) {
		return firstLoadMessages();
	}

	const auto loadMigrated = _migrated
		&& !(_migrated->isEmpty()
			|| _migrated->loadedAtBottom()
			|| (!_history->isEmpty() && !_history->loadedAtTop()));
	const auto from = loadMigrated ? _migrated : _history;
	if (from->loadedAtBottom()) {
		if (_sponsoredMessagesStateKnown) {
			session().sponsoredMessages().request(_history, nullptr);
		}
		return;
	}

	const auto loadCount = kMessagesPerPage;
	auto addOffset = -loadCount;
	auto offsetId = from->maxMsgId();
	if (!offsetId) {
		if (loadMigrated || !_migrated) {
			return;
		}
		++offsetId;
		++addOffset;
	}
	const auto offsetDate = 0;
	const auto maxId = 0;
	const auto minId = 0;
	const auto historyHash = uint64(0);

	DEBUG_LOG(("JumpToEnd(%1, %2, %3): Loading down after %4."
		).arg(_history->peer->name()
		).arg(_history->inboxReadTillId().bare
		).arg(Logs::b(_history->loadedAtBottom())
		).arg(offsetId.bare));

	const auto history = from;
	const auto type = Data::Histories::RequestType::History;
	auto &histories = history->owner().histories();
	_preloadDownRequest = histories.sendRequest(history, type, [=](
			Fn<void()> finish) {
		return history->session().api().request(MTPmessages_GetHistory(
			history->peer->input(),
			MTP_int(offsetId + 1),
			MTP_int(offsetDate),
			MTP_int(addOffset),
			MTP_int(loadCount),
			MTP_int(maxId),
			MTP_int(minId),
			MTP_long(historyHash)
		)).done([=](const MTPmessages_Messages &result) {
			messagesReceived(history->peer, result, _preloadDownRequest);
			finish();
		}).fail([=](const MTP::Error &error) {
			messagesFailed(error, _preloadDownRequest);
			finish();
		}).send();
	});
}

void HistoryWidget::delayedShowAt(
		MsgId showAtMsgId,
		const Window::SectionShow &params) {
	if (!_history) {
		return;
	}
	_delayedShowAtMsgParams = params;
	if (_delayedShowAtRequest && _delayedShowAtMsgId == showAtMsgId) {
		return;
	}

	clearAllLoadRequests();
	_delayedShowAtMsgId = showAtMsgId;

	DEBUG_LOG(("JumpToEnd(%1, %2, %3): Loading delayed around %4."
		).arg(_history->peer->name()
		).arg(_history->inboxReadTillId().bare
		).arg(Logs::b(_history->loadedAtBottom())
		).arg(showAtMsgId.bare));

	auto from = _history;
	auto offsetId = MsgId();
	auto offset = 0;
	auto loadCount = kMessagesPerPage;
	_firstLoadFromTheStart = false;
	if (_delayedShowAtMsgId == ShowAtUnreadMsgId) {
		if (const auto around = _migrated ? _migrated->loadAroundId() : 0) {
			from = _migrated;
			offset = -loadCount / 2;
			offsetId = around;
		} else if (const auto around = _history->loadAroundId()) {
			offset = -loadCount / 2;
			offsetId = around;
			_firstLoadFromTheStart = (around == 1);
		} else {
			loadCount = kMessagesPerPageFirst;
		}
	} else if (_delayedShowAtMsgId == ShowAtTheEndMsgId) {
		loadCount = kMessagesPerPageFirst;
	} else if (_delayedShowAtMsgId > 0) {
		offset = -loadCount / 2;
		offsetId = _delayedShowAtMsgId;
	} else if (_delayedShowAtMsgId < 0 && _history->peer->isChannel()) {
		if ((_delayedShowAtMsgId < 0)
			&& (-_delayedShowAtMsgId < ServerMaxMsgId)
			&& _migrated) {
			from = _migrated;
			offset = -loadCount / 2;
			offsetId = -_delayedShowAtMsgId;
		}
	}
	const auto offsetDate = 0;
	const auto maxId = 0;
	const auto minId = 0;
	const auto historyHash = uint64(0);

	const auto history = from;
	const auto type = Data::Histories::RequestType::History;
	auto &histories = history->owner().histories();
	_delayedShowAtRequest = histories.sendRequest(history, type, [=](
			Fn<void()> finish) {
		return history->session().api().request(MTPmessages_GetHistory(
			history->peer->input(),
			MTP_int(offsetId),
			MTP_int(offsetDate),
			MTP_int(offset),
			MTP_int(loadCount),
			MTP_int(maxId),
			MTP_int(minId),
			MTP_long(historyHash)
		)).done([=](const MTPmessages_Messages &result) {
			messagesReceived(history->peer, result, _delayedShowAtRequest);
			finish();
		}).fail([=](const MTP::Error &error) {
			messagesFailed(error, _delayedShowAtRequest);
			finish();
		}).send();
	});
}

void HistoryWidget::handleScroll() {
	invalidateFrostedBackground();
	if (!_itemsRevealHeight) {
		preloadHistoryIfNeeded();
	}
	visibleAreaUpdated();
	if (!_itemsRevealHeight) {
		updatePinnedViewer();
	}
	const auto now = crl::now();
	if (!_synteticScrollEvent) {
		_lastUserScrolled = now;
	}
	const auto scrollTop = _scroll->scrollTop();
	if (scrollTop != _lastScrollTop) {
		if (!_synteticScrollEvent) {
			checkLastPinnedClickedIdReset(_lastScrollTop, scrollTop);
		}
		_lastScrolled = now;
		_lastScrollTop = scrollTop;
	}
}

bool HistoryWidget::isItemCompletelyHidden(HistoryItem *item) const {
	const auto view = item ? item->mainView() : nullptr;
	if (!view) {
		return true;
	}
	const auto top = _list ? _list->itemTop(item) : -2;
	if (top < 0) {
		return true;
	}

	const auto bottom = top + view->height();
	const auto scrollTop = visibleScrollTop();
	const auto scrollBottom = visibleScrollBottom();
	return (top >= scrollBottom || bottom <= scrollTop);
}

int HistoryWidget::composeOverlap() const {
	return _composeOverlap;
}

int HistoryWidget::topBarsOverlap() const {
	if (!_pinnedBar || !_pinnedBar->height()) {
		return 0;
	}
	auto bars = Ui::ChatBarStack();
	bars.add(_groupCallBar ? _groupCallBar->height() : 0);
	bars.add(_requestsBar ? _requestsBar->height() : 0);
	bars.add(_pinnedBar->height());
	bars.add(_sponsoredMessageBar ? _sponsoredMessageBar->height() : 0);
	bars.add(_translateBar ? _translateBar->height() : 0);
	bars.add(_paysStatus ? _paysStatus->bar().height() : 0);
	bars.add(_contactStatus ? _contactStatus->bar().height() : 0);
	bars.add(_businessBotStatus ? _businessBotStatus->bar().height() : 0);
	return bars.height();
}

int HistoryWidget::visibleScrollHeight() const {
	return std::max(0,
		_scroll->height() - topBarsOverlap() - _composeOverlap);
}

int HistoryWidget::visibleScrollTop() const {
	return std::min(
		_scroll->scrollTop() + topBarsOverlap(),
		visibleScrollBottom());
}

int HistoryWidget::visibleScrollBottom() const {
	return _scroll->scrollTop() + _scroll->height() - _composeOverlap;
}

int HistoryWidget::physicalScrollTop(int visibleTop) const {
	return (visibleTop == ScrollMax)
		? ScrollMax
		: (visibleTop - topBarsOverlap());
}

QRect HistoryWidget::visibleScrollGeometry() const {
	const auto available = std::max(0, _scroll->height() - _composeOverlap);
	return _scroll->geometry().marginsRemoved({
		0, std::min(topBarsOverlap(), available), 0, _composeOverlap });
}

void HistoryWidget::visibleAreaUpdated() {
	if (_list && !_firstLoadRequest && !_scroll->isHidden()) {
		_list->visibleAreaUpdated(
			visibleScrollTop(),
			visibleScrollBottom());
		controller()->floatPlayerAreaUpdated();
		session().data().itemVisibilitiesUpdated();
	}
}

void HistoryWidget::preloadHistoryIfNeeded() {
	if (_firstLoadRequest
		|| _delayedShowAtRequest
		|| _scroll->isHidden()
		|| !_peer
		|| !_historyInited) {
		return;
	}

	_cornerButtons.updateJumpDownVisibility();
	_cornerButtons.updateUnreadThingsVisibility();
	if (!_scrollToAnimation.animating()) {
		preloadHistoryByScroll();
		checkReplyReturns();
	}
	const auto hasNonEmpty = _history->findFirstNonEmpty();
	const auto readyForBotStart = hasNonEmpty
		|| (_history->loadedAtTop() && _history->loadedAtBottom());
	if (readyForBotStart && clearMaybeSendStart() && hasNonEmpty) {
		sendBotStartCommand();
	}
}

void HistoryWidget::preloadHistoryByScroll() {
	if (_firstLoadRequest
		|| _delayedShowAtRequest
		|| _scroll->isHidden()
		|| !_peer
		|| !_historyInited) {
		return;
	}

	auto scrollTop = _scroll->scrollTop();
	auto scrollTopMax = _scroll->scrollTopMax();
	auto scrollHeight = _scroll->height();
	if (scrollTop + kPreloadHeightsCount * scrollHeight >= scrollTopMax) {
		loadMessagesDown();
	}
	if (scrollTop <= kPreloadHeightsCount * scrollHeight) {
		loadMessages();
	}
	if (session().supportMode()) {
		crl::on_main(this, [=] { checkSupportPreload(); });
	}
}

void HistoryWidget::checkSupportPreload(bool force) {
	if (!_history
		|| _firstLoadRequest
		|| _preloadRequest
		|| _preloadDownRequest
		|| (_supportPreloadRequest && !force)
		|| controller()->activeChatEntryCurrent().key.history() != _history) {
		return;
	}

	const auto setting = session().settings().supportSwitch();
	const auto command = Support::GetSwitchCommand(setting);
	const auto descriptor = !command
		? Dialogs::RowDescriptor()
		: (*command == Shortcuts::Command::ChatNext)
		? controller()->resolveChatNext()
		: controller()->resolveChatPrevious();
	auto history = descriptor.key.history();
	if (!history || _supportPreloadHistory == history) {
		return;
	}
	clearSupportPreloadRequest();
	_supportPreloadHistory = history;
	_supportPreloadRequest = Support::SendPreloadRequest(history, [=] {
		_supportPreloadRequest = 0;
		_supportPreloadHistory = nullptr;
		crl::on_main(this, [=] { checkSupportPreload(); });
	});
}

void HistoryWidget::checkReplyReturns() {
	if (_firstLoadRequest
		|| _scroll->isHidden()
		|| !_peer
		|| !_historyInited) {
		return;
	}
	auto scrollTop = visibleScrollTop();
	auto scrollTopMax = _scroll->scrollTopMax();
	auto scrollHeight = visibleScrollHeight();
	while (const auto replyReturn = _cornerButtons.replyReturn()) {
		auto below = !replyReturn->mainView()
			&& (replyReturn->history() == _history)
			&& !_history->isEmpty()
			&& (replyReturn->id
				< _history->blocks.back()->messages.back()->data()->id);
		if (!below) {
			below = !replyReturn->mainView()
				&& (replyReturn->history() == _migrated)
				&& !_history->isEmpty();
		}
		if (!below) {
			below = !replyReturn->mainView()
				&& _migrated
				&& (replyReturn->history() == _migrated)
				&& !_migrated->isEmpty()
				&& (replyReturn->id
					< _migrated->blocks.back()->messages.back()->data()->id);
		}
		if (!below && replyReturn->mainView()) {
			below = (_scroll->scrollTop() >= scrollTopMax)
				|| (_list->itemTop(replyReturn)
					< scrollTop + scrollHeight / 2);
		}
		if (below) {
			_cornerButtons.calculateNextReplyReturn();
		} else {
			break;
		}
	}
}

void HistoryWidget::handleHistoryChange(not_null<const History*> history) {
	if (_list && (_history == history || _migrated == history)) {
		handlePendingHistoryUpdate();
		updateBotKeyboard();
		if (!_scroll->isHidden()) {
			const auto unblock = isBlocked();
			const auto botStart = isBotStart();
			const auto joinChannel = isJoinChannel();
			const auto muteUnmute = isMuteUnmute();
			const auto discuss = muteUnmute && hasDiscussionGroup();
			const auto reportMessages = isReportMessages();
			const auto update = false
				|| (_reportMessages->isHidden() == reportMessages)
				|| (!reportMessages && _unblock->isHidden() == unblock)
				|| (!reportMessages
					&& !unblock
					&& _botStart->isHidden() == botStart)
				|| (!reportMessages
					&& !unblock
					&& !botStart
					&& _joinChannel->isHidden() == joinChannel)
				|| (!reportMessages
					&& !unblock
					&& !botStart
					&& !joinChannel
					&& (_muteUnmute->isHidden() == muteUnmute || _discuss->isHidden() == discuss));
			if (update) {
				updateControlsVisibility();
				updateControlsGeometry();
			}
		}
	}
}

QPixmap HistoryWidget::grabForShowAnimation(
		const Window::SectionSlideParams &params) {
	if (params.withTopBarShadow) {
		_topShadow->hide();
	}
	_inGrab = true;
	updateControlsGeometry();
	auto result = Ui::GrabWidget(this);
	_inGrab = false;
	updateControlsGeometry();
	if (params.withTopBarShadow) {
		_topShadow->show();
	}
	return result;
}

bool HistoryWidget::skipItemRepaint() {
	auto ms = crl::now();
	if (_lastScrolled + kSkipRepaintWhileScrollMs <= ms) {
		return false;
	}
	_updateHistoryItems.callOnce(
		_lastScrolled + kSkipRepaintWhileScrollMs - ms);
	return true;
}

void HistoryWidget::updateHistoryItemsByTimer() {
	if (!_list) {
		return;
	}

	auto ms = crl::now();
	if (_lastScrolled + kSkipRepaintWhileScrollMs <= ms) {
		_list->update();
	} else {
		_updateHistoryItems.callOnce(
			_lastScrolled + kSkipRepaintWhileScrollMs - ms);
	}
}

void HistoryWidget::handlePendingHistoryUpdate() {
	if (hasPendingResizedItems() || _updateHistoryGeometryRequired) {
		updateHistoryGeometry();
		_list->update();
	}
}

void HistoryWidget::resizeEvent(QResizeEvent *e) {
	//updateTabbedSelectorSectionShown();
	recountChatWidth();
	updateControlsGeometry();
}

void HistoryWidget::updateControlsGeometry() {
	const auto width = this->width();

	const auto margin = st::historyComposeCapsuleMargin;
	_topBar->resizeToWidth(width);
	_topBar->moveToLeft(0, 0);

	const auto tabsLeftSkip = _subsectionTabs
		? _subsectionTabs->leftSkip()
		: 0;
	const auto innerWidth = std::max(0, width - tabsLeftSkip - 2 * margin);

	_voiceRecordBar->resizeToWidth(width);

	moveFieldControls();

	_topBars->move(
		tabsLeftSkip + margin,
		_topBar->bottomNoMargins()
			+ (_subsectionTabs ? _subsectionTabs->topSkip() : 0));
	auto stack = Ui::ChatBarStack();
	const auto place = [&](int height) { return stack.add(height); };
	const auto groupCallTop = place(_groupCallBar ? _groupCallBar->height() : 0);
	if (_groupCallBar) {
		_groupCallBar->move(0, groupCallTop);
		_groupCallBar->resizeToWidth(innerWidth);
	}
	const auto requestsTop = place(_requestsBar ? _requestsBar->height() : 0);
	if (_requestsBar) {
		_requestsBar->move(0, requestsTop);
		_requestsBar->resizeToWidth(innerWidth);
	}
	const auto pinnedBarTop = place(_pinnedBar ? _pinnedBar->height() : 0);
	if (_pinnedBar) {
		_pinnedBar->move(0, pinnedBarTop);
		_pinnedBar->resizeToWidth(innerWidth);
	}
	const auto sponsoredMessageBarTop = place(
		_sponsoredMessageBar ? _sponsoredMessageBar->height() : 0);
	if (_sponsoredMessageBar) {
		_sponsoredMessageBar->move(0, sponsoredMessageBarTop);
		_sponsoredMessageBar->resizeToWidth(innerWidth);
	}
	const auto translateTop = place(_translateBar ? _translateBar->height() : 0);
	if (_translateBar) {
		_translateBar->move(0, translateTop);
		_translateBar->resizeToWidth(innerWidth);
	}
	const auto paysStatusTop = place(
		_paysStatus ? _paysStatus->bar().height() : 0);
	if (_paysStatus) {
		_paysStatus->bar().move(0, paysStatusTop);
	}
	const auto contactStatusTop = place(
		_contactStatus ? _contactStatus->bar().height() : 0);
	if (_contactStatus) {
		_contactStatus->bar().move(0, contactStatusTop);
	}
	const auto businessBotTop = place(
		_businessBotStatus ? _businessBotStatus->bar().height() : 0);
	if (_businessBotStatus) {
		_businessBotStatus->bar().move(0, businessBotTop);
	}
	const auto overlap = _pinnedBar && _pinnedBar->height()
		? stack.height()
		: 0;
	const auto scrollAreaTop = _topBars->y() + stack.height() - overlap;
	_topBars->resize(innerWidth, stack.height());
	if (overlap) {
		_topBars->setMask(stack.cardRegion(innerWidth));
	} else {
		_topBars->clearMask();
	}
	_scroll->setBarTopInset(overlap);
	if (_scroll->y() != scrollAreaTop || _scroll->x() != tabsLeftSkip) {
		_scroll->moveToLeft(tabsLeftSkip, scrollAreaTop);
		if (_autocomplete) {
			_autocomplete->setBoundings(visibleScrollGeometry());
		}
		if (_supportAutocomplete) {
			_supportAutocomplete->setBoundings(visibleScrollGeometry());
		}
	}

	updateHistoryGeometry(
		false,
		false,
		{ ScrollChangeAdd, base::take(_topDelta) });
	_lastScrollAreaY = scrollAreaTop;

	updateFieldSize();

	_cornerButtons.updatePositions();
	_pullToNext->updateGeometry();

	if (_membersDropdown) {
		_membersDropdown->setMaxHeight(countMembersDropdownHeightMax());
	}

	const auto isOneColumn = controller()->adaptive().isOneColumn();
	const auto isThreeColumn = controller()->adaptive().isThreeColumn();
	const auto topShadowLeft = (isOneColumn || _inGrab)
		? 0
		: st::lineWidth;
	const auto topShadowRight = (isThreeColumn && !_inGrab && _peer)
		? st::lineWidth
		: 0;
	// 悬浮样式下隐藏顶部分隔线,高度置 0 避免各处 show 生效
	_topShadow->setGeometryToLeft(
		topShadowLeft,
		_topBar->bottomNoMargins(),
		width - topShadowLeft - topShadowRight,
		0);
	updateFrostedAreas();
}

void HistoryWidget::itemRemoved(not_null<const HistoryItem*> item) {
	if (item == _replyEditMsg && _editMsgId) {
		cancelEdit();
	}
	if (item == _replyEditMsg && _replyTo) {
		cancelReply();
	}
	if (item == _processingReplyItem) {
		_processingReplyTo = {};
		_processingReplyItem = nullptr;
	}
	if (_kbReplyTo && item == _kbReplyTo) {
		toggleKeyboard();
		_kbReplyTo = nullptr;
	}
	const auto i = _itemRevealAnimations.find(item);
	if (i != end(_itemRevealAnimations)) {
		_itemRevealAnimations.erase(i);
		revealItemsCallback();
	}
	const auto j = _itemRevealPending.find(item);
	if (j != _itemRevealPending.end()) {
		_itemRevealPending.erase(j);
	}
}

void HistoryWidget::itemEdited(not_null<HistoryItem*> item) {
	if (item.get() == _replyEditMsg) {
		updateReplyEditTexts(true);
	}
}

FullReplyTo HistoryWidget::replyTo() const {
	return _replyTo
		? _replyTo
		: _kbReplyTo
		? FullReplyTo{ _kbReplyTo->fullId() }
		: (_peer && _peer->forum() && !_peer->isBot())
		? FullReplyTo{ .topicRootId = Data::ForumTopic::kGeneralId }
		: FullReplyTo();
}

SuggestOptions HistoryWidget::suggestOptions(
		bool skipNoAdminCheck) const {
	const auto checked = skipNoAdminCheck
		|| (_history && _history->suggestDraftAllowed());
	return (checked && _suggestOptions)
		? _suggestOptions->values()
		: SuggestOptions();
}

bool HistoryWidget::hasSavedScroll() const {
	Expects(_history != nullptr);

	return _history->scrollTopItem
		|| (_migrated && _migrated->scrollTopItem);
}

int HistoryWidget::countInitialScrollTop() {
	if (hasSavedScroll()) {
		return physicalScrollTop(_list->historyScrollTop());
	} else if (_showAtMsgId
		&& (IsServerMsgId(_showAtMsgId)
			|| IsClientMsgId(_showAtMsgId)
			|| IsServerMsgId(-_showAtMsgId))) {
		const auto item = getItemFromHistoryOrMigrated(_showAtMsgId);
		const auto itemTop = _list->itemTop(item);
		if (itemTop < 0) {
			setMsgId(ShowAtUnreadMsgId);
			controller()->showToast(tr::lng_message_not_found(tr::now));
			return countInitialScrollTop();
		} else {
			const auto view = item->mainView();
			Assert(view != nullptr);

			enqueueMessageHighlight({
				item,
				base::take(_showAtMsgParams.highlight),
			});
			const auto result = itemTopForHighlight(view);
			createUnreadBarIfBelowVisibleArea(result);
			return physicalScrollTop(result);
		}
	} else if (_showAtMsgId == ShowAtTheEndMsgId) {
		return ScrollMax;
	} else if (_showAtMsgId == ShowAtUnreadMsgId
		&& _history->loadedAtTop()
		&& (_history->loadAroundId() == 1)
		&& (!_migrated || !_migrated->unreadCount())) {
		createUnreadBarIfBelowVisibleArea(0);
		return 0;
	} else if (const auto top = unreadBarTop()) {
		return physicalScrollTop(*top);
	} else {
		_history->calculateFirstUnreadMessage();
		return countAutomaticScrollTop();
	}
}

void HistoryWidget::createUnreadBarIfBelowVisibleArea(int withScrollTop) {
	Expects(_history != nullptr);

	if (_history->unreadBar()) {
		return;
	}
	_history->calculateFirstUnreadMessage();
	if (const auto unread = _history->firstUnreadMessage()) {
		if (_list->itemTop(unread) > withScrollTop) {
			createUnreadBarAndResize();
		}
	}
}

void HistoryWidget::createUnreadBarAndResize() {
	if (!_history->firstUnreadMessage()) {
		return;
	}
	const auto was = base::take(_historyInited);
	_history->addUnreadBar();
	if (hasPendingResizedItems()) {
		updateListSize();
	}
	_historyInited = was;
}

int HistoryWidget::countAutomaticScrollTop() {
	Expects(_history != nullptr);
	Expects(_list != nullptr);

	if (const auto unread = _history->firstUnreadMessage()) {
		const auto firstUnreadTop = _list->itemTop(unread);
		const auto possibleUnreadBarTop = _scroll->scrollTopMax()
			+ topBarsOverlap()
			+ HistoryView::UnreadBar::height()
			- HistoryView::UnreadBar::marginTop();
		if (firstUnreadTop < possibleUnreadBarTop) {
			createUnreadBarAndResize();
			if (_history->unreadBar() != nullptr) {
				setMsgId(ShowAtUnreadMsgId);
				return countInitialScrollTop();
			}
		}
	}
	return ScrollMax;
}

Data::SendError HistoryWidget::computeSendRestriction() const {
	if (!_canSendMessages
		&& _peer->amMonoforumAdmin()
		&& !_peer->asChannel()->monoforumDisabled()) {
		return Data::SendError({
			.text = tr::lng_monoforum_choose_to_reply(tr::now),
			.monoforumAdmin = true,
		});
	}
	const auto allWithoutPolls = Data::AllSendRestrictions()
		& ~ChatRestriction::SendPolls;
	return (_peer && !Data::CanSendAnyOf(_peer, allWithoutPolls))
		? Data::RestrictionError(_peer, ChatRestriction::SendOther)
		: Data::SendError();
}

void HistoryWidget::updateSendRestriction() {
	const auto restriction = computeSendRestriction();
	if (_sendRestrictionKey == restriction.text) {
		return;
	}
	_sendRestrictionKey = restriction.text;
	if (AyuForward::isForwarding(_peer->id)) {
		_sendRestriction = AyuForwardWriteRestriction(this, _peer->id, session());
	} else if (!restriction) {
		_sendRestriction = nullptr;
	} else if (restriction.frozen) {
		const auto show = controller()->uiShow();
		_sendRestriction = FrozenWriteRestriction(
			this,
			show,
			FrozenWriteRestrictionType::MessageField);
	} else if (restriction.premiumToLift) {
		_sendRestriction = PremiumRequiredSendRestriction(
			this,
			_peer->asUser(),
			controller());
	} else if (const auto lifting = restriction.boostsToLift) {
		const auto show = controller()->uiShow();
		_sendRestriction = BoostsToLiftWriteRestriction(
			this,
			show,
			_peer,
			lifting);
	} else {
		_sendRestriction = TextErrorSendRestriction(this, restriction.text);
	}
	if (_sendRestriction) {
		_sendRestriction->setObjectName(u"chatAction.restriction"_q);
		Ui::ApplyChatControlSurface(
			_sendRestriction.get(), st::historyComposeCapsuleRadius);
		_sendRestriction->show();
		moveFieldControls();
	}
}

void HistoryWidget::updateHistoryGeometry(
		bool initial,
		bool loadedDown,
		const ScrollChange &change) {
	const auto guard = gsl::finally([&] {
		_itemRevealPending.clear();
	});
	if (!_history
		|| (initial && _historyInited)
		|| (!initial && !_historyInited && !_firstLoadRequest)) {
		return;
	}
	if (_showAnimation) {
		_updateHistoryGeometryRequired = true;
		return;
	}

	const auto newScrollWidth = width()
		- (_subsectionTabs ? _subsectionTabs->leftSkip() : 0);
	const auto subsectionTabsTop = _topBar->bottomNoMargins();
	auto newScrollHeight = height()
		- subsectionTabsTop
		- (_subsectionTabs ? _subsectionTabs->topSkip() : 0)
		- (_subsectionTabs ? _subsectionTabs->bottomSkip() : 0);
	auto bars = Ui::ChatBarStack();
	bars.add(_groupCallBar ? _groupCallBar->height() : 0);
	bars.add(_requestsBar ? _requestsBar->height() : 0);
	bars.add(_pinnedBar ? _pinnedBar->height() : 0);
	bars.add(_sponsoredMessageBar ? _sponsoredMessageBar->height() : 0);
	bars.add(_translateBar ? _translateBar->height() : 0);
	bars.add(_paysStatus ? _paysStatus->bar().height() : 0);
	bars.add(_contactStatus ? _contactStatus->bar().height() : 0);
	bars.add(_businessBotStatus ? _businessBotStatus->bar().height() : 0);
	newScrollHeight -= bars.height() - topBarsOverlap();
	// 输入区悬浮在列表底部之上，四周留白透出消息，只有胶囊本身遮挡。
	const auto margin = st::historyComposeCapsuleMargin;
	auto composeHeight = 0;
	auto capsule = QRect();
	if (isChoosingTheme()) {
		newScrollHeight -= _chooseTheme->height();
	} else if (!editingMessage()
		&& (isSearching()
			|| isBlocked()
			|| isBotStart()
			|| isJoinChannel()
			|| isMuteUnmute()
			|| isReportMessages())) {
		composeHeight = _unblock->height() + 2 * margin;
	} else {
		if (editingMessage() || _canSendMessages) {
			composeHeight = fieldHeight()
				+ 2 * st::historySendPadding
				+ 2 * margin;
		} else if (_sendRestriction) {
			composeHeight = _sendRestriction->height() + 2 * margin;
		}
		if (_editMsgId
			|| replyTo()
			|| readyToForward()
			|| _previewDrawPreview
			|| _suggestOptions) {
			composeHeight += st::historyReplyHeight;
		}
		if (_kbShown) {
			newScrollHeight -= _kbScroll->height();
		}
		if (editingMessage() || _canSendMessages) {
			// 与 drawField 的胶囊范围一致。
			const auto header = _editMsgId
				|| _replyTo
				|| readyToForward()
				|| _kbReplyTo
				|| _previewDrawPreview
				|| _suggestOptions;
			const auto capsuleHeight = fieldHeight()
				+ 2 * st::historySendPadding
				+ (header ? st::historyReplyHeight : 0);
			const auto capsuleBottom = height()
				- (_kbShown ? _kbScroll->height() : 0)
				- margin;
			capsule = QRect(
				margin,
				capsuleBottom - capsuleHeight,
				width() - 2 * margin,
				capsuleHeight);
		}
	}
	if (_subsectionTabs && _subsectionTabs->bottomSkip()) {
		// 底部标签栏夹在列表与输入区之间，此时不重叠。
		newScrollHeight -= composeHeight;
		composeHeight = 0;
		capsule = QRect();
	}
	if (newScrollHeight - composeHeight - topBarsOverlap() <= 0) {
		return;
	}
	// 列表占用胶囊上方一半留白，最近一条消息更贴近输入区。
	const auto overlap = composeHeight ? (composeHeight - margin / 2) : 0;
	const auto overlapChanged = (_composeOverlap != overlap);
	_composeOverlap = overlap;
	const auto topOverlap = topBarsOverlap();
	const auto topOverlapChanged = (_lastTopBarsOverlap != topOverlap);
	auto topShift = 0;
	if (topOverlapChanged && _historyInited && !initial) {
		const auto newScrollY = _topBars->y() + bars.height() - topOverlap;
		topShift = _preserveScrollTop
			? 0
			: (newScrollY - _lastScrollAreaY)
				+ (topOverlap - _lastTopBarsOverlap);
		_topDelta = 0;
	}
	_lastTopBarsOverlap = topOverlap;
	const auto wasScrollTop = _scroll->scrollTop();
	const auto wasAtBottom = (wasScrollTop >= _scroll->scrollTopMax());
	const auto needResize = (_scroll->width() != newScrollWidth)
		|| (_scroll->height() != newScrollHeight);
	if (needResize) {
		_scroll->resize(newScrollWidth, newScrollHeight);
	}
	updateComposeSurface(capsule);
	// on initial updateListSize we didn't put the _scroll->scrollTop
	// correctly yet so visibleAreaUpdated() call will erase it
	// with the new (undefined) value
	if ((needResize || overlapChanged)
		&& !topOverlapChanged
		&& !initial) {
		visibleAreaUpdated();
	}
	if (needResize || overlapChanged || topOverlapChanged || initial) {
		if (_autocomplete) {
			_autocomplete->setBoundings(visibleScrollGeometry());
		}
		if (_supportAutocomplete) {
			_supportAutocomplete->setBoundings(visibleScrollGeometry());
		}
		_scroll->setBarBottomInset(_composeOverlap);
		_cornerButtons.setBottomSkip(_composeOverlap);
		_cornerButtons.updatePositions();
		_pullToNext->setBottomSkip(_composeOverlap);
		_pullToNext->updateGeometry();
		controller()->floatPlayerAreaUpdated();
	}
	if (_subsectionTabs) {
		const auto tabsBottomSkip = _subsectionTabs->bottomSkip();
		const auto scrollBottom = _scroll->y()
			+ newScrollHeight
			- _composeOverlap;
		const auto areaHeight = scrollBottom
			+ tabsBottomSkip
			- subsectionTabsTop;
		_subsectionTabs->setBoundingRect(
			{ 0, subsectionTabsTop, width(), areaHeight });
	}
	if (_firstLoadRequest) {
		// The scroll area stays visible (and possibly focused) while the
		// first messages are being loaded, so its viewport geometry above
		// is maintained even now. The list layout and the scroll position
		// are still deferred until the requested messages arrive:
		// scrollTopMax etc are not working after recountHistoryGeometry()
		// and the initial scroll position can not be counted yet.
		_updateHistoryGeometryRequired = true;
		return;
	}

	updateListSize();
	_updateHistoryGeometryRequired = false;

	auto newScrollTop = 0;
	if (initial) {
		newScrollTop = countInitialScrollTop();
		_historyInited = true;
		_scrollToAnimation.stop();
	} else if (wasAtBottom && !loadedDown && !_history->unreadBar()) {
		newScrollTop = countAutomaticScrollTop();
	} else {
		newScrollTop = std::min(
			physicalScrollTop(_list->historyScrollTop()),
			_scroll->scrollTopMax());
		if (change.type == ScrollChangeAdd) {
			newScrollTop += topOverlapChanged ? topShift : change.value;
		} else if (change.type == ScrollChangeNoJumpToBottom) {
			newScrollTop = wasScrollTop;
		} else if (topOverlapChanged) {
			newScrollTop += topShift;
		}
	}
	const auto toY = std::clamp(newScrollTop, 0, _scroll->scrollTopMax());
	synteticScrollToY(toY);
	if (initial && _showAtMsgId) {
		const auto timestamp = base::take(_showAtMsgParams.videoTimestamp);
		if (timestamp.has_value()) {
			const auto item = session().data().message(_peer, _showAtMsgId);
			const auto media = item ? item->media() : nullptr;
			const auto document = media ? media->document() : nullptr;
			if (document && document->isVideoFile()) {
				const auto draw = canWriteMessage();
				controller()->openDocument(
					document,
					true,
					{ .id = item->fullId(), .showDrawButton = draw },
					nullptr,
					timestamp);
			}
		}
	}
}

void HistoryWidget::revealItemsCallback() {
	auto height = 0;
	if (!_historyInited) {
		_itemRevealAnimations.clear();
	}
	for (auto i = begin(_itemRevealAnimations)
		; i != end(_itemRevealAnimations);) {
		if (!i->second.animation.animating()) {
			i = _itemRevealAnimations.erase(i);
		} else {
			height += anim::interpolate(
				i->second.startHeight,
				0,
				i->second.animation.value(1.));
			++i;
		}
	}
	if (_itemsRevealHeight != height) {
		const auto wasScrollTop = _scroll->scrollTop();
		const auto wasAtBottom = (wasScrollTop >= _scroll->scrollTopMax());
		if (!wasAtBottom) {
			height = 0;
			_itemRevealAnimations.clear();
		}

		_itemsRevealHeight = height;
		_list->changeItemsRevealHeight(_itemsRevealHeight);

		const auto newScrollTop = (wasAtBottom && !_history->unreadBar())
			? countAutomaticScrollTop()
			: physicalScrollTop(_list->historyScrollTop());
		const auto toY = std::clamp(newScrollTop, 0, _scroll->scrollTopMax());
		synteticScrollToY(toY);
	}
}

void HistoryWidget::startItemRevealAnimations() {
	for (const auto &item : base::take(_itemRevealPending)) {
		if (const auto view = item->mainView()) {
			if (const auto top = _list->itemTop(view); top >= 0) {
				if (const auto height = view->height()) {
					startMessageSendingAnimation(item);
					if (!_itemRevealAnimations.contains(item)) {
						auto &animation = _itemRevealAnimations[item];
						animation.startHeight = height;
						_itemsRevealHeight += height;
						animation.animation.start(
							[=] { revealItemsCallback(); },
							0.,
							1.,
							st::itemRevealDuration,
							anim::easeOutCirc);
						if (item->out() || _history->peer->isSelf()) {
							_list->theme()->rotateComplexGradientBackground();
						}
					}
				}
			}
		}
	}
}

void HistoryWidget::startMessageSendingAnimation(
		not_null<HistoryItem*> item) {
	if (_list->elementChatMode() == HistoryView::ElementChatMode::Default
		&& width() > st::columnMaximalWidthLeft
		&& !item->media()) {
		return;
	}
	auto &sendingAnimation = controller()->sendingAnimation();
	if (!sendingAnimation.checkExpectedType(item)) {
		return;
	}
	Assert(item->mainView() != nullptr);

	auto globalEndTopLeft = rpl::merge(
		_scroll->innerResizes() | rpl::to_empty,
		session().data().newItemAdded() | rpl::to_empty,
		geometryValue() | rpl::to_empty,
		_scroll->geometryValue() | rpl::to_empty,
		_list->geometryValue() | rpl::to_empty
	) | rpl::map([=]() -> std::optional<QPoint> {
		const auto view = item->mainView();
		const auto top = view ? _list->itemTop(view) : -1;
		if (top < 0) {
			return std::nullopt;
		}
		const auto additional = (_list->height() == _scroll->height())
			? view->height()
			: 0;
		return _list->mapToGlobal(QPoint(0, top - additional));
	});

	sendingAnimation.startAnimation({
		.globalEndTopLeft = std::move(globalEndTopLeft),
		.view = [=] { return item->mainView(); },
		.paintContext = [=] { return _list->preparePaintContext({}); },
	});
}

void HistoryWidget::updateListSize() {
	Expects(_list != nullptr);

	_list->recountHistoryGeometry(!_historyInited);
	auto washidden = _scroll->isHidden();
	if (washidden) {
		_scroll->show();
	}
	startItemRevealAnimations();
	_list->setItemsRevealHeight(_itemsRevealHeight);
	_list->updateSize();
	if (washidden) {
		_scroll->hide();
	}
	_updateHistoryGeometryRequired = true;
}

bool HistoryWidget::hasPendingResizedItems() const {
	if (!_list) {
		// Based on the crash reports there is a codepath (at least on macOS)
		// that leads from _list = _scroll->setOwnedWidget(...) right into
		// the HistoryWidget::paintEvent (by sending fake mouse move events
		// inside scroll area -> hiding tooltip window -> exposing the main
		// window -> syncing it backing store synchronously).
		//
		// So really we could get here with !_list && (_history != nullptr).
		return false;
	}
	return (_history && _history->hasPendingResizedItems())
		|| (_migrated && _migrated->hasPendingResizedItems());
}

std::optional<int> HistoryWidget::unreadBarTop() const {
	const auto bar = [&]() -> HistoryView::Element* {
		if (const auto bar = _migrated ? _migrated->unreadBar() : nullptr) {
			return bar;
		}
		return _history->unreadBar();
	}();
	if (bar) {
		const auto result = _list->itemTop(bar)
			+ HistoryView::UnreadBar::marginTop();
		if (bar->Has<HistoryView::DateBadge>()) {
			return result + bar->Get<HistoryView::DateBadge>()->height();
		}
		return result;
	}
	return std::nullopt;
}

void HistoryWidget::addMessagesToFront(
		not_null<PeerData*> peer,
		const QVector<MTPMessage> &messages) {
	_list->messagesReceived(peer, messages);
	if (!_firstLoadRequest) {
		updateHistoryGeometry();
		updateBotKeyboard();
	}
}

void HistoryWidget::addMessagesToBack(
		not_null<PeerData*> peer,
		const QVector<MTPMessage> &messages) {
	const auto checkForUnreadStart = [&] {
		if (_history->unreadBar() || !_history->trackUnreadMessages()) {
			return false;
		}
		_history->calculateFirstUnreadMessage();
		return !_history->firstUnreadMessage();
	}();
	_list->messagesReceivedDown(peer, messages);
	if (checkForUnreadStart) {
		_history->calculateFirstUnreadMessage();
		createUnreadBarAndResize();
	}
	if (!_firstLoadRequest) {
		updateHistoryGeometry(false, true, { ScrollChangeNoJumpToBottom, 0 });
	}
	injectSponsoredMessages();
}
