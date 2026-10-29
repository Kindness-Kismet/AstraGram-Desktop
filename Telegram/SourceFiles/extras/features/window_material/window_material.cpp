#include "extras/features/window_material/window_material.h"

#include "extras/extras_settings.h"
#include "chat_helpers/tabbed_panel.h"
#include "extras/features/window_material/platform/window_material_platform.h"
#include "ui/controls/jump_down_button.h"
#include "ui/gl/gl_surface.h"
#include "ui/layers/layer_widget.h"
#include "rpl/map.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/rp_window.h"
#include "ui/widgets/separate_panel.h"
#include "ui/ui_utility.h"
#include "ui/style/style_core_palette.h"
#include "ui/style/style_widget_color.h"
#include "window/themes/window_theme.h"
#include "window/window_main_menu.h"
#include "styles/style_widgets.h"
#include "styles/palette.h"

#include <QtCore/QEvent>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtCore/QVariant>
#include <QtGui/QPainter>
#include <QtGui/QWindow>
#include <QtWidgets/QRhiWidget>

namespace ExtrasFeatures::WindowMaterial {
namespace {

constexpr auto kActiveProperty = "ExtrasWindowMaterialActive";
constexpr auto kModeProperty = "ExtrasWindowMaterialEffectiveMode";
constexpr auto kRevisionProperty = "ExtrasWindowMaterialRevision";
constexpr auto kWatchProperty = "ExtrasWindowMaterialSurface";

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
		_widget->setProperty("ExtrasWindowMaterialSurfaceActive", active);
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
			if (name == kRevisionProperty) {
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
		ExtrasSettings::getInstance().windowMaterialValue(
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
		const auto mode = ExtrasSettings::getInstance().windowMaterial();
		const auto active = _backend->apply(mode, Window::Theme::IsNightMode());
		const auto effective = active ? mode : ::WindowMaterial::Off;
		if (paletteChanged
			|| _window->property(kActiveProperty).toBool() != active
			|| _window->property(kModeProperty).toInt() != int(effective)) {
			_window->setProperty(kModeProperty, int(effective));
			_window->setProperty(kActiveProperty, active);
			refreshTitleStyle(active);
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

	void refreshTitleStyle(bool active) {
		if (!active) {
			_window->setTitleStyle(st::defaultWindowTitle);
			return;
		}
		_titlePalette = *style::main_palette::get();
		const auto dark = Window::Theme::IsNightMode();
		const auto hover = dark
			? QColor(255, 255, 255, 26)
			: QColor(0, 0, 0, 26);
		// 复合图标的底色使用纯黑白叠加，避免被材质的前景色适配加深。
		for (const auto name : {
			"titleButtonBgOver",
			"titleButtonBgActiveOver",
		}) {
			_titlePalette.setColor(QLatin1String(name), hover);
		}
		_titlePalette.finalize();
		auto updated = std::make_unique<style::WindowTitle>(st::defaultWindowTitle);
		// 只替换最小化、最大化与还原的悬停图标，关闭按钮保留原样式。
		for (const auto icon : {
			&updated->minimize.iconOver,
			&updated->minimizeIconActiveOver,
			&updated->maximize.iconOver,
			&updated->maximizeIconActiveOver,
			&updated->restoreIconOver,
			&updated->restoreIconActiveOver,
		}) {
			*icon = icon->withPalette(_titlePalette);
		}
		_window->setTitleStyle(*updated);
		_titleStyle = std::move(updated);
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
	style::palette _titlePalette;
	std::unique_ptr<style::WindowTitle> _titleStyle;
	QTimer _timer;
	int _revision = 0;
	bool _refreshing = false;
};

// 面板四周是自绘阴影的半透明边距：未生效时必须完整释放，交回分层窗口绘制。
class PanelController final : public QObject {
public:
	explicit PanelController(not_null<Ui::SeparatePanel*> panel)
	: QObject(panel.get())
	, _panel(panel)
	, _backend(Platform::create(panel)) {
		_panel->installEventFilter(this);
		ExtrasSettings::getInstance().windowMaterialValue(
		) | rpl::on_next([=] { refresh(); }, _panel->lifetime());
		style::PaletteChanged(
		) | rpl::on_next([=] { refresh(true); }, _panel->lifetime());
		_timer.setInterval(1500);
		QObject::connect(&_timer, &QTimer::timeout, this, [=] { refresh(); });
		_timer.start();
		refresh();
	}

private:
	void refresh(bool paletteChanged = false) {
		const auto mode = ExtrasSettings::getInstance().windowMaterial();
		const auto active = _panel->windowHandle()
			&& _backend->panelSupported(mode)
			&& _backend->apply(mode, Window::Theme::IsNightMode())
			&& _backend->setRoundedCorners(true);
		if (!active) {
			_backend->release();
		}
		const auto effective = active ? mode : ::WindowMaterial::Off;
		if (!paletteChanged
			&& _panel->property(kActiveProperty).toBool() == active
			&& _panel->property(kModeProperty).toInt() == int(effective)) {
			return;
		}
		_panel->setProperty(kModeProperty, int(effective));
		_panel->setProperty(kActiveProperty, active);
		_panel->setSystemBackdrop(active, active
			? rootTintColor(_panel.get())
			: QColor(Qt::transparent));
		_panel->setProperty(kRevisionProperty, ++_revision);
		Ui::ForceFullRepaint(_panel);
	}

	bool eventFilter(QObject *, QEvent *event) override {
		if (event->type() == QEvent::WinIdChange
			|| event->type() == QEvent::Show) {
			QTimer::singleShot(0, this, [=] { refresh(); });
		}
		return false;
	}

	const not_null<Ui::SeparatePanel*> _panel;
	const std::unique_ptr<Platform::Backend> _backend;
	QTimer _timer;
	int _revision = 0;
};

class PopupController final : public QObject {
public:
	explicit PopupController(not_null<Ui::PopupMenu*> menu)
	: QObject(menu.get())
	, _menu(menu)
	, _backend(Platform::create(menu)) {
		_menu->installEventFilter(this);
		ExtrasSettings::getInstance().windowMaterialValue(
		) | rpl::on_next([=] { settingsChanged(); }, _menu->lifetime());
		style::PaletteChanged(
		) | rpl::on_next([=] { settingsChanged(); }, _menu->lifetime());
	}

	void refresh() {
		if (_refreshing) {
			return;
		}
		_refreshing = true;
		const auto mode = ExtrasSettings::getInstance().windowMaterial();
		const auto supported = _backend->panelSupported(mode);
		if (supported) {
			// 原生窗口创建前准备画布；内嵌菜单使用宿主原有的绘制接口。
			if (!_rhiPrepared) {
				Ui::GL::EnsureWindowRhi(_menu);
				_rhiPrimer = _menu->findChild<QRhiWidget*>(
					QString(), Qt::FindDirectChildrenOnly);
				_rhiPrepared = true;
			}
			// 几何准备前保留原生窗口，避免把整个宿主窗口当成菜单设置材质。
			if (!_menu->systemBackdrop()) {
				_menu->setSystemBackdrop(true);
			}
			_menu->createWinId();
			// 菜单尺寸逐帧变化，提交不能再等待一次屏幕刷新。
			auto format = _menu->windowHandle()->requestedFormat();
			format.setSwapInterval(0);
			_menu->windowHandle()->setFormat(format);
		}
		const auto active = supported
			&& _backend->apply(mode, Window::Theme::IsNightMode())
			&& _backend->setRoundedCorners(true);
		if (!active) {
			_backend->release();
			// 内嵌前移除原生画布控件，避免将不同的绘制接口带进宿主。
			delete _rhiPrimer.data();
			_rhiPrepared = false;
		}
		_menu->setProperty(kModeProperty, int(active ? mode : ::WindowMaterial::Off));
		_menu->setProperty(kActiveProperty, active);
		_menu->setSystemBackdrop(active, active
			? rootTintColor(_menu.get())
			: QColor(Qt::transparent));
		_menu->setProperty(kRevisionProperty, ++_revision);
		_refreshing = false;
	}

private:
	void settingsChanged() {
		if (_menu->isVisible()) {
			// 切换窗口类型会重建句柄，显示中的菜单先关闭，下次展开时重新准备。
			_menu->hideMenu(true);
			return;
		}
		refresh();
	}

	bool eventFilter(QObject *, QEvent *event) override {
		if (event->type() == QEvent::WinIdChange && !_refreshing) {
			QTimer::singleShot(0, this, [=] { refresh(); });
		} else if (event->type() == QEvent::Show) {
			_backend->activatePopupBackdrop();
		}
		return false;
	}

	const not_null<Ui::PopupMenu*> _menu;
	const std::unique_ptr<Platform::Backend> _backend;
	QPointer<QRhiWidget> _rhiPrimer;
	int _revision = 0;
	bool _refreshing = false;
	bool _rhiPrepared = false;
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

void attachPanel(not_null<Ui::SeparatePanel*> panel) {
	if (availableModes().size() <= 1) {
		return;
	}
	new PanelController(panel);
}

void attachPopup(not_null<Ui::PopupMenu*> menu) {
	if (availableModes().size() <= 1) {
		return;
	}
	for (const auto child : menu->children()) {
		if (const auto controller = dynamic_cast<PopupController*>(child)) {
			controller->refresh();
			return;
		}
	}
	new PopupController(menu);
}

bool isActive(const QWidget *widget) {
	if (!widget || !widget->window()->property(kActiveProperty).toBool()) {
		return false;
	}
	for (auto ancestor = widget; ancestor; ancestor = ancestor->parentWidget()) {
		if ((dynamic_cast<const Ui::LayerWidget*>(ancestor)
				&& !dynamic_cast<const Window::MainMenu*>(ancestor))
			|| (dynamic_cast<const Ui::PopupMenu*>(ancestor)
				&& !ancestor->isWindow())
			|| dynamic_cast<const ChatHelpers::TabbedPanel*>(ancestor)) {
			return false;
		}
		if (ancestor == widget->window()) {
			break;
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

rpl::producer<bool> changes(not_null<QWidget*> widget) {
	watchSurface(widget);
	for (const auto child : widget->children()) {
		if (const auto watcher = dynamic_cast<SurfaceWatcher*>(child)) {
			return watcher->value();
		}
	}
	Unexpected("Missing window material surface watcher.");
}

QColor rootTintColor(not_null<const QWidget*> widget) {
	if (!isActive(widget.get())) {
		return widget->palette().color(QPalette::Window);
	}
	if (widget->window()->property(kModeProperty).toInt()
		!= int(::WindowMaterial::Acrylic)) {
		return QColor(Qt::transparent);
	}
	// 根部只叠加一次中性色，以保证正文对比度。
	if (!Window::Theme::IsNightMode()) {
		return QColor(255, 255, 255, 153);
	}
	return dynamic_cast<const Ui::PopupMenu*>(widget->window())
		? QColor(0, 0, 0, 153)
		: QColor(27, 27, 27, 179);
}

QColor cardColor(const QWidget *widget, QColor opaque) {
	if (!isActive(widget)) {
		return opaque;
	}
	if (widget->window()->property(kModeProperty).toInt()
		== int(::WindowMaterial::Acrylic)) {
		// 输入表面使用两成中性色，保留原生材质的背景色。
		return Window::Theme::IsNightMode()
			? QColor(0, 0, 0, 51)
			: QColor(255, 255, 255, 51);
	}
	return Window::Theme::IsNightMode()
		? QColor(0, 0, 0, 0xCC)
		: QColor(0xFF, 0xFF, 0xFF, 0xCC);
}

QColor secondaryColor(QColor normal) {
	const auto foreground = st::windowFg->c;
	return QColor(
		(foreground.red() * 4 + normal.red()) / 5,
		(foreground.green() * 4 + normal.green()) / 5,
		(foreground.blue() * 4 + normal.blue()) / 5,
		normal.alpha());
}

QColor widgetColor(
		not_null<const QWidget*> widget,
		QColor normal,
		Ui::WidgetColorRole role) {
	// 跳转按钮由底色、阴影和箭头叠成，整体沿用聊天配色。
	if (!normal.alpha()
		|| !isActive(widget.get())
		|| dynamic_cast<const Ui::JumpDownButton*>(widget.get())) {
		return normal;
	}
	const auto menu = dynamic_cast<const Ui::PopupMenu*>(widget->window());
	if (menu) {
		// 普通行与分隔区透出同一层材质，悬停和语义色仍由各项绘制。
		if (role == Ui::WidgetColorRole::Background
			&& normal == menu->st().menu.itemBg->c) {
			return Qt::transparent;
		}
	}
	// 只适配中性色，警告、链接、选中态等语义色沿用原样式。
	const auto low = std::min({ normal.red(), normal.green(), normal.blue() });
	const auto high = std::max({ normal.red(), normal.green(), normal.blue() });
	if (high - low > 64) {
		return normal;
	}
	if (menu && role == Ui::WidgetColorRole::Text) {
		// 菜单的固定深色配色随材质切换，主文字与说明文字保持层次。
		const auto &menuStyle = menu->st().menu;
		const auto primary = (normal == menuStyle.itemFg->c)
			|| (normal == menuStyle.itemFgOver->c);
		const auto secondary = (normal == menuStyle.itemFgDisabled->c)
			|| (normal == menuStyle.itemFgShortcut->c)
			|| (normal == menuStyle.itemFgShortcutOver->c)
			|| (normal == menuStyle.itemFgShortcutDisabled->c);
		if (primary || secondary) {
			auto result = primary
				? st::windowFg->c
				: secondaryColor(st::windowSubTextFg->c);
			result.setAlpha(normal.alpha());
			return result;
		}
	}
	switch (role) {
	case Ui::WidgetColorRole::Text:
		return (high > 60 && low < 220)
			? secondaryColor(normal)
			: normal;
	case Ui::WidgetColorRole::Background:
		return normal.alpha() == 255 ? cardColor(widget.get(), normal) : normal;
	case Ui::WidgetColorRole::Hover: {
		auto result = st::windowFg->c;
		result.setAlpha(26);
		return result;
	}
	case Ui::WidgetColorRole::Border: {
		auto result = st::windowFg->c;
		result.setAlpha(38);
		return result;
	}
	}
	Unexpected("Invalid widget color role.");
}

std::optional<QColor> foregroundOverride(const QWidget *widget) {
	return isActive(widget)
		? std::make_optional(st::windowFg->c)
		: std::nullopt;
}

QColor foregroundColor(const QWidget *widget, QColor normal) {
	return foregroundOverride(widget).value_or(normal);
}

void applyIconButton(
		not_null<Ui::IconButton*> button,
		const style::color &over) {
	const auto fg = foregroundOverride(button);
	button->setIconColorOverride(fg);
	button->setIconOverColorOverride(fg
		? std::make_optional(over->c)
		: std::nullopt);
}

void watchIconButton(
		not_null<Ui::IconButton*> button,
		const style::color &over) {
	// 按地址读取样式色，主题切换后取到新值。
	const auto overColor = &over;
	changes(button) | rpl::on_next([=] {
		applyIconButton(button, *overColor);
	}, button->lifetime());
}

std::vector<::WindowMaterial> availableModes() {
	return Platform::availableModes();
}

} // namespace ExtrasFeatures::WindowMaterial
