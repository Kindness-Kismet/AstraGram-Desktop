/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_compose_search.h"

#include "api/api_messages_search_merged.h"
#include "base/event_filter.h"
#include "boxes/peer_list_box.h"
#include "chat_helpers/message_field.h"
#include "core/click_handler_types.h"
#include "data/data_message_reactions.h"
#include "data/data_saved_messages.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_key.h"
#include "dialogs/dialogs_search_from_controllers.h" // SearchFromBox
#include "dialogs/dialogs_search_tags.h"
#include "dialogs/ui/chat_search_in.h"
#include "dialogs/ui/dialogs_layout.h"
#include "history/view/history_view_context_menu.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/chat/floating_bar.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_dialogs.h"
#include "styles/style_info.h"
#include "styles/style_menu_icons.h"
#include "styles/style_window.h"

namespace HistoryView {
namespace {

using Activation = ComposeSearch::Activation;
using SearchRequest = Api::MessagesSearchMerged::Request;

[[nodiscard]] bool HasChooseFrom(not_null<History*> history) {
	return history->peer->isChat() || history->peer->isMegagroup();
}

class Row final : public PeerListRow {
public:
	explicit Row(std::unique_ptr<Dialogs::FakeRow> fakeRow);

	[[nodiscard]] FullMsgId fullId() const;
	[[nodiscard]] not_null<Dialogs::FakeRow*> fakeRow() const;

private:
	const std::unique_ptr<Dialogs::FakeRow> _fakeRow;

};

Row::Row(std::unique_ptr<Dialogs::FakeRow> fakeRow)
: PeerListRow(
	fakeRow->searchInChat().history()->peer,
	fakeRow->item()->fullId().msg.bare)
, _fakeRow(std::move(fakeRow)) {
}

FullMsgId Row::fullId() const {
	return _fakeRow->item()->fullId();
}

not_null<Dialogs::FakeRow*> Row::fakeRow() const {
	return _fakeRow.get();
}

class ListController final : public PeerListController {
public:
	explicit ListController(not_null<History*> history);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

	int customRowHeight() override;
	void customRowPaint(
		Painter &p,
		crl::time now,
		not_null<PeerListRow*> row,
		bool selected) override;
	bool customRowSelectionPoint(
		not_null<PeerListRow*> row,
		int x,
		int y) override;
	void customRowAddRipple(
		not_null<PeerListRow*> row,
		QPoint point,
		Fn<void()> updateCallback) override;

	void loadMoreRows() override;

	void setContent(PeerListContent *content);
	void addItems(const MessageIdsList &ids, bool clear);
	void setQuery(const QString &query);

	[[nodiscard]] rpl::producer<FullMsgId> showItemRequests() const;
	[[nodiscard]] rpl::producer<> searchMoreRequests() const;
	[[nodiscard]] rpl::producer<> resetScrollRequests() const;

private:
	const not_null<History*> _history;
	PeerListContent *_content = nullptr;
	rpl::event_stream<FullMsgId> _showItemRequests;
	rpl::event_stream<> _searchMoreRequests;
	rpl::event_stream<> _resetScrollRequests;

	QString _query;

};

ListController::ListController(not_null<History*> history)
: _history(history) {
}

Main::Session &ListController::session() const {
	return _history->session();
}

void ListController::prepare() {
}

void ListController::rowClicked(not_null<PeerListRow*> row) {
	_showItemRequests.fire_copy(static_cast<Row*>(row.get())->fullId());
}

int ListController::customRowHeight() {
	return st::dialogsRowHeight;
}

// 底色由结果卡片的表面绘制，行内只画悬停高亮与内容。
void ListController::customRowPaint(
		Painter &p,
		crl::time now,
		not_null<PeerListRow*> row,
		bool selected) {
	const auto outerWidth = _content->width();
	const auto fakeRow = static_cast<Row*>(row.get())->fakeRow();
	Dialogs::Ui::RowPainter::Paint(p, fakeRow, {
		.st = &st::defaultDialogRow,
		.currentBg = QBrush(Qt::transparent),
		.now = now,
		.searchLowerText = QStringView(_query),
		.width = outerWidth,
		.selected = selected,
		.paused = p.inactive(),
		.search = true,
	});
}

bool ListController::customRowSelectionPoint(
		not_null<PeerListRow*> row,
		int x,
		int y) {
	return true;
}

// 搜索结果沿用会话行的无水波纹样式。
void ListController::customRowAddRipple(
		not_null<PeerListRow*>,
		QPoint,
		Fn<void()>) {
}

void ListController::loadMoreRows() {
	_searchMoreRequests.fire({});
}

void ListController::setContent(PeerListContent *content) {
	_content = content;
}

rpl::producer<FullMsgId> ListController::showItemRequests() const {
	return _showItemRequests.events();
}

rpl::producer<> ListController::searchMoreRequests() const {
	return _searchMoreRequests.events();
}

rpl::producer<> ListController::resetScrollRequests() const {
	return _resetScrollRequests.events();
}

void ListController::addItems(const MessageIdsList &ids, bool clear) {
	if (clear) {
		_resetScrollRequests.fire({});
		for (auto i = 0; i != delegate()->peerListFullRowsCount();) {
			delegate()->peerListRemoveRow(delegate()->peerListRowAt(i));
		}
	}

	const auto &owner = _history->owner();
	const auto key = Dialogs::Key{ _history };
	for (const auto &id : ids) {
		if (const auto item = owner.message(id)) {
			const auto shared = std::make_shared<Row*>(nullptr);
			auto row = std::make_unique<Row>(
				std::make_unique<Dialogs::FakeRow>(
					key,
					item,
					[=] { delegate()->peerListUpdateRow(*shared); }));
			*shared = row.get();
			delegate()->peerListAppendRow(std::move(row));
		}
	}

	delegate()->peerListRefreshRows();
}

void ListController::setQuery(const QString &query) {
	_query = query;
}

struct List {
	base::unique_qptr<Ui::RpWidget> container;
	std::unique_ptr<ListController> controller;
	PeerListContent *content = nullptr;
};

// 结果列表是浮在查找栏上方的卡片，表面与查找栏一致。
List CreateList(
		not_null<Ui::RpWidget*> parent,
		not_null<History*> history,
		rpl::producer<not_null<QKeyEvent*>> scrollKeys) {
	auto list = List{
		base::make_unique_q<Ui::RpWidget>(parent),
		std::make_unique<ListController>(history),
	};
	const auto container = list.container.get();
	container->setObjectName(u"chatBar.search.results"_q);
	container->hide();
	Ui::ApplyChatControlSurface(container, st::windowCardRadius);
	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(container);

	using Delegate = PeerListContentDelegateSimple;
	const auto delegate = scroll->lifetime().make_state<Delegate>();
	list.controller->setStyleOverrides(&st::searchInChatCardList);

	const auto content = scroll->setOwnedWidget(
		object_ptr<PeerListContent>(scroll, list.controller.get()));
	list.content = content;

	list.controller->resetScrollRequests(
	) | rpl::on_next([=] {
		scroll->scrollToY(0);
	}, scroll->lifetime());

	const auto updateVisible = [=] {
		const auto top = scroll->scrollTop();
		content->setVisibleTopBottom(top, top + scroll->height());
	};
	scroll->scrolls(
	) | rpl::on_next(updateVisible, scroll->lifetime());

	delegate->setContent(content);
	list.controller->setDelegate(delegate);
	list.controller->setContent(content);
	content->setMode(PeerListContent::Mode::Custom);

	container->sizeValue(
	) | rpl::on_next([=](const QSize &size) {
		content->resize(size.width(), content->height());
		scroll->resize(size);
		updateVisible();
	}, container->lifetime());

	std::move(
		scrollKeys
	) | rpl::on_next([=](not_null<QKeyEvent*> e) {
		const auto delta = scroll->height();
		const auto now = scroll->scrollTop();
		scroll->scrollToY((e->key() == Qt::Key_PageUp)
			? (now - delta)
			: (now + delta));
	}, scroll->lifetime());

	return list;
}

// 可移除的筛选标签：头像或类型图标、名称、移除标记，整块点击即移除。
class FilterChip final : public Ui::AbstractButton {
public:
	FilterChip(
		QWidget *parent,
		const QString &text,
		PaintRoundImageCallback paintImage);

