#include "extras/ui/components/drawer_menu.h"

#include "extras/extras_settings.h"
#include "ui/abstract_button.h"
#include "ui/painter.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/vertical_layout_reorder.h"
#include "styles/style_menu_icons.h"
#include "styles/style_window.h"

#include <QtCore/QPointer>
#include <QtCore/QEvent>
#include <QtCore/QTimer>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QGraphicsEffect>
#include <map>

namespace ExtrasUi {
namespace {

QRect opaqueBounds(const QImage &image) {
	auto bounds = QRect();
	for (auto y = 0; y != image.height(); ++y) {
		for (auto x = 0; x != image.width(); ++x) {
			if (qAlpha(image.pixel(x, y)) > 0) {
				bounds |= QRect(x, y, 1, 1);
			}
		}
	}
	return bounds;
}

class DrawerIconEffect final : public QGraphicsEffect {
public:
	explicit DrawerIconEffect(not_null<QWidget*> widget)
	: QGraphicsEffect(widget.get())
	, _widget(widget) {
		const auto reference = st::menuIconSettings.instance(Qt::white);
		const auto bounds = opaqueBounds(reference);
		_extent = std::max(bounds.width(), bounds.height())
			/ reference.devicePixelRatio();
	}

private:
	void draw(QPainter *p) override {
		auto offset = QPoint();
		const auto source = sourcePixmap(Qt::LogicalCoordinates, &offset, NoPad);
		if (_cacheKey != source.cacheKey()) {
			_cacheKey = source.cacheKey();
			_bounds = opaqueBounds(source.toImage());
		}
		// 机器人图标下载完成前可能没有内容。
		if (_bounds.isEmpty()) {
			return;
		}
		const auto scale = _extent / std::max(_bounds.width(), _bounds.height());
		const auto size = QSizeF(_bounds.size()) * scale;
		p->setRenderHint(QPainter::SmoothPixmapTransform);
		p->drawPixmap(QRectF(
			QPointF((_widget->width() - size.width()) / 2.,
				(_widget->height() - size.height()) / 2.), size), source, _bounds);
	}

	const not_null<QWidget*> _widget;
	qreal _extent = 0.;
	qint64 _cacheKey = 0;
	QRect _bounds;
};

class DrawerReorder final : public QObject {
public:
	explicit DrawerReorder(not_null<Ui::VerticalLayout*> layout)
	: QObject(layout.get())
	, _layout(layout) {
		auto scroll = (Ui::ScrollArea*)nullptr;
		for (auto parent = layout->parentWidget(); parent; parent = parent->parentWidget()) {
			if ((scroll = dynamic_cast<Ui::ScrollArea*>(parent))) {
				break;
			}
		}
		_reorder = scroll
			? std::make_unique<Ui::VerticalLayoutReorder>(layout, scroll)
			: std::make_unique<Ui::VerticalLayoutReorder>(layout);
		for (auto i = 0; i != layout->count(); ++i) {
			const auto row = layout->widgetAt(i);
			const auto proxy = new Ui::RpWidget(layout.get());
			proxy->hide();
			_proxies.emplace(row.get(), proxy);
			watch(row);
		}
		_reorder->setMouseEventProxy([=](int index) {
			return not_null<Ui::RpWidget*>(_proxies.at(_layout->widgetAt(index).get()));
		});
		_timer.setSingleShot(true);
		connect(&_timer, &QTimer::timeout, this, [=] { beginDrag(); });
		_reorder->updates() | rpl::on_next([=](const auto &update) {
			if (update.state != Ui::VerticalLayoutReorder::State::Applied) {
				return;
			}
			auto order = ExtrasSettings::getInstance().drawerOrder();
			auto ids = std::vector<QString>();
			for (auto i = 0; i != _layout->count(); ++i) {
				ids.push_back(itemId(_layout->widgetAt(i)));
			}
			auto next = ids.begin();
			// 未显示的选项保留原位置，只替换当前列表的顺序。
			for (auto &id : order) {
				if (ranges::contains(ids, id)) {
					id = *next++;
				}
			}
			_saving = true;
			ExtrasSettings::getInstance().setDrawerOrder(std::move(order));
			_saving = false;
		}, _lifetime);
		ExtrasSettings::getInstance().drawerOrderValue(
		) | rpl::on_next([=](const std::vector<QString> &order) {
			if (_saving) {
				return;
			}
			cancelDrag();
			_reorder->cancel();
			auto position = 0;
			for (const auto &id : order) {
				for (auto i = position; i != _layout->count(); ++i) {
					if (itemId(_layout->widgetAt(i)) == id) {
						_layout->reorderRows(i, position++);
						break;
					}
				}
			}
			_reorder->start();
		}, _lifetime);
	}

private:
	static QString itemId(not_null<QObject*> row) {
		return row->property("drawerItem").toString();
	}

