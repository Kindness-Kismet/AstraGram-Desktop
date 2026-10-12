/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "base/weak_ptr.h"

class History;

namespace Data {
class Thread;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class RpWidget;
} // namespace Ui

namespace ExtrasFeatures::TopicSwitcher {
class Button;
class Panel;
} // namespace ExtrasFeatures::TopicSwitcher

namespace HistoryView {

class TopBarWidget;

// 标签论坛的话题切换：顶栏第二行的按钮与下拉话题面板。
class SubsectionTabs final : public base::has_weak_ptr {
public:
	SubsectionTabs(
		not_null<Window::SessionController*> controller,
		not_null<TopBarWidget*> topBar,
		not_null<Data::Thread*> thread);
	~SubsectionTabs();

	[[nodiscard]] Main::Session &session();

	[[nodiscard]] bool switchTo(
		not_null<Data::Thread*> thread,
		not_null<TopBarWidget*> topBar);

	[[nodiscard]] static bool UsedFor(not_null<Data::Thread*> thread);

	[[nodiscard]] bool dying() const;
	[[nodiscard]] rpl::producer<> removeRequests() const;

	void extractToParent(not_null<Ui::RpWidget*> parent);

	// 会话区重排子控件后调用，保持面板在最上层。
	void raise();

private:
	using Button = ExtrasFeatures::TopicSwitcher::Button;
	using Panel = ExtrasFeatures::TopicSwitcher::Panel;

	void attach(not_null<TopBarWidget*> topBar);
	void track();
	void scheduleRefresh();
	void refreshButton();
	void togglePanel();
	void closePanel();
	void choose(not_null<Data::Thread*> thread, bool newWindow);

	const not_null<Window::SessionController*> _controller;
	const not_null<History*> _history;
	not_null<Data::Thread*> _active;

	QPointer<TopBarWidget> _topBar;
	base::unique_qptr<Button> _button;
	QPointer<Panel> _panel;
	bool _refreshScheduled = false;

	rpl::lifetime _topBarLifetime;
	rpl::lifetime _lifetime;

};

} // namespace HistoryView
