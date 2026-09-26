#include "ayu/features/window_material/window_material.h"

#include "ayu/ayu_settings.h"
#include "ayu/features/window_material/platform/window_material_platform.h"
#include "ui/layers/layer_widget.h"
#include "rpl/map.h"
#include "ui/widgets/rp_window.h"
#include "ui/ui_utility.h"
#include "window/themes/window_theme.h"
#include "styles/style_widgets.h"
#include "styles/palette.h"

#include <QtCore/QEvent>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtCore/QVariant>
#include <QtGui/QPainter>

namespace AyuFeatures::WindowMaterial {
namespace {

constexpr auto kActiveProperty = "AyuWindowMaterialActive";
constexpr auto kModeProperty = "AyuWindowMaterialEffectiveMode";
constexpr auto kRevisionProperty = "AyuWindowMaterialRevision";
constexpr auto kWatchProperty = "AyuWindowMaterialSurface";

class SurfaceWatcher final : public QObject {
	struct State {
		bool active = false;
		int revision = 0;
		bool operator==(const State &) const = default;
	};

public:
	explicit SurfaceWatcher(not_null<QWidget*> widget)
	: QObject(widget.get())
	, _widget(widget)
	, _opaque(widget->testAttribute(Qt::WA_OpaquePaintEvent))
	, _autoFill(widget->autoFillBackground()) {
		_widget->installEventFilter(this);
		rebind();
	}

	[[nodiscard]] rpl::producer<bool> value() const {
		return _state.value() | rpl::map([](const State &state) {
			return state.active;
		});
	}

private:
	void rebind() {
		if (_root != _widget->window()) {
			if (_root) {
				_root->removeEventFilter(this);
			}
			_root = _widget->window();
			if (_root != _widget.get()) {
				_root->installEventFilter(this);
			}
		}
		refresh();
	}

	void refresh() {
		const auto active = isActive(_widget.get());
		if (_updating) {
			return;
		}
		_updating = true;
		_widget->setProperty("AyuWindowMaterialSurfaceActive", active);
		_widget->setAttribute(Qt::WA_OpaquePaintEvent, active ? false : _opaque);
		_widget->setAutoFillBackground(active ? false : _autoFill);
		const auto state = State{
			active,
			_root->property(kRevisionProperty).toInt(),
		};
		_updating = false;
		_widget->update();
		// 订阅方可能在通知中销毁控件，发送后不再访问成员。
		_state = state;
	}

	bool eventFilter(QObject *object, QEvent *event) override {
		const auto watched = QPointer<QObject>(object);
		if (object == _widget.get()
			&& (event->type() == QEvent::ParentChange
				|| event->type() == QEvent::Show)) {
			rebind();
		} else if (event->type() == QEvent::DynamicPropertyChange) {
			const auto name = static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName();
			if (name == kRevisionProperty || name == "AyuWindowMaterialAllowedLayer") {
				refresh();
			}
		}
		return watched.isNull();
	}

	const not_null<QWidget*> _widget;
	QPointer<QWidget> _root;
	const bool _opaque;
	const bool _autoFill;
	rpl::variable<State> _state = State();
	bool _updating = false;
};

class Controller final : public QObject {
public:
	explicit Controller(not_null<Ui::RpWindow*> window)
	: QObject(window.get())
	, _window(window)
	, _backend(Platform::create(window)) {
		_window->installEventFilter(this);
		AyuSettings::getInstance().windowMaterialValue(
		) | rpl::on_next([=] { refresh(); }, _window->lifetime());
		style::PaletteChanged(
		) | rpl::on_next([=] { refresh(true); }, _window->lifetime());
		_window->paintRequest(
		) | rpl::on_next([=](QRect clip) {
			const auto color = rootTintColor(_window.get());
			QPainter painter(_window.get());
			painter.setCompositionMode(QPainter::CompositionMode_Source);
			painter.fillRect(clip, color);
		}, _window->lifetime());
		_timer.setInterval(1500);
		QObject::connect(&_timer, &QTimer::timeout, this, [=] { refresh(); });
		_timer.start();
		refresh();
	}

private:
	void refresh(bool paletteChanged = false) {
		if (_refreshing) {
			return;
		}
		_refreshing = true;
		const auto mode = AyuSettings::getInstance().windowMaterial();
		const auto active = _backend->apply(mode, Window::Theme::IsNightMode());
		const auto effective = active ? mode : ::WindowMaterial::Off;
		if (paletteChanged
			|| _window->property(kActiveProperty).toBool() != active
			|| _window->property(kModeProperty).toInt() != int(effective)) {
			_window->setProperty(kModeProperty, int(effective));
			_window->setProperty(kActiveProperty, active);
			// 模式与生效状态写齐后统一通知，避免暴露一半更新的状态。
			_window->setProperty(kRevisionProperty, ++_revision);
			std::vector<QPointer<Ui::LayerStackWidget>> layers;
			for (const auto child : _window->findChildren<QWidget*>()) {
				if (const auto layer = dynamic_cast<Ui::LayerStackWidget*>(child)) {
					layers.emplace_back(layer);
				}
			}
			for (const auto &layer : layers) {
				if (layer) {
					layer->finishAnimating();
				}
			}
			Ui::ForceFullRepaint(_window);
		}
		_refreshing = false;
	}