	[[nodiscard]] int minimalWidth() const;
	[[nodiscard]] int naturalWidth() const;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const QString _text;
	const PaintRoundImageCallback _paintImage;

};

FilterChip::FilterChip(
	QWidget *parent,
	const QString &text,
	PaintRoundImageCallback paintImage)
: AbstractButton(parent)
, _text(text)
, _paintImage(std::move(paintImage)) {
	setCursor(style::cur_pointer);
	setAccessibleName(text);
	resize(naturalWidth(), st::historyComposeSearchChipHeight);
}

int FilterChip::minimalWidth() const {
	const auto &padding = st::historyComposeSearchChipPadding;
	return padding.left()
		+ st::historyComposeSearchChipImage
		+ st::historyComposeSearchChipSkip
		+ st::historyComposeSearchChipRemove.width()
		+ padding.right();
}

int FilterChip::naturalWidth() const {
	const auto text = st::historyComposeSearchChipFont->width(_text);
	return std::min(
		minimalWidth() + text + st::historyComposeSearchChipSkip,
		st::historyComposeSearchChipMaxWidth);
}

void FilterChip::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	{
		auto hq = PainterHighQualityEnabler(p);
		const auto radius = height() / 2.;
		p.setPen(Qt::NoPen);
		p.setBrush(st::lightButtonBgOver);
		p.drawRoundedRect(rect(), radius, radius);
		if (isOver()) {
			p.setBrush(ExtrasUi::chatSurfaceHover());
			p.drawRoundedRect(rect(), radius, radius);
		}
	}
	const auto &padding = st::historyComposeSearchChipPadding;
	const auto image = st::historyComposeSearchChipImage;
	const auto skip = st::historyComposeSearchChipSkip;
	_paintImage(p, padding.left(), (height() - image) / 2, width(), image);

	const auto &remove = st::historyComposeSearchChipRemove;
	const auto removeLeft = width() - padding.right() - remove.width();
	remove.paint(p, removeLeft, (height() - remove.height()) / 2, width());

	const auto textLeft = padding.left() + image + skip;
	const auto textWidth = removeLeft - skip - textLeft;
	if (textWidth <= 0) {
		return;
	}
	const auto &font = st::historyComposeSearchChipFont;
	p.setFont(font);
	p.setPen(st::lightButtonFg);
	p.drawTextLeft(
		textLeft,
		(height() - font->height) / 2,
		width(),
		font->elided(_text, textWidth));
}

// 查找栏占用输入区胶囊：关键词、筛选标签、计数与前后定位、列表与筛选入口。
class FindBar final : public Ui::RpWidget {
public:
	using Index = int;

	FindBar(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> window,
		not_null<History*> history,
		PeerData *from,
		const QString &query);

	void setInnerFocus();
	void raiseWithSurface();
	void setQuery(const QString &query);
	void setFrom(PeerData *peer);
	void setFixedFilter(Api::SearchFilter filter);
	void setTotal(int total, bool reset);
	void clearResults();
	void setLoading(bool loading);
	void setFailed(bool failed);
	void setListShown(bool shown);
	void setCurrent(int current);
	bool navigate(int delta);
	void requestSearch(bool cache = true);

	[[nodiscard]] bool searchPending() const;
	bool handleKeyPress(not_null<QKeyEvent*> e);

	[[nodiscard]] rpl::producer<SearchRequest> searchRequests() const;
	[[nodiscard]] rpl::producer<Index> showItemRequests() const;
	[[nodiscard]] rpl::producer<Qt::KeyboardModifiers> submits() const;
	[[nodiscard]] rpl::producer<> closeRequests() const;
	[[nodiscard]] rpl::producer<> cancelRequests() const;
	[[nodiscard]] rpl::producer<> showListRequests() const;
	[[nodiscard]] rpl::producer<> showCalendarRequests() const;
	[[nodiscard]] rpl::producer<> showBoxFromRequests() const;
	[[nodiscard]] rpl::producer<not_null<QKeyEvent*>> keyEvents() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	[[nodiscard]] SearchRequest currentRequest() const;
	[[nodiscard]] int rowTop() const;
	void requestSearchDelayed();
	void setTypeFilter(Api::SearchFilter filter);
	void removeLastChip();
	void showTypeMenu();
	void showMoreMenu();
	void popupMenuAbove(not_null<QWidget*> button);
	[[nodiscard]] bool fromAvailable() const;
	[[nodiscard]] std::vector<not_null<Ui::IconButton*>> collapsible() const;
	void refreshChips();
	void refreshTags();
	void refreshButtons();
	void updateCounterText();
	void updateHeight();
	void updateControlsGeometry();

