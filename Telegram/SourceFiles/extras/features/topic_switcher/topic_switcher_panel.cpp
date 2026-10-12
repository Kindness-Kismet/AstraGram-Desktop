#include "extras/features/topic_switcher/topic_switcher_panel.h"

#include "base/unique_qptr.h"
#include "data/data_changes.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_saved_messages.h"
#include "data/data_saved_sublist.h"
#include "data/data_send_action.h"
#include "data/data_session.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_row.h"
#include "dialogs/ui/dialogs_layout.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/controls/tabbed_search.h"
#include "ui/effects/panel_animation.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_dialogs.h"
#include "styles/style_extras_styles.h"
#include "styles/style_widgets.h"

#include <QtWidgets/QApplication>

namespace ExtrasFeatures::TopicSwitcher {
namespace {

// 论坛取话题列表，频道私信取会话列表；调用方保证二者之一存在。
[[nodiscard]] not_null<Dialogs::MainList*> ThreadsList(
		not_null<History*> history) {
	if (const auto forum = history->peer->forum()) {
		return forum->topicsList();
	}
	const auto monoforum = history->peer->monoforum();
	Expects(monoforum != nullptr);
	return monoforum->chatsList();
}

[[nodiscard]] rpl::producer<> ThreadsListChanges(not_null<History*> history) {
	if (const auto forum = history->peer->forum()) {
		return forum->chatsListChanges();
	}
	return history->peer->monoforum()->chatsListChanges();
}

[[nodiscard]] rpl::producer<not_null<Data::Thread*>> ThreadDestroyed(
		not_null<History*> history) {
	if (const auto forum = history->peer->forum()) {
		return forum->topicDestroyed(
		) | rpl::map([](not_null<Data::ForumTopic*> topic) {
			return not_null<Data::Thread*>(topic);
		});
	}
	return history->peer->monoforum()->sublistDestroyed(
	) | rpl::map([](not_null<Data::SavedSublist*> sublist) {
		return not_null<Data::Thread*>(sublist);
	});
}

void LoadMore(not_null<History*> history) {
	if (const auto forum = history->peer->forum()) {
		forum->requestTopics();
	} else {
		history->peer->monoforum()->loadMore();
	}
}

} // namespace

QString AllLabel(not_null<History*> history) {
	return history->peer->forum()
		? tr::extras_TopicSwitcherAllTopics(tr::now)
		: tr::lng_filters_all(tr::now);
}

class Panel::List final : public Ui::RpWidget {
public:
	List(QWidget *parent, const PanelDescriptor &descriptor);

	void setQuery(const std::vector<QString> &query);
	void selectSkip(int direction);
	void chooseSelected(bool newWindow);
	[[nodiscard]] int activeIndex() const;
	[[nodiscard]] int rowHeight() const;
	[[nodiscard]] rpl::producer<Ui::ScrollToRequest> scrollToRequests() const;

private:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(int visibleTop, int visibleBottom) override;
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	void setupSubscriptions();
	void refresh();
	void refilter();
	void setSelected(int index);
	void showMenu(not_null<Data::Thread*> thread, QPoint globalPosition);
	void paintAll(Painter &p, QRect geometry, bool active, bool selected);
	void paintEmpty(Painter &p);

	[[nodiscard]] int count() const;
	[[nodiscard]] int indexAt(QPoint position) const;
	[[nodiscard]] Data::Thread *threadAt(int index) const;
	[[nodiscard]] bool ours(const Data::Thread *thread) const;

