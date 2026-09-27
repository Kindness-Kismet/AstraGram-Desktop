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

void HistoryWidget::showHistory(
		PeerId peerId,
		MsgId showAtMsgId,
		const Window::SectionShow &params) {
	_pinnedClickedId = FullMsgId();
	_minPinnedId = std::nullopt;
	_showAtMsgParams = {};

	const auto wasState = controller()->dialogsEntryStateCurrent();
	const auto startBot = (showAtMsgId == ShowAndStartBotMsgId);
	_showAndMaybeSendStart = (showAtMsgId == ShowAndMaybeStartBotMsgId);
	if (startBot || _showAndMaybeSendStart) {
		showAtMsgId = ShowAtTheEndMsgId;
	}

	_highlighter.clear();
	controller()->sendingAnimation().clear();
	_topToast.hide(anim::type::instant);
	_hiddenSenderTooltip.hide();
	if (_history) {
		if (_peer->id == peerId) {
			updateForwarding();

			if (params.reapplyLocalDraft) {
				return;
			} else if (showAtMsgId == ShowAtUnreadMsgId
				&& insideJumpToEndInsteadOfToUnread()) {
				DEBUG_LOG(("JumpToEnd(%1, %2, %3): "
					"Jumping to end instead of unread."
					).arg(_history->peer->name()
					).arg(_history->inboxReadTillId().bare
					).arg(Logs::b(_history->loadedAtBottom())));
				showAtMsgId = ShowAtTheEndMsgId;
			} else if (showAtMsgId == ShowForChooseMessagesMsgId) {
				if (_chooseForReport) {
					clearSelected();
					_chooseForReport->active = true;
					_list->setChooseReportReason(
						_chooseForReport->reportInput);
					updateControlsVisibility();
					updateControlsGeometry();
					updateTopBarChooseForReport();
				}
				return;
			}
			if (!IsServerMsgId(showAtMsgId)
				&& !IsClientMsgId(showAtMsgId)
				&& !IsServerMsgId(-showAtMsgId)) {
				// To end or to unread.
				destroyUnreadBar();
			}
			const auto canShowNow = _history->isReadyFor(showAtMsgId);
			if (!canShowNow) {
				if (!_firstLoadRequest) {
					DEBUG_LOG(("JumpToEnd(%1, %2, %3): Showing delayed at %4."
						).arg(_history->peer->name()
						).arg(_history->inboxReadTillId().bare
						).arg(Logs::b(_history->loadedAtBottom())
						).arg(showAtMsgId.bare));
					delayedShowAt(showAtMsgId, params);
				} else if (_showAtMsgId != showAtMsgId) {
					clearAllLoadRequests();
					setMsgId(showAtMsgId, params);
					firstLoadMessages();
					doneShow();
				}
			} else {
				_history->forgetScrollState();
				if (_migrated) {
					_migrated->forgetScrollState();
				}

				clearDelayedShowAt();
				const auto skipId = (_migrated && showAtMsgId < 0)
					? FullMsgId(_migrated->peer->id, -showAtMsgId)
					: (showAtMsgId > 0)
					? FullMsgId(_history->peer->id, showAtMsgId)
					: FullMsgId();
				if (skipId) {
					_cornerButtons.skipReplyReturn(skipId);
				}

				setMsgId(showAtMsgId, params);
				if (_historyInited) {
					DEBUG_LOG(("JumpToEnd(%1, %2, %3): "
						"Showing instant at %4."
						).arg(_history->peer->name()
						).arg(_history->inboxReadTillId().bare
						).arg(Logs::b(_history->loadedAtBottom())
						).arg(showAtMsgId.bare));

					const auto to = countInitialScrollTop();
					const auto item = getItemFromHistoryOrMigrated(
						_showAtMsgId);
					animatedScrollToY(
						std::clamp(to, 0, _scroll->scrollTopMax()),
						item);
				} else {
					historyLoaded();
				}
			}

			_topBar->update();
			update();

			if (const auto user = _peer->asUser()) {
				if (const auto &info = user->botInfo) {
					if (startBot || clearMaybeSendStart()) {
						if (startBot && wasState.key) {
							info->inlineReturnTo = wasState;
						}
						sendBotStartCommand();
						_history->clearLocalDraft(MsgId(), PeerId());
						applyDraft();
						_send->finishAnimating();
					}
				}
			}
			return;
		} else {
			_sponsoredMessagesStateKnown = false;
			session().sponsoredMessages().clearItems(_history);
			session().data().hideShownSpoilers();
			_composeSearch = nullptr;
		}
		session().sendProgressManager().update(
			_history,
			Api::SendProgressType::Typing,
			-1);
		session().data().histories().sendPendingReadInbox(_history);
		session().sendProgressManager().cancelTyping(_history);
	}

	_cornerButtons.clearReplyReturns();
	if (_history) {
		if (Ui::InFocusChain(this)) {
			// Removing focus from list clears selected and updates top bar.
			setFocus();
		}
		controller()->session().api().saveCurrentDraftToCloud();
		if (_migrated) {
			_migrated->clearDrafts(); // use migrated draft only once
		}

		_history->showAtMsgId = _showAtMsgId;

		destroyUnreadBarOnClose();
		_sponsoredMessageBar = nullptr;
		_pinnedBar = nullptr;
		_translateBar = nullptr;
		_pinnedTracker = nullptr;
		_groupCallBar = nullptr;
		_requestsBar = nullptr;
		_chooseTheme = nullptr;
		_membersDropdown.destroy();
		_scrollToAnimation.stop();

		setHistory(nullptr);
		_list = nullptr;
		_peer = nullptr;
		_suggestOptions = nullptr;
		_sendPayment.clear();
		_topicsRequested.clear();
		_canSendMessages = false;
		_canSendTexts = false;
		_fieldDisabled = nullptr;
		_silent.destroy();
		updateBotKeyboard();

		_subsectionCheckLifetime.destroy();
		_subsectionTopicsLifetime.destroy();
		if (_subsectionTabs) {
			_subsectionTabsLifetime.destroy();
			controller()->saveSubsectionTabs(base::take(_subsectionTabs));
		}
	} else {
		Assert(_list == nullptr);
	}

	HistoryView::Element::ClearGlobal();

	_saveEditMsgRequestId = 0;
	_processingReplyItem = _replyEditMsg = nullptr;
	_processingReplyTo = _replyTo = FullReplyTo();
	_editMsgId = MsgId();
	_canReplaceMedia = _canAddMedia = false;
	_photoEditMedia = nullptr;
	updateReplaceMediaButton();
	_fieldBarCancel->hide();

	_mediaEditManager.cancel();
	_membersDropdownShowTimer.cancel();
	_scroll->takeWidget<HistoryInner>().destroy();

	clearInlineBot();

	_showAtMsgId = showAtMsgId;
	_showAtMsgParams = params;
	_historyInited = false;
	_paysStatus = nullptr;
	_contactStatus = nullptr;
	_businessBotStatus = nullptr;

	Core::App().mediaDevices().refreshRecordAvailability();

	if (peerId) {
		using namespace HistoryView;
		_peer = session().data().peer(peerId);
		_contactStatus = std::make_unique<ContactStatus>(
			controller(),
			_topBars.get(),
			_peer,
			false);
		_contactStatus->bar().heightValue(
		) | rpl::on_next([=] {
			updateControlsGeometry();
		}, _contactStatus->bar().lifetime());

		refreshGiftToChannelShown();
		refreshDirectMessageShown();
		if (const auto user = _peer->asUser()) {
			_paysStatus = std::make_unique<PaysStatus>(
				controller(),
				_topBars.get(),
				user);
			_paysStatus->bar().heightValue(
			) | rpl::on_next([=] {
				updateControlsGeometry();
			}, _paysStatus->bar().lifetime());
			_businessBotStatus = std::make_unique<BusinessBotStatus>(
				controller(),
				_topBars.get(),
				user);
			_businessBotStatus->bar().heightValue(
			) | rpl::on_next([=] {
				updateControlsGeometry();
			}, _businessBotStatus->bar().lifetime());
		}
		orderWidgets();
		controller()->tabbedSelector()->setCurrentPeer(_peer);
	}
	refreshTabbedPanel();
	initFieldAutocomplete();

	if (_peer) {
		_unblock->setText(((_peer->isUser()
			&& _peer->asUser()->isBot()
			&& !_peer->asUser()->isSupport())
				? tr::lng_restart_button(tr::now)
				: tr::lng_unblock_button(tr::now)).toUpper());
	}

	_nonEmptySelection = false;
	_itemRevealPending.clear();
	_itemRevealAnimations.clear();
	_itemsRevealHeight = 0;

	if (_peer) {
		setHistory(_peer->owner().history(_peer));
		if (_migrated
			&& !_migrated->isEmpty()
			&& (!_history->loadedAtTop() || !_migrated->loadedAtBottom())) {
			_migrated->clear(History::ClearType::Unload);
		}
		_history->setFakeUnreadWhileOpened(true);

		if (_showAtMsgId == ShowForChooseMessagesMsgId) {
			_showAtMsgId = ShowAtUnreadMsgId;
			if (_chooseForReport) {
				_chooseForReport->active = true;
			}
		} else {
			_chooseForReport = nullptr;
		}
		if (_showAtMsgId == ShowAtUnreadMsgId
			&& !_history->trackUnreadMessages()
			&& !hasSavedScroll()) {
			_showAtMsgId = ShowAtTheEndMsgId;
		}
		refreshTopBarActiveChat();
		updateTopBarSelection();

		if (_peer->isChannel()) {
			updateNotifyControls();
			session().data().notifySettings().request(_peer);
			refreshSilentToggle();
		} else if (_peer->isRepliesChat() || _peer->isVerifyCodes()) {
			updateNotifyControls();
		}
		refreshSuggestPostToggle();
		refreshScheduledToggle();
		refreshSendGiftToggle();
		refreshSendAsToggle();

		if (_showAtMsgId == ShowAtUnreadMsgId) {
			if (_history->scrollTopItem) {
				_showAtMsgId = _history->showAtMsgId;
			}
		} else {
			_history->forgetScrollState();
			if (_migrated) {
				_migrated->forgetScrollState();
			}
		}

		_list = _scroll->setOwnedWidget(
			object_ptr<HistoryInner>(this, _scroll, controller(), _history));
		_pullToNext->reset(anim::type::instant);
		_list->sendIntroSticker(
		) | rpl::on_next([=](not_null<DocumentData*> sticker) {
			sendExistingDocument(
				sticker,
				Api::MessageToSend(prepareSendAction({})));
		}, _list->lifetime());
		_list->show();

		if (const auto channel = _peer->asChannel()) {
			channel->updateFull();
			if (!channel->isBroadcast()) {
				using Flags = Data::Flags<ChannelDataFlags>;
				channel->flagsValue() | rpl::skip(
					1
				) | rpl::on_next([=](Flags::Change change) {
					refreshJoinChannelText();
					if (change.diff & ChannelDataFlag::MonoforumDisabled) {
						updateCanSendMessage();
						updateSendRestriction();
						updateHistoryGeometry();
					}
				}, _list->lifetime());
			}
			refreshJoinChannelText();
		}

		controller()->adaptive().changes(
		) | rpl::on_next([=] {
			_history->forceFullResize();
			if (_migrated) {
				_migrated->forceFullResize();
			}
			updateHistoryGeometry();
			update();
		}, _list->lifetime());

		if (_chooseForReport && _chooseForReport->active) {
			_list->setChooseReportReason(_chooseForReport->reportInput);
		}
		updateTopBarChooseForReport();

		_updateHistoryItems.cancel();

		setupTranslateBar();
		setupPinnedTracker();
		setupGroupCallBar();
		setupRequestsBar();
		checkMessagesTTL();
		if (_history->scrollTopItem
			|| (_migrated && _migrated->scrollTopItem)
			|| _history->isReadyFor(_showAtMsgId)) {
			historyLoaded();
		} else {
			firstLoadMessages();
			doneShow();
		}

		handlePeerUpdate();

		session().local().readDraftsWithCursors(_history);
		if (!applyDraft()) {
			clearFieldText();
		}
		checkCharsCount();
		_send->finishAnimating();

		updateControlsGeometry();

		if (const auto user = _peer->asUser()) {
			if (const auto &info = user->botInfo) {
				if (startBot
					|| (!_history->isEmpty() && clearMaybeSendStart())) {
					if (startBot && wasState.key) {
						info->inlineReturnTo = wasState;
					}
					sendBotStartCommand();
				}
			} else {
				Info::Profile::BirthdayValue(
					user
				) | rpl::map(
					Data::IsBirthdayTodayValue
				) | rpl::flatten_latest(
				) | rpl::distinct_until_changed(
				) | rpl::on_next([=] {
					refreshSendGiftToggle();
					updateControlsVisibility();
					updateControlsGeometry();
				}, _list->lifetime());
			}
		}
		if (!_history->folderKnown()) {
			session().data().histories().requestDialogEntry(_history);
		}

		// Must be done before unreadCountUpdated(), or we auto-close.
		if (_history->unreadMark()) {
			session().data().histories().changeDialogUnreadMark(
				_history,
				false);
		}
		if (_migrated && _migrated->unreadMark()) {
			session().data().histories().changeDialogUnreadMark(
				_migrated,
				false);
		}
		unreadCountUpdated(); // set _historyDown badge.
		showAboutTopPromotion();

		if (!session().sponsoredMessages().isTopBarFor(_history)) {
			const auto checkState = [=] {
				using State = Data::SponsoredMessages::State;
				const auto state = session().sponsoredMessages().state(
					_history);
				_sponsoredMessagesStateKnown = (state != State::None);
				if (state == State::InjectToMiddle) {
					injectSponsoredMessages();
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
			checkState();
		} else {
			requestSponsoredMessageBar();
		}
	} else {
		_chooseForReport = nullptr;
		refreshTopBarActiveChat();
		updateTopBarSelection();
		checkMessagesTTL();
		clearFieldText();
		doneShow();
	}
	updateForwarding();
	updateOverStates(mapFromGlobal(QCursor::pos()));

	if (_history) {
		const auto msgId = (_showAtMsgId == ShowAtTheEndMsgId)
			? ShowAtUnreadMsgId
			: _showAtMsgId;
		controller()->setActiveChatEntry({
			_history,
			FullMsgId(_history->peer->id, msgId) });
	}
	update();
	controller()->floatPlayerAreaUpdated();
	session().data().itemVisibilitiesUpdated();

	crl::on_main(this, [=] { controller()->widget()->setInnerFocus(); });
}

void HistoryWidget::setHistory(History *history) {
	if (_history == history) {
		return;
	}
	_composeSurface->hide();
	_composeSurfaceRect = QRect();
	resetFrostedBackground();
	_pullToNext->setHistory(history);

	const auto &settings = AyuSettings::getInstance();

	const auto was = _attachBotsMenu && _history && _history->peer->isUser();
	const auto now = _attachBotsMenu && history && history->peer->isUser() && settings.showAttachPopup();
	if (was && !now) {
		_attachToggle->removeEventFilter(_attachBotsMenu.get());
		_attachBotsMenu->hideFast();
	} else if (now && !was) {
		_attachToggle->installEventFilter(_attachBotsMenu.get());
	}

	const auto unloadHeavyViewParts = [](History *history) {
		if (history) {
			history->owner().unloadHeavyViewParts(
				history->delegateMixin()->delegate());
			history->forceFullResize();
		}
	};

	if (_history) {
		untrackThreadFieldVisibility();
		unregisterDraftSources();
		clearAllLoadRequests();
		clearSupportPreloadRequest();
		_historySponsoredPreloading.destroy();
		const auto wasHistory = base::take(_history);
		const auto wasMigrated = base::take(_migrated);
		unloadHeavyViewParts(wasHistory);
		unloadHeavyViewParts(wasMigrated);
		if (const auto wasCreatingBotTopic = base::take(_creatingBotTopic)) {
			wasCreatingBotTopic->forum()->discardCreatingId(
				wasCreatingBotTopic->rootId());
		}
	}
	if (history) {
		_history = history;
		_migrated = _history ? _history->migrateFrom() : nullptr;
		registerDraftSource();
		if (_history) {
			setupPreview();
			trackThreadFieldVisibility();
		} else {
			_previewDrawPreview = nullptr;
			_preview = nullptr;
		}
	}
	refreshAttachBotsMenu();
}

void HistoryWidget::setupPreview() {
	Expects(_history != nullptr);

	using namespace HistoryView::Controls;
	_preview = std::make_unique<WebpageProcessor>(_history, _field);
	_preview->repaintRequests() | rpl::on_next([=] {
		updateField();
	}, _preview->lifetime());

	_preview->parsedValue(
	) | rpl::on_next([=](WebpageParsed value) {
		_previewTitle.setText(
			st::msgNameStyle,
			value.title,
			Ui::NameTextOptions());
		_previewDescription.setText(
			st::defaultTextStyle,
			value.description,
			Ui::DialogTextOptions());
		const auto changed = (!_previewDrawPreview != !value.drawPreview);
		_previewDrawPreview = value.drawPreview;
		if (changed) {
			updateControlsGeometry();
			updateControlsVisibility();
		}
		updateField();
	}, _preview->lifetime());
}

void HistoryWidget::injectSponsoredMessages() const {
	session().sponsoredMessages().inject(
		_history,
		_showAtMsgId,
		_scroll->height() * 2,
		_scroll->width());
}

void HistoryWidget::refreshAttachBotsMenu() {
	_attachBotsMenu = nullptr;
	if (!_history) {
		return;
	}

	const auto &settings = AyuSettings::getInstance();

	_attachBotsMenu = InlineBots::MakeAttachBotsMenu(
		this,
		controller(),
		_history->peer,
		[=] { return prepareSendAction({}); },
		[=] { return sendMenuDetails(); },
		[=](bool compress) { chooseAttach(compress); },
		crl::guard(this, [=] {
			return _field->getTextWithAppliedMarkdown();
		}),
		crl::guard(this, [=] {
			migrateFieldToRichEditor();
		}));
	if (!_attachBotsMenu) {
		return;
	}
	_attachBotsMenu->setOrigin(
		Ui::PanelAnimation::Origin::BottomLeft);
	if (settings.showAttachPopup()) {
		_attachToggle->installEventFilter(_attachBotsMenu.get());
	}
	_attachBotsMenu->heightValue(
	) | rpl::on_next([=] {
		moveFieldControls();
	}, _attachBotsMenu->lifetime());
}

void HistoryWidget::unregisterDraftSources() {
	if (!_history) {
		return;
	}
	session().local().unregisterDraftSource(
		_history,
		Data::DraftKey::Local(MsgId(), PeerId()));
	session().local().unregisterDraftSource(
		_history,
		Data::DraftKey::LocalEdit(MsgId(), PeerId()));
}

void HistoryWidget::registerDraftSource() {
	if (!_history) {
		return;
	}
	const auto peerId = _history->peer->id;
	const auto editMsgId = _editMsgId;
	if (!editMsgId && isComposeBoxOpen()) {
		return;
	}
	const auto draft = [=] {
		return Storage::MessageDraft{
			(editMsgId
				? FullReplyTo{ FullMsgId(peerId, editMsgId) }
				: _replyTo),
			suggestOptions(editMsgId != 0),
			_field->getTextWithTags(),
			_preview->draft(),
		};
	};
	auto draftSource = Storage::MessageDraftSource{
		.draft = draft,
		.cursor = [=] { return MessageCursor(_field); },
	};
	session().local().registerDraftSource(
		_history,
		(editMsgId
			? Data::DraftKey::LocalEdit(MsgId(), PeerId())
			: Data::DraftKey::Local(MsgId(), PeerId())),
		std::move(draftSource));
}

void HistoryWidget::untrackThreadFieldVisibility() {
	_threadFieldVisibleLifetime.destroy();
	_threadFieldVisible = false;
}

void HistoryWidget::trackThreadFieldVisibility() {
	if (!_history) {
		_threadFieldVisible = false;
		return;
	}
	const auto peerId = _history->peer->id;
	Iv::Editor::FieldVisibleValue(
		&session(),
		peerId,
		MsgId(),
		PeerId()
	) | rpl::distinct_until_changed(
	) | rpl::on_next([=](bool visible) {
		_threadFieldVisible = visible;
		if (visible && !_editMsgId) {
			cancelPendingDraftSaves();
		}
		unregisterDraftSources();
		registerDraftSource();
		updateCmdStartShown();
		updateSendButtonType();
		updateControlsVisibility();
		updateControlsGeometry();
	}, _threadFieldVisibleLifetime);
}

void HistoryWidget::setEditMsgId(MsgId msgId) {
	unregisterDraftSources();
	_editMsgId = msgId;
	if (!msgId) {
		_mediaEditManager.cancel();
		_canReplaceMedia = _canAddMedia = false;
		if (_preview) {
			_preview->setDisabled(false);
		}
	}
	if (_history) {
		refreshSendAsToggle();
		orderWidgets();
	}
	registerDraftSource();
}

void HistoryWidget::clearDelayedShowAt() {
	_delayedShowAtMsgId = -1;
	clearDelayedShowAtRequest();
}

void HistoryWidget::clearDelayedShowAtRequest() {
	Expects(_history != nullptr);

	if (_delayedShowAtRequest) {
		_history->owner().histories().cancelRequest(_delayedShowAtRequest);
		_delayedShowAtRequest = 0;
	}
}

void HistoryWidget::clearSupportPreloadRequest() {
	Expects(_history != nullptr);

	if (_supportPreloadRequest) {
		auto &histories = _history->owner().histories();
		histories.cancelRequest(_supportPreloadRequest);
		_supportPreloadRequest = 0;
	}
}

void HistoryWidget::clearAllLoadRequests() {
	Expects(_history != nullptr);

	auto &histories = _history->owner().histories();
	clearDelayedShowAtRequest();
	if (_firstLoadRequest) {
		histories.cancelRequest(_firstLoadRequest);
		_firstLoadRequest = 0;
	}
	if (_preloadRequest) {
		histories.cancelRequest(_preloadRequest);
		_preloadRequest = 0;
	}
	if (_preloadDownRequest) {
		histories.cancelRequest(_preloadDownRequest);
		_preloadDownRequest = 0;
	}
}

bool HistoryWidget::updateReplaceMediaButton() {
	if (!_canReplaceMedia && !_canAddMedia) {
		const auto result = (_replaceMedia != nullptr);
		_replaceMedia.destroy();
		return result;
	} else if (_replaceMedia) {
		return false;
	}
	_replaceMedia.create(
		this,
		_canReplaceMedia ? st::historyReplaceMedia : st::historyAddMedia);
	_replaceMedia->setAccessibleName(_canReplaceMedia
		? tr::lng_attach_replace(tr::now)
		: tr::lng_attach(tr::now));
	const auto hideDuration = st::historyReplaceMedia.ripple.hideDuration;
	_replaceMedia->setClickedCallback([=] {
		base::call_delayed(hideDuration, this, [=] {
			EditCaptionBox::StartMediaReplace(
				controller(),
				{ _history->peer->id, _editMsgId },
				_field->getTextWithTags(),
				suggestOptions(),
				_mediaEditManager.spoilered(),
				_mediaEditManager.invertCaption(),
				crl::guard(_list, [=] { cancelEdit(); }));
		});
	});
	return true;
}

void HistoryWidget::updateFieldSubmitSettings() {
	const auto settings = _isInlineBot
		? Ui::InputField::SubmitSettings::None
		: Core::App().settings().sendSubmitWay();
	_field->setSubmitSettings(settings);
}

void HistoryWidget::updateNotifyControls() {
	if (!_peer || (!_peer->isChannel() && !_peer->isRepliesChat() && !_peer->isVerifyCodes())) {
		return;
	}

	_muteUnmute->setText((_history->muted()
		? tr::lng_channel_unmute(tr::now)
		: tr::lng_channel_mute(tr::now)).toUpper());
	if (!session().data().notifySettings().silentPostsUnknown(_peer)) {
		if (_silent) {
			_silent->setChecked(
				session().data().notifySettings().silentPosts(_peer));
			updateFieldPlaceholder();
		} else if (hasSilentToggle()) {
			refreshSilentToggle();
			updateControlsVisibility();
			updateControlsGeometry();
		}
	}
}

void HistoryWidget::refreshSilentToggle() {
	if (!_silent && hasSilentToggle()) {
		_silent.create(this, _peer->asChannel());
		orderWidgets();
	} else if (_silent && !hasSilentToggle()) {
		_silent.destroy();
	}
}

void HistoryWidget::setupFastButtonMode() {
	const auto field = _field->rawTextEdit();
	base::install_event_filter(field, [=](not_null<QEvent*> e) {
		if (e->type() != QEvent::KeyPress
			|| !_history
			|| !FastButtonsMode()
			|| !session().fastButtonsBots().enabled(_history->peer)
			|| !_field->getLastText().isEmpty()) {
			return base::EventFilterResult::Continue;
		}
		const auto k = static_cast<QKeyEvent*>(e.get());
		const auto key = k->key();
		if (key < Qt::Key_1 || key > Qt::Key_9 || k->modifiers()) {
			return base::EventFilterResult::Continue;
		}
		const auto item = _history ? _history->lastMessage() : nullptr;
		const auto markup = item ? item->inlineReplyKeyboard() : nullptr;
		const auto link = markup
			? markup->getLinkByIndex(key - Qt::Key_1)
			: nullptr;
		if (!link) {
			return base::EventFilterResult::Continue;
		}
		ActivateClickHandler(window(), link, {
			Qt::LeftButton,
			QVariant::fromValue(ClickHandlerContext{
				.itemId = item->fullId(),
				.sessionWindow = base::make_weak(controller()),
			}),
		});
		return base::EventFilterResult::Cancel;
	});
}

void HistoryWidget::setupScheduledToggle() {
	controller()->activeChatValue(
	) | rpl::map([=](Dialogs::Key key) -> rpl::producer<> {
		if (const auto history = key.history()) {
			return session().scheduledMessages().updates(history);
		} else if (const auto topic = key.topic()) {
			return session().scheduledMessages().updates(
				topic->owningHistory());
		}
		return rpl::never<rpl::empty_value>();
	}) | rpl::flatten_latest(
	) | rpl::on_next([=] {
		refreshScheduledToggle();
		updateControlsVisibility();
		updateControlsGeometry();
	}, lifetime());
}

void HistoryWidget::refreshScheduledToggle() {
	const auto has = _history
		&& _canSendMessages
		&& (session().scheduledMessages().count(_history) > 0);
	if (!_scheduled && has) {
		_scheduled.create(this, st::historyScheduledToggle);
		_scheduled->setAccessibleName(tr::lng_scheduled_messages(tr::now));
		_scheduled->show();
		_scheduled->addClickHandler([=] {
			controller()->showSection(
				std::make_shared<HistoryView::ScheduledMemento>(_history));
		});
		orderWidgets(); // Raise drag areas to the top.
	} else if (_scheduled && !has) {
		_scheduled.destroy();
	}
}

void HistoryWidget::refreshSendGiftToggle() {
	using Type = Api::DisallowedGiftType;

	const auto &settings = AyuSettings::getInstance();
	const auto user = _peer ? _peer->asUser() : nullptr;
	const auto disallowed = user ? user->disallowedGiftTypes() : Type();
	const auto all = Type::Premium
		| Type::Unlimited
		| Type::Limited
		| Type::Unique;
	const auto has = user
		&& _canSendMessages
		&& !user->isServiceUser()
		&& !user->isSelf()
		&& !user->isBot()
		&& settings.showGiftButtonInMessageField()
		&& ((disallowed & Type::SendHide)
			|| (session().user()->disallowedGiftTypes() & Type::SendHide)
			|| Data::IsBirthdayToday(user->birthday()))
		&& ((disallowed & all) != all);
	if (!_giftToUser && has) {
		_giftToUser.create(this, st::historyGiftToUser);
		_giftToUser->setAccessibleName(tr::lng_gift_send_title(tr::now));
		_giftToUser->show();
		_giftToUser->addClickHandler([=] {
			Ui::ShowStarGiftBox(controller(), _peer);
		});
		orderWidgets(); // Raise drag areas to the top.
	} else if (_giftToUser && !has) {
		_giftToUser.destroy();
	}
}

void HistoryWidget::applySuggestOptions(
		SuggestOptions suggest,
		HistoryView::SuggestMode mode) {
	Expects(suggest.exists);

	using namespace HistoryView;
	_suggestOptions = std::make_unique<SuggestOptionsBar>(
		controller()->uiShow(),
		_peer,
		suggest,
		mode);
	_suggestOptions->updates() | rpl::on_next([=] {
		updateField();
		saveDraftWithTextNow();
	}, _suggestOptions->lifetime());
	saveDraftWithTextNow();
}

void HistoryWidget::saveDraftWithTextNow() {
	if (bypassNormalDraftHandling()) {
		cancelPendingDraftSaves();
		return;
	}
	_saveDraftText = true;
	_saveDraftStart = crl::now();
	saveDraft();
}

void HistoryWidget::refreshSuggestPostToggle() {
	const auto has = _peer
		&& _peer->isMonoforum()
		&& !_peer->amMonoforumAdmin();
	if (!_toggleSuggestPost && has) {
		_toggleSuggestPost.create(this, st::historySuggestPostToggle);
		_toggleSuggestPost->setVisible(!_suggestOptions);
		_toggleSuggestPost->addClickHandler([=] {
			using namespace HistoryView;
			applySuggestOptions({ .exists = 1 }, SuggestMode::New);
			cancelReply();
			_processingReplyTo = FullReplyTo();
			_processingReplyItem = nullptr;
			updateControlsVisibility();
			updateControlsGeometry();
		});
		orderWidgets();
	} else if (_toggleSuggestPost && !has) {
		_toggleSuggestPost.destroy();
		cancelSuggestPost();
	}
}

void HistoryWidget::setupSendAsToggle() {
	session().sendAsPeers().updated(
	) | rpl::filter([=](Main::SendAsKey key) {
		return (key.peer == _peer)
			&& (key.type == Main::SendAsType::Message);
	}) | rpl::on_next([=] {
		refreshSendAsToggle();
		updateControlsVisibility();
		updateControlsGeometry();
		orderWidgets();
	}, lifetime());
}

void HistoryWidget::refreshSendAsToggle() {
	Expects(_peer != nullptr);

	const auto key = Main::SendAsKey{ _peer, Main::SendAsType::Message };
	if (_editMsgId || !session().sendAsPeers().shouldChoose(key)) {
		_sendAs.destroy();
		return;
	} else if (_sendAs) {
		return;
	}
	const auto &st = st::defaultComposeControls.chooseSendAs;
	_sendAs.create(this, st.button);
	_sendAs->setAccessibleName(tr::lng_send_as_title(tr::now));
	Ui::SetupSendAsButton(_sendAs.data(), st, controller());
}

bool HistoryWidget::contentOverlapped(const QRect &globalRect) {
	return _attachDragAreas.document->overlaps(globalRect)
		|| _attachDragAreas.photo->overlaps(globalRect)
		|| (_autocomplete && _autocomplete->overlaps(globalRect))
		|| (_tabbedPanel && _tabbedPanel->overlaps(globalRect))
		|| (_inlineResults && _inlineResults->overlaps(globalRect));
}

bool HistoryWidget::canWriteMessage() const {
	return _history
		&& _canSendMessages
		&& !isBlocked()
		&& !isJoinChannel()
		&& !isMuteUnmute()
		&& !isBotStart()
		&& !isSearching();
}