	const not_null<Window::SessionController*> _window;
	const not_null<History*> _history;
	const base::unique_qptr<Ui::RpWidget> _surface;
	const base::unique_qptr<Ui::InputField> _field;
	const base::unique_qptr<Ui::IconButton> _older;
	const base::unique_qptr<Ui::IconButton> _newer;
	const base::unique_qptr<Ui::IconButton> _showList;
	const base::unique_qptr<Ui::IconButton> _type;
	const base::unique_qptr<Ui::IconButton> _chooseFrom;
	const base::unique_qptr<Ui::IconButton> _calendar;
	const base::unique_qptr<Ui::IconButton> _more;
	const base::unique_qptr<Ui::IconButton> _close;
	base::unique_qptr<FilterChip> _fromChip;
	PeerData *_fromChipPeer = nullptr;
	base::unique_qptr<FilterChip> _typeChip;
	Api::SearchFilter _typeChipFilter = Api::SearchFilter::NoFilter;
	base::unique_qptr<Ui::PopupMenu> _menu;

	std::vector<Data::ReactionId> _searchTagsSelected;
	base::unique_qptr<Ui::RpWidget> _tagsArea;
	std::unique_ptr<Dialogs::SearchTags> _searchTags;
	std::optional<QPoint> _mouseGlobalPosition;

	rpl::variable<PeerData*> _from = nullptr;
	Api::SearchFilter _typeFilter = Api::SearchFilter::NoFilter;
	std::optional<Api::SearchFilter> _fixedFilter;

	base::Timer _searchTimer;
	Api::MessagesSearchMerged::CachedRequests _typedRequests;
	std::optional<SearchRequest> _lastRequest;

	int _total = -1;
	int _current = 0;
	bool _loading = false;
	bool _failed = false;
	bool _listShown = false;
	bool _iconShown = false;
	QString _counterText;
	QRect _counterRect;
	int _dividerRight = 0;

	rpl::event_stream<SearchRequest> _searchRequests;
	rpl::event_stream<Index> _showItemRequests;
	rpl::event_stream<> _cancelRequests;
	rpl::event_stream<> _showListRequests;
	rpl::event_stream<> _showCalendarRequests;
	rpl::event_stream<> _showBoxFromRequests;
	rpl::event_stream<not_null<QKeyEvent*>> _keyEvents;

};

FindBar::FindBar(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> window,
	not_null<History*> history,
	PeerData *from,
	const QString &query)
: Ui::RpWidget(parent)
, _window(window)
, _history(history)
, _surface(base::make_unique_q<Ui::RpWidget>(parent))
, _field(base::make_unique_q<Ui::InputField>(
	this,
	st::historyComposeSearchField,
	Ui::InputField::Mode::SingleLine,
	tr::lng_dlg_filter()))
, _older(base::make_unique_q<Ui::IconButton>(
	this,
	st::historyComposeSearchOlder))
, _newer(base::make_unique_q<Ui::IconButton>(
	this,
	st::historyComposeSearchNewer))
, _showList(base::make_unique_q<Ui::IconButton>(
	this,
	st::historyComposeSearchList))
, _type(base::make_unique_q<Ui::IconButton>(
	this,
	st::historyComposeSearchType))
, _chooseFrom(base::make_unique_q<Ui::IconButton>(
	this,
	st::historyComposeSearchFrom))
, _calendar(base::make_unique_q<Ui::IconButton>(
	this,
	st::historyComposeSearchCalendar))
, _more(base::make_unique_q<Ui::IconButton>(
	this,
	st::historyComposeSearchMore))
, _close(base::make_unique_q<Ui::IconButton>(
	this,
	st::historyComposeSearchClose))
, _searchTagsSelected(Data::SearchTagsFromQuery(query))
, _from(from)
, _searchTimer([=] { requestSearch(); }) {
	setObjectName(u"chatBar.search"_q);

	// 磨砂底放在下层同级控件：Qt 以反色绘制光标，画进图形效果后不可见。
	_surface->setObjectName(u"chatBar.search.surface"_q);
	_surface->setAttribute(Qt::WA_TransparentForMouseEvents);
	Ui::ApplyChatControlSurface(_surface.get(), MessageFieldCapsuleRadius());
	_surface->stackUnder(this);
	geometryValue() | rpl::on_next([=](QRect geometry) {
		_surface->setGeometry(geometry);
	}, lifetime());
	shownValue() | rpl::on_next([=](bool shown) {
		_surface->setVisible(shown);
	}, lifetime());

	_field->setObjectName(u"chatBar.search.field"_q);
	_field->customUpDown(true);
	if (_searchTagsSelected.empty()) {
		_field->setText(query);
		_field->setCursorPosition(query.size());
	}
	_older->setObjectName(u"chatBar.search.older"_q);
	_newer->setObjectName(u"chatBar.search.newer"_q);
	_showList->setObjectName(u"chatBar.search.list"_q);
	_type->setObjectName(u"chatBar.search.type"_q);
	_chooseFrom->setObjectName(u"chatBar.search.from"_q);
	_calendar->setObjectName(u"chatBar.search.date"_q);
	_more->setObjectName(u"chatBar.search.more"_q);
	_close->setObjectName(u"chatBar.search.close"_q);
	_showList->setAccessibleName(tr::extras_SearchInChatList(tr::now));
	_type->setAccessibleName(tr::extras_SearchFilterType(tr::now));
	_more->setAccessibleName(tr::lng_profile_action_short_more(tr::now));
	_chooseFrom->setAccessibleName(tr::lng_search_messages_from(tr::now));
	_calendar->setAccessibleName(tr::lng_sr_search_date(tr::now));

	_field->changes(
	) | rpl::on_next([=] {
		requestSearchDelayed();
	}, _field->lifetime());

	_field->cancelled(
	) | rpl::on_next([=] {
		_cancelRequests.fire({});
	}, _field->lifetime());

	// 关键词为空时退格依次移除类型、发送者标签。
	base::install_event_filter(_field->rawTextEdit(), [=](not_null<QEvent*> e) {
		if (e->type() != QEvent::KeyPress
			|| static_cast<QKeyEvent*>(e.get())->key() != Qt::Key_Backspace
			|| !_field->getLastText().isEmpty()
			|| (!_fromChip && !_typeChip)) {
			return base::EventFilterResult::Continue;
		}
		removeLastChip();
		return base::EventFilterResult::Cancel;
	});

	_older->setClickedCallback([=] { navigate(1); });
	_newer->setClickedCallback([=] { navigate(-1); });
	_type->setClickedCallback([=] { showTypeMenu(); });
	_more->setClickedCallback([=] { showMoreMenu(); });
	_showList->setClickedCallback([=] { _showListRequests.fire({}); });
	_calendar->setClickedCallback([=] { _showCalendarRequests.fire({}); });
	_chooseFrom->setClickedCallback([=] { _showBoxFromRequests.fire({}); });

	_from.value() | rpl::skip(1) | rpl::on_next([=] {
		refreshChips();
		refreshButtons();
	}, lifetime());

	refreshChips();
	refreshTags();
	refreshButtons();
	updateHeight();

	sizeValue(
	) | rpl::on_next([=] {
		updateControlsGeometry();
	}, lifetime());
}

int FindBar::rowTop() const {
	return height() - st::historyComposeSearchButton.height;
}

void FindBar::setInnerFocus() {
	if (Ui::AppInFocus() && Ui::InFocusChain(window())) {
		_field->setFocusFast();
	}
}

void FindBar::raiseWithSurface() {
	_surface->raise();
	raise();
}

void FindBar::setQuery(const QString &query) {
	if (auto tags = Data::SearchTagsFromQuery(query); !tags.empty()) {
		if (_searchTagsSelected != tags) {
			_searchTagsSelected = std::move(tags);
			refreshTags();
		}
		_field->setText(QString());
		requestSearch(false);
		return;
	}
	_field->setText(query);
	_field->setCursorPosition(query.size());
	requestSearch();
}

void FindBar::setFrom(PeerData *peer) {
	if (_from.current() == peer) {
		return;
	}
	_from = peer;
	requestSearch();
}

void FindBar::setFixedFilter(Api::SearchFilter filter) {
	_fixedFilter = filter;
	refreshChips();
	refreshButtons();
}

void FindBar::setTypeFilter(Api::SearchFilter filter) {
	if (_typeFilter == filter) {
		return;
	}
	_typeFilter = filter;
	refreshChips();
	refreshButtons();
	requestSearch();
}

void FindBar::removeLastChip() {
	if (_typeChip) {
		setTypeFilter(Api::SearchFilter::NoFilter);
	} else if (_fromChip) {
		setFrom(nullptr);
	}
}

void FindBar::showTypeMenu() {
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::popupMenuWithIcons);
	Dialogs::FillSearchTypeMenu(
		_menu.get(),
		_typeFilter,
		crl::guard(this, [=](Api::SearchFilter filter) {
			setTypeFilter(filter);
		}));
	popupMenuAbove(_type.get());
}

