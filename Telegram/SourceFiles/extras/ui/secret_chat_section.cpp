#include "extras/ui/secret_chat_box.h"
#include "extras/features/window_material/window_material.h"
#include "extras/ui/secret_chat_attachments.h"

#include "extras/features/secret_chats/secret_chat_dialog.h"
#include "extras/features/secret_chats/secret_chat_peer.h"
#include "extras/ui/components/floating_surface_host.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/controls/history_view_compose_controls.h"
#include "history/view/controls/history_view_compose_search.h"
#include "history/view/history_view_corner_buttons.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_list_frosted.h"
#include "history/view/history_view_list_widget.h"
#include "history/view/history_view_top_bar_widget.h"
#include "history/history_view_swipe_back_session.h"
#include "core/application.h"
#include "core/shortcuts.h"
#include "base/call_delayed.h"
#include "core/core_screenshot_protection.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "chat_helpers/message_field.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "menu/menu_send.h"
#include "ui/chat/chat_theme.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/ui_utility.h"
#include "ui/screen_reader_mode.h"
#include "apiwrap.h"
#include "ui/effects/message_sending_animation_controller.h"
#include "crl/crl_on_main.h"
#include "ui/widgets/elastic_scroll.h"
#include "window/section_memento.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"
#include "window/window_peer_menu.h"
#include "window/window_adaptive.h"
#include "window/window_controller.h"
#include "window/window_slide_animation.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_window.h"

