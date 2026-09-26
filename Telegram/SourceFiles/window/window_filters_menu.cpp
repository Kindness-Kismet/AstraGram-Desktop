/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_filters_menu.h"

#include "ayu/features/window_material/window_material.h"
#include "menu/menu_mark_as_read.h"
#include "mainwindow.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "window/window_main_menu.h"
#include "window/window_peer_menu.h"
#include "window/window_shell_color.h"
#include "window/window_filters_favorite.h"
#include "main/main_session.h"
#include "base/event_filter.h"
#include "base/options.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/ui_integration.h"
#include "data/data_session.h"
#include "data/data_chat_filters.h"
#include "data/data_user.h"
#include "data/data_peer_values.h"
#include "data/data_premium_limits.h"
#include "data/data_unread_value.h"
#include "lang/lang_keys.h"
#include "ui/filter_icons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/vertical_layout_reorder.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "ui/boxes/confirm_box.h"
#include "ui/power_saving.h"
#include "ui/screen_reader_mode.h"
#include "ui/ui_utility.h"
#include "boxes/filters/edit_filter_box.h"
#include "boxes/choose_filter_box.h"
#include "boxes/premium_limits_box.h"
#include "settings/sections/settings_folders.h"
#include "storage/storage_media_prepare.h"
#include "api/api_chat_filters.h"
#include "apiwrap.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"
#include "styles/style_menu_icons.h"

#include <QtGui/QtEvents>
#include <QtGui/QPainterPath>

#include "ayu/ayu_settings.h"


namespace Window {
namespace {

// 向屏幕阅读器提供可选择的文件夹列表。
class TabListLayout final : public Ui::VerticalLayout {
public:
	using Ui::VerticalLayout::VerticalLayout;