	const not_null<Window::SessionController*> _controller;
	const not_null<History*> _history;
	const not_null<Data::Thread*> _active;
	const Fn<void(not_null<Data::Thread*>, bool)> _chosen;
	const not_null<const style::DialogRow*> _st;
	QStringList _query;
	std::vector<not_null<Data::Thread*>> _filtered;
	int _selected = -1;
	int _pressed = -1;
	base::unique_qptr<Ui::PopupMenu> _menu;
	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;

};

Panel::List::List(QWidget *parent, const PanelDescriptor &descriptor)
: RpWidget(parent)
, _controller(descriptor.controller)
, _history(descriptor.history)
, _active(descriptor.active)
, _chosen(descriptor.chosen)
, _st(_history->peer->forum()
	? &st::forumTopicRow
	: &st::defaultDialogRow) {
	setObjectName(u"topicSwitcherList"_q);
	setMouseTracking(true);
	setupSubscriptions();
	if (!ThreadsList(_history)->loaded()) {
		LoadMore(_history);
	}
}

void Panel::List::setupSubscriptions() {
	const auto session = &_history->session();
	session->changes().entryUpdates(
		Data::EntryUpdate::Flag::Repaint
	) | rpl::filter([=](const Data::EntryUpdate &update) {
		return ours(update.entry->asThread());
	}) | rpl::on_next([=] {
		update();
	}, lifetime());

	using AnimationUpdate = Data::SendActionManager::AnimationUpdate;
	session->data().sendActionManager().animationUpdated(
	) | rpl::filter([=](const AnimationUpdate &update) {
		return ours(update.thread);
	}) | rpl::on_next([=] {
		update();
	}, lifetime());

	using Refresh = Data::Session::ChatListEntryRefresh;
	rpl::merge(
		ThreadsListChanges(_history),
		ThreadsList(_history)->unreadStateChanges() | rpl::to_empty,
		session->data().pinnedDialogsOrderUpdated(),
		session->data().chatListEntryRefreshes(
		) | rpl::filter([=](const Refresh &event) {
			return !event.filterId && ours(event.key.thread());
		}) | rpl::to_empty
	) | rpl::on_next([=] {
		refresh();
	}, lifetime());

	// 话题先发销毁事件再析构，这里同步移除，绘制时不会碰到失效指针。
	ThreadDestroyed(
		_history
	) | rpl::on_next([=](not_null<Data::Thread*> thread) {
		_filtered.erase(
			ranges::remove(_filtered, thread),
			end(_filtered));
		refresh();
	}, lifetime());
}

bool Panel::List::ours(const Data::Thread *thread) const {
	return thread && (thread->owningHistory() == _history);
}

void Panel::List::refresh() {
	refilter();
	resizeToWidth(width());
	update();
}

void Panel::List::setQuery(const std::vector<QString> &query) {
	const auto words = query.empty()
		? QStringList()
		: TextUtilities::PrepareSearchWords(query.front());
	if (_query == words) {
		return;
	}
	_query = words;
	_selected = -1;
	refresh();
	// 搜索时默认选中首项，回车即可打开。
	if (!_query.isEmpty() && count() > 0) {
		setSelected(0);
	}
}

void Panel::List::refilter() {
	_filtered.clear();
	if (_query.isEmpty()) {
		return;
	}
	for (const auto row : ThreadsList(_history)->indexed()->filtered(_query)) {
		if (const auto thread = row->thread()) {
			_filtered.push_back(thread);
		}
	}
}

int Panel::List::count() const {
	return _query.isEmpty()
		? (1 + ThreadsList(_history)->indexed()->all().size())
		: int(_filtered.size());
}

int Panel::List::rowHeight() const {
	return _st->height;
}

Data::Thread *Panel::List::threadAt(int index) const {
	if (index < 0 || index >= count()) {
		return nullptr;
	} else if (!_query.isEmpty()) {
		return _filtered[index];
	} else if (!index) {
		return _history;
	}
	const auto &all = ThreadsList(_history)->indexed()->all();
	return (*(all.begin() + (index - 1)))->thread();
}

int Panel::List::indexAt(QPoint position) const {
	if (position.y() < 0 || !rect().contains(position)) {
		return -1;
	}
	const auto index = position.y() / rowHeight();
	return (index < count()) ? index : -1;
}

int Panel::List::activeIndex() const {
	for (auto i = 0, n = count(); i != n; ++i) {
		if (threadAt(i) == _active) {
			return i;
		}
	}
	return -1;
}

rpl::producer<Ui::ScrollToRequest> Panel::List::scrollToRequests() const {
	return _scrollToRequests.events();
}

int Panel::List::resizeGetHeight(int newWidth) {
	const auto rows = count();
	return rows ? (rows * rowHeight()) : st::topicSwitcherEmptyHeight;
}

void Panel::List::visibleTopBottomUpdated(int visibleTop, int visibleBottom) {
	// 接近底部且话题还没加载完时继续请求。
	const auto preload = rowHeight() * 5;
	if (_query.isEmpty()
		&& (visibleBottom + preload >= height())
		&& !ThreadsList(_history)->loaded()) {
		LoadMore(_history);
	}
}

void Panel::List::setSelected(int index) {
	if (_selected == index) {
		return;
	}
	const auto height = rowHeight();
	update(0, _selected * height, width(), height);
	_selected = index;
	update(0, _selected * height, width(), height);
}

void Panel::List::selectSkip(int direction) {
	const auto rows = count();
	if (!rows) {
		return;
	}
	const auto active = activeIndex();
	const auto from = (_selected >= 0)
		? _selected
		: (active >= 0)
		? active
		: (direction > 0 ? -1 : rows);
	const auto index = std::clamp(from + direction, 0, rows - 1);
	setSelected(index);
	_scrollToRequests.fire({
		index * rowHeight(),
		(index + 1) * rowHeight(),
	});
}

void Panel::List::chooseSelected(bool newWindow) {
	if (const auto thread = threadAt(_selected)) {
		_chosen(thread, newWindow);
	}
}

void Panel::List::mouseMoveEvent(QMouseEvent *e) {
	if (!_menu) {
		setSelected(indexAt(e->pos()));
	}
}

void Panel::List::leaveEventHook(QEvent *e) {
	if (!_menu) {
		setSelected(-1);
	}
}

void Panel::List::mousePressEvent(QMouseEvent *e) {
	_pressed = (e->button() == Qt::LeftButton) ? indexAt(e->pos()) : -1;
}

void Panel::List::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = std::exchange(_pressed, -1);
	if (pressed < 0 || pressed != indexAt(e->pos())) {
		return;
	}
	if (const auto thread = threadAt(pressed)) {
		_chosen(thread, (e->modifiers() & Qt::ControlModifier) != 0);
	}
}