// 只列出被收起的入口，顺序与按钮从左到右一致。
void FindBar::showMoreMenu() {
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::popupMenuWithIcons);
	const auto menu = _menu.get();
	if (_showList->isHidden()) {
		menu->addAction(
			tr::extras_SearchInChatList(tr::now),
			[=] { _showListRequests.fire({}); },
			&st::historyComposeSearchMenuList
		)->setEnabled(_total > 0);
	}
	if (!_fixedFilter && _type->isHidden()) {
		auto submenu = std::make_unique<Ui::PopupMenu>(
			menu,
			st::popupMenuWithIcons);
		Dialogs::FillSearchTypeMenu(
			submenu.get(),
			_typeFilter,
			crl::guard(this, [=](Api::SearchFilter filter) {
				setTypeFilter(filter);
			}));
		menu->addAction(
			tr::extras_SearchFilterType(tr::now),
			std::move(submenu),
			&st::menuIconTagFilter);
	}
	if (fromAvailable() && _chooseFrom->isHidden()) {
		menu->addAction(
			tr::lng_search_messages_from(tr::now),
			[=] { _showBoxFromRequests.fire({}); },
			&st::historyComposeSearchMenuFrom);
	}
	if (_calendar->isHidden()) {
		menu->addAction(
			tr::lng_sr_search_date(tr::now),
			[=] { _showCalendarRequests.fire({}); },
			&st::menuIconSchedule);
	}
	popupMenuAbove(_more.get());
}

void FindBar::popupMenuAbove(not_null<QWidget*> button) {
	const auto shadow = Ui::BoxShadow::ExtendFor(
		st::popupMenuWithIcons.shadow);
	_menu->setForcedOrigin(Ui::PanelAnimation::Origin::BottomRight);
	_menu->popup(button->mapToGlobal(QPoint(
		button->width() - shadow.right(),
		st::historyComposeSearchButton.rippleAreaPosition.y()
			- shadow.bottom())));
}

bool FindBar::fromAvailable() const {
	return !_fixedFilter && HasChooseFrom(_history) && !_from.current();
}

// 窄屏时按此顺序收进“更多”菜单。
std::vector<not_null<Ui::IconButton*>> FindBar::collapsible() const {
	auto result = std::vector<not_null<Ui::IconButton*>>{ _calendar.get() };
	if (fromAvailable()) {
		result.push_back(_chooseFrom.get());
	}
	if (!_fixedFilter) {
		result.push_back(_type.get());
	}
	result.push_back(_showList.get());
	return result;
}

void FindBar::refreshChips() {
	const auto from = _from.current();
	if (!from || _history->peer->isSelf()) {
		_fromChip = nullptr;
		_fromChipPeer = nullptr;
	} else if (_fromChipPeer != from) {
		_fromChipPeer = from;
		_fromChip = base::make_unique_q<FilterChip>(
			this,
			from->shortName(),
			PaintUserpicCallback(from, false));
		_fromChip->setObjectName(u"chatBar.search.fromChip"_q);
		_fromChip->setClickedCallback([=] { setFrom(nullptr); });
		_fromChip->show();
	}

	const auto filter = _typeFilter;
	if (_fixedFilter || filter == Api::SearchFilter::NoFilter) {
		_typeChip = nullptr;
		_typeChipFilter = Api::SearchFilter::NoFilter;
	} else if (_typeChipFilter != filter) {
		_typeChipFilter = filter;
		_typeChip = base::make_unique_q<FilterChip>(
			this,
			Dialogs::SearchFilterName(filter),
			[=](Painter &p, int x, int y, int outerWidth, int size) {
				Dialogs::SearchFilterIcon(filter).paintInCenter(
					p,
					style::rtlrect(x, y, size, size, outerWidth),
					st::lightButtonFg->c);
			});
		_typeChip->setObjectName(u"chatBar.search.typeChip"_q);
		_typeChip->setClickedCallback([=] {
			setTypeFilter(Api::SearchFilter::NoFilter);
		});
		_typeChip->show();
	}
	_field->setPlaceholderHidden(_fromChip || _typeChip);
	updateControlsGeometry();
}