	bool eventFilter(QObject *, QEvent *event) override {
		if (event->type() == QEvent::WinIdChange
			|| event->type() == QEvent::Show
			|| event->type() == QEvent::WindowStateChange
			|| event->type() == QEvent::Resize) {
			QTimer::singleShot(0, this, [=] { refresh(); });
		}
		return false;
	}

	const not_null<Ui::RpWindow*> _window;
	const std::unique_ptr<Platform::Backend> _backend;
	QTimer _timer;
	int _revision = 0;
	bool _refreshing = false;
};

} // namespace

void initialize(not_null<Ui::RpWindow*> window) {
	if (availableModes().size() <= 1) {
		return;
	}
	watchSurface(window->body());
	if (const auto title = window->titleWidget()) {
		watchSurface(title);
	}
	new Controller(window);
}

bool isActive(const QWidget *widget) {
	if (!widget || !widget->window()->property(kActiveProperty).toBool()) {
		return false;
	}
	for (auto ancestor = widget; ancestor; ancestor = ancestor->parentWidget()) {
		if (dynamic_cast<const Ui::LayerWidget*>(ancestor)
			&& !ancestor->property("AyuWindowMaterialAllowedLayer").toBool()) {
			return false;
		}
	}
	return true;
}

QColor surfaceColor(const QWidget *widget, QColor opaque, int alpha) {
	if (isActive(widget)) {
		opaque.setAlpha(alpha);
	}
	return opaque;
}

void watchSurface(not_null<QWidget*> widget) {
	if (!widget->property(kWatchProperty).toBool()) {
		widget->setProperty(kWatchProperty, true);
		new SurfaceWatcher(widget);
	}
}

void allowMaterialLayer(not_null<Ui::LayerWidget*> widget) {
	widget->setProperty("AyuWindowMaterialAllowedLayer", true);
	watchSurface(widget);
}

rpl::producer<bool> changes(not_null<QWidget*> widget) {
	watchSurface(widget);
	for (const auto child : widget->children()) {
		if (const auto watcher = dynamic_cast<SurfaceWatcher*>(child)) {
			return watcher->value();
		}
	}
	Unexpected("Missing window material surface watcher.");
}

QColor rootTintColor(const QWidget *widget) {
	if (!isActive(widget)) {
		return widget->palette().color(QPalette::Window);
	}
	const auto mode = ::WindowMaterial(widget->window()->property(kModeProperty).toInt());
	if (mode == ::WindowMaterial::Mica) {
		return QColor(Qt::transparent);
	}
	return Window::Theme::IsNightMode()
		? QColor(0x21, 0x21, 0x21, 0xB3)
		: QColor(0xFF, 0xFF, 0xFF, 0xB3);
}

QColor cardColor(const QWidget *widget, QColor opaque) {
	if (!isActive(widget)) {
		return opaque;
	}
	return Window::Theme::IsNightMode()
		? QColor(0, 0, 0, 0xCC)
		: QColor(0xFF, 0xFF, 0xFF, 0xCC);
}

std::vector<::WindowMaterial> availableModes() {
	return Platform::availableModes();
}

} // namespace AyuFeatures::WindowMaterial
