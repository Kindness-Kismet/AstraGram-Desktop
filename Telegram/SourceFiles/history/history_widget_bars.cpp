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

void HistoryWidget::updatePinnedViewer() {
	if (_firstLoadRequest
		|| _delayedShowAtRequest
		|| _scroll->isHidden()
		|| !_history
		|| !_historyInited
		|| !_pinnedTracker) {
		return;
	}
	const auto visibleBottom = visibleScrollBottom();
	auto [view, offset] = _list->findViewForPinnedTracking(visibleBottom);
	const auto lessThanId = !view
		? (ServerMaxMsgId - 1)
		: (view->history() != _history)
		? (view->data()->id + (offset > 0 ? 1 : 0) - ServerMaxMsgId)
		: (view->data()->id + (offset > 0 ? 1 : 0));
	const auto lastClickedId = !_pinnedClickedId
		? (ServerMaxMsgId - 1)
		: (!_migrated || peerIsChannel(_pinnedClickedId.peer))
		? _pinnedClickedId.msg
		: (_pinnedClickedId.msg - ServerMaxMsgId);
	if (_pinnedClickedId
		&& lessThanId <= lastClickedId
		&& !_scrollToAnimation.animating()) {
		_pinnedClickedId = FullMsgId();
	}
	if (_pinnedClickedId && !_minPinnedId) {
		_minPinnedId = Data::ResolveMinPinnedId(
			_peer,
			MsgId(0), // topicRootId
			PeerId(0), // monoforumPeerId
			_migrated ? _migrated->peer.get() : nullptr);
	}
	if (_pinnedClickedId
		&& _minPinnedId
		&& (_minPinnedId >= _pinnedClickedId)) {
		// After click on the last pinned message we should the top one.
		_pinnedTracker->trackAround(ServerMaxMsgId - 1);
	} else {
		_pinnedTracker->trackAround(std::min(lessThanId, lastClickedId));
	}
}

void HistoryWidget::checkLastPinnedClickedIdReset(
		int wasScrollTop,
		int nowScrollTop) {
	if (_firstLoadRequest
		|| _delayedShowAtRequest
		|| _scroll->isHidden()
		|| !_history
		|| !_historyInited) {
		return;
	}
	if (wasScrollTop < nowScrollTop && _pinnedClickedId) {
		// User scrolled down.
		_pinnedClickedId = FullMsgId();
		_minPinnedId = std::nullopt;
		updatePinnedViewer();
	}
}

void HistoryWidget::setupTranslateBar() {
	Expects(_history != nullptr);

	_translateBar = std::make_unique<HistoryView::TranslateBar>(
		_topBars.get(),
		controller(),
		_history);

	controller()->adaptive().oneColumnValue(
	) | rpl::on_next([=, raw = _translateBar.get()](bool one) {
		raw->setShadowGeometryPostprocess([=](QRect geometry) {
			if (!one) {
				geometry.setLeft(geometry.left() + st::lineWidth);
			}
			return geometry;
		});
	}, _translateBar->lifetime());

	_translateBarHeight = 0;
	_translateBar->heightValue(
	) | rpl::on_next([=](int height) {
		_topDelta = _preserveScrollTop ? 0 : (height - _translateBarHeight);
		_translateBarHeight = height;
		updateHistoryGeometry();
		updateControlsGeometry();
		_topDelta = 0;
	}, _translateBar->lifetime());

	orderWidgets();
}

void HistoryWidget::setupPinnedTracker() {
	Expects(_history != nullptr);

	_pinnedTracker = std::make_unique<HistoryView::PinnedTracker>(_history);
	_pinnedBar = nullptr;
	checkPinnedBarState();
}