// 收藏夹按标签搜索时，标签条放在胶囊上部，查找栏随之增高。
void FindBar::refreshTags() {
	_searchTags = nullptr;
	_tagsArea = nullptr;
	if (!_history->peer->isSelf()) {
		updateHeight();
		return;
	}
	auto fullTagsList = _from.value() | rpl::map([=](PeerData *from) {
		const auto sublist = from
			? _history->owner().savedMessages().sublist(from).get()
			: nullptr;
		return _history->owner().reactions().myTagsValue(sublist);
	}) | rpl::flatten_latest();
	_searchTags = std::make_unique<Dialogs::SearchTags>(
		&_history->owner(),
		std::move(fullTagsList),
		_searchTagsSelected);
	_tagsArea = base::make_unique_q<Ui::RpWidget>(this);
	const auto area = _tagsArea.get();
	const auto tags = _searchTags.get();
	const auto &padding = st::historyComposeSearchTagsPadding;
	const auto position = QPoint(padding.left(), padding.top());

	tags->selectedChanges(
	) | rpl::on_next([=](std::vector<Data::ReactionId> &&list) {
		_searchTagsSelected = std::move(list);
		requestSearch(false);
	}, tags->lifetime());

	tags->menuRequests(
	) | rpl::on_next([=](Data::ReactionId id) {
		ShowTagInListMenu(
			&_menu,
			_mouseGlobalPosition.value_or(QCursor::pos()),
			this,
			id,
			_window);
	}, tags->lifetime());

	tags->repaintRequests() | rpl::on_next([=] {
		area->update();
	}, tags->lifetime());

	widthValue() | rpl::on_next([=](int width) {
		tags->resizeToWidth(width - padding.left() - padding.right());
	}, tags->lifetime());

	rpl::combine(
		widthValue(),
		tags->heightValue()
	) | rpl::on_next([=](int width, int height) {
		area->setVisible(height > 0);
		area->setGeometry(
			0,
			0,
			width,
			height ? (height + padding.top() + padding.bottom()) : 0);
		updateHeight();
	}, tags->lifetime());

	area->paintRequest() | rpl::on_next([=] {
		auto p = Painter(area);
		tags->paint(p, position, crl::now(), false);
	}, area->lifetime());

	area->setMouseTracking(true);
	area->events() | rpl::on_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::MouseMove) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			_mouseGlobalPosition = mouse->globalPos();
			const auto handler = tags->lookupHandler(mouse->pos() - position);
			ClickHandler::setActive(handler);
			area->setCursor(handler
				? style::cur_pointer
				: style::cur_default);
		} else if (e->type() == QEvent::MouseButtonPress) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			if (mouse->button() == Qt::LeftButton) {
				ClickHandler::pressed();
			}
		} else if (e->type() == QEvent::MouseButtonRelease) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			if (mouse->button() == Qt::LeftButton) {
				const auto handler = ClickHandler::unpressed();
				ActivateClickHandler(area, handler, ClickContext{
					.button = mouse->button(),
					.other = QVariant::fromValue(ClickHandlerContext{
						.sessionWindow = _window,
					}),
				});
			}
		}
	}, area->lifetime());
}

void FindBar::updateHeight() {
	const auto tags = _searchTags ? _searchTags->height() : 0;
	const auto &padding = st::historyComposeSearchTagsPadding;
	resize(
		width(),
		st::historyComposeSearchButton.height
			+ (tags ? (tags + padding.top() + padding.bottom()) : 0));
}

void FindBar::refreshButtons() {
	const auto olderEnabled = (_current > 0) && (_current < _total);
	const auto newerEnabled = (_current > 1);
	const auto listEnabled = (_total > 0);
	_older->setAttribute(Qt::WA_TransparentForMouseEvents, !olderEnabled);
	const auto olderIcon = olderEnabled
		? nullptr
		: &st::historyComposeSearchOlderDisabled;
	_older->setIconOverride(olderIcon, olderIcon);
	_newer->setAttribute(Qt::WA_TransparentForMouseEvents, !newerEnabled);
	const auto newerIcon = newerEnabled
		? nullptr
		: &st::historyComposeSearchNewerDisabled;
	_newer->setIconOverride(newerIcon, newerIcon);
	_showList->setAttribute(Qt::WA_TransparentForMouseEvents, !listEnabled);
	const auto listIcon = !listEnabled
		? &st::historyComposeSearchListDisabled
		: _listShown
		? &st::historyComposeSearchListActive
		: nullptr;
	_showList->setIconOverride(listIcon, listIcon);
	const auto typeIcon = (_typeFilter != Api::SearchFilter::NoFilter)
		? &st::historyComposeSearchTypeActive
		: nullptr;
	_type->setIconOverride(typeIcon, typeIcon);
	updateControlsGeometry();
}

void FindBar::setLoading(bool loading) {
	if (_loading == loading) {
		return;
	}
	_loading = loading;
	updateCounterText();
}

void FindBar::setListShown(bool shown) {
	_listShown = shown;
	refreshButtons();
}

void FindBar::setFailed(bool failed) {
	if (_failed == failed) {
		return;
	}
	_failed = failed;
	updateCounterText();
}

void FindBar::updateCounterText() {
	_counterText = _failed
		? tr::extras_SearchInChatFailed(tr::now)
		: (_loading && _total < 0)
		? tr::extras_SearchInChatLoading(tr::now)
		: (_total < 0)
		? QString()
		: (_total == 0)
		? tr::lng_search_messages_none(tr::now)
		: tr::extras_SearchInChatCounter(
			tr::now,
			lt_n,
			QString::number(std::max(_current, 1)),
			lt_amount,
			QString::number(_total));
	updateControlsGeometry();
	update();
}

void FindBar::setTotal(int total, bool reset) {
	_total = total;
	if (reset) {
		setCurrent(1);
		return;
	}
	refreshButtons();
	updateCounterText();
}

void FindBar::clearResults() {
	_total = -1;
	_current = 0;
	_failed = false;
	refreshButtons();
	updateCounterText();
}

void FindBar::setCurrent(int current) {
	_current = current;
	refreshButtons();
	updateCounterText();
	_showItemRequests.fire(current - 1);
}

bool FindBar::navigate(int delta) {
	const auto current = _current + delta;
	if (_current <= 0 || current < 1 || current > _total) {
		return false;
	}
	setCurrent(current);
	return true;
}

SearchRequest FindBar::currentRequest() const {
	return {
		.query = _field->getLastText().trimmed(),
		.from = _from.current(),
		.tags = _searchTagsSelected,
		.filter = _fixedFilter.value_or(_typeFilter),
	};
}

bool FindBar::searchPending() const {
	return _searchTimer.isActive()
		|| !_lastRequest
		|| (*_lastRequest != currentRequest());
}

void FindBar::requestSearch(bool cache) {
	_searchTimer.cancel();
	const auto search = currentRequest();
	if (cache) {
		_typedRequests.insert(search);
	}
	_lastRequest = search;
	_searchRequests.fire_copy(search);
}

