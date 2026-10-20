
#include "extras/features/streamer_mode/streamer_mode.h"

#include "extras/extras_settings.h"
#include "extras/features/streamer_mode/platform/platform_streamer_mode.h"
#include "core/application.h"
#include "core/core_screenshot_protection.h"

#include <QtCore/QEvent>
#include <QtCore/QVariant>
#include <QtWidgets/QWidget>

#include <memory>

namespace ExtrasFeatures::StreamerMode {
namespace {

constexpr auto kUserIdProperty = "ExtrasStreamerModeUserId";
constexpr auto kHiddenProperty = "ExtrasStreamerModeHidden";
constexpr auto kOwnerProperty = "ExtrasStreamerModeExplicitOwner";
constexpr auto kBindingName = "extras.streamerModeBinding";

class WindowBinding final : public QObject {
public:
	explicit WindowBinding(not_null<QWidget*> window)
	: QObject(window)
	, _window(window) {
		setObjectName(QLatin1String(kBindingName));
		_window->installEventFilter(this);
	}

	void bind(uint64 userId, rpl::lifetime &lifetime) {
		const auto previous = _window->property(kUserIdProperty);
		if (previous.isValid()
			&& previous.toULongLong() == userId
			&& _ownerLifetime == &lifetime
			&& (!userId || (_subscription && bool(*_subscription)))) {
			return;
		}
		_subscription = nullptr;
		_ownerLifetime = &lifetime;
		_window->setProperty(kUserIdProperty, QVariant::fromValue(qulonglong(userId)));
		if (!userId) {
			_window->setProperty(kHiddenProperty, false);
			refreshWidgetWindow(_window);
			return;
		}
		_subscription = std::make_shared<rpl::lifetime>();
		ExtrasSettings::streamer(userId).enabledValue(
		) | rpl::on_next([=](bool excluded) {
			_window->setProperty(kHiddenProperty, excluded);
			refreshWidgetWindow(_window);
		}, *_subscription);
		lifetime.add([weak = std::weak_ptr(_subscription)] {
			if (const auto subscription = weak.lock()) {
				subscription->destroy();
			}
		});
	}

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

protected:
	bool eventFilter(QObject *object, QEvent *event) override {
		if (event->type() == QEvent::WinIdChange
			|| event->type() == QEvent::Show) {
			refreshWidgetWindow(_window);
		}
		return QObject::eventFilter(object, event);
	}

private:
	const not_null<QWidget*> _window;
	std::shared_ptr<rpl::lifetime> _subscription;
	rpl::lifetime *_ownerLifetime = nullptr;
	rpl::lifetime _lifetime;
};

[[nodiscard]] not_null<WindowBinding*> bindingFor(not_null<QWidget*> window) {
	const auto existing = window->findChild<QObject*>(
		QLatin1String(kBindingName), Qt::FindDirectChildrenOnly);
	return existing
		? static_cast<WindowBinding*>(existing)
		: new WindowBinding(window);
}

[[nodiscard]] uint64 inheritedUserId(not_null<QWidget*> owner) {
	for (auto parent = owner.get(); parent; parent = parent->parentWidget()) {
		const auto value = parent->property(kUserIdProperty);
		if (value.isValid()) {
			return value.toULongLong();
		}
	}
	return 0;
}

} // namespace

void bindWidgetWindow(
		not_null<QWidget*> widget,
		uint64 userId,
		rpl::lifetime &lifetime) {
	const auto window = widget->window();
	window->setProperty(kOwnerProperty, true);
	bindingFor(window)->bind(userId, lifetime);
}

void inheritWidgetWindow(not_null<QWidget*> widget) {
	if (!widget->isWindow() || widget->property(kOwnerProperty).toBool()) {
		return;
	}
	const auto binding = bindingFor(widget);
	const auto owner = widget->parentWidget();
	binding->bind(owner ? inheritedUserId(owner) : 0, binding->lifetime());
}

void inheritWidgetWindow(not_null<QWidget*> widget, not_null<QWidget*> owner) {
	Expects(widget->isWindow());
	widget->setProperty(kOwnerProperty, true);
	const auto binding = bindingFor(widget);
	binding->bind(inheritedUserId(owner), binding->lifetime());
}

void refreshWidgetWindow(not_null<QWidget*> widget) {
	const auto window = widget->window();
	if (!window->internalWinId()) {
		return;
	}
	Platform::SetWindowCaptureExcluded(window,
		window->property(kHiddenProperty).toBool()
			|| window->property("ExtrasContentScreenshotProtected").toBool()
			|| Core::App().screenshotProtection().active());
}

} // namespace ExtrasFeatures::StreamerMode