void HistoryWidget::checkPinnedBarState() {
	Expects(_pinnedTracker != nullptr);
	Expects(_list != nullptr);

	const auto hiddenId = _peer->canPinMessages()
		? MsgId(0)
		: session().settings().hiddenPinnedMessageId(_peer->id);
	const auto currentPinnedId = Data::ResolveTopPinnedId(
		_peer,
		MsgId(0), // topicRootId
		PeerId(0), // monoforumPeerId
		_migrated ? _migrated->peer.get() : nullptr);
	const auto universalPinnedId = !currentPinnedId
		? int32(0)
		: (_migrated && !peerIsChannel(currentPinnedId.peer))
		? (currentPinnedId.msg - ServerMaxMsgId)
		: currentPinnedId.msg;
	if (universalPinnedId == hiddenId) {
		_pinnedTracker->reset();
		_list->setShownPinned(nullptr);
		if (_pinnedBar) {
			_hidingPinnedBar = std::move(_pinnedBar);
			_hidingPinnedBar->finishAnimating();
			clearHidingPinnedBar();
		}
		const auto topDelta = _preserveScrollTop ? 0 : -_pinnedBarHeight;
		_topDelta = topDelta;
		_pinnedBarHeight = 0;
		updateHistoryGeometry();
		updateControlsGeometry();
		_topDelta = 0;
		return;
	}
	if (_pinnedBar || !universalPinnedId) {
		return;
	}

	clearHidingPinnedBar();
	_pinnedBar = std::make_unique<Ui::PinnedBar>(_topBars.get(), [=] {
		return controller()->isGifPausedAtLeastFor(
			Window::GifPauseReason::Any);
	}, controller()->gifPauseLevelChanged());
	auto pinnedRefreshed = Info::Profile::SharedMediaCountValue(
		_peer,
		MsgId(0), // topicRootId
		PeerId(0), // monoforumPeerId
		nullptr,
		Storage::SharedMediaType::Pinned
	) | rpl::distinct_until_changed(
	) | rpl::map([=](int count) {
		if (_pinnedClickedId) {
			_pinnedClickedId = FullMsgId();
			_minPinnedId = std::nullopt;
			updatePinnedViewer();
		}
		return (count > 1);
	}) | rpl::distinct_until_changed();
	auto customButtonItem = HistoryView::PinnedBarItemWithCustomButton(
		&session(),
		_pinnedTracker->shownMessageId());
	rpl::combine(
		rpl::duplicate(pinnedRefreshed),
		rpl::duplicate(customButtonItem)
	) | rpl::on_next([=](bool many, HistoryItem *item) {
		refreshPinnedBarButton(many, item);
	}, _pinnedBar->lifetime());

	_pinnedBar->setContent(rpl::combine(
		HistoryView::PinnedBarContent(
			&session(),
			_pinnedTracker->shownMessageId(),
			[bar = _pinnedBar.get()] { bar->customEmojiRepaint(); }),
		std::move(pinnedRefreshed),
		std::move(customButtonItem)
	) | rpl::map([=](Ui::MessageBarContent &&content, bool, HistoryItem*) {
		const auto id = (!content.title.isEmpty() || !content.text.empty())
			? _pinnedTracker->currentMessageId().message
			: FullMsgId();
		if (const auto list = _list.data()) {
			// Sometimes we get here with non-empty content and id of
			// message that is being deleted right now. We get here in
			// the moment when _itemRemoved was already fired (so in
			// the _list the _pinnedItem is already cleared) and the
			// MessageUpdate::Flag::Destroyed being fired right now,
			// so the message is still in Data::Session. So we need to
			// call data().message() async, otherwise we get a nearly-
			// destroyed message from it and save the pointer in _list.
			crl::on_main(list, [=] {
				list->setShownPinned(session().data().message(id));
			});
		}
		return std::move(content);
	}));

	controller()->adaptive().oneColumnValue(
	) | rpl::on_next([=, raw = _pinnedBar.get()](bool one) {
		raw->setShadowGeometryPostprocess([=](QRect geometry) {
			if (!one) {
				geometry.setLeft(geometry.left() + st::lineWidth);
			}
			return geometry;
		});
	}, _pinnedBar->lifetime());

	_pinnedBar->barClicks(
	) | rpl::on_next([=] {
		const auto id = _pinnedTracker->currentMessageId();
		if (const auto item = session().data().message(id.message)) {
			controller()->showPeerHistory(
				item->history()->peer,
				Window::SectionShow::Way::Forward,
				item->id);
			if (const auto group = session().data().groups().find(item)) {
				// Hack for the case when a non-first item of an album
				// is pinned and we still want the 'show last after first'.
				_pinnedClickedId = group->items.front()->fullId();
			} else {
				_pinnedClickedId = id.message;
			}
			_minPinnedId = std::nullopt;
			updatePinnedViewer();
		}
	}, _pinnedBar->lifetime());

	_pinnedBar->barRightClicks(
	) | rpl::on_next([=] {
		if (_pinnedBarHasCustomButton) {
			return;
		}
		const auto reference = _pinnedClickedId
			? _pinnedClickedId
			: _pinnedTracker->currentMessageId().message;
		if (!reference) {
			return;
		}
		const auto universal = [&](FullMsgId id) {
			return (!id || !_migrated || peerIsChannel(id.peer))
				? id.msg
				: (id.msg - ServerMaxMsgId);
		};
		const auto migrated = _migrated ? _migrated->peer.get() : nullptr;
		const auto referenceId = universal(reference);
		const auto top = Data::ResolveTopPinnedId(
			_peer,
			MsgId(0), // topicRootId
			PeerId(0), // monoforumPeerId
			migrated);
		const auto targetId = (top && referenceId >= universal(top))
			? Data::ResolveMinPinnedId(
				_peer,
				MsgId(0), // topicRootId
				PeerId(0), // monoforumPeerId
				migrated)
			: _pinnedTracker->nextPinnedId(referenceId);
		if (!targetId) {
			return;
		}
		controller()->showPeerHistory(
			session().data().peer(targetId.peer),
			Window::SectionShow::Way::Forward,
			targetId.msg);
		_pinnedClickedId = FullMsgId();
		_minPinnedId = std::nullopt;
		updatePinnedViewer();
	}, _pinnedBar->lifetime());

	_pinnedBarHeight = 0;
	_pinnedBar->heightValue(
	) | rpl::on_next([=](int height) {
		_topDelta = _preserveScrollTop ? 0 : (height - _pinnedBarHeight);
		_pinnedBarHeight = height;
		updateHistoryGeometry();
		updateControlsGeometry();
		_topDelta = 0;
	}, _pinnedBar->lifetime());

	orderWidgets();
}

