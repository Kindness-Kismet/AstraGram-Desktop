/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/platform/win/base_windows_shlobj_h.h"
#include "base/platform/win/base_windows_winrt.h"
#include "base/timer.h"
#include "platform/platform_integration.h"
#include "base/timer.h"

#include <QAbstractNativeEventFilter>
#include <QtGui/QColor>
#include <rpl/variable.h>

namespace Platform {

class TaskbarButtons;

class WindowsIntegration final
	: public Integration
	, public QAbstractNativeEventFilter {
public:
	~WindowsIntegration();

	void init() override;

	[[nodiscard]] ITaskbarList3 *taskbarList() const;
	[[nodiscard]] std::optional<QColor> systemAccentColor() const;
	[[nodiscard]] rpl::producer<std::optional<QColor>>
	systemAccentColorValue() const;

	[[nodiscard]] static WindowsIntegration &Instance();

private:
	enum class PowerState {
		Awake,
		SuspendNotified,
		SuspendSettled,
		AutomaticResumeNotified,
		AutomaticResumeSettled,
		UserResumeNotified,
	};

	bool nativeEventFilter(
		const QByteArray &eventType,
		void *message,
		native_event_filter_result *result) override;
	bool processEvent(
		HWND hWnd,
		UINT msg,
		WPARAM wParam,
		LPARAM lParam,
		LRESULT *result);

	void createCustomJumpList();
	void refreshCustomJumpList();
	void setupTaskbarButtons(HWND window);
	void refreshSystemAccentColor();
	void scheduleSystemAccentColorRefresh();

	uint32 _taskbarCreatedMsgId = 0;
	PowerState _powerState = PowerState::Awake;
	base::Timer _powerBroadcastTimer;
	winrt::com_ptr<ITaskbarList3> _taskbarList;
	winrt::com_ptr<ICustomDestinationList> _jumpList;
	std::unique_ptr<TaskbarButtons> _taskbarButtons;
	rpl::variable<std::optional<QColor>> _systemAccentColor;
	base::Timer _systemAccentColorRefresh;

};

[[nodiscard]] std::unique_ptr<Integration> CreateIntegration();

} // namespace Platform