void FindBar::requestSearchDelayed() {
	if (_typedRequests.contains(currentRequest())) {
		requestSearch(false);
		return;
	}
	_searchTimer.callOnce(AutoSearchTimeout);
}

bool FindBar::handleKeyPress(not_null<QKeyEvent*> e) {
	const auto modifiers = e->modifiers();
	if (e->key() == Qt::Key_F3) {
		if (modifiers == Qt::NoModifier) {
			return navigate(1);
		} else if (modifiers == Qt::ShiftModifier) {
			return navigate(-1);
		}
	}
#ifdef Q_OS_MAC
	if (e->key() == Qt::Key_G && modifiers.testFlag(Qt::ControlModifier)) {
		return navigate(modifiers.testFlag(Qt::ShiftModifier) ? -1 : 1);
	}
#endif
	return false;
}

void FindBar::updateControlsGeometry() {
	const auto rowHeight = st::historyComposeSearchButton.height;
	const auto top = rowTop();
	const auto &counterFont = st::historyComposeSearchCounterFont;
	const auto counterSkip = st::historyComposeSearchCounterSkip;
	const auto counterWidth = _counterText.isEmpty()
		? 0
		: counterFont->width(_counterText);

	auto chips = std::vector<not_null<FilterChip*>>();
	auto chipsMin = 0;
	for (const auto chip : { _fromChip.get(), _typeChip.get() }) {
		if (chip) {
			chips.push_back(chip);
			chipsMin += chip->minimalWidth()
				+ st::historyComposeSearchChipSkip;
		}
	}
	const auto fieldMin = st::historyComposeSearchFieldMinWidth;
	const auto minLeft = chips.empty()
		? st::historyComposeSearchFieldLeft
		: st::historyComposeSearchIconLeft;

	// 按常见计数宽度判断收起，避免结果到达时按钮跳动。
	const auto counterReserve = std::max(
		counterWidth,
		counterFont->width(u"00 / 000"_q)) + 2 * counterSkip;
	const auto buttons = collapsible();
	auto free = width()
		- st::historyComposeCapsulePadding
		- _close->width()
		- 2 * st::historyComposeSearchDividerSkip
		- st::lineWidth
		- _newer->width()
		- _older->width()
		- counterReserve;
	for (const auto button : buttons) {
		free -= button->width();
	}
	const auto needed = minLeft + chipsMin + fieldMin;
	auto collapsed = 0;
	if (free < needed) {
		free -= _more->width();
		while (collapsed < int(buttons.size()) && free < needed) {
			free += buttons[collapsed++]->width();
		}
	}
	if (!fromAvailable()) {
		_chooseFrom->hide();
	}
	if (_fixedFilter) {
		_type->hide();
	}
	for (auto i = 0; i != int(buttons.size()); ++i) {
		buttons[i]->setVisible(i >= collapsed);
	}
	_more->setVisible(collapsed > 0);

	auto right = st::historyComposeCapsulePadding;
	const auto placeRight = [&](not_null<Ui::RpWidget*> widget) {
		if (widget->isHidden()) {
			return;
		}
		widget->moveToRight(right, top + (rowHeight - widget->height()) / 2);
		right += widget->width();
	};
	placeRight(_close.get());
	placeRight(_more.get());
	placeRight(_calendar.get());
	placeRight(_chooseFrom.get());
	placeRight(_type.get());
	placeRight(_showList.get());
	right += st::historyComposeSearchDividerSkip;
	_dividerRight = right;
	right += st::lineWidth + st::historyComposeSearchDividerSkip;
	placeRight(_newer.get());
	placeRight(_older.get());
	if (_counterText.isEmpty()) {
		_counterRect = QRect();
	} else {
		right += counterSkip;
		_counterRect = QRect(
			width() - right - counterWidth,
			top,
			counterWidth,
			rowHeight);
		right += counterWidth + counterSkip;
	}

	const auto iconSpace = st::historyComposeSearchIconLeft
		+ st::historyComposeSearchIcon.width()
		+ st::historyComposeSearchChipSkip;
	free = width() - right;
	_iconShown = chips.empty() && (free >= iconSpace + fieldMin);
	auto left = _iconShown ? iconSpace : minLeft;
	auto chipsSpace = std::max(free - left - fieldMin, 0);
	auto chipsLeft = int(chips.size());
	for (const auto &chip : chips) {
		const auto share = chipsSpace / chipsLeft--;
		const auto chipWidth = std::max(
			chip->minimalWidth(),
			std::min(chip->naturalWidth(), share));
		chip->resize(chipWidth, st::historyComposeSearchChipHeight);
		chip->moveToLeft(left, top + (rowHeight - chip->height()) / 2);
		left += chipWidth + st::historyComposeSearchChipSkip;
		chipsSpace -= chipWidth + st::historyComposeSearchChipSkip;
	}
	_field->resizeToWidth(std::max(free - left, 0));
	_field->moveToLeft(left, top + (rowHeight - _field->height()) / 2);
	update();
}

void FindBar::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto rowHeight = st::historyComposeSearchButton.height;
	const auto top = rowTop();
	if (_iconShown) {
		const auto &icon = st::historyComposeSearchIcon;
		icon.paint(
			p,
			st::historyComposeSearchIconLeft,
			top + (rowHeight - icon.height()) / 2,
			width());
	}
	const auto dividerHeight = st::historyComposeSearchDividerHeight;
	p.fillRect(
		myrtlrect(
			width() - _dividerRight - st::lineWidth,
			top + (rowHeight - dividerHeight) / 2,
			st::lineWidth,
			dividerHeight),
		st::windowDividerFg);
	if (!_counterText.isEmpty()) {
		p.setFont(st::historyComposeSearchCounterFont);
		p.setPen(_failed ? st::attentionButtonFg : st::windowSubTextFg);
		p.drawText(myrtlrect(_counterRect), style::al_center, _counterText);
	}
}

void FindBar::keyPressEvent(QKeyEvent *e) {
	_keyEvents.fire_copy(e);
}

rpl::producer<SearchRequest> FindBar::searchRequests() const {
	return _searchRequests.events();
}

rpl::producer<FindBar::Index> FindBar::showItemRequests() const {
	return _showItemRequests.events();
}

rpl::producer<Qt::KeyboardModifiers> FindBar::submits() const {
	return _field->submits();
}

rpl::producer<> FindBar::closeRequests() const {
	return _close->clicks() | rpl::to_empty;
}

rpl::producer<> FindBar::cancelRequests() const {
	return _cancelRequests.events();
}

rpl::producer<> FindBar::showListRequests() const {
	return _showListRequests.events();
}

rpl::producer<> FindBar::showCalendarRequests() const {
	return _showCalendarRequests.events();
}