	QAccessible::Role accessibilityRole() override {
		return QAccessible::List;
	}
	Qt::FocusPolicy accessibilityFocusPolicy() override {
		// 由无障碍层决定屏幕阅读器模式下的焦点入口。
		return Qt::ClickFocus;
	}
	std::optional<Qt::Orientation> accessibilityOrientation() const override {
		// 文件夹标签使用纵向排列。
		return Qt::Vertical;
	}
	bool accessibilitySelectionList() const override {
		// 单选与焦点转交仅作用于文件夹列表。
		return true;
	}
	std::vector<not_null<QWidget*>> accessibilityChildWidgets() const override {
		// 按拖动后的视觉顺序提供无障碍子项。
		auto result = std::vector<not_null<QWidget*>>();
		const auto rows = count();
		result.reserve(rows);
		for (auto i = 0; i != rows; ++i) {
			result.push_back(widgetAt(i).get());
		}
		return result;
	}
};

} // namespace

FiltersMenu::FiltersMenu(
	not_null<Ui::RpWidget*> parent,
	not_null<SessionController*> session)
: _session(session)
, _parent(parent)
, _outer(_parent)
, _menu(&_outer, TextWithEntities(), st::windowFiltersMainMenu)
, _scroll(&_outer)
, _container(
	_scroll.setOwnedWidget(
		object_ptr<Ui::VerticalLayout>(&_scroll))) {

	_drag.timer.setCallback([=] {
		if (_drag.filterId >= 0) {
			_session->setActiveChatsFilter(_drag.filterId);
		}
	});
	setup();
}

FiltersMenu::~FiltersMenu() = default;

void FiltersMenu::setup() {
	_outer.setObjectName(u"chatFolders.sidebar"_q);
	_menu.setObjectName(u"chatFolders.menu"_q);
	setupDragAndDrop();
	setupMainMenuIcon();
	_menu.setIsMenuButton(true);
	_menu.setAccessibleName(tr::lng_main_menu(tr::now));

	_outer.setAttribute(Qt::WA_OpaquePaintEvent);
	AyuFeatures::WindowMaterial::watchSurface(&_outer);
	AyuFeatures::WindowMaterial::watchSurface(&_menu);
	_outer.show();

	// 菜单、文件夹、收藏与设置的焦点顺序跟随视觉排列。
	_outer.setVisualTabOrder(true);
	_container->setVisualTabOrder(true);
	const auto gap = st::windowCardGap;
	_parent->heightValue() | rpl::on_next([=](int height) {
		const auto width = st::windowFiltersWidth;
		const auto innerWidth = width - gap;
		_outer.setGeometry(0, 0, width, height);
		_menu.resizeToWidth(innerWidth);
		_menu.move(gap, gap);
		_scroll.setGeometry(gap, gap + _menu.height(), innerWidth,
			std::max(0, height - gap * 2 - _menu.height()));
		_container->resizeToWidth(innerWidth);
	}, _outer.lifetime());
	_outer.paintRequest() | rpl::on_next([=](QRect clip) {
		auto p = QPainter(&_outer);
		p.fillRect(clip, AyuFeatures::WindowMaterial::surfaceColor(
			&_outer, ShellBackgroundColor(&_outer)->c));
		p.fillRect(QRect(gap, gap, _outer.width() - gap,
			_outer.height() - 2 * gap), AyuFeatures::WindowMaterial::surfaceColor(
				&_outer, st::windowBg->c));
	}, _outer.lifetime());

	// 菜单悬停会重绘不透明按钮，圆角遮罩必须跟随按钮一起绘制。
	const auto corner = Ui::CreateChild<Ui::RpWidget>(&_menu);
	corner->setAttribute(Qt::WA_TransparentForMouseEvents);
	corner->resize(st::windowCardRadius, st::windowCardRadius);
	corner->paintRequest() | rpl::on_next([=] {
		const auto radius = st::windowCardRadius;
		auto square = QPainterPath();
		square.addRect(QRect(0, 0, radius, radius));
		auto rounded = QPainterPath();
		rounded.addEllipse(QRect(0, 0, radius * 2, radius * 2));
		auto p = QPainter(corner);
		p.setRenderHint(QPainter::Antialiasing);
		const auto material = AyuFeatures::WindowMaterial::isActive(&_outer);
		if (material) {
			p.setCompositionMode(QPainter::CompositionMode_Source);
		}
		p.fillPath(square.subtracted(rounded), material
			? AyuFeatures::WindowMaterial::rootTintColor(&_outer)
			: ShellBackgroundColor(&_outer)->c);
	}, corner->lifetime());
	corner->show();

	const auto overlay = Ui::CreateChild<Ui::RpWidget>(&_outer);
	overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
	_outer.sizeValue() | rpl::on_next([=](QSize size) {
		overlay->resize(size);
		overlay->raise();
	}, overlay->lifetime());
	overlay->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(overlay);
		p.fillRect(_outer.width() - st::lineWidth, gap, st::lineWidth,
			_outer.height() - 2 * gap, st::windowDividerFg);
	}, overlay->lifetime());
	overlay->show();

	auto premium = Data::AmPremiumValue(&_session->session());

	const auto filters = &_session->session().data().chatsFilters();
	rpl::combine(
		rpl::single(rpl::empty) | rpl::then(filters->changed()),
		std::move(premium)
	) | rpl::on_next([=] {
		refresh();
	}, _outer.lifetime());

	_activeFilterId = _session->activeChatsFilterCurrent();
	_session->activeChatsFilter(
	) | rpl::filter([=](FilterId id) {
		return (id != _activeFilterId);
	}) | rpl::on_next([=](FilterId id) {
		if (!_list) {
			_activeFilterId = id;
			return;
		}
		const auto i = _filters.find(_activeFilterId);
		if (i != end(_filters)) {
			i->second->setActive(false);
		}
		_activeFilterId = id;
		const auto j = _filters.find(_activeFilterId);
		if (j != end(_filters)) {
			j->second->setActive(true);
			scrollToButton(j->second);
		}
		_reorder->finishReordering();
	}, _outer.lifetime());

	_menu.setClickedCallback([=] {
		_session->widget()->showMainMenu();
	});

	Core::App().settings().chatFiltersTabsModeValue(
	) | rpl::skip(1) | rpl::on_next([=] {
		if (!_list) {
			return;
		}
		_setup = prepareButton(
			_container,
			-1,
			{ TextWithEntities{ tr::lng_filters_setup(tr::now) } },
			Ui::FilterIcon::Edit);
		if (_favorite) {
			_favorite = nullptr;
			updateFavorite();
		}
		refresh();
	}, _outer.lifetime());
}