void Panel::List::contextMenuEvent(QContextMenuEvent *e) {
	const auto index = (e->reason() == QContextMenuEvent::Mouse)
		? indexAt(e->pos())
		: _selected;
	if (const auto thread = threadAt(index)) {
		setSelected(index);
		showMenu(thread, e->globalPos());
	}
}

void Panel::List::showMenu(
		not_null<Data::Thread*> thread,
		QPoint globalPosition) {
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuExpandedSeparator);
	Window::FillDialogsEntryMenu(
		_controller,
		Dialogs::EntryState{
			.key = Dialogs::Key{ thread },
			.section = Dialogs::EntryState::Section::SubsectionTabsMenu,
		},
		Ui::Menu::CreateAddActionCallback(_menu));
	if (_menu->empty()) {
		_menu = nullptr;
		return;
	}
	_menu->popup(globalPosition);
}

void Panel::List::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto rows = count();
	if (!rows) {
		paintEmpty(p);
		return;
	}
	const auto clip = e->rect();
	const auto height = rowHeight();
	const auto from = std::max(clip.y() / height, 0);
	const auto till = std::min((clip.y() + clip.height() + height - 1) / height, rows);
	const auto forum = _history->peer->forum();
	auto context = Dialogs::Ui::PaintContext{
		.st = _st,
		.forum = forum,
		.currentBg = st::emojiPanBg->b,
		.now = crl::now(),
		.width = width(),
		.paused = _controller->isGifPausedAtLeastFor(
			Window::GifPauseReason::Any),
	};
	p.translate(0, from * height);
	for (auto i = from; i != till; ++i) {
		const auto thread = threadAt(i);
		const auto active = (thread == _active);
		const auto selected = (i == _selected);
		if (thread == _history) {
			paintAll(p, QRect(0, 0, width(), height), active, selected);
		} else if (const auto links = thread->chatListLinks(FilterId())) {
			context.active = active;
			context.selected = selected;
			Dialogs::Ui::RowPainter::Paint(p, links->main, nullptr, context);
		}
		p.translate(0, height);
	}
}

