#ifdef _DEBUG
#include "ayu/debug/commands/commands_internal.h"

#include "core/application.h"
#include "ui/abstract_button.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_controller.h"

#include <QAbstractButton>
#include <QApplication>
#include <QEnterEvent>
#include <QLabel>
#include <QKeyEvent>
#include <QMap>
#include <QMouseEvent>
#include <QPointer>
#include <QWidget>
#include <QAccessible>
#include <QContextMenuEvent>
#include <cmath>
#include <QLineEdit>
#include <QComboBox>
#include "ui/widgets/checkbox.h"
#include "ui/widgets/continuous_sliders.h"

namespace AyuDebug::Commands {
namespace {

using json = nlohmann::json;

// ---- UI 探查与合成交互 ----

// list 的 #index 与 click 的序号寻址共用这份先序 DFS 顺序，改遍历顺序等于
// 让所有序号失效，勿动。
struct WidgetInfo {
	QPointer<QWidget> widget;
	QWidget *root = nullptr; // 所属顶层窗口，visible/enabled 判定基准
	int depth = 0;
};

[[nodiscard]] std::vector<WidgetInfo> CollectWidgets(bool allTopLevels) {
	auto result = std::vector<WidgetInfo>();
	auto stack = std::vector<std::pair<QWidget*, int>>();
	const auto pushRoot = [&](QWidget *root) {
		stack.emplace_back(root, 0);
	};
	if (allTopLevels) {
		const auto tops = QApplication::topLevelWidgets();
		for (auto i = tops.rbegin(); i != tops.rend(); ++i) {
			pushRoot(*i);
		}
	} else {
		const auto window = Core::App().activeWindow();
		if (window) {
			pushRoot(window->widget());
		}
	}
	while (!stack.empty()) {
		const auto [widget, depth] = stack.back();
		stack.pop_back();
		if (!widget) {
			continue;
		}
		result.push_back({ widget, widget->window(), depth });
		const auto children = widget->findChildren<QWidget*>(
			QString(),
			Qt::FindDirectChildrenOnly);
		// 逆序入栈，出栈即恢复先序。
		for (auto i = children.rbegin(); i != children.rend(); ++i) {
			stack.emplace_back(*i, depth + 1);
		}
	}
	return result;
}

[[nodiscard]] QString WidgetText(const QWidget *widget) {
	if (const auto button = qobject_cast<const QAbstractButton*>(widget)) {
		return button->text();
	} else if (const auto label = qobject_cast<const QLabel*>(widget)) {
		return label->text();
	} else if (const auto rp = dynamic_cast<const Ui::RpWidget*>(widget)) {
		return const_cast<Ui::RpWidget*>(rp)->accessibilityName();
	}
	return QString();
}

[[nodiscard]] json DescribeWidget(const WidgetInfo &info) {
	const auto widget = info.widget.data();
	const auto topLeft = widget->mapToGlobal(QPoint(0, 0));
	return json{
		{ "index", nullptr }, // 由 ControlList 填全树序号
		{ "depth", info.depth },
		{ "class", widget->metaObject()->className() },
		{ "name", widget->objectName().toStdString() },
		{ "accessible", widget->accessibleName().toStdString() },
		{ "text", WidgetText(widget).toStdString() },
		{ "rect", json{ widget->x(), widget->y(), widget->width(), widget->height() } },
		{ "globalRect", json{ topLeft.x(), topLeft.y(), widget->width(), widget->height() } },
		{ "visible", widget->isVisibleTo(info.root) },
		{ "enabled", widget->isEnabledTo(info.root) },
		{ "isWindow", widget->isWindow() },
	};
}

// 过滤只作用于展示，序号始终是全树序号，保证 click #index 与 list 对得上。
[[nodiscard]] bool MatchesFilter(const WidgetInfo &info, const QString &filter) {
	if (filter.isEmpty()) {
		return true;
	}
	const auto widget = info.widget.data();
	if (filter == u"@scroll"_q) {
		return dynamic_cast<Ui::ScrollArea*>(widget)
			|| dynamic_cast<Ui::ElasticScroll*>(widget);
	} else if (filter == u"@menu"_q) {
		return dynamic_cast<Ui::PopupMenu*>(widget);
	}
	return widget->objectName().contains(filter, Qt::CaseInsensitive)
		|| QString::fromLatin1(widget->metaObject()->className())
			.contains(filter, Qt::CaseInsensitive)
		|| widget->accessibleName().contains(filter, Qt::CaseInsensitive)
		|| WidgetText(widget).contains(filter, Qt::CaseInsensitive);
}

[[nodiscard]] Result ControlList(const QStringList &args) {
	auto filter = QString();
	auto all = false;
	for (const auto &arg : args) {
		if (arg == u"--all"_q) {
			all = true;
		} else if (filter.isEmpty()) {
			filter = arg;
		} else {
			return Result::Err(u"usage: control.list [filter] [--all]"_q);
		}
	}
	const auto widgets = CollectWidgets(all);
	if (widgets.empty()) {
		return Result::Err(u"no active window"_q);
	}
	constexpr auto kMaxShown = 500;
	auto nodes = json::array();
	auto shown = 0;
	auto truncated = false;
	for (auto i = 0; i < int(widgets.size()); ++i) {
		if (!MatchesFilter(widgets[i], filter)) {
			continue;
		}
		if (shown >= kMaxShown) {
			truncated = true;
			break;
		}
		auto node = DescribeWidget(widgets[i]);
		node["index"] = i;
		nodes.push_back(std::move(node));
		++shown;
	}
	return Result::Ok(Compact(json{
		{ "total", widgets.size() },
		{ "shown", shown },
		{ "truncated", truncated },
		{ "widgets", std::move(nodes) },
	}));
}

[[nodiscard]] QWidget *findControl(const QString &selector, bool all = false) {
	if (selector == u"@menu"_q) {
		return Ui::PopupMenu::Active();
	}
	const auto widgets = CollectWidgets(all);
	if (selector.startsWith(u'#')) {
		auto ok = false;
		const auto index = selector.mid(1).toInt(&ok);
		return (ok && index >= 0 && index < int(widgets.size()))
			? widgets[index].widget.data()
			: nullptr;
	}
	// 多套聊天输入区会同时存在，只定位当前可见的控件。
	const auto named = [&](const auto &candidates, bool accessible) -> QWidget* {
		for (const auto &info : candidates) {
			const auto widget = info.widget.data();
			if (widget && widget->isVisible()
				&& (accessible ? widget->accessibleName() : widget->objectName()) == selector) {
				return widget;
			}
		}
		return nullptr;
	};
	if (const auto widget = named(widgets, false)) {
		return widget;
	}
	const auto topLevels = all ? widgets : CollectWidgets(true);
	if (const auto widget = named(topLevels, false)) {
		return widget;
	}
	if (const auto widget = named(topLevels, true)) return widget;
	for (const auto &info : topLevels) {
		if (info.widget && info.widget->isVisible() && WidgetText(info.widget) == selector) {
			return info.widget;
		}
	}
	return nullptr;
}

// 与鼠标松开的顺序一致：复选框先切换、单选框只选中，再通知点击回调。
void activateButton(not_null<Ui::AbstractButton*> button) {
	const auto alive = QPointer<QWidget>(button.get());
	// Radiobutton 隐藏了同名接口，经基类访问，与它自身的 handlePress 相同。
	if (const auto checkbox = dynamic_cast<Ui::Checkbox*>(button.get())) {
		if (!dynamic_cast<Ui::Radiobutton*>(button.get())) {
			checkbox->setChecked(!checkbox->checked());
		} else if (!checkbox->checked()) {
			checkbox->setChecked(true);
		}
	}
	if (alive) {
		button->clicked({}, Qt::LeftButton);
	}
}

[[nodiscard]] Result ControlClick(const QStringList &args) {
	auto selector = QString();
	auto all = false;
	auto mouse = false;
	for (const auto &arg : args) {
		if (arg == u"--all"_q) {
			all = true;
		} else if (arg == u"--mouse"_q) {
			mouse = true;
		} else if (selector.isEmpty()) {
			selector = arg;
		} else {
			return Result::Err(
				u"usage: control.click <objectName | #index> [--all] [--mouse]"_q);
		}
	}
	if (selector.isEmpty()) {
		return Result::Err(
			u"usage: control.click <objectName | #index> [--all] [--mouse]"_q);
	}
	const auto target = findControl(selector, all);
	if (!target) {
		return Result::Err(u"widget not found: "_q
			+ selector
			+ u", run control.list to list"_q);
	}
	if (!target->isVisible()) {
		return Result::Err(u"widget is not visible: "_q + selector);
	}
	if (!target->isEnabled()) {
		return Result::Err(u"widget is disabled: "_q + selector);
	}
	// 语义触发优先：AbstractButton 直接执行控件动作，不受命中偏移和遮挡影响。
	// lib_ui 不挂 Q_OBJECT，qobject_cast 不可用，dynamic_cast 走 RTTI。
	if (const auto button = dynamic_cast<Ui::AbstractButton*>(target)
		; button && !mouse) {
		// 回调可能销毁按钮自身（菜单项点击后 PopupMenu 整体销毁），
		// 返回字段必须先拷值，触发后不再访问 target。
		const auto className = QString::fromLatin1(
			target->metaObject()->className()).toStdString();
		const auto objectName = target->objectName().toStdString();
		activateButton(button);
		return Result::Ok(Compact(json{
			{ "class", className },
			{ "name", objectName },
			{ "mode", "semantic" },
		}));
	}
	// 命中测试找最深子控件，模拟真实分发：事件先给子控件，不消费再冒泡。
	const auto center = target->rect().center();
	const auto root = mouse ? target->window() : target;
	const auto hit = root->childAt(root->mapFromGlobal(
		target->mapToGlobal(center)));
	if (mouse && (!hit || (hit != target && !target->isAncestorOf(hit)))) {
		return Result::Err(u"widget is covered at its center"_q);
	}
	const auto receiver = hit ? static_cast<QWidget*>(hit) : target;
	const auto local = QPointF(receiver->mapFromGlobal(target->mapToGlobal(center)));
	const auto windowPos = QPointF(
		receiver->window()->mapFromGlobal(receiver->mapToGlobal(local.toPoint())));
	const auto global = QPointF(receiver->mapToGlobal(local.toPoint()));
	// 事件处理（release 触发点击回调）可能销毁 receiver/target（如菜单项
	// 点击后 PopupMenu 整体销毁），返回字段先拷值，每步发送前用 QPointer 验活。
	const auto targetClass = QString::fromLatin1(
		target->metaObject()->className()).toStdString();
	const auto targetName = target->objectName().toStdString();
	const auto receiverClass = QString::fromLatin1(
		receiver->metaObject()->className()).toStdString();
	const auto receiverName = receiver->objectName().toStdString();
	const auto alive = QPointer<QWidget>(receiver);

	auto hover = QEnterEvent(local, windowPos, global);
	QApplication::sendEvent(receiver, &hover);
	auto press = QMouseEvent(
		QEvent::MouseButtonPress,
		local,
		windowPos,
		global,
		Qt::LeftButton,
		Qt::LeftButton,
		Qt::NoModifier);
	const auto handled = QApplication::sendEvent(receiver, &press);
	if (alive) {
		auto release = QMouseEvent(
			QEvent::MouseButtonRelease,
			local,
			windowPos,
			global,
			Qt::LeftButton,
			Qt::NoButton,
			Qt::NoModifier);
		QApplication::sendEvent(receiver, &release);
	}
	if (alive) {
		auto leave = QEvent(QEvent::Leave);
		QApplication::sendEvent(receiver, &leave);
	}
	return Result::Ok(Compact(json{
		{ "class", targetClass },
		{ "name", targetName },
		{ "receiver", receiverClass },
		{ "receiverName", receiverName },
		{ "point", json{ global.x(), global.y() } },
		{ "handled", handled },
		{ "destroyed", !alive },
	}));
}

// 直接修改输入控件，供多行布局验证使用，不触发发送动作。
[[nodiscard]] Result controlInput(const QStringList &args) {
	if (args.size() != 2 || !args[1].startsWith(u"b64:"_q)) {
		return Result::Err(u"usage: control.input <objectName> <base64>"_q);
	}
	const auto target = findControl(args.front());
	const auto field = dynamic_cast<Ui::InputField*>(target);
	if (!field || !field->isVisible() || !field->isEnabled()) {
		return Result::Err(u"editable input not found"_q);
	}
	const auto previous = field->getLastText();
	const auto text = QString::fromUtf8(
		QByteArray::fromBase64(args[1].mid(4).toLatin1()));
	field->setTextWithTags({ text, {} });
	return Result::Ok(Compact(json{
		{ "previousText", previous.toStdString() },
		{ "length", text.size() },
		{ "height", field->height() },
	}));
}

[[nodiscard]] Result controlKey(const QStringList &args) {
	if (args.size() != 2) {
		return Result::Err(u"usage: control.key <objectName | #index | @menu> <key>"_q);
	}
	static const auto keys = QMap<QString, Qt::Key>{
		{ u"escape"_q, Qt::Key_Escape },
		{ u"up"_q, Qt::Key_Up },
		{ u"down"_q, Qt::Key_Down },
		{ u"left"_q, Qt::Key_Left },
		{ u"right"_q, Qt::Key_Right },
		{ u"enter"_q, Qt::Key_Return },
		{ u"tab"_q, Qt::Key_Tab },
	};
	const auto key = keys.constFind(args[1]);
	const auto target = QPointer<QWidget>(findControl(args[0]));
	if (key == keys.cend() || !target || !target->isVisible() || !target->isEnabled()) {
		return Result::Err(u"expected a visible enabled control and a supported key"_q);
	}
	auto press = QKeyEvent(QEvent::KeyPress, *key, Qt::NoModifier);
	QApplication::sendEvent(target, &press);
	if (target) {
		auto release = QKeyEvent(QEvent::KeyRelease, *key, Qt::NoModifier);
		QApplication::sendEvent(target, &release);
	}
	return Result::Ok(u"sent"_q);
}

[[nodiscard]] Result controlHover(const QStringList &args) {
	if (args.size() != 2 || (args[1] != u"on"_q && args[1] != u"off"_q)) {
		return Result::Err(u"usage: control.hover <objectName | #index> <on|off>"_q);
	}
	const auto button = dynamic_cast<Ui::AbstractButton*>(findControl(args[0]));
	if (!button || !button->isVisible() || button->isDisabled()) {
		return Result::Err(u"visible enabled button not found"_q);
	}
	// 仅设置绘制状态，不点击，也不移动系统光标。
	button->setSynteticOver(args[1] == u"on"_q);
	return Result::Ok(Compact(json{ { "hovered", button->isOver() } }));
}

[[nodiscard]] Result controlPointer(const QStringList &args) {
	if (args.size() != 1 && args.size() != 3) {
		return Result::Err(u"usage: control.pointer <objectName | #index> [x y]"_q);
	}
	const auto target = findControl(args[0]);
	if (!target || !target->isVisible() || !target->isEnabled()) {
		return Result::Err(u"visible enabled control not found"_q);
	}
	// 只保留弱引用；合成事件不改变系统光标位置。
	static auto pointed = QPointer<QWidget>();
	if (args.size() == 1) {
		if (pointed) {
			auto leave = QEvent(QEvent::Leave);
			QApplication::sendEvent(pointed, &leave);
			pointed.clear();
		}
		return Result::Ok(u"left"_q);
	}
	auto xOk = false;
	auto yOk = false;
	const auto point = QPoint(args[1].toInt(&xOk), args[2].toInt(&yOk));
	if (!xOk || !yOk || !target->rect().contains(point)) {
		return Result::Err(u"expected coordinates inside the control"_q);
	}
	const auto global = target->mapToGlobal(point);
	const auto root = target->window();
	const auto receiver = root->childAt(root->mapFromGlobal(global));
	if (!receiver || (receiver != target && !target->isAncestorOf(receiver))) {
		return Result::Err(u"control is covered at the requested point"_q);
	}
	const auto local = receiver->mapFromGlobal(global);
	if (pointed != receiver) {
		if (pointed) {
			auto leave = QEvent(QEvent::Leave);
			QApplication::sendEvent(pointed, &leave);
		}
		pointed = receiver;
		auto enter = QEnterEvent(local, root->mapFromGlobal(global), global);
		QApplication::sendEvent(pointed, &enter);
	}
	if (pointed) {
		auto move = QMouseEvent(QEvent::MouseMove, local, global,
			Qt::NoButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(pointed, &move);
	}
	return Result::Ok(Compact(json{
		{ "receiver", pointed ? pointed->metaObject()->className() : "" },
		{ "point", json{ point.x(), point.y() } },
	}));
}

[[nodiscard]] Result controlScroll(const QStringList &args) {
	if (args.isEmpty() || args.size() > 2) {
		return Result::Err(u"usage: control.scroll <objectName | #index> [top]"_q);
	}
	const auto target = findControl(args.front());
	const auto scroll = dynamic_cast<Ui::ElasticScroll*>(target);
	const auto area = dynamic_cast<Ui::ScrollArea*>(target);
	if ((!scroll && !area) || !target->isVisible()) {
		return Result::Err(u"visible scroll area not found"_q);
	}
	if (args.size() == 2) {
		auto ok = false;
		const auto top = args[1].toInt(&ok);
		if (!ok) {
			return Result::Err(u"expected integer scroll position"_q);
		}
		if (scroll) {
			scroll->scrollToY(top);
		} else {
			area->scrollToY(top);
		}
	}
	return Result::Ok(Compact(json{
		{ "top", scroll ? scroll->scrollTop() : area->scrollTop() },
		{ "maximum", scroll ? scroll->scrollTopMax() : area->scrollTopMax() },
		{ "height", target->height() },
	}));
}


[[nodiscard]] Result describeControlValue(QWidget *target) {
	auto data = json{{"name", target->objectName().toStdString()}, {"text", WidgetText(target).toStdString()},
		{"enabled", target->isEnabled()}};
	if (const auto slider = dynamic_cast<Ui::ContinuousSlider*>(target)) {
		data["value"] = slider->value();
		data["kind"] = "slider";
		data["minimum"] = 0;
		data["maximum"] = 1;
	} else if (const auto field = dynamic_cast<Ui::InputField*>(target)) {
		data["value"] = field->getLastText().toStdString();
		data["kind"] = "text";
	} else if (const auto line = qobject_cast<QLineEdit*>(target)) {
		data["value"] = line->text().toStdString();
		data["kind"] = "text";
	} else if (const auto rp = dynamic_cast<Ui::RpWidget*>(target); rp && rp->accessibilityState().checkable) {
		data["value"] = rp->accessibilityState().checked;
		data["kind"] = "check";
	} else if (const auto combo = qobject_cast<QComboBox*>(target)) {
		data["value"] = combo->currentIndex();
		data["kind"] = "choice";
		auto choices = json::array();
		for (auto i = 0; i != combo->count(); ++i) choices.push_back(combo->itemText(i).toStdString());
		data["choices"] = choices;
	}
	if (const auto interface = QAccessible::queryAccessibleInterface(target)) {
		data["accessibleValue"] = interface->text(QAccessible::Value).toStdString();
		auto actions = json::array();
		if (const auto action = interface->actionInterface()) {
			for (const auto &name : action->actionNames()) actions.push_back(name.toStdString());
		}
		data["actions"] = actions;
	}
	return Result::Ok(Compact(data));
}

[[nodiscard]] Result controlGet(const QStringList &args) {
	if (args.size() != 1) return Result::Err(u"usage: control.get <target>"_q);
	const auto target = findControl(args.front());
	if (!target || !target->isVisible()) return Result::Err(u"visible control not found"_q);
	return describeControlValue(target);
}

[[nodiscard]] Result controlSet(const QStringList &args) {
	if (args.size() != 2) return Result::Err(u"usage: control.set <target> <value>"_q);
	const auto target = QPointer<QWidget>(findControl(args[0]));
	if (!target || !target->isVisible() || !target->isEnabled()) {
		return Result::Err(u"visible enabled control not found"_q);
	}
	if (const auto slider = dynamic_cast<Ui::ContinuousSlider*>(target.data())) {
		auto ok = false;
		const auto value = args[1].toDouble(&ok);
		if (!ok || !std::isfinite(value) || value < 0 || value > 1 || slider->isDisabled()) {
			return Result::Err(u"expected an enabled slider and a value between 0 and 1"_q);
		}
		slider->setValueForDebug(value);
	} else if (const auto field = dynamic_cast<Ui::InputField*>(target.data())) {
		field->setTextWithTags({args[1], {}});
	} else if (const auto line = qobject_cast<QLineEdit*>(target.data())) {
		if (line->isReadOnly()) return Result::Err(u"input is read only"_q);
		line->setText(args[1]);
	} else if (const auto rp = dynamic_cast<Ui::RpWidget*>(target.data()); rp && rp->accessibilityState().checkable) {
		if (args[1] != u"true"_q && args[1] != u"false"_q) return Result::Err(u"expected true or false"_q);
		const auto checked = args[1] == u"true"_q;
		if (rp->accessibilityState().checked != checked) {
			if (dynamic_cast<Ui::Radiobutton*>(rp) && !checked) {
				return Result::Err(u"select another radio button to change this value"_q);
			}
			const auto button = dynamic_cast<Ui::AbstractButton*>(rp);
			if (!button) return Result::Err(u"control has no click handler"_q);
			activateButton(button);
		}
	} else if (const auto combo = qobject_cast<QComboBox*>(target.data())) {
		auto ok = false;
		const auto index = args[1].toInt(&ok);
		if (!ok || index < 0 || index >= combo->count()) return Result::Err(u"choice index out of range"_q);
		combo->setCurrentIndex(index);
		if (target) Q_EMIT combo->activated(index);
	} else {
		return Result::Err(u"control has no editable value; use control.get or control.click"_q);
	}
	return target ? describeControlValue(target) : Result::Ok(u"control closed after applying the value"_q);
}

[[nodiscard]] Result controlAction(const QStringList &args) {
	if (args.size() != 2) return Result::Err(u"usage: control.action <target> <action>"_q);
	const auto target = findControl(args[0]);
	if (!target || !target->isVisible() || !target->isEnabled()) return Result::Err(u"visible enabled control not found"_q);
	const auto interface = QAccessible::queryAccessibleInterface(target);
	const auto action = interface ? interface->actionInterface() : nullptr;
	if (!action || !action->actionNames().contains(args[1])) return Result::Err(u"action not available; inspect control.get"_q);
	action->doAction(args[1]);
	return Result::Ok();
}


[[nodiscard]] Result controlMouse(const QStringList &args) {
	if (args.size() < 3 || args.size() > 4) return Result::Err(u"usage: control.mouse <target> <x> <y> [left|right|double]"_q);
	const auto target = QPointer<QWidget>(findControl(args[0]));
	if (!target || !target->isVisible() || !target->isEnabled()) return Result::Err(u"visible enabled control not found"_q);
	auto xOk = false;
	auto yOk = false;
	const auto point = QPoint(args[1].toInt(&xOk), args[2].toInt(&yOk));
	const auto mode = args.size() == 4 ? args[3] : u"left"_q;
	if (!xOk || !yOk || !target->rect().contains(point)) return Result::Err(u"expected coordinates inside the control"_q);
	if (mode != u"left"_q && mode != u"right"_q && mode != u"double"_q) return Result::Err(u"unknown mouse action"_q);
	const auto global = target->mapToGlobal(point);
	const auto root = target->window();
	auto receiver = QPointer<QWidget>(root->childAt(root->mapFromGlobal(global)));
	if (!receiver) receiver = root;
	if (receiver != target && !target->isAncestorOf(receiver)) return Result::Err(u"control is covered at the requested point"_q);
	const auto local = receiver->mapFromGlobal(global);
	const auto button = mode == u"right"_q ? Qt::RightButton : Qt::LeftButton;
	auto press = QMouseEvent(QEvent::MouseButtonPress, local, global, button, button, Qt::NoModifier);
	QApplication::sendEvent(receiver, &press);
	if (receiver) {
		auto release = QMouseEvent(QEvent::MouseButtonRelease, local, global, button, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(receiver, &release);
	}
	if (receiver && mode == u"double"_q) {
		auto doubleClick = QMouseEvent(QEvent::MouseButtonDblClick, local, global, button, button, Qt::NoModifier);
		QApplication::sendEvent(receiver, &doubleClick);
		if (receiver) {
			auto release = QMouseEvent(QEvent::MouseButtonRelease, local, global, button, Qt::NoButton, Qt::NoModifier);
			QApplication::sendEvent(receiver, &release);
		}
	}
	if (receiver && mode == u"right"_q) {
		auto context = QContextMenuEvent(QContextMenuEvent::Mouse, local, global);
		QApplication::sendEvent(receiver, &context);
	}
	return Result::Ok();
}

} // namespace

const HandlerMap &ControlHandlers() {
	static const auto result = HandlerMap{
		{ u"control.list"_q, &ControlList },
		{ u"control.click"_q, &ControlClick },
		{ u"control.input"_q, &controlInput },
		{ u"control.scroll"_q, &controlScroll },
		{ u"control.hover"_q, &controlHover },
		{ u"control.pointer"_q, &controlPointer },
		{ u"control.key"_q, &controlKey },
		{ u"control.get"_q, &controlGet },
		{ u"control.set"_q, &controlSet },
		{ u"control.action"_q, &controlAction },
		{ u"control.mouse"_q, &controlMouse },
	};
	return result;
}

} // namespace AyuDebug::Commands
#endif // _DEBUG