void FiltersMenu::setupDragAndDrop() {
	SetupFilterDragAndDrop(
		&_outer,
		&_session->session(),
		[=](QPoint globalPos) -> std::optional<FilterId> {
			if (!_list) {
				return std::nullopt;
			}
			const auto localPos = _list->mapFromGlobal(globalPos);
			for (const auto &[id, button] : _filters) {
				if (button->geometry().contains(localPos)) {
					return id;
				}
			}
			return std::nullopt;
		},
		[=] { return _activeFilterId; },
		[=](FilterId filterId) {
			for (const auto &[id, button] : _filters) {
				button->setForceRippled(id == filterId);
			}
		});
}

void FiltersMenu::setupMainMenuIcon() {
	OtherAccountsUnreadState(
		&_session->session().account()
	) | rpl::on_next([=](const OthersUnreadState &state) {
		auto icon = !state.count
			? nullptr
			: !state.allMuted
			? &st::windowFiltersMainMenuUnread
			: &st::windowFiltersMainMenuUnreadMuted;

		const auto &settings = AyuSettings::getInstance();
		if (settings.hideNotificationCounters()) {
			icon = nullptr;
		}

		_menu.setIconOverride(icon, icon);
	}, _outer.lifetime());
}

void FiltersMenu::scrollToButton(not_null<Ui::RpWidget*> widget) {
	const auto globalPosition = widget->mapToGlobal(QPoint(0, 0));
	const auto localTop = _scroll.mapFromGlobal(globalPosition).y();
	const auto localBottom = localTop + widget->height() - _scroll.height();
	const auto isTopEdge = (localTop < 0);
	const auto isBottomEdge = (localBottom > 0);
	if (!isTopEdge && !isBottomEdge) {
		return;
	}

	_scrollToAnimation.stop();
	const auto scrollTop = _scroll.scrollTop();
	const auto scrollTo = scrollTop + (isBottomEdge ? localBottom : localTop);

	auto scroll = [=] {
		const auto animated
			= int(base::SafeRound(_scrollToAnimation.value(scrollTo)));
		_scroll.scrollToY(animated);
	};

	_scrollToAnimation.start(
		std::move(scroll),
		scrollTop,
		scrollTo,
		st::slideDuration,
		anim::sineInOut);
}

void FiltersMenu::applyFilterAt(int start, int delta) {
	const auto &list = _session->session().data().chatsFilters().list();
	const auto count = int(list.size());
	// 方向键仅移动焦点且不循环，回车才激活文件夹。
	for (auto index = start; index >= 0 && index < count; index += delta) {
		const auto i = _filters.find(list[index].id());
		if (i != end(_filters)) {
			const auto raw = i->second.get();
			// 方向键只移动焦点，需要另行滚动以保持目标可见。
			raw->setFocus();
			scrollToButton(raw);
			return;
		}
	}
}

void FiltersMenu::moveToFilter(int delta) {
	const auto &list = _session->session().data().chatsFilters().list();
	const auto count = int(list.size());
	// 从当前焦点继续导航；没有焦点时从已选文件夹开始。
	auto current = 0;
	for (auto i = 0; i != count; ++i) {
		const auto it = _filters.find(list[i].id());
		if (it != end(_filters) && it->second->hasFocus()) {
			current = i;
			break;
		} else if (list[i].id() == _activeFilterId) {
			current = i;
		}
	}
	applyFilterAt(current + delta, delta);
}

void FiltersMenu::moveToFilterEdge(int delta) {
	const auto count = int(
		_session->session().data().chatsFilters().list().size());
	applyFilterAt((delta > 0) ? 0 : (count - 1), delta);
}

void FiltersMenu::setListTabStop(not_null<Ui::SideBarButton*> stop) {
	// 列表只保留一个可由 Tab 进入的文件夹。
	if (const auto previous = _tabStop.get(); previous && previous != stop) {
		previous->setFocusPolicy(Qt::ClickFocus);
	}
	stop->setFocusPolicy(Qt::TabFocus);
	_tabStop = stop.get();

	// 焦点入口变化后同步视觉顺序，避免 Tab 使用旧位置。
	_outer.refreshVisualTabOrder();
	_container->refreshVisualTabOrder();
}