void Panel::List::paintAll(
		Painter &p,
		QRect geometry,
		bool active,
		bool selected) {
	if (active || selected) {
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(active ? st::dialogsBgActive : st::dialogsBgOver);
		const auto radius = _st->activeRadius;
		p.drawRoundedRect(
			geometry.marginsRemoved(_st->activeMargin),
			radius,
			radius);
	}
	const auto size = _st->photoSize;
	const auto photo = QRect(
		_st->padding.left(),
		geometry.y() + (geometry.height() - size) / 2,
		size,
		size);
	(active
		? st::topicSwitcherAllIconActive
		: st::topicSwitcherAllIcon).paintInCenter(p, photo);

	const auto &font = st::semiboldFont;
	const auto available = width() - _st->nameLeft - _st->padding.right();
	p.setPen(active ? st::dialogsNameFgActive : st::dialogsNameFg);
	p.setFont(font);
	p.drawTextLeft(
		_st->nameLeft,
		geometry.y() + (geometry.height() - font->height) / 2,
		width(),
		font->elided(AllLabel(_history), available));
}

void Panel::List::paintEmpty(Painter &p) {
	p.setPen(st::windowSubTextFg);
	p.setFont(st::normalFont);
	p.drawText(
		rect(),
		(_history->peer->forum()
			? tr::lng_topics_not_found(tr::now)
			: tr::lng_bot_chats_not_found(tr::now)),
		style::al_center);
}

Panel::Panel(not_null<QWidget*> parent, PanelDescriptor &&descriptor)
: RpWidget(parent)
, _toggle(descriptor.toggle)
, _hidingCallback(std::move(descriptor.hiding))
, _shadow(st::defaultEmojiPan.showAnimation.shadow)
, _roundRect(st::emojiPanRadius, st::emojiPanBg)
, _search(Ui::CreateChild<Ui::TabbedSearchField>(
	this,
	st::defaultEmojiPan.search))
, _scroll(Ui::CreateChild<Ui::ScrollArea>(this, st::defaultSolidScroll))
, _list(_scroll->setOwnedWidget(
	object_ptr<List>(_scroll, descriptor)).data()) {
	setObjectName(u"topicSwitcherPanel"_q);
	hide();
	setupSearch();

	_list->heightValue(
	) | rpl::skip(1) | rpl::on_next([=] {
		updateSize();
	}, lifetime());
	_list->scrollToRequests(
	) | rpl::on_next([=](Ui::ScrollToRequest request) {
		_scroll->scrollToY(request.ymin, request.ymax);
	}, lifetime());
	_scroll->scrolls(
	) | rpl::on_next([=] {
		const auto top = _scroll->scrollTop();
		_list->setVisibleTopBottom(top, top + _scroll->height());
	}, lifetime());

	qApp->installEventFilter(this);
}

Panel::~Panel() = default;

void Panel::setupSearch() {
	const auto field = _search->field();
	field->customUpDown(true);
	field->submits(
	) | rpl::on_next([=](Qt::KeyboardModifiers modifiers) {
		_list->chooseSelected((modifiers & Qt::ControlModifier) != 0);
	}, lifetime());
	_search->escapes(
	) | rpl::on_next([=] {
		hideAnimated();
	}, lifetime());
	_search->queryValue(
	) | rpl::skip(1) | rpl::on_next([=](std::vector<QString> query) {
		_list->setQuery(query);
		_scroll->scrollToY(0);
	}, lifetime());
}

QRect Panel::innerRect() const {
	return rect().marginsRemoved(st::emojiPanMargins);
}

void Panel::showAt(QPoint position, int maxBottom) {
	_maxBottom = maxBottom;
	const auto margins = st::emojiPanMargins;
	move(position - QPoint(margins.left(), margins.top()));
	updateSize();

	if (const auto index = _list->activeIndex(); index > 0) {
		const auto height = _list->rowHeight();
		_scroll->scrollToY(index * height, (index + 1) * height);
	}
	raise();
	show();
	startShowAnimation();
}

void Panel::updateSize() {
	const auto margins = st::emojiPanMargins;
	const auto &searchMargin = st::defaultEmojiPan.searchMargin;
	const auto width = st::topicSwitcherPanelWidth;
	const auto searchTop = searchMargin.top();
	_search->resizeToWidth(width - searchMargin.left() - searchMargin.right());
	const auto searchHeight = _search->height();
	const auto listTop = searchTop + searchHeight + searchMargin.bottom();
	// 列表底部留出圆角高度，行背景不会画出圆角外。
	const auto bottomSkip = st::emojiPanRadius;
	const auto available = std::min(
		st::topicSwitcherPanelMaxHeight,
		_maxBottom - y() - margins.top() - margins.bottom());
	_list->resizeToWidth(width);
	const auto listHeight = std::min(
		_list->height(),
		std::max(available - listTop - bottomSkip, 0));
	const auto inner = listTop + listHeight + bottomSkip;
	resize(
		margins.left() + width + margins.right(),
		margins.top() + inner + margins.bottom());
	_search->moveToLeft(
		margins.left() + searchMargin.left(),
		margins.top() + searchTop);
	_scroll->setGeometry(
		margins.left(),
		margins.top() + listTop,
		width,
		listHeight);
	const auto top = _scroll->scrollTop();
	_list->setVisibleTopBottom(top, top + listHeight);
	update();
}