	void watch(not_null<QObject*> object) {
		object->installEventFilter(this);
		for (const auto child : object->children()) {
			watch(child);
		}
	}

	bool eventFilter(QObject *watched, QEvent *event) override {
		if (event->type() == QEvent::ChildAdded) {
			watch(static_cast<QChildEvent*>(event)->child());
			return false;
		}
		if (event->type() == QEvent::Hide || event->type() == QEvent::WindowDeactivate) {
			cancelDrag();
			return false;
		}
		if (event->type() != QEvent::MouseButtonPress
			&& event->type() != QEvent::MouseMove
			&& event->type() != QEvent::MouseButtonRelease) {
			return false;
		}
		const auto mouse = static_cast<QMouseEvent*>(event);
		if (event->type() == QEvent::MouseButtonPress) {
			if (mouse->button() != Qt::LeftButton) {
				return false;
			}
			cancelDrag();
			const auto button = dynamic_cast<Ui::AbstractButton*>(watched);
			if (!button) {
				return false;
			}
			// 弹出菜单创建后会改挂到主窗口，其中的按钮不属于任何一行。
			auto widget = static_cast<QWidget*>(button);
			while (widget && widget->parentWidget() != _layout.get()) {
				widget = widget->parentWidget();
			}
			const auto proxy = _proxies.find(static_cast<Ui::RpWidget*>(widget));
			if (proxy == end(_proxies)) {
				return false;
			}
			_pressed = button;
			_cursor = button->cursor();
			_proxy = proxy->second;
			_start = mouse->globalPos();
			_timer.start(QApplication::startDragTime());
			return false;
		}
		if (!_pressed) {
			return false;
		}
		if (!_dragging) {
			if (event->type() == QEvent::MouseButtonRelease
				|| (mouse->globalPos() - _start).manhattanLength()
					> QApplication::startDragDistance()) {
				cancelDrag();
			}
			return false;
		}
		QCoreApplication::sendEvent(_proxy, event);
		if (event->type() == QEvent::MouseButtonRelease) {
			_pressed->setCursor(_cursor);
			_pressed = nullptr;
			_dragging = false;
		}
		return true;
	}

	void beginDrag() {
		if (!_pressed) {
			return;
		}
		_dragging = true;
		_pressed->clearState();
		_pressed->setCursor(Qt::ClosedHandCursor);
		auto press = QMouseEvent(QEvent::MouseButtonPress, QPointF(), _start,
			Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		QCoreApplication::sendEvent(_proxy, &press);
	}

	void cancelDrag() {
		_timer.stop();
		if (_dragging) {
			_reorder->cancel();
			_reorder->finishReordering();
			_reorder->start();
		}
		if (_pressed) {
			_pressed->setCursor(_cursor);
		}
		_pressed = nullptr;
		_dragging = false;
	}

	const not_null<Ui::VerticalLayout*> _layout;
	std::unique_ptr<Ui::VerticalLayoutReorder> _reorder;
	std::map<Ui::RpWidget*, Ui::RpWidget*> _proxies;
	QPointer<Ui::AbstractButton> _pressed;
	Ui::RpWidget *_proxy = nullptr;
	QPoint _start;
	QCursor _cursor;
	QTimer _timer;
	bool _dragging = false;
	bool _saving = false;
	rpl::lifetime _lifetime;
};

} // namespace

void fitDrawerIcon(not_null<QWidget*> icon) {
	icon->resize(st::menuIconSettings.size());
	icon->setGraphicsEffect(new DrawerIconEffect(icon));
}

void addDrawerIcon(
		not_null<Ui::AbstractButton*> button,
		not_null<const style::icon*> icon) {
	const auto widget = Ui::CreateChild<Ui::RpWidget>(button);
	widget->setAttribute(Qt::WA_TransparentForMouseEvents);
	fitDrawerIcon(widget);
	button->sizeValue() | rpl::on_next([=](QSize size) {
		widget->moveToLeft(st::mainMenuButton.iconLeft,
			(size.height() - widget->height()) / 2, size.width());
	}, widget->lifetime());
	widget->paintRequest() | rpl::on_next([=] {
		auto painter = QPainter(widget);
		icon->paint(painter, 0, 0, widget->width(), st::windowFg->c);
	}, widget->lifetime());
	widget->show();
}

void setupDrawerReorder(not_null<Ui::VerticalLayout*> layout) {
	const auto reorder = new DrawerReorder(layout);
	// 先断开代理事件，再由父控件销毁各行。
	layout->lifetime().add([=] { delete reorder; });
}

} // namespace ExtrasUi