bool FiltersMenu::listFocused() const {
	for (const auto &[id, button] : _filters) {
		if (button->hasFocus()) {
			return true;
		}
	}
	return false;
}

void FiltersMenu::refresh() {
	// 隐藏全部聊天时不保留对应入口。
	const auto &settings = AyuSettings::getInstance();

	const auto filters = &_session->session().data().chatsFilters();
	if (!filters->has() || _ignoreRefresh) {
		return;
	}
	const auto oldTop = _scroll.scrollTop();
	const auto reorderAll = premium();
	if (!_list) {
		setupList();
	}
	_reorder->cancel();

	_reorder->clearPinnedIntervals();
	const auto maxLimit = (reorderAll ? 1 : 0)
		+ Data::PremiumLimits(&_session->session()).dialogFiltersCurrent();
	const auto premiumFrom = (reorderAll ? 0 : 1) + maxLimit;
	if (!reorderAll && !settings.hideAllChatsFolder()) {
		_reorder->addPinnedInterval(0, 1);
	}
	_reorder->addPinnedInterval(
		premiumFrom,
		std::max(1, int(filters->list().size()) - maxLimit));

	// 重建前保留焦点所在文件夹，便于恢复唯一焦点入口。
	auto focusedId = std::optional<FilterId>();
	for (const auto &[id, button] : _filters) {
		if (button->hasFocus()) {
			focusedId = id;
			break;
		}
	}

	auto now = base::flat_map<int, base::unique_qptr<Ui::SideBarButton>>();
	const auto &currentFilter = _session->activeChatsFilterCurrent();
	for (const auto &filter : filters->list()) {
		const auto nextIsLocked = (now.size() >= premiumFrom);
		if (nextIsLocked && (currentFilter == filter.id())) {
			_session->setActiveChatsFilter(FilterId(0));
		}
		auto button = prepareButton(
			_list,
			filter.id(),
			filter.title(),
			Ui::ComputeFilterIcon(filter),
			nextIsLocked);
		now.emplace(filter.id(), std::move(button));
	}
	_filters = std::move(now);
	// 重建后优先恢复原焦点，否则使用当前选中的文件夹。
	auto refocus = (Ui::SideBarButton*)nullptr;
	if (Ui::ScreenReaderModeActive()) {
		auto i = focusedId ? _filters.find(*focusedId) : end(_filters);
		if (i == end(_filters)) {
			i = _filters.find(_activeFilterId);
		}
		if (i != end(_filters)) {
			setListTabStop(i->second.get());
			// 重建会销毁旧按钮，滚动恢复后再将焦点交给新按钮。
			if (focusedId) {
				refocus = i->second.get();
			}
		}
	}
	_reorder->start();

	_container->resizeToWidth(_outer.width() - st::windowCardGap);

	// 刷新会重置滚动位置，需要恢复。
	_scroll.scrollToY(oldTop);

	if (settings.hideAllChatsFolder()
		&& _session->widget()->sessionContent()) {
		_session->setActiveChatsFilter(filters->lookupId(0));
	}

	if (refocus) {
		refocus->setFocus();
		scrollToButton(refocus);
	}
}

void FiltersMenu::setupList() {
	_list = _container->add(object_ptr<TabListLayout>(_container));
	_list->setAccessibleName(tr::lng_filters_title(tr::now));
	_setup = prepareButton(
		_container,
		-1,
		{ TextWithEntities{ tr::lng_filters_setup(tr::now) } },
		Ui::FilterIcon::Edit);
	_reorder = std::make_unique<Ui::VerticalLayoutReorder>(_list, &_scroll);

	_reorder->updates(
	) | rpl::on_next([=](Ui::VerticalLayoutReorder::Single data) {
		using State = Ui::VerticalLayoutReorder::State;
		if (data.state == State::Started) {
			++_reordering;
		} else {
			Ui::PostponeCall(&_outer, [=] {
				--_reordering;
			});
			if (data.state == State::Applied) {
				applyReorder(data.widget, data.oldPosition, data.newPosition);
			}
		}
	}, _outer.lifetime());

	base::options::lookup<QString>(kOptionFolderFavoriteLink).changes(
	) | rpl::on_next([=] {
		updateFavorite();
	}, _outer.lifetime());
	updateFavorite();
}