namespace Extras::SecretChats {
namespace {

class Widget;

class Memento final : public Window::SectionMemento {
public:
	explicit Memento(not_null<Dialog*> dialog) : _dialog(dialog) {}
	object_ptr<Window::SectionWidget> createWidget(QWidget *parent,
		not_null<Window::SessionController*> controller, Window::Column column,
		const QRect &geometry) override;
	[[nodiscard]] Dialog *dialog() const { return _dialog.get(); }
	HistoryView::ListMemento list;
	MsgId requestedMessage;
	Window::SectionShow showParams;
private:
	base::weak_ptr<Dialog> _dialog;
};

[[nodiscard]] ChatHelpers::ComposeFeatures composeFeatures() {
	return {
		.sendAs = false,
		.ttlInfo = false,
		.silentBroadcastToggle = false,
		.attachBotsMenu = false,
		.inlineBots = false,
		.megagroupSet = false,
		.stickersSettings = false,
		.openStickerSets = false,
		.autocompleteHashtags = false,
		.autocompleteMentions = false,
		.autocompleteCommands = false,
		.suggestStickersByEmoji = false,
		.commonTabbedPanel = false,
		.recordMediaMessage = false,
		.emojiOnlyPanel = true,
		.richEditor = false,
	};
}

class Widget final : public Window::SectionWidget
	, private HistoryView::WindowListDelegate
	, private HistoryView::CornerButtonsDelegate {
public:
	Widget(QWidget *parent, not_null<Window::SessionController*> controller,
		not_null<Dialog*> dialog)
	: SectionWidget(parent, controller, dialog->user())
	, WindowListDelegate(controller)
	, _dialog(dialog)
	, _manager(&controller->session().secretChats())
	, _scroll(this, controller->chatStyle()->value(lifetime(), st::historyScroll))
	, _topBar(this, controller)
	, _compose(std::make_unique<HistoryView::ComposeControls>(this,
		HistoryView::ComposeControlsDescriptor{
			.show = controller->uiShow(),
			.sendMenuDetails = [] { return SendMenu::Details(); },
			.regularWindow = controller,
			.features = composeFeatures(),
		}))
	, _corners(_scroll.data(), controller->chatStyle(),
		static_cast<HistoryView::CornerButtonsDelegate*>(this))
	, _draftTimer([=] { saveDraft(); }) {
		setObjectName(u"secret/chat/"_q + QString::number(dialog->chatId()));
		Window::ChatThemeValueFromPeer(controller, dialog->user())
			| rpl::on_next([=](std::shared_ptr<Ui::ChatTheme> theme) {
				_theme = std::move(theme);
				controller->setChatStyleTheme(_theme);
				update();
			}, lifetime());
		const auto state = Dialogs::EntryState{ .key = static_cast<Data::Thread*>(dialog.get()), .section = Dialogs::EntryState::Section::History };
		_topBar->setActiveChat(state, nullptr);
		_compose->setCurrentDialogsEntryState(state);
		controller->setDialogsEntryState(state);
		_topBar->show();
		_scroll->setHandleTouch(false);
		_inner = _scroll->setOwnedWidget(object_ptr<HistoryView::ListWidget>(
			this, &controller->session(), static_cast<HistoryView::ListDelegate*>(this)));
		_inner->setObjectName(u"secret/messages"_q);
		_scroll->setObjectName(u"secret/scroll"_q);
		_scroll->setOverscrollBg(QColor(0, 0, 0, 0));
		_scroll->setOverscrollEdges([=] { return _inner->loadedAtTop(); }, [=] { return _inner->loadedAtBottom(); });
		_scroll->scrolls() | rpl::on_next([=] { updateVisible(); }, lifetime());
		_frosted = HistoryView::SetupListFrostedBackground({
			.root = this,
			.scroll = _scroll.data(),
			.list = _inner.data(),
			.scrolls = _scroll->scrolls(),
			.paletteChanged = controller->chatStyle()->paletteChanged(),
			.paintBackground = HistoryView::SectionFrostedBackground(this, [=] { return _theme.get(); }),
		});
		_compose->setHistory({
			.history = dialog->owningHistory().get(),
			.sendActionFactory = [=] { return Api::SendAction(dialog->owningHistory()); },
			.canSendTexts = Data::CanSendAnyOfValue(dialog->owningHistory()->peer, ChatRestriction::SendOther),
		});
		const auto draft = manager().draft(dialog->chatId());
		_lastDraft = draft;
		_compose->setText({ draft.text });
		if (draft.replyTo && dialog->localId(draft.replyTo)) {
			_compose->replyToMessage({ .messageId = FullMsgId(dialog->owningHistory()->peer->id, dialog->localId(draft.replyTo)) });
		}
		_compose->field()->setObjectName(u"secret/compose"_q);
		_compose->field()->changes() | rpl::on_next([=] { _draftTimer.callOnce(500); }, lifetime());
		_compose->height() | rpl::on_next([=] { updateGeometry(); }, lifetime());
		_compose->sendRequests() | rpl::on_next([=] { send(); }, lifetime());
		_compose->scrollToMaxRequests() | rpl::on_next([=] {
			_inner->showAtPosition(Data::MaxMessagePosition, {});
		}, lifetime());
		_compose->editLastMessageRequests() | rpl::on_next([=](not_null<QKeyEvent*> event) {
			_scroll->keyPressEvent(event);
		}, lifetime());
		_compose->attachRequests() | rpl::filter([=] {
			return !_choosingAttach;
		}) | rpl::on_next([=] {
			_choosingAttach = true;
			base::call_delayed(st::historyAttach.ripple.hideDuration, this, [=] {
				_choosingAttach = false;
				chooseAttachment(controller, this, dialog->chatId(), _compose.get());
			});
		}, lifetime());
		_compose->setPasteToastParent(_scroll.data());
		_compose->setMimeDataHook([=](not_null<const QMimeData*> data, Ui::InputField::MimeAction action) {
			if (action == Ui::InputField::MimeAction::Check) {
				return Core::CanSendFiles(data);
			}
			return insertAttachments(controller, this, dialog->chatId(), _compose.get(), data);
		});
		_compose->viewportEvents() | rpl::on_next([=](not_null<QEvent*> event) { _scroll->viewportEvent(event); }, lifetime());
		_compose->replyingToMessageValue() | rpl::on_next([=] { _draftTimer.callOnce(500); }, lifetime());
		_compose->replyNextRequests() | rpl::on_next([=](HistoryView::ComposeControls::ReplyNextRequest request) {
			_inner->replyNextMessage(request.replyId,
				request.direction == HistoryView::ComposeControls::ReplyNextRequest::Direction::Next);
		}, lifetime());
		_compose->cancelRequests() | rpl::on_next([=] { listCancelRequest(); }, lifetime());
		rpl::merge(_compose->scrollKeyEvents(), _inner->scrollKeyEvents())
			| rpl::on_next([=](not_null<QKeyEvent*> event) {
				_scroll->keyPressEvent(event);
			}, lifetime());
		_compose->jumpToItemRequests() | rpl::on_next([=](FullReplyTo to) { showMessage(to.messageId.peer, {}, to.messageId.msg); }, lifetime());
		_inner->replyToMessageRequested() | rpl::on_next([=](HistoryView::ListWidget::ReplyToMessageRequest request) {
			_compose->replyToMessage(request.to);
			_compose->focus();
			if (_search) { _search->hideAnimated(); }
			_draftTimer.callOnce(500);
		}, lifetime());
		_inner->showMessageRequested() | rpl::on_next([=](FullMsgId id) { showMessage(id.peer, {}, id.msg); }, lifetime());
		_inner->setInsertTextCallback([=](QString text) { _compose->insertTextToField(text); });
		_topBar->searchRequest() | rpl::on_next([=] { showSearch({}); }, lifetime());
		_topBar->deleteSelectionRequest() | rpl::on_next([=] { listDeleteRequest(); }, lifetime());
		_topBar->clearSelectionRequest() | rpl::on_next([=] { _inner->cancelSelection(); }, lifetime());
		_topBar->selectBetweenRequest() | rpl::on_next([=] { _inner->selectItemsBetween(); }, lifetime());
		manager().draftCleared() | rpl::filter([=](int chatId) {
			return chatId == _dialog->chatId();
		}) | rpl::on_next([=] {
			_lastDraft = {};
			_compose->clear();
			_draftTimer.cancel();
		}, lifetime());
		manager().changed() | rpl::on_next([=] {
			if (!_dialog->shouldBeInChatList()) {
				controller->showBackFromStack();
				return;
			}
			const auto draft = manager().draft(_dialog->chatId());
			if (draft.text != _lastDraft.text || draft.replyTo != _lastDraft.replyTo) {
				const auto edited = _compose->fieldLastText() != _lastDraft.text
					|| _dialog->randomId(_compose->replyingToMessage().messageId.msg) != _lastDraft.replyTo;
				_lastDraft = draft;
				if (edited) {
					_draftTimer.callOnce(500);
				} else {
					_compose->setText({ draft.text });
					_compose->cancelReplyMessage();
					if (draft.replyTo && _dialog->localId(draft.replyTo)) {
						_compose->replyToMessage({ .messageId = FullMsgId(_dialog->owningHistory()->peer->id, _dialog->localId(draft.replyTo)) });
					}
				}
			}
			const auto reply = _compose->replyingToMessage();
			if (reply.messageId.msg && !_dialog->randomId(reply.messageId.msg)) {
				_compose->cancelReplyMessage();
			}
			checkActivation();
		}, lifetime());
		_scroll->show();
		Window::SetupSwipeBackSection(this, _scroll, _inner);
		Shortcuts::Requests() | rpl::filter([=] {
			return Ui::AppInFocus()
				&& Ui::InFocusChain(this)
				&& !controller->isLayerShown()
				&& (Core::App().activeWindow() == &controller->window());
		}) | rpl::on_next([=](not_null<Shortcuts::Request*> request) {
			request->check(Shortcuts::Command::Search, 1) && request->handle([=] {
				showSearch({});
				return true;
			});
			request->check(Shortcuts::Command::ShowChatMenu, 1) && request->handle([=] {
				Window::ActivateWindow(controller);
				_topBar->showPeerMenu();
				return true;
			});
		}, lifetime());
		Core::App().screenshotProtection().addContentReason(
			controller->widget(), _screenshotProtected.value(), lifetime(), this);
		controller->widget()->ui_layerShownValue(
		) | rpl::on_next([=](bool shown) {
			_layerShown = shown;
			updateScreenshotProtection();
		}, lifetime());
		rpl::merge(
			shownValue() | rpl::to_empty,
			events() | rpl::filter([](not_null<QEvent*> event) {
				return event->type() == QEvent::Show
					|| event->type() == QEvent::Hide;
			}) | rpl::to_empty
		) | rpl::on_next([=] { updateScreenshotProtection(); }, lifetime());
	}

	~Widget() { saveDraft(); controller()->sendingAnimation().clear(); }
	Dialogs::RowDescriptor activeChat() const override { return { static_cast<Data::Thread*>(_dialog.get()), {} }; }
	bool showInternal(not_null<Window::SectionMemento*> memento, const Window::SectionShow &) override {
		const auto saved = dynamic_cast<Memento*>(memento.get());
		if (!saved || saved->dialog() != _dialog) { return false; }
		_inner->restoreState(&saved->list);
		if (saved->requestedMessage) {
			showMessage(_dialog->peer()->id, saved->showParams, saved->requestedMessage);
		}
		return true;
	}
	std::shared_ptr<Window::SectionMemento> createMemento() override {
		auto result = std::make_shared<Memento>(_dialog);
		_inner->saveState(&result->list);
		return result;
	}
	bool showMessage(PeerId peer, const Window::SectionShow &params, MsgId id) override {
		if (peer != _dialog->owningHistory()->peer->id) { return false; }
		const auto item = session().data().message(peer, id);
		if (!item) { return false; }
		_inner->showAtPosition(item->position(), params);
		return true;
	}
	bool searchInChatEmbedded(QString query, Dialogs::Key chat, PeerData *) override {
		if (chat && chat != Dialogs::Key(static_cast<Data::Thread*>(_dialog.get()))) { return false; }
		showSearch(query);
		return true;
	}
	void checkActivation() override { if (_inner) { _inner->checkActivation(); } }
	void showAnimated(Window::SlideDirection,
			const Window::SectionSlideParams &params) override {
		// 正文实时显示，旧画面淡出，不为加密内容创建动画截图。
		showFast();
		Window::ShowFadeOutAnimation(this, rect(), params.oldContentCache,
			Window::InPlaceCrossFade(params.crossFadeDuration).duration
		)->setObjectName(u"secret/chat-transition"_q);
	}
	QPixmap grabForShowAnimation(const Window::SectionSlideParams &) override {
		// 原生切页动画只保存背景，加密正文不进入截图缓存。
		auto result = QPixmap(size() * style::DevicePixelRatio());
		result.setDevicePixelRatio(style::DevicePixelRatio());
		result.fill(ExtrasFeatures::WindowMaterial::isActive(this)
			? ExtrasFeatures::WindowMaterial::rootTintColor(this)
			: QColor(Qt::transparent));
		render(&result, QPoint(), QRegion(), QWidget::DrawWindowBackground);
		return result;
	}
	bool floatPlayerHandleWheelEvent(QEvent *event) override { return _scroll->viewportEvent(event); }
	QRect floatPlayerAvailableRect() override { return mapToGlobal(_scroll->geometry()); }

protected:
	void resizeEvent(QResizeEvent *) override {
		controller()->adaptive().setChatLayout(width() < st::adaptiveChatWideWidth
			? Window::Adaptive::ChatLayout::Normal : Window::Adaptive::ChatLayout::Wide);
		updateGeometry();
	}
	void paintEvent(QPaintEvent *event) override {
		if (animatingShow()) { SectionWidget::paintEvent(event); return; }
		if (_theme) { SectionWidget::PaintBackground(controller(), _theme.get(), this, event->rect()); }
	}
	void doSetInnerFocus() override {
		const auto selected = !_inner->getSelectedText().rich.text.isEmpty()
			|| !_inner->getSelectedItems().empty();
		if (_search && !selected) {
			_search->setInnerFocus();
		} else if (selected || !_compose->focus()) {
			_inner->setFocus();
		}
	}
	void showAnimatedHook(const Window::SectionSlideParams &) override {
		_topBar->setAnimatingMode(true);
		_compose->showStarted();
	}
	void showFinishedHook() override {
		_topBar->setAnimatingMode(false);
		_compose->showFinished();
		if (_search) { _search->raise(); }
		_inner->showFinished();
	}

private:
	void updateScreenshotProtection() {
		if (_changingVisibility) {
			return;
		}
		if (_layerShown) {
			hideForLayer();
			_screenshotProtected = false;
			return;
		}
		if (_restoreAfterLayer) {
			_restoreAfterLayer = false;
			_changingVisibility = true;
			// 先恢复保护，再重新显示正文；父级隐藏时等待真实显示事件。
			_screenshotProtected = parentWidget()->isVisible();
			show();
			_changingVisibility = false;
		}
		_screenshotProtected = isVisible();
	}
	void hideForLayer() {
		if (isHidden()) {
			return;
		}
		_restoreAfterLayer = true;
		_changingVisibility = true;
		if (animatingShow()) {
			showFast();
		}
		hide();
		controller()->widget()->repaint();
		_changingVisibility = false;
	}
	Manager &manager() const { return *_manager; }
	void showSearch(const QString &query) {
		if (_search) {
			_search->setQuery(query);
			_search->setInnerFocus();
			return;
		}
		_search = std::make_unique<HistoryView::ComposeSearch>(this, controller(),
			_dialog->owningHistory(), nullptr, query, HistoryView::ComposeSearch::LocalSource{
				.search = [=](const Api::MessagesSearch::Request &request) {
					auto result = Api::FoundMessages();
					const auto slice = _dialog->slice(request.query.trimmed());
					for (auto i = slice.ids.rbegin(); i != slice.ids.rend(); ++i) {
						const auto item = session().data().message(*i);
						if (item && Api::MessagesSearch::matchesLocalFilter(item, request.filter)
							&& (!request.from || item->from() == request.from)) {
							result.messages.push_back(*i);
						}
					}
					return result;
				},
				.changes = _dialog->historyChanged(),
			});
		_search->setCalendarChat(static_cast<Data::Thread*>(_dialog.get()));
		_search->setCalendarJumpHandler([=](FullMsgId id, Fn<void()> close) {
			close();
			showMessage(id.peer, {}, id.msg);
		});
		_compose->hide();
		_search->heightChanges() | rpl::on_next([=] { updateGeometry(); }, _search->lifetime());
		_search->activations() | rpl::on_next([=](HistoryView::ComposeSearch::Activation activation) {
			auto params = Window::SectionShow(Window::SectionShow::Way::Forward, anim::type::instant);
			params.highlight = Window::SearchHighlightId(activation.query);
			_inner->showAtPosition(activation.item->position(), params);
		}, _search->lifetime());
		_search->destroyRequests() | rpl::take(1) | rpl::on_next([=] {
			_search = nullptr;
			_compose->show();
			updateGeometry();
			doSetInnerFocus();
		}, _search->lifetime());
		updateGeometry();
		doSetInnerFocus();
	}
	int composeHeight() const {
		return _search ? _search->height() + 2 * st::historyComposeCapsuleMargin : _compose->heightCurrent();
	}
	void updateGeometry() {
		if (!_inner || !width() || !height()) { return; }
		const auto atBottom = _scroll->scrollTop() >= _scroll->scrollTopMax();
		_compose->resizeToWidth(width());
		_topBar->resizeToWidth(width());
		_overlap = composeHeight() - st::historyComposeCapsuleMargin / 2;
		_scroll->setGeometry(0, _topBar->height(), width(), std::max(0, height() - _topBar->height()));
		(void)_inner->setBottomPadding(_overlap);
		_inner->resizeToWidth(width(), _scroll->height());
		_scroll->setBarBottomInset(_overlap);
		_compose->move(0, height() - _compose->heightCurrent());
		_compose->setAutocompleteBoundingRect(_scroll->geometry().marginsRemoved({ 0, 0, 0, _overlap }));
		_corners.setBottomSkip(_overlap);
		_corners.updatePositions();
		if (atBottom) { _scroll->scrollToY(_scroll->scrollTopMax()); }
		updateVisible();
	}
	void updateVisible() {
		if (_inner) {
			_inner->setVisibleTopBottom(_scroll->scrollTop(), _scroll->scrollTop() + _scroll->height() - _overlap);
			_corners.updateJumpDownVisibility();
		}
	}
	void saveDraft() {
		if (!_manager || !manager().available() || !_dialog->shouldBeInChatList()) { return; }
		const auto reply = _compose->replyingToMessage();
		const auto draft = Draft{ _compose->fieldLastText(), _dialog->randomId(reply.messageId.msg) };
		const auto current = manager().draft(_dialog->chatId());
		if (draft.text != current.text || draft.replyTo != current.replyTo) {
			manager().setDraft(_dialog->chatId(), draft, crl::guard(this, [=](QString error) {
				if (error.isEmpty()) { _lastDraft = manager().draft(_dialog->chatId()); }
				else { controller()->showToast(tr::extras_SecretChatFailed(tr::now)); }
			}));
		}
	}
	void send() {
		if (_sending) { return; }
		const auto text = _compose->getTextWithAppliedMarkdown().text;
		if (text.trimmed().isEmpty()) { return; }
		const auto reply = _dialog->randomId(_compose->replyingToMessage().messageId.msg);
		const auto field = _compose->field();
		const auto animate = field->document()->size().height() <= field->height();
		const auto start = field->mapToGlobal(Rect(field->size()).translated(
			0, MessageFieldCenteringMargins(field).top()));
		const auto original = _compose->fieldLastText();
		_sending = true;
		manager().sendReply(_dialog->chatId(), text, reply, crl::guard(this, [=](qint64 randomId, QString error) {
			_sending = false;
			if (!error.isEmpty()) { controller()->showToast(tr::extras_SecretChatFailed(tr::now)); return; }
			if (animate) {
				controller()->sendingAnimation().appendSending({
					.type = Ui::MessageSendingAnimationFrom::Type::Text,
					.localId = _dialog->ensureLocalId(randomId),
					.globalStartGeometry = start,
				});
			}
			const auto currentReply = _dialog->randomId(_compose->replyingToMessage().messageId.msg);
			if (_compose->fieldLastText() == original && currentReply == reply) {
				_compose->clear();
			}
			_inner->showAtPosition(Data::MaxMessagePosition, {});
		}));
	}
	HistoryView::Context listContext() override { return HistoryView::Context::History; }
	bool listScrollTo(int top, bool = true) override {
		_scroll->scrollToY(std::clamp(top, 0, _scroll->scrollTopMax()));
		updateVisible();
		return true;
	}
	void listCancelRequest() override {
		if (!_inner->getSelectedItems().empty()) { _inner->cancelSelection(); return; }
		if (_search) { _search->hideAnimated(); return; }
		if (_compose->handleCancelRequest()) { return; }
		controller()->showBackFromStack();
	}
	void listDeleteRequest() override { HistoryView::ConfirmDeleteSelectedItems(_inner); }
	void listTryProcessKeyInput(not_null<QKeyEvent*> event) override { _compose->tryProcessKeyInput(event); }
	rpl::producer<Data::MessagesSlice> listSource(Data::MessagePosition, int, int) override {
		return rpl::single(rpl::empty) | rpl::then(_dialog->historyChanged())
			| rpl::map([=] { return _dialog->slice(); });
	}
	bool listAllowsMultiSelect() override { return true; }
	bool listIsItemGoodForSelection(not_null<HistoryItem*> item) override { return item->canBeSelected(); }
	bool listIsLessInOrder(not_null<HistoryItem*> a, not_null<HistoryItem*> b) override {
		return a->date() != b->date() ? a->date() < b->date() : a->id < b->id;
	}
	void listSelectionChanged(HistoryView::SelectedItems &&items) override {
		auto selected = HistoryView::TopBarWidget::SelectedState{ .count = int(items.size()) };
		selected.canSelectBetween = _inner->canSelectItemsBetween();
		for (const auto &item : items) { selected.canDeleteCount += item.canDelete; }
		_topBar->showSelected(selected);
		if (!items.empty() && _search) { _search->hideAnimated(); }
		if (!_inner->hasFocus() || !Ui::ScreenReaderModeActive()) {
			doSetInnerFocus();
		}
	}
	void listMarkReadTill(not_null<HistoryItem*> item) override {
		const auto chat = manager().chat(_dialog->chatId());
		if (chat && chat->state == State::Active && !item->out() && item->unread(_dialog)) { manager().read(_dialog->chatId(), item->date()); }
	}
	void listMarkContentsRead(const base::flat_set<not_null<HistoryItem*>> &items) override { session().api().markContentsRead(items); }
	HistoryView::MessagesBarData listMessagesBar(const std::vector<not_null<HistoryView::Element*>> &elements, bool markLastAsRead) override {
		auto skippedRead = false;
		for (const auto &view : elements) {
			const auto item = view->data();
			if (item->out()) { continue; }
			if (!item->unread(_dialog)) { skippedRead = true; continue; }
			if (markLastAsRead) { listMarkReadTill(item); continue; }
			if (skippedRead) { return { .bar = { .element = view, .focus = true }, .text = tr::lng_unread_bar_some() }; }
		}
		return {};
	}
	void listContentRefreshed() override { updateVisible(); }
	void listUpdateDateLink(ClickHandlerPtr &link, not_null<HistoryView::Element*> view) override {
		const auto date = view->dateTime().date();
		if (!link) {
			link = std::make_shared<Window::DateClickHandler>(
				Dialogs::Key(static_cast<Data::Thread*>(_dialog.get())), date);
		} else {
			static_cast<Window::DateClickHandler*>(link.get())->setDate(date);
		}
	}
	bool listElementHideReply(not_null<const HistoryView::Element*>) override { return false; }
	bool listElementShownUnread(not_null<const HistoryView::Element*> view) override { return !view->data()->out() && view->data()->unread(_dialog); }
	bool listIsGoodForAroundPosition(not_null<const HistoryView::Element*>) override { return true; }
	void listSendBotCommand(const QString &, const FullMsgId &) override {}
	void listSearch(const QString &query, const FullMsgId &) override { showSearch(query); }
	void listHandleViaClick(not_null<UserData*>) override {}
	not_null<Ui::ChatTheme*> listChatTheme() override { return _theme.get(); }
	HistoryView::CopyRestrictionType listCopyRestrictionType(HistoryItem *item) override {
		return item && item->forbidsSaving() ? HistoryView::CopyRestrictionType::User : HistoryView::CopyRestrictionType::None;
	}
	HistoryView::CopyRestrictionType listCopyMediaRestrictionType(not_null<HistoryItem*> item) override { return listCopyRestrictionType(item); }
	HistoryView::CopyRestrictionType listSelectRestrictionType() override { return HistoryView::CopyRestrictionType::None; }
	rpl::producer<Data::AllowedReactions> listAllowedReactionsValue() override { return rpl::single(Data::AllowedReactions()); }
	void listShowPremiumToast(not_null<DocumentData*>) override {}
	void listOpenPhoto(not_null<PhotoData*> photo, FullMsgId context) override { controller()->openPhoto(photo, { .id = context }); }
	void listOpenDocument(not_null<DocumentData*> document, FullMsgId context, bool mediaView) override { controller()->openDocument(document, mediaView, { .id = context }); }
	void listPaintEmpty(Painter &, const Ui::ChatPaintContext &) override {}
	QString listElementAuthorRank(not_null<const HistoryView::Element*>) override { return {}; }
	bool listElementHideTopicButton(not_null<const HistoryView::Element*>) override { return true; }
	History *listTranslateHistory() override { return nullptr; }
	void listAddTranslatedItems(not_null<HistoryView::TranslateTracker*>) override {}
	bool listAllowsDragForward() override { return false; }
	Ui::ElasticScroll *listScrollArea() const override { return _scroll.data(); }
	void cornerButtonsShowAtPosition(Data::MessagePosition position) override { _inner->showAtPosition(position, {}); }
	Data::Thread *cornerButtonsThread() override { return _dialog; }
	FullMsgId cornerButtonsCurrentId() override { return {}; }
	bool cornerButtonsIgnoreVisibility() override { return animatingShow(); }
	std::optional<bool> cornerButtonsDownShown() override { return std::nullopt; }
	bool cornerButtonsUnreadMayBeShown() override { return false; }
	bool cornerButtonsHas(HistoryView::CornerButtonType type) override { return type == HistoryView::CornerButtonType::Down; }

	const not_null<Dialog*> _dialog;
	const base::weak_ptr<Manager> _manager;
	std::shared_ptr<Ui::ChatTheme> _theme;
	object_ptr<Ui::ElasticScroll> _scroll;
	object_ptr<HistoryView::TopBarWidget> _topBar;
	QPointer<HistoryView::ListWidget> _inner;
	std::unique_ptr<HistoryView::ComposeControls> _compose;
	std::unique_ptr<HistoryView::ComposeSearch> _search;
	HistoryView::CornerButtons _corners;
	std::unique_ptr<ExtrasUi::FloatingSurfaceHost> _frosted;
	Draft _lastDraft;
	base::Timer _draftTimer;
	int _overlap = 0;
	bool _sending = false;
	bool _choosingAttach = false;
	rpl::variable<bool> _screenshotProtected = false;
	bool _layerShown = false;
	bool _restoreAfterLayer = false;
	bool _changingVisibility = false;
};

object_ptr<Window::SectionWidget> Memento::createWidget(QWidget *parent,
		not_null<Window::SessionController*> controller, Window::Column column,
		const QRect &geometry) {
	if (!_dialog || column == Window::Column::Third) { return nullptr; }
	auto result = object_ptr<Widget>(parent, controller, _dialog.get());
	result->setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(result);
	result->showInternal(this, {});
	return result;
}

} // namespace

void showChat(not_null<Window::SessionController*> controller, int chatId) {
	showChat(controller, chatId, Window::SectionShow::Way::ClearStack, 0);
}

void showChat(not_null<Window::SessionController*> controller, int chatId,
		const Window::SectionShow &params, MsgId messageId) {
	const auto dialog = controller->session().secretChats().dialog(chatId);
	if (!dialog) { return; }
	auto memento = std::make_shared<Memento>(dialog);
	memento->showParams = params;
	if (messageId == ShowAtTheEndMsgId) {
		memento->list.setAroundPosition(Data::MaxMessagePosition);
	} else if (IsClientMsgId(messageId) || IsServerMsgId(messageId)) {
		if (const auto item = dialog->owner().message(dialog->peer()->id, messageId)) {
			memento->requestedMessage = messageId;
			memento->list.setAroundPosition(item->position());
		}
	}
	controller->showSection(std::move(memento), params);
}

} // namespace Extras::SecretChats
