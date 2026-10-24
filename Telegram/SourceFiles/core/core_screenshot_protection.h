/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

#include <QtCore/QPointer>
#include <QtCore/QRect>

class QPixmap;

namespace Core {

// 理由按宿主窗口合并，界面销毁后自动撤销。
class ScreenshotProtection final : public base::has_weak_ptr {
public:
	ScreenshotProtection();
	~ScreenshotProtection();

	void addContentReason(
		not_null<QWidget*> window,
		rpl::producer<bool> active,
		rpl::lifetime &lifetime);

	// 仅平台支持时保护当前窗口的周边上下文。
	void addAmbientReason(
		not_null<QWidget*> window,
		rpl::producer<bool> active,
		rpl::lifetime &lifetime);

	[[nodiscard]] bool active(not_null<QWidget*> window) const;
	[[nodiscard]] rpl::producer<bool> activeValue(
		not_null<QWidget*> window) const;

	// 受保护窗口的动画只保存背景，不保存正文。
	[[nodiscard]] QPixmap grabForAnimation(
		not_null<QWidget*> widget,
		QRect rect = QRect()) const;

private:
	struct Reason {
		QPointer<QWidget> window;
		bool active = false;
	};
	void addReason(
		not_null<QWidget*> window,
		rpl::producer<bool> active,
		rpl::lifetime &lifetime);
	void refresh(not_null<QWidget*> window);

	base::flat_map<uint64, Reason> _reasons;
	uint64 _lastReasonId = 0;
	rpl::event_stream<not_null<QWidget*>> _changes;
	std::unique_ptr<QObject> _filter;

};

} // namespace Core