void HistoryWidget::clearHidingPinnedBar() {
	if (!_hidingPinnedBar) {
		return;
	}
	if (const auto delta = -_pinnedBarHeight) {
		_pinnedBarHeight = 0;
		setGeometryWithTopMoved(geometry(), delta);
	}
	_hidingPinnedBar = nullptr;
}

void HistoryWidget::checkMessagesTTL() {
	if (!_peer || !_peer->messagesTTL()) {
		if (_ttlInfo) {
			_ttlInfo = nullptr;
			updateControlsGeometry();
			updateControlsVisibility();
		}
	} else if (!_ttlInfo || _ttlInfo->peer() != _peer) {
		_ttlInfo = std::make_unique<HistoryView::Controls::TTLButton>(
			this,
			controller()->uiShow(),
			_peer);
		orderWidgets();
		updateControlsGeometry();
		updateControlsVisibility();
	}
}

void HistoryWidget::setChooseReportMessagesDetails(
		Data::ReportInput reportInput,
		Fn<void(std::vector<MsgId>)> callback) {
	if (!callback) {
		const auto refresh = _chooseForReport && _chooseForReport->active;
		_chooseForReport = nullptr;
		if (_list) {
			_list->clearChooseReportReason();
		}
		if (refresh) {
			clearSelected();
			updateControlsVisibility();
			updateControlsGeometry();
			updateTopBarChooseForReport();
		}
	} else {
		_chooseForReport = std::make_unique<ChooseMessagesForReport>(
			ChooseMessagesForReport{
				.reportInput = reportInput,
				.callback = std::move(callback) });
	}
}

void HistoryWidget::refreshPinnedBarButton(bool many, HistoryItem *item) {
	if (!_pinnedBar) {
		return; // It can be in process of hiding.
	}
	const auto openSection = [=] {
		const auto id = _pinnedTracker
			? _pinnedTracker->currentMessageId()
			: HistoryView::PinnedId();
		if (!id.message) {
			return;
		}
		controller()->showSection(
			std::make_shared<HistoryView::PinnedMemento>(
				_history,
				((!_migrated || peerIsChannel(id.message.peer))
					? id.message.msg
					: (id.message.msg - ServerMaxMsgId))));
	};
	const auto context = [copy = _list](FullMsgId itemId) {
		if (const auto raw = copy.data()) {
			return raw->prepareClickHandlerContext(itemId);
		}
		return ClickHandlerContext();
	};
	auto customButton = CreatePinnedBarCustomButton(this, item, context);
	if (customButton) {
		_pinnedBarHasCustomButton = true;
		struct State {
			base::unique_qptr<Ui::PopupMenu> menu;
		};
		const auto buttonRaw = customButton.data();
		const auto state = buttonRaw->lifetime().make_state<State>();
		_pinnedBar->contextMenuRequested(
		) | rpl::on_next([=] {
			state->menu = base::make_unique_q<Ui::PopupMenu>(buttonRaw);
			state->menu->addAction(
				tr::lng_settings_events_pinned(tr::now),
				openSection);
			state->menu->popup(QCursor::pos());
		}, buttonRaw->lifetime());
		_pinnedBar->setRightButton(std::move(customButton));
		return;
	}
	_pinnedBarHasCustomButton = false;

	const auto close = !many;
	auto button = object_ptr<Ui::IconButton>(
		this,
		close ? st::historyReplyCancel : st::historyPinnedShowAll);
	button->setAccessibleName(close
		? tr::lng_pinned_unpin(tr::now)
		: tr::lng_settings_events_pinned(tr::now));
	button->clicks(
	) | rpl::on_next([=] {
		if (close) {
			hidePinnedMessage();
		} else {
			openSection();
		}
	}, button->lifetime());
	_pinnedBar->setRightButton(std::move(button));
}