void Panel::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	if (_a_show.animating()) {
		if (const auto opacity = _a_opacity.value(_hiding ? 0. : 1.)) {
			_showAnimation->paintFrame(
				p,
				0,
				0,
				width(),
				_a_show.value(1.),
				opacity);
		}
	} else if (_a_opacity.animating()) {
		p.setOpacity(_a_opacity.value(0.));
		p.drawPixmap(0, 0, _cache);
	} else if (!_hiding) {
		const auto inner = innerRect();
		_shadow.paint(p, inner, st::emojiPanRadius);
		_roundRect.paint(p, inner);
	}
}

QImage Panel::grabForAnimation() {
	Ui::SendPendingMoveResizeEvents(this);
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	auto p = QPainter(&result);
	_roundRect.paint(p, innerRect());
	for (const auto widget : { (QWidget*)_search, (QWidget*)_scroll }) {
		Ui::RenderWidget(p, widget, widget->pos());
	}
	p.end();
	return result;
}

void Panel::startShowAnimation() {
	const auto ratio = style::DevicePixelRatio();
	const auto inner = innerRect();
	_showAnimation = std::make_unique<Ui::PanelAnimation>(
		st::emojiPanAnimation,
		Ui::PanelAnimation::Origin::TopLeft);
	_showAnimation->setFinalImage(
		grabForAnimation(),
		QRect(inner.topLeft() * ratio, inner.size() * ratio),
		st::emojiPanRadius);
	_showAnimation->setCornerMasks(Images::CornersMask(st::emojiPanRadius));
	_showAnimation->start();
	hideChildren();
	_a_show.start(
		[=] { showAnimationCallback(); },
		0.,
		1.,
		st::emojiPanShowDuration);
}

void Panel::showAnimationCallback() {
	update();
	if (_a_show.animating()) {
		return;
	}
	_showAnimation = nullptr;
	if (!_hiding) {
		showChildren();
		_search->stealFocus();
	}
}

void Panel::hideAnimated() {
	if (_hiding || isHidden()) {
		return;
	}
	_search->returnFocus();
	if (_a_show.animating()) {
		_a_show.stop();
		_showAnimation = nullptr;
		showChildren();
	}
	_cache = Ui::GrabWidget(this);
	_hiding = true;
	hideChildren();
	_a_opacity.start(
		[=] { opacityAnimationCallback(); },
		1.,
		0.,
		st::emojiPanDuration);
	if (const auto onstack = _hidingCallback) {
		onstack();
	}
}

void Panel::opacityAnimationCallback() {
	update();
	if (_a_opacity.animating()) {
		return;
	}
	_cache = QPixmap();
	hide();
	deleteLater();
}

void Panel::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Down) {
		_list->selectSkip(1);
	} else if (e->key() == Qt::Key_Up) {
		_list->selectSkip(-1);
	} else {
		RpWidget::keyPressEvent(e);
	}
}

// 只看本窗口内的点击，弹出菜单等独立窗口不会误关面板。
bool Panel::outside(QObject *o) const {
	if (!o->isWidgetType()) {
		return false;
	}
	const auto widget = static_cast<QWidget*>(o);
	const auto inside = [&](not_null<const QWidget*> parent) {
		return (widget == parent) || parent->isAncestorOf(widget);
	};
	return (widget->window() == window())
		&& !inside(this)
		&& !inside(_toggle);
}

bool Panel::eventFilter(QObject *o, QEvent *e) {
	if (e->type() == QEvent::MouseButtonPress && !_hiding && outside(o)) {
		hideAnimated();
	}
	return RpWidget::eventFilter(o, e);
}

} // namespace ExtrasFeatures::TopicSwitcher