rpl::producer<> FindBar::showBoxFromRequests() const {
	return _showBoxFromRequests.events();
}

rpl::producer<not_null<QKeyEvent*>> FindBar::keyEvents() const {
	return _keyEvents.events();
}

} // namespace

class ComposeSearch::Inner final {
public:
	Inner(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> window,
		not_null<History*> history,
		PeerData *from,
		const QString &query);
	~Inner();

	void hideAnimated();
	void setInnerFocus();
	void setQuery(const QString &query);
	void setFrom(PeerData *from);
	void raise();
	void setTopMsgId(MsgId topMsgId);
	void setSearchFilter(Api::SearchFilter filter);
	void setCalendarChat(const Dialogs::Key &chat);
	void setCalendarJumpHandler(Fn<void(FullMsgId, Fn<void()>)> jump);

	[[nodiscard]] int height() const;
	[[nodiscard]] rpl::producer<int> heightChanges() const;
	[[nodiscard]] rpl::producer<Activation> activations() const;
	[[nodiscard]] rpl::producer<> destroyRequests() const;
	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void clearResults();
	void showList();
	void hideList();
	void updateListGeometry();

	const not_null<Ui::RpWidget*> _parent;
	const not_null<Window::SessionController*> _window;
	const not_null<History*> _history;
	const base::unique_qptr<FindBar> _findBar;
	const List _list;

	Api::MessagesSearchMerged _apiSearch;
	bool _searching = false;
	bool _autoJump = false;

	struct {
		struct {
			QString token;
			FindBar::Index index = -1;
		} data;
		rpl::event_stream<FindBar::Index> jumps;
	} _pendingJump;

	MsgId _topMsgId;
	Dialogs::Key _calendarChat;
	Fn<void(FullMsgId, Fn<void()>)> _calendarJump;

	rpl::event_stream<Activation> _activations;
	rpl::event_stream<> _destroyRequests;

};

ComposeSearch::Inner::Inner(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> window,
	not_null<History*> history,
	PeerData *from,
	const QString &query)
: _parent(parent)
, _window(window)
, _history(history)
, _findBar(base::make_unique_q<FindBar>(parent, window, history, from, query))
, _list(
	CreateList(
		parent,
		history,
		_findBar->keyEvents() | rpl::filter([](not_null<QKeyEvent*> e) {
			return e->key() == Qt::Key_PageDown || e->key() == Qt::Key_PageUp;
		})))
, _apiSearch(history)
, _calendarChat(history) {
	_findBar->show();
	_findBar->raiseWithSurface();

	// 查找栏贴在宿主底部，与输入区胶囊同样四周留边。
	rpl::combine(
		_parent->sizeValue(),
		_findBar->heightValue()
	) | rpl::on_next([=](QSize size, int height) {
		const auto margin = st::historyComposeCapsuleMargin;
		_findBar->setGeometry(
			margin,
			size.height() - margin - height,
			std::max(size.width() - 2 * margin, 0),
			height);
	}, _findBar->lifetime());

	rpl::combine(
		_findBar->geometryValue(),
		_list.content->heightValue()
	) | rpl::on_next([=] {
		updateListGeometry();
	}, _list.container->lifetime());

	_findBar->keyEvents(
	) | rpl::on_next([=](not_null<QKeyEvent*> e) {
		_findBar->handleKeyPress(e);
	}, _findBar->lifetime());

	_findBar->searchRequests(
	) | rpl::on_next([=](SearchRequest search) {
		search.topMsgId = _topMsgId;
		if (search.query.isEmpty()
			&& search.tags.empty()
			&& search.filter == Api::SearchFilter::NoFilter
			&& (!search.from || _history->peer->isSelf())) {
			clearResults();
			return;
		}
		_searching = true;
		_pendingJump.data = {};
		_findBar->clearResults();
		_findBar->setLoading(true);
		_list.controller->addItems({}, true);
		_list.controller->setQuery(search.query);
		_apiSearch.search(search);
	}, _findBar->lifetime());

	// 回车：条件有变或上次失败时立即搜索，否则定位上一条，上档键反向。
	_findBar->submits(
	) | rpl::on_next([=](Qt::KeyboardModifiers modifiers) {
		if (_findBar->searchPending() || _apiSearch.messages().failed) {
			_findBar->requestSearch();
		} else {
			_findBar->navigate(
				modifiers.testFlag(Qt::ShiftModifier) ? -1 : 1);
		}
	}, _findBar->lifetime());

	_findBar->closeRequests(
	) | rpl::on_next([=] {
		hideAnimated();
	}, _findBar->lifetime());

	_findBar->cancelRequests(
	) | rpl::on_next([=] {
		if (!_list.container->isHidden()) {
			hideList();
		} else {
			hideAnimated();
		}
	}, _findBar->lifetime());

	_apiSearch.newFounds(
	) | rpl::on_next([=] {
		if (!_searching) {
			return;
		}
		const auto &apiData = _apiSearch.messages();
		_findBar->setLoading(apiData.totalOnly && !apiData.failed);
		_findBar->setFailed(apiData.failed);
		if (!apiData.totalOnly || apiData.failed) {
			_list.controller->addItems(apiData.messages, true);
		}
		// 自动定位第一条结果时保留已打开的列表。
		_autoJump = true;
		_findBar->setTotal(apiData.total, true);
		_autoJump = false;
	}, _findBar->lifetime());

	_apiSearch.nextFounds(
	) | rpl::on_next([=] {
		if (!_searching) {
			return;
		}
		const auto &apiData = _apiSearch.messages();
		_findBar->setLoading(false);
		if (!apiData.totalOnly || apiData.failed) {
			_list.controller->addItems(apiData.messages, false);
		}
		_findBar->setTotal(apiData.total, false);
		if (_pendingJump.data.token == apiData.nextToken) {
			_pendingJump.jumps.fire_copy(_pendingJump.data.index);
		}
	}, _findBar->lifetime());

	rpl::merge(
		_pendingJump.jumps.events() | rpl::filter(rpl::mappers::_1 >= 0),
		_findBar->showItemRequests()
	) | rpl::on_next([=](FindBar::Index index) {
		const auto &apiData = _apiSearch.messages();
		const auto &messages = apiData.messages;
		const auto size = int(messages.size());
		if (index >= (size - 1) && !apiData.finished) {
			_apiSearch.searchMore();
		}
		if (index >= size || index < 0) {
			_pendingJump.data = { apiData.nextToken, index };
			return;
		}
		_pendingJump.data = {};
		const auto item = _history->owner().message(messages[index]);
		if (!item) {
			return;
		}
		const auto autoJump = _autoJump;
		const auto weak = base::make_weak(_findBar.get());
		_activations.fire_copy({ item, _apiSearch.request().query });
		if (weak && !autoJump) {
			hideList();
		}
	}, _findBar->lifetime());

	_list.controller->showItemRequests(
	) | rpl::on_next([=](const FullMsgId &id) {
		const auto &messages = _apiSearch.messages().messages;
		const auto it = ranges::find(messages, id);
		if (it != end(messages)) {
			_findBar->setCurrent(std::distance(begin(messages), it) + 1);
		}
	}, _list.container->lifetime());

	_list.controller->searchMoreRequests(
	) | rpl::on_next([=] {
		const auto &apiData = _apiSearch.messages();
		if (_searching && !apiData.finished) {
			_apiSearch.searchMore();
		}
	}, _list.container->lifetime());

	_findBar->showCalendarRequests(
	) | rpl::on_next([=] {
		hideList();
		auto descriptor = Window::SessionController::ShowCalendarDescriptor();
		descriptor.chat = _calendarChat;
		descriptor.customJump = _calendarJump;
		_window->showCalendar(std::move(descriptor));
	}, _findBar->lifetime());

	_findBar->showBoxFromRequests(
	) | rpl::on_next([=] {
		const auto bar = _findBar.get();
		auto box = Dialogs::SearchFromBox(
			_history->peer,
			crl::guard(bar, [=](not_null<PeerData*> from) {
				_window->hideLayer();
				bar->setFrom(from);
			}),
			crl::guard(bar, [=] { setInnerFocus(); }));
		_window->show(std::move(box));
	}, _findBar->lifetime());

	_findBar->showListRequests(
	) | rpl::on_next([=] {
		if (_list.container->isHidden()) {
			showList();
		} else {
			hideList();
		}
	}, _findBar->lifetime());

	if (!query.isEmpty() || from) {
		crl::on_main(_findBar.get(), [=] {
			_findBar->requestSearch();
		});
	}
}