void HistoryWidget::setupGroupCallBar() {
	Expects(_history != nullptr);

	const auto peer = _history->peer;
	if (!peer->isChannel() && !peer->isChat()) {
		_groupCallBar = nullptr;
		return;
	}
	_groupCallBar = std::make_unique<Ui::GroupCallBar>(
		_topBars.get(),
		HistoryView::GroupCallBarContentByPeer(
			peer,
			st::historyGroupCallUserpics.size,
			false),
		Core::App().appDeactivatedValue());

	controller()->adaptive().oneColumnValue(
	) | rpl::on_next([=](bool one) {
		_groupCallBar->setShadowGeometryPostprocess([=](QRect geometry) {
			if (!one) {
				geometry.setLeft(geometry.left() + st::lineWidth);
			}
			return geometry;
		});
	}, _groupCallBar->lifetime());

	rpl::merge(
		_groupCallBar->barClicks(),
		_groupCallBar->joinClicks()
	) | rpl::on_next([=] {
		const auto peer = _history->peer;
		if (peer->groupCall()) {
			controller()->startOrJoinGroupCall(peer, {});
		}
	}, _groupCallBar->lifetime());

	_groupCallBarHeight = 0;
	_groupCallBar->heightValue(
	) | rpl::on_next([=](int height) {
		_topDelta = _preserveScrollTop ? 0 : (height - _groupCallBarHeight);
		_groupCallBarHeight = height;
		updateHistoryGeometry();
		updateControlsGeometry();
		_topDelta = 0;
	}, _groupCallBar->lifetime());

	orderWidgets();
}

void HistoryWidget::setupRequestsBar() {
	Expects(_history != nullptr);

	const auto peer = _history->peer;
	if (!peer->isChannel() && !peer->isChat()) {
		_requestsBar = nullptr;
		return;
	}
	_requestsBar = std::make_unique<Ui::RequestsBar>(
		_topBars.get(),
		HistoryView::RequestsBarContentByPeer(
			peer,
			st::historyRequestsUserpics.size,
			false));

	controller()->adaptive().oneColumnValue(
	) | rpl::on_next([=](bool one) {
		_requestsBar->setShadowGeometryPostprocess([=](QRect geometry) {
			if (!one) {
				geometry.setLeft(geometry.left() + st::lineWidth);
			}
			return geometry;
		});
	}, _requestsBar->lifetime());

	_requestsBar->barClicks(
	) | rpl::on_next([=] {
		RequestsBoxController::Start(controller(), _peer);
	}, _requestsBar->lifetime());

	_requestsBarHeight = 0;
	_requestsBar->heightValue(
	) | rpl::on_next([=](int height) {
		_topDelta = _preserveScrollTop ? 0 : (height - _requestsBarHeight);
		_requestsBarHeight = height;
		updateHistoryGeometry();
		updateControlsGeometry();
		_topDelta = 0;
	}, _requestsBar->lifetime());

	orderWidgets();
}

void HistoryWidget::requestMessageData(MsgId msgId) {
	if (!_peer) {
		return;
	}
	const auto peer = _peer;
	const auto callback = crl::guard(this, [=] {
		messageDataReceived(peer, msgId);
	});
	session().api().requestMessageData(_peer, msgId, callback);
}

bool HistoryWidget::checkSponsoredMessageBarVisibility() const {
	const auto h = _list->height()
		- (_kbScroll->isHidden() ? 0 : _kbScroll->height());
	return (h > _scroll->height());
}