void FiltersMenu::updateFavorite() {
	const auto link = base::options::lookup<QString>(
		kOptionFolderFavoriteLink).value().trimmed();
	if (link.isEmpty()) {
		if (_favorite && _favorite->toggled()) {
			// 隐藏动画结束后再销毁。
			_favorite->toggle(false, anim::type::normal);
		} else if (_favorite) {
			// 尚未显示时直接安排销毁。
			destroyFavorite();
		}
		return;
	}
	if (!_favorite) {
		createFavorite();
	}
	const auto button = _favorite->entity();
	button->setLink(link);
	if (button->shown() && !_favorite->toggled()) {
		_favorite->toggle(true, anim::type::normal);
	}
}

void FiltersMenu::createFavorite() {
	_favorite = base::unique_qptr<Ui::SlideWrap<FolderFavoriteButton>>(
		_container->insert(
			_container->count() - 1,
			object_ptr<Ui::SlideWrap<FolderFavoriteButton>>(
				_container,
				object_ptr<FolderFavoriteButton>(
					_container,
					_session,
					buttonStyle()))));
	_favorite->toggle(false, anim::type::instant);
	_favorite->setFinishedCallback([=] {
		if (_favorite && !_favorite->toggled()) {
			destroyFavorite();
		}
	});
	_favorite->entity()->shownValue(
	) | rpl::on_next([=](bool shown) {
		if (_favorite && shown) {
			_favorite->toggle(true, anim::type::normal);
		}
	}, _favorite->lifetime());
}

void FiltersMenu::destroyFavorite() {
	Ui::PostponeCall(&_outer, [=] {
		const auto empty = base::options::lookup<QString>(
			kOptionFolderFavoriteLink).value().trimmed().isEmpty();
		if (_favorite && !_favorite->toggled() && empty) {
			_favorite = nullptr;
		}
	});
}

bool FiltersMenu::premium() const {
	return _session->session().user()->isPremium();
}

Ui::ChatsFiltersTabsMode FiltersMenu::tabsMode() const {
	return Ui::VerticalChatsFiltersTabsMode(
		Core::App().settings().chatFiltersTabsMode());
}

const style::SideBarButton &FiltersMenu::buttonStyle() const {
	using Mode = Ui::ChatsFiltersTabsMode;
	switch (tabsMode()) {
	case Mode::TextOnly: return st::windowFiltersButtonTextOnly;
	case Mode::TextAndIcons: return st::windowFiltersButton;
	case Mode::IconsOnly: return st::windowFiltersButtonIconsOnly;
	}
	return st::windowFiltersButton;
}

