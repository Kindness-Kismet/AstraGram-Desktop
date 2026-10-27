/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/core_screenshot_protection.h"
#include "window/window_animation_snapshot.h"
#include "extras/features/window_material/window_material.h"

#include "base/event_filter.h"
#include "platform/platform_specific.h"
#include "ui/style/style_core.h"
#include "ui/ui_utility.h"

namespace Core {

ScreenshotProtection::ScreenshotProtection() = default;

ScreenshotProtection::~ScreenshotProtection() = default;

void ScreenshotProtection::addContentReason(
		not_null<QWidget*> window,
		rpl::producer<bool> active,
		rpl::lifetime &lifetime,
		QWidget *animationRedaction) {
	addReason(window, std::move(active), lifetime, animationRedaction);
}

void ScreenshotProtection::addAmbientReason(
		not_null<QWidget*> window,
		rpl::producer<bool> active,
		rpl::lifetime &lifetime) {
	if (Platform::AmbientScreenshotProtectionSupported()) {
		addReason(window, std::move(active), lifetime);
	}
}

void ScreenshotProtection::addReason(
		not_null<QWidget*> window,
		rpl::producer<bool> active,
		rpl::lifetime &lifetime,
		QWidget *animationRedaction) {
	Expects(window->isWindow());
	Expects(!animationRedaction || animationRedaction->window() == window);
	const auto weakWindow = QPointer<QWidget>(window.get());
	const auto id = ++_lastReasonId;
	_reasons.emplace(id, Reason{
		.window = weakWindow,
		.animationRedaction = animationRedaction,
	});
	std::move(
		active
	) | rpl::on_next(crl::guard(this, [=](bool value) {
		_reasons[id].active = value;
		if (weakWindow) {
			refresh(weakWindow.data());
		}
	}), lifetime);
	base::install_event_filter(window, crl::guard(this, [=](
			not_null<QEvent*> event) {
		if (event->type() == QEvent::WinIdChange) {
			refresh(window, true);
		}
		return base::EventFilterResult::Continue;
	}), lifetime);
	lifetime.add(crl::guard(this, [=] {
		_reasons.remove(id);
		if (weakWindow) {
			refresh(weakWindow.data());
		}
	}));
}

bool ScreenshotProtection::active(not_null<QWidget*> window) const {
	return ranges::any_of(_reasons, [&](const auto &reason) {
		return reason.second.active
			&& reason.second.window.data() == window.get();
	});
}

rpl::producer<bool> ScreenshotProtection::activeValue(
		not_null<QWidget*> window) const {
	return _changes.events_starting_with_copy(window)
		| rpl::filter([=](not_null<QWidget*> changed) {
			return changed == window;
		})
		| rpl::map([=] { return active(window); })
		| rpl::distinct_until_changed();
}

QPixmap ScreenshotProtection::grabForAnimation(
		not_null<QWidget*> widget,
		QRect rect) const {
	if (!active(widget->window())) {
		return Window::GrabForAnimation(widget, rect);
	}
	Ui::SendPendingMoveResizeEvents(widget);
	if (rect.isNull()) {
		rect = widget->rect();
	}
	auto result = QPixmap(rect.size() * style::DevicePixelRatio());
	result.setDevicePixelRatio(style::DevicePixelRatio());
	result.fill(ExtrasFeatures::WindowMaterial::isActive(widget.get())
		? ExtrasFeatures::WindowMaterial::rootTintColor(widget)
		: QColor(Qt::transparent));
	auto visible = QRegion(rect);
	auto redactions = std::vector<std::pair<QWidget*, QRect>>();
	for (const auto &[id, reason] : _reasons) {
		if (!reason.active || reason.window != widget->window()) {
			continue;
		}
		const auto target = reason.animationRedaction.data();
		if (!target) {
			widget->render(&result, QPoint(), rect, QWidget::DrawWindowBackground);
			return result;
		}
		if (target != widget.get() && target->isAncestorOf(widget)) {
			return result;
		}
		if (target != widget.get()
			&& (!widget->isAncestorOf(target) || !target->isVisibleTo(widget))) {
			continue;
		}
		const auto area = QRect(target->mapTo(widget, QPoint()), target->size());
		if (!area.intersects(rect)) {
			continue;
		}
		visible -= area;
		redactions.emplace_back(target, area);
	}
	// 先排除正文区域再绘制，禁止先截取正文再盖住。
	if (!visible.isEmpty()) {
		widget->render(&result,
			visible.boundingRect().topLeft() - rect.topLeft(), visible);
	}
	for (const auto &[target, area] : redactions) {
		const auto clipped = area.intersected(rect);
		target->render(&result, clipped.topLeft() - rect.topLeft(),
			QRegion(clipped.translated(-area.topLeft())),
			QWidget::DrawWindowBackground);
	}
	return result;
}

void ScreenshotProtection::refresh(not_null<QWidget*> window, bool force) {
	for (auto i = _applied.begin(); i != _applied.end();) {
		if (i->second.window) {
			++i;
		} else {
			i = _applied.erase(i);
		}
	}
	const auto enabled = active(window);
	const auto i = _applied.find(window);
	if (!force && i != _applied.end() && i->second.enabled == enabled) {
		return;
	}
	Platform::SetWindowScreenshotProtection(window, enabled);
	if (i == _applied.end()) {
		_applied.emplace(window, Applied{ window.get(), enabled });
	} else {
		i->second.enabled = enabled;
	}
	_changes.fire_copy(window);
}

} // namespace Core
