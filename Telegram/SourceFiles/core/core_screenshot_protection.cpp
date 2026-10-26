/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/core_screenshot_protection.h"
#include "window/window_animation_snapshot.h"
#include "extras/features/window_material/window_material.h"

#include "platform/platform_specific.h"
#include "ui/style/style_core.h"
#include "ui/ui_utility.h"

#include <QtWidgets/QApplication>

namespace Core {
namespace {

class WindowFilter final : public QObject {
public:
	explicit WindowFilter(Fn<void(not_null<QWidget*>)> appeared);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	Fn<void(not_null<QWidget*>)> _appeared;

};

WindowFilter::WindowFilter(Fn<void(not_null<QWidget*>)> appeared)
: _appeared(std::move(appeared)) {
}

bool WindowFilter::eventFilter(QObject *watched, QEvent *event) {
	const auto type = event->type();
	if (type == QEvent::WinIdChange || type == QEvent::Show) {
		if (const auto widget = qobject_cast<QWidget*>(watched)) {
			if (widget->isWindow()) {
				_appeared(widget);
			}
		}
	}
	return QObject::eventFilter(watched, event);
}

} // namespace

ScreenshotProtection::ScreenshotProtection() {
	_filter = std::make_unique<WindowFilter>(crl::guard(this, [=](
			not_null<QWidget*> window) {
		refresh(window);
	}));
	qApp->installEventFilter(_filter.get());
}

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

void ScreenshotProtection::refresh(not_null<QWidget*> window) {
	Platform::SetWindowScreenshotProtection(window, active(window));
	_changes.fire_copy(window);
}

} // namespace Core