base::unique_qptr<Ui::SideBarButton> FiltersMenu::prepareButton(
		not_null<Ui::VerticalLayout*> container,
		FilterId id,
		Data::ChatFilterTitle title,
		Ui::FilterIcon icon,
		bool locked) {
	const auto isStatic = title.isStatic;
	const auto paused = [=] {
		return On(PowerSaving::kEmojiChat)
			|| _session->isGifPausedAtLeastFor(Window::GifPauseReason::Any);
	};
	// 文件夹作为可选择列表项；插入前设置角色，避免无障碍角色闪变。
	const auto listItem = (id >= 0);
	const auto mode = tabsMode();
	auto prepared = object_ptr<Ui::SideBarButton>(
		container,
		id ? title.text : TextWithEntities{ tr::lng_filters_all(tr::now) },
		buttonStyle(),
		Core::TextContext({
			.session = &_session->session(),
			.customEmojiLoopLimit = isStatic ? -1 : 0,
		}),
		paused);
	prepared->setLocked(locked);
	prepared->setIsListItem(listItem);
	prepared->setShowIcon(mode != Ui::ChatsFiltersTabsMode::TextOnly);
	prepared->setShowText(mode != Ui::ChatsFiltersTabsMode::IconsOnly);
	auto added = container->add(std::move(prepared));
	auto button = base::unique_qptr<Ui::SideBarButton>(std::move(added));
	AyuFeatures::WindowMaterial::watchSurface(button.get());
	const auto raw = button.get();
	raw->setObjectName(u"chatFolders.folder.%1"_q.arg(id));
	const auto nameText = id
		? title.text.text
		: tr::lng_filters_all(tr::now);
	const auto &icons = Ui::LookupFilterIcon(id
		? icon
		: Ui::FilterIcon::All);
	raw->setIconOverride(icons.normal, icons.active);
	if (id >= 0) {
		if (locked) {
			// 向屏幕阅读器说明锁定状态及点击后果。
			raw->setAccessibleName(
				tr::lng_sr_folder_locked(tr::now, lt_text, nameText));
			raw->setAccessibleDescription(
				tr::lng_sr_folder_locked_about(tr::now));
		}
		rpl::combine(
			Data::UnreadStateValue(&_session->session(), id),
			Data::IncludeMutedCounterFoldersValue(),
			AyuSettings::getInstance().hideNotificationCountersValue()
		) | rpl::on_next([=](
				const Dialogs::UnreadState &state,
				bool includeMuted,
				bool hideCounters) {
			const auto chats = state.chats;
			const auto chatsMuted = state.chatsMuted;
			auto muted = (chatsMuted + state.marksMuted);
			auto count = (chats + state.marks)
				- (includeMuted ? 0 : muted);

			if (hideCounters) {
				count = 0;
				muted = 0;
			}

			const auto string = !count
				? QString()
				: (count > 999)
				? "99+"
				: QString::number(count);
			raw->setBadge(string, includeMuted && (count == muted));
			if (!locked) {
				raw->setAccessibleName(count
					? tr::lng_filter_unread_chats(
						tr::now,
						lt_count,
						count,
						lt_text,
						nameText)
					: nameText);
			}
		}, raw->lifetime());
	}
	if (listItem) {
		// 屏幕阅读器模式下保留唯一 Tab 入口，并跟随选中项和焦点。
		rpl::combine(
			Ui::ScreenReaderModeActiveValue(),
			rpl::single(
				_session->activeChatsFilterCurrent()
			) | rpl::then(
				_session->activeChatsFilter()
			) | rpl::map([=](FilterId active) {
				return (active == id);
			}) | rpl::distinct_until_changed()
		) | rpl::on_next([=](bool screenReaderActive, bool selected) {
			if (!screenReaderActive) {
				raw->setFocusPolicy(Qt::NoFocus);
			} else if (selected && !listFocused()) {
				setListTabStop(raw);
			} else if (raw->focusPolicy() == Qt::NoFocus) {
				raw->setFocusPolicy(Qt::ClickFocus);
			}
		}, raw->lifetime());
		// 上下键移动焦点，首尾键定位边界，回车激活文件夹。
		base::install_event_filter(raw, [=](not_null<QEvent*> event) {
			if (event->type() == QEvent::FocusIn) {
				setListTabStop(raw);
				return base::EventFilterResult::Continue;
			} else if (event->type() != QEvent::KeyPress) {
				return base::EventFilterResult::Continue;
			}
			switch (static_cast<QKeyEvent*>(event.get())->key()) {
			case Qt::Key_Up: moveToFilter(-1); break;
			case Qt::Key_Down: moveToFilter(1); break;
			case Qt::Key_Home: moveToFilterEdge(1); break;
			case Qt::Key_End: moveToFilterEdge(-1); break;
			default: return base::EventFilterResult::Continue;
			}
			return base::EventFilterResult::Cancel;
		});
	}
	raw->setActive(_session->activeChatsFilterCurrent() == id);
	raw->setClickedCallback([=] {
		if (_reordering) {
			return;
		} else if (raw->locked()) {
			_session->show(Box(
				FiltersLimitBox,
				&_session->session(),
				std::nullopt));
		} else if (id >= 0) {
			_session->setActiveChatsFilter(id);
		} else {
			openFiltersSettings();
		}
	});
	if (id >= 0) {
		raw->setAcceptDrops(true);
		raw->events(
		) | rpl::filter([=](not_null<QEvent*> e) {
			return ((e->type() == QEvent::ContextMenu) && (id >= 0))
				|| e->type() == QEvent::DragEnter
				|| e->type() == QEvent::DragMove
				|| e->type() == QEvent::DragLeave;
		}) | rpl::on_next([=](not_null<QEvent*> e) {
			if (raw->locked()) {
				return;
			}
			if (e->type() == QEvent::ContextMenu) {
				showMenu(QCursor::pos(), id);
			} else if (e->type() == QEvent::DragEnter) {
				using namespace Storage;
				const auto d = static_cast<QDragEnterEvent*>(e.get());
				const auto data = d->mimeData();
				if (ComputeMimeDataState(data) != MimeDataState::None) {
					_drag.timer.callOnce(ChoosePeerByDragTimeout);
					_drag.filterId = id;
					d->setDropAction(Qt::CopyAction);
					d->accept();
				}
			} else if (e->type() == QEvent::DragMove) {
				_drag.timer.callOnce(ChoosePeerByDragTimeout);
			} else if (e->type() == QEvent::DragLeave) {
				_drag.filterId = FilterId(-1);
				_drag.timer.cancel();
			}
		}, raw->lifetime());
	}
	return button;
}