void HistoryWidget::requestSponsoredMessageBar() {
	if (!_history || !session().sponsoredMessages().isTopBarFor(_history)) {
		return;
	}
	const auto checkState = [=, this] {
		using State = Data::SponsoredMessages::State;
		const auto state = session().sponsoredMessages().state(
			_history);
		_sponsoredMessagesStateKnown = (state != State::None);
		if (state == State::AppendToTopBar) {
			createSponsoredMessageBar();
			if (checkSponsoredMessageBarVisibility()) {
				_sponsoredMessageBar->toggle(true, anim::type::normal);
			} else {
				auto &lifetime = _sponsoredMessageBar->lifetime();
				const auto heightLifetime
					= lifetime.make_state<rpl::lifetime>();
				_list->heightValue(
				) | rpl::on_next([=, this] {
					if (_sponsoredMessageBar->toggled()) {
						heightLifetime->destroy();
					} else if (checkSponsoredMessageBarVisibility()) {
						_sponsoredMessageBar->toggle(
							true,
							anim::type::normal);
						heightLifetime->destroy();
					}
				}, *heightLifetime);
			}
		}
	};
	const auto history = _history;
	session().sponsoredMessages().request(
		_history,
		crl::guard(this, [=, this] {
			if (history == _history) {
				checkState();
			}
		}));
}

void HistoryWidget::checkSponsoredMessageBar() {
	if (!_history || !session().sponsoredMessages().isTopBarFor(_history)) {
		return;
	}
	const auto state = session().sponsoredMessages().state(_history);
	if (state == Data::SponsoredMessages::State::AppendToTopBar) {
		if (checkSponsoredMessageBarVisibility()) {
			if (!_sponsoredMessageBar) {
				createSponsoredMessageBar();
			}
			_sponsoredMessageBar->toggle(true, anim::type::instant);
		}
	}
}

void HistoryWidget::createSponsoredMessageBar() {
	_sponsoredMessageBar = base::make_unique_q<Ui::SlideWrap<>>(
		_topBars.get(),
		object_ptr<Ui::RpWidget>(this));

	_sponsoredMessageBar->entity()->resizeToWidth(_scroll->width());
	const auto maybeFullId = session().sponsoredMessages().fillTopBar(
		_history,
		_sponsoredMessageBar->entity());
	session().sponsoredMessages().itemRemoved(
		maybeFullId
	) | rpl::on_next([this] {
		_sponsoredMessageBar->toggle(false, anim::type::normal);
		_sponsoredMessageBar->shownValue() | rpl::filter(
			!rpl::mappers::_1
		) | rpl::on_next([this] {
			_sponsoredMessageBar = nullptr;
		}, _sponsoredMessageBar->lifetime());
	}, _sponsoredMessageBar->lifetime());

	if (maybeFullId) {
		const auto viewLifetime
			= _sponsoredMessageBar->lifetime().make_state<rpl::lifetime>();
		rpl::combine(
			_sponsoredMessageBar->entity()->heightValue(),
			_sponsoredMessageBar->heightValue()
		) | rpl::filter(
			rpl::mappers::_1 == rpl::mappers::_2
		) | rpl::on_next([=] {
			session().sponsoredMessages().view(maybeFullId);
			viewLifetime->destroy();
		}, *viewLifetime);
	}

	_sponsoredMessageBarHeight = 0;
	_sponsoredMessageBar->heightValue(
	) | rpl::on_next([=](int height) {
		_topDelta = _preserveScrollTop
			? 0
			: (height - _sponsoredMessageBarHeight);
		_sponsoredMessageBarHeight = height;
		updateHistoryGeometry();
		updateControlsGeometry();
		_topDelta = 0;
	}, _sponsoredMessageBar->lifetime());
	_sponsoredMessageBar->toggle(false, anim::type::instant);
}

void HistoryWidget::showInfoTooltip(
		const TextWithEntities &text,
		Fn<void()> hiddenCallback) {
	_topToast.show(
		_scroll.data(),
		&session(),
		text,
		std::move(hiddenCallback));
}

void HistoryWidget::showHiddenSenderTooltip(
		QRect globalArea,
		const TextWithEntities &text) {
	_hiddenSenderTooltip.show(
		_scroll.data(),
		_scroll->scrolls(),
		globalArea,
		text);
}

void HistoryWidget::showPremiumStickerTooltip(
		not_null<const HistoryView::Element*> view) {
	if (const auto media = view->data()->media()) {
		if (const auto document = media->document()) {
			showPremiumToast(document);
		}
	}
}

