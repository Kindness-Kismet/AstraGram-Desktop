/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_subsection_tabs.h"

#include "core/ui_integration.h"
#include "data/data_changes.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_saved_messages.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_row.h"
#include "extras/features/topic_switcher/topic_switcher_button.h"
#include "extras/features/topic_switcher/topic_switcher_panel.h"
#include "history/history.h"
#include "history/view/history_view_top_bar_widget.h"
#include "main/main_session.h"
#include "ui/ui_utility.h"
#include "window/window_separate_id.h"
#include "window/window_session_controller.h"
#include "styles/style_extras_styles.h"

namespace HistoryView {

SubsectionTabs::SubsectionTabs(
	not_null<Window::SessionController*> controller,
	not_null<TopBarWidget*> topBar,
	not_null<Data::Thread*> thread)
: _controller(controller)
, _history(thread->owningHistory())
, _active(thread)
, _button(base::make_unique_q<Button>(topBar.get(), [=] {
	return controller->isGifPausedAtLeastFor(Window::GifPauseReason::Any);
})) {
	_button->setClickedCallback([=] {
		togglePanel();
	});
	track();
	attach(topBar);
	refreshButton();
}

SubsectionTabs::~SubsectionTabs() {
	if (const auto panel = base::take(_panel)) {
		panel->hide();
		panel->deleteLater();
	}
	if (_topBar) {
		_topBar->setSubsectionSwitcher(nullptr);
	}
}

Main::Session &SubsectionTabs::session() {
	return _history->session();
}

void SubsectionTabs::attach(not_null<TopBarWidget*> topBar) {
	_topBarLifetime.destroy();
	_topBar = topBar.get();
	_button->setParent(topBar);
	topBar->setSubsectionSwitcher(_button.get());

	// 顶栏尺寸变化后按钮位置也会变，面板直接收起。
	topBar->sizeValue(
	) | rpl::skip(1) | rpl::on_next([=] {
		closePanel();
	}, _topBarLifetime);
}

void SubsectionTabs::track() {
	using Event = Data::Session::ChatListEntryRefresh;
	if (const auto forum = _history->peer->forum()) {
		forum->topicsList()->unreadStateChanges(
		) | rpl::on_next([=] {
			scheduleRefresh();
		}, _lifetime);

		forum->owner().chatListEntryRefreshes(
		) | rpl::filter([=](const Event &event) {
			const auto topic = event.filterId ? nullptr : event.key.topic();
			return (topic && topic->forum() == forum);
		}) | rpl::on_next([=] {
			scheduleRefresh();
		}, _lifetime);

		forum->session().changes().topicUpdates(
			Data::TopicUpdate::Flag::Title
			| Data::TopicUpdate::Flag::IconId
			| Data::TopicUpdate::Flag::ColorId
		) | rpl::filter([=](const Data::TopicUpdate &update) {
			return update.topic->forum() == forum;
		}) | rpl::on_next([=] {
			scheduleRefresh();
		}, _lifetime);
	} else if (const auto monoforum = _history->peer->monoforum()) {
		monoforum->chatsList()->unreadStateChanges(
		) | rpl::on_next([=] {
			scheduleRefresh();
		}, _lifetime);

		monoforum->owner().chatListEntryRefreshes(
		) | rpl::filter([=](const Event &event) {
			const auto sublist = event.filterId
				? nullptr
				: event.key.sublist();
			return (sublist && sublist->parent() == monoforum);
		}) | rpl::on_next([=] {
			scheduleRefresh();
		}, _lifetime);
	} else {
		Unexpected("Peer in SubsectionTabs::track.");
	}
}

void SubsectionTabs::scheduleRefresh() {
	if (_refreshScheduled) {
		return;
	}
	_refreshScheduled = true;
	InvokeQueued(_button.get(), [=] {
		if (_refreshScheduled) {
			refreshButton();
		}
	});
}

void SubsectionTabs::refreshButton() {
	_refreshScheduled = false;

	auto label = ExtrasFeatures::TopicSwitcher::ThreadLabel(_active);

	// 只统计有未读数的其他话题，全部静音时徽标用灰色。
	const auto forum = _history->peer->forum();
	const auto list = forum
		? forum->topicsList()
		: _history->peer->monoforum()->chatsList();
	auto unread = 0;
	auto muted = true;
	for (const auto &row : list->indexed()->all()) {
		const auto thread = row->thread();
		if (!thread || thread == _active) {
			continue;
		}
		const auto badges = thread->chatListBadgesState();
		if (badges.unreadCounter > 0) {
			unread += badges.unreadCounter;
			muted = muted && badges.unreadMuted;
		}
	}
	_button->setContent({
		.label = std::move(label),
		.context = Core::TextContext({ .session = &session() }),
		.unread = unread,
		.unreadMuted = muted,
	});
}

void SubsectionTabs::togglePanel() {
	if (_panel) {
		closePanel();
		return;
	} else if (!_topBar) {
		return;
	}
	const auto parent = _topBar->parentWidget();
	using Descriptor = ExtrasFeatures::TopicSwitcher::PanelDescriptor;
	_panel = Ui::CreateChild<Panel>(parent, Descriptor{
		.controller = _controller,
		.history = _history,
		.active = _active,
		.toggle = _button.get(),
		.chosen = crl::guard(this, [=](
				not_null<Data::Thread*> thread,
				bool newWindow) {
			choose(thread, newWindow);
		}),
		.hiding = crl::guard(this, [=] {
			_panel = nullptr;
			_button->setOpened(false);
		}),
	});
	const auto skip = st::topicSwitcherPanelSkip;
	const auto left = _button->mapTo(parent, QPoint()).x();
	const auto maxLeft = parent->width() - skip - st::topicSwitcherPanelWidth;
	_panel->showAt(
		{ std::max(std::min(left, maxLeft), skip),
			_topBar->bottomNoMargins() + skip },
		parent->height() - skip);
	_button->setOpened(true);
}

void SubsectionTabs::closePanel() {
	if (const auto panel = base::take(_panel)) {
		panel->hideAnimated();
	}
	_button->setOpened(false);
}

void SubsectionTabs::choose(not_null<Data::Thread*> thread, bool newWindow) {
	closePanel();
	if (!newWindow && thread == _active) {
		return;
	}
	// 选中发生在面板的事件处理里，切换会话会销毁面板所在的会话区，放到下一轮执行。
	const auto weak = base::make_weak(thread);
	InvokeQueued(_button.get(), [=] {
		const auto strong = weak.get();
		if (!strong) {
			return;
		} else if (newWindow) {
			_controller->showInNewWindow(Window::SeparateId(strong));
			return;
		}
		auto params = Window::SectionShow();
		params.way = Window::SectionShow::Way::ClearStack;
		params.animated = anim::type::instant;
		_controller->showThread(strong, ShowAtUnreadMsgId, params);
	});
}

bool SubsectionTabs::dying() const {
	return !UsedFor(_history);
}

rpl::producer<> SubsectionTabs::removeRequests() const {
	if (const auto forum = _history->peer->forum()) {
		return forum->destroyed();
	} else if (const auto monoforum = _history->peer->monoforum()) {
		return monoforum->destroyed();
	} else {
		Unexpected("Peer in SubsectionTabs::removeRequests.");
	}
}

void SubsectionTabs::extractToParent(not_null<Ui::RpWidget*> parent) {
	if (const auto panel = base::take(_panel)) {
		panel->hide();
		panel->deleteLater();
	}
	_button->setOpened(false);
	_topBarLifetime.destroy();
	if (const auto topBar = base::take(_topBar)) {
		topBar->setSubsectionSwitcher(nullptr);
	}
	_button->hide();
	_button->setParent(parent);
}

void SubsectionTabs::raise() {
	if (_panel) {
		_panel->raise();
	}
}

bool SubsectionTabs::switchTo(
		not_null<Data::Thread*> thread,
		not_null<TopBarWidget*> topBar) {
	if (thread->owningHistory() != _history) {
		return false;
	}
	_active = thread;
	attach(topBar);
	refreshButton();
	return true;
}

bool SubsectionTabs::UsedFor(not_null<Data::Thread*> thread) {
	const auto history = thread->owningHistory();
	return history->amMonoforumAdmin()
		|| history->peer->displaySubsectionTabs();
}

} // namespace HistoryView