void FiltersMenu::openFiltersSettings() {
	const auto filters = &_session->session().data().chatsFilters();
	if (filters->suggestedLoaded()) {
		_session->showSettings(Settings::FoldersId());
	} else if (!_waitingSuggested) {
		_waitingSuggested = true;
		filters->requestSuggested();
		filters->suggestedUpdated(
		) | rpl::take(1) | rpl::on_next([=] {
			_session->showSettings(Settings::FoldersId());
		}, _outer.lifetime());
	}
}

void FiltersMenu::showMenu(QPoint position, FilterId id) {
	if (_popupMenu) {
		_popupMenu = nullptr;
		return;
	}
	const auto i = _filters.find(id);
	if ((i == end(_filters)) && id) {
		return;
	}
	_popupMenu = base::make_unique_q<Ui::PopupMenu>(
		i->second.get(),
		st::popupMenuWithIcons);
	const auto addAction = Ui::Menu::CreateAddActionCallback(_popupMenu);
	if (id) {
		addAction(
			tr::lng_filters_context_edit(tr::now),
			crl::guard(&_outer, [=] { EditExistingFilter(_session, id); }),
			&st::menuIconEdit);

		auto filteredChats = [=] {
			return _session->session().data().chatsFilters().chatsList(id);
		};
		MarkAsReadMenu::AddChatListAction(
			_session,
			MarkAsReadMenu::ChatListKind::Folder,
			std::move(filteredChats),
			addAction);

		addAction({
			.text = tr::lng_filters_context_remove(tr::now),
			.handler = crl::guard(&_outer, [=, this] {
				_removeApi.request(base::make_weak(&_outer), _session, id);
			}),
			.icon = &st::menuIconDeleteAttention,
			.isAttention = true,
		});
	} else {
		MarkAsReadMenu::AddChatListAction(
			_session,
			MarkAsReadMenu::ChatListKind::AllChats,
			[=] { return _session->session().data().chatsList(); },
			addAction);

		addAction(
			tr::lng_filters_setup_menu(tr::now),
			crl::guard(&_outer, [=] { openFiltersSettings(); }),
			&st::menuIconEdit);
	}
	if (_popupMenu->empty()) {
		_popupMenu = nullptr;
		return;
	}
	_popupMenu->popup(position);
}

void FiltersMenu::applyReorder(
		not_null<Ui::RpWidget*> widget,
		int oldPosition,
		int newPosition) {
	if (newPosition == oldPosition) {
		return;
	}

	// 隐藏全部聊天时不保留对应入口。
	const auto &settings = AyuSettings::getInstance();

	const auto filters = &_session->session().data().chatsFilters();
	const auto &list = filters->list();
	if (!settings.hideAllChatsFolder() && !premium()) {
		if (list[0].id() != FilterId()) {
			filters->moveAllToFront();
		}
	}
	Assert(oldPosition >= 0 && oldPosition < list.size());
	Assert(newPosition >= 0 && newPosition < list.size());
	const auto id = list[oldPosition].id();
	const auto i = _filters.find(id);
	Assert(i != end(_filters));
	Assert(i->second == widget);

	auto order = ranges::views::all(
		list
	) | ranges::views::transform(
		&Data::ChatFilter::id
	) | ranges::to_vector;
	base::reorder(order, oldPosition, newPosition);

	_ignoreRefresh = true;
	filters->saveOrder(order);
	_ignoreRefresh = false;
}

} // namespace Window