void ComposeSearch::Inner::clearResults() {
	_searching = false;
	_pendingJump.data = {};
	_findBar->setLoading(false);
	_findBar->clearResults();
	_list.controller->addItems({}, true);
	hideList();
}

// 卡片底边贴近查找栏，高度随结果增长，顶部不越过聊天顶栏。
void ComposeSearch::Inner::updateListGeometry() {
	const auto bar = _findBar->geometry();
	const auto bottom = bar.y() - st::historyComposeSearchListSkip;
	const auto limit = st::topBarHeight + st::historyComposeCapsuleMargin;
	const auto top = std::max(limit, bottom - _list.content->height());
	_list.container->setGeometry(
		bar.x(),
		top,
		bar.width(),
		std::max(bottom - top, 0));
}

void ComposeSearch::Inner::showList() {
	updateListGeometry();
	_list.container->show();
	_list.container->raise();
	_findBar->setListShown(true);
}

void ComposeSearch::Inner::hideList() {
	if (_list.container->isHidden()) {
		return;
	}
	_list.container->hide();
	_findBar->setListShown(false);
}

void ComposeSearch::Inner::setInnerFocus() {
	_findBar->setInnerFocus();
}

void ComposeSearch::Inner::setQuery(const QString &query) {
	_findBar->setQuery(query);
}

void ComposeSearch::Inner::setFrom(PeerData *from) {
	_findBar->setFrom(from);
}

void ComposeSearch::Inner::raise() {
	_findBar->raiseWithSurface();
	if (!_list.container->isHidden()) {
		_list.container->raise();
	}
}

void ComposeSearch::Inner::setTopMsgId(MsgId topMsgId) {
	if (topMsgId) {
		_apiSearch.disableMigrated();
	}
	_topMsgId = topMsgId;
}

void ComposeSearch::Inner::setSearchFilter(Api::SearchFilter filter) {
	_findBar->setFixedFilter(filter);
}

void ComposeSearch::Inner::setCalendarChat(const Dialogs::Key &chat) {
	_calendarChat = chat;
}

void ComposeSearch::Inner::setCalendarJumpHandler(
		Fn<void(FullMsgId, Fn<void()>)> jump) {
	_calendarJump = std::move(jump);
}

int ComposeSearch::Inner::height() const {
	return _findBar->height();
}

rpl::producer<int> ComposeSearch::Inner::heightChanges() const {
	return _findBar->heightValue() | rpl::skip(1);
}

void ComposeSearch::Inner::hideAnimated() {
	hideList();
	_findBar->hide();
	_destroyRequests.fire({});
}

rpl::producer<Activation> ComposeSearch::Inner::activations() const {
	return _activations.events();
}

rpl::producer<> ComposeSearch::Inner::destroyRequests() const {
	return _destroyRequests.events();
}

rpl::lifetime &ComposeSearch::Inner::lifetime() {
	return _findBar->lifetime();
}

ComposeSearch::Inner::~Inner() {
}

ComposeSearch::ComposeSearch(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> window,
	not_null<History*> history,
	PeerData *from,
	const QString &query)
: _inner(std::make_unique<Inner>(parent, window, history, from, query)) {
}

ComposeSearch::~ComposeSearch() {
}

void ComposeSearch::hideAnimated() {
	_inner->hideAnimated();
}

void ComposeSearch::setInnerFocus() {
	_inner->setInnerFocus();
}

void ComposeSearch::setQuery(const QString &query) {
	_inner->setQuery(query);
}

void ComposeSearch::setFrom(PeerData *from) {
	_inner->setFrom(from);
}

void ComposeSearch::raise() {
	_inner->raise();
}

void ComposeSearch::setTopMsgId(MsgId topMsgId) {
	_inner->setTopMsgId(topMsgId);
}

void ComposeSearch::setSearchFilter(Api::SearchFilter filter) {
	_inner->setSearchFilter(filter);
}

void ComposeSearch::setCalendarChat(const Dialogs::Key &chat) {
	_inner->setCalendarChat(chat);
}

void ComposeSearch::setCalendarJumpHandler(
		Fn<void(FullMsgId, Fn<void()>)> jump) {
	_inner->setCalendarJumpHandler(std::move(jump));
}

int ComposeSearch::height() const {
	return _inner->height();
}

rpl::producer<int> ComposeSearch::heightChanges() const {
	return _inner->heightChanges();
}

rpl::producer<ComposeSearch::Activation> ComposeSearch::activations() const {
	return _inner->activations();
}

rpl::producer<> ComposeSearch::destroyRequests() const {
	return _inner->destroyRequests();
}

rpl::lifetime &ComposeSearch::lifetime() {
	return _inner->lifetime();
}

} // namespace HistoryView
