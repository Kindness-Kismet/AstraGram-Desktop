#include "extras/features/topic_switcher/topic_switcher_panel.h"

#include "base/unique_qptr.h"
#include "core/ui_integration.h"
#include "data/data_changes.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_saved_messages.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_row.h"
#include "extras/features/topic_switcher/topic_switcher_button.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/controls/tabbed_search.h"
#include "ui/effects/panel_animation.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"
#include "ui/ui_utility.h"
#include "ui/unread_badge_paint.h"
#include "ui/unread_counter_format.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
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

[[nodiscard]] QString AllLabel(not_null<History*> history) {
	return history->peer->forum()
		? tr::extras_TopicSwitcherAllTopics(tr::now)
		: tr::lng_filters_all(tr::now);
}

} // namespace

TextWithEntities ThreadLabel(not_null<Data::Thread*> thread) {
	if (const auto topic = thread->asTopic()) {
		return topic->titleWithIconOrLogo();
	} else if (const auto sublist = thread->asSublist()) {
		const auto peer = sublist->sublistPeer();
		const auto manager = &thread->owner().customEmojiManager();
		return Ui::Text::SingleCustomEmoji(
			manager->peerUserpicEmojiData(peer),
			u"@"_q
		).append(' ').append(peer->shortName());
	}
	return { AllLabel(thread->owningHistory()) };
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
	void paintRow(
		Painter &p,
		QRect geometry,
		not_null<Data::Thread*> thread,
		bool selected);
	void paintEmpty(Painter &p);
	[[nodiscard]] const Ui::Text::String &label(
		not_null<Data::Thread*> thread);

	[[nodiscard]] int count() const;
	[[nodiscard]] int indexAt(QPoint position) const;
	[[nodiscard]] Data::Thread *threadAt(int index) const;
	[[nodiscard]] bool ours(const Data::Thread *thread) const;

	const not_null<Window::SessionController*> _controller;
	const not_null<History*> _history;
	const not_null<Data::Thread*> _active;
	const Fn<void(not_null<Data::Thread*>, bool)> _chosen;
	struct Label {
		TextWithEntities source;
		Ui::Text::String text;
	};
	base::flat_map<not_null<Data::Thread*>, Label> _labels;
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
, _chosen(descriptor.chosen) {
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

	rpl::merge(
		session->changes().topicUpdates(
			Data::TopicUpdate::Flag::Title
			| Data::TopicUpdate::Flag::IconId
			| Data::TopicUpdate::Flag::ColorId
		) | rpl::to_empty,
		session->changes().peerUpdates(
			Data::PeerUpdate::Flag::Name
		) | rpl::to_empty
	) | rpl::on_next([=] {
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

	// 话题先发销毁事件再析构，这里同步移除，保证绘制时指针有效。
	ThreadDestroyed(
		_history
	) | rpl::on_next([=](not_null<Data::Thread*> thread) {
		_filtered.erase(
			ranges::remove(_filtered, thread),
			end(_filtered));
		_labels.remove(thread);
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
	return st::topicSwitcherRowHeight;
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
	const auto clip = e->rect();
	p.fillRect(clip, st::emojiPanBg);
	const auto rows = count();
	if (!rows) {
		paintEmpty(p);
		return;
	}
	const auto height = rowHeight();
	const auto from = std::max(clip.y() / height, 0);
	const auto till = std::min((clip.y() + clip.height() + height - 1) / height, rows);
	// 行与搜索框左右对齐。
	const auto &padding = st::topicSwitcherPanelPadding;
	const auto rowWidth = width() - padding.left() - padding.right();
	for (auto i = from; i != till; ++i) {
		paintRow(
			p,
			QRect(padding.left(), i * height, rowWidth, height),
			threadAt(i),
			(i == _selected));
	}
}

const Ui::Text::String &Panel::List::label(not_null<Data::Thread*> thread) {
	auto source = ThreadLabel(thread);
	auto &entry = _labels[thread];
	if (entry.source != source) {
		entry.text.setMarkedText(
			st::defaultTextStyle,
			source,
			kDefaultTextOptions,
			Core::TextContext({
				.session = &thread->session(),
				.repaint = [=] { update(); },
			}));
		entry.source = std::move(source);
	}
	return entry.text;
}

void Panel::List::paintRow(
		Painter &p,
		QRect geometry,
		not_null<Data::Thread*> thread,
		bool selected) {
	const auto active = (thread == _active);
	if (active || selected) {
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(active ? st::lightButtonBgOver : st::dialogsBgOver);
		const auto radius = st::topicSwitcherRowRadius;
		p.drawRoundedRect(
			geometry.marginsRemoved(st::topicSwitcherRowMargin),
			radius,
			radius);
	}
	const auto &padding = st::topicSwitcherRowPadding;
	auto left = geometry.x() + padding.left();
	auto right = geometry.x() + geometry.width() - padding.right();
	const auto badges = (thread == _history)
		? Dialogs::BadgesState()
		: thread->chatListBadgesState();
	if (badges.unreadCounter > 0) {
		const auto style = UnreadBadge(badges.unreadMuted);
		const auto text = FormatUnreadCounter(badges.unreadCounter, false);
		right -= Ui::CountUnreadBadgeSize(text, style).width();
		Ui::PaintUnreadBadge(
			p,
			text,
			right,
			geometry.y() + (geometry.height() - style.size) / 2,
			style);
		right -= st::topicSwitcherSkip;
	}
	// 全部入口的图标占一个表情的宽度，与话题图标对齐。
	const auto &font = st::normalFont;
	if (thread == _history) {
		const auto icon = st::emojiSize + 2 * st::emojiPadding;
		st::topicSwitcherAllIcon.paintInCenter(
			p,
			QRect(left, geometry.y(), icon, geometry.height()));
		left += icon + font->spacew;
	}
	p.setPen(active ? st::lightButtonFg : st::windowBoldFg);
	label(thread).draw(p, {
		.position = {
			left,
			geometry.y() + (geometry.height() - font->height) / 2,
		},
		.outerWidth = width(),
		.availableWidth = std::max(right - left, 0),
		.now = crl::now(),
		.paused = _controller->isGifPausedAtLeastFor(
			Window::GifPauseReason::Any),
		.elisionLines = 1,
	});
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
	st::defaultEmojiPan.search, true))
, _scroll(Ui::CreateChild<Ui::ScrollArea>(this, st::defaultScrollArea))
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
	const auto &padding = st::topicSwitcherPanelPadding;
	const auto width = st::topicSwitcherPanelWidth;
	_search->resizeToWidth(width - padding.left() - padding.right());
	const auto listTop = padding.top()
		+ _search->height()
		+ st::topicSwitcherSearchSkip;
	const auto bottomSkip = padding.bottom();
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
		margins.left() + padding.left(),
		margins.top() + padding.top());
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

// 只统计本窗口内的点击，弹出菜单等独立窗口忽略。
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