void HistoryWidget::showPremiumToast(not_null<DocumentData*> document) {
	if (!_stickerToast) {
		_stickerToast = std::make_unique<HistoryView::StickerToast>(
			controller(),
			this,
			[=] { _stickerToast = nullptr; });
	}
	_stickerToast->showFor(document);
}

void HistoryWidget::validateSubsectionTabs() {
	if (!_subsectionCheckLifetime && _history) {
		if (const auto group = _history->peer->asMegagroup()) {
			_subsectionCheckLifetime = group->flagsValue(
			) | rpl::skip(
				1
			) | rpl::filter([=](Data::Flags<ChannelDataFlags>::Change change) {
				const auto mask = ChannelDataFlag::Forum
					| ChannelDataFlag::ForumTabs
					| ChannelDataFlag::MonoforumAdmin;
				return change.diff & mask;
			}) | rpl::on_next([=] {
				validateSubsectionTabs();
			});
		} else if (const auto user = _history->peer->asBot()) {
			_subsectionCheckLifetime = user->flagsValue(
			) | rpl::skip(
				1
			) | rpl::filter([=](Data::Flags<UserDataFlags>::Change change) {
				return change.diff & UserDataFlag::Forum;
			}) | rpl::on_next([=] {
				_subsectionTopicsLifetime.destroy();
				validateSubsectionTabs();
			});
		}
	}
	if (_history && !_subsectionTopicsLifetime) {
		if (const auto user = _history->peer->asBot()) {
			if (const auto forum = user->forum()) {
				_subsectionTopicsLifetime = forum->topicsList()->fullSize().value(
				) | rpl::map([](int size) {
					return size > 0;
				}) | rpl::distinct_until_changed(
				) | rpl::skip(
					1
				) | rpl::on_next([=] {
					validateSubsectionTabs();
				});
			}
		}
	}
	if (!_history || !HistoryView::SubsectionTabs::UsedFor(_history)) {
		if (_subsectionTabs) {
			_subsectionTabsLifetime.destroy();
			_subsectionTabs = nullptr;
			updateControlsGeometry();

			const auto forum = _history ? _history->asForum() : nullptr;
			if (forum && !_history->peer->isUser()) {
				controller()->showForum(forum, {
					Window::SectionShow::Way::Backward,
					anim::type::normal,
					anim::activation::background,
				});
			}
		}
		return;
	} else if (_subsectionTabs) {
		return;
	}
	_subsectionTabs = controller()->restoreSubsectionTabsFor(this, _history);
	if (!_subsectionTabs) {
		_subsectionTabs = std::make_unique<HistoryView::SubsectionTabs>(
			controller(),
			this,
			_history);
	}
	_subsectionTabs->removeRequests() | rpl::on_next([=] {
		_subsectionTabsLifetime.destroy();
		_subsectionTabs = nullptr;
		updateControlsGeometry();
	}, _subsectionTabsLifetime);
	_subsectionTabs->layoutRequests() | rpl::on_next([=] {
		_list->toggleRemoveFromUserpics(_subsectionTabs->leftSkip() > 0);
		updateControlsGeometry();
		orderWidgets();
	}, _subsectionTabsLifetime);
	_list->toggleRemoveFromUserpics(_subsectionTabs->leftSkip() > 0);
	updateControlsGeometry();
	orderWidgets();
}

void HistoryWidget::checkCharsCount() {
	_fieldCharsCountManager.setCount(Ui::ComputeFieldCharacterCount(_field));
	checkCharsLimitation();
}

void HistoryWidget::checkCharsLimitation() {
	if (!_history || !_editMsgId) {
		_charsLimitation = nullptr;
		return;
	}
	const auto item = session().data().message(_history->peer, _editMsgId);
	if (!item) {
		_charsLimitation = nullptr;
		return;
	}
	const auto hasMediaWithCaption = item->media()
		&& item->media()->allowsEditCaption();
	const auto limits = Data::PremiumLimits(&session());
	const auto maxTextSize = hasMediaWithCaption
		? limits.captionLengthCurrent()
		: limits.messageLengthCurrent();
	const auto remove = _fieldCharsCountManager.count() - maxTextSize;
	if (remove > 0) {
		if (!_charsLimitation) {
			_charsLimitation = base::make_unique_q<CharactersLimitLabel>(
				this,
				_send.get(),
				style::al_bottom);
			_charsLimitation->show();
		}
		_charsLimitation->setLeft(remove);
	} else {
		_charsLimitation = nullptr;
	}
}
