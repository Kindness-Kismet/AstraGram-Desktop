#include "extras/ui/components/window_appearance.h"

#include "base/event_filter.h"
#include "extras/features/window_material/window_material.h"
#include "window/window_shell_color.h"
#include "styles/style_window.h"

#include <QtCore/QEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>

namespace ExtrasUi::WindowAppearance {

void paintTopCorners(
		QPainter &p,
		QRect rect,
		QColor background,
		bool left,
		bool right) {
	const auto radius = st::windowCardRadius;
	auto square = QPainterPath();
	square.addRect(QRect(0, 0, radius, radius));
	auto rounded = QPainterPath();
	rounded.addEllipse(QRect(0, 0, 2 * radius, 2 * radius));
	const auto corner = square.subtracted(rounded);
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.translate(rect.topLeft());
	if (left) {
		p.fillPath(corner, background);
	}
	if (right) {
		p.translate(rect.width(), 0);
		p.scale(-1., 1.);
		p.fillPath(corner, background);
	}
	p.restore();
}

Ui::RpWindow::Surface surface() {
	return ExtrasFeatures::WindowMaterial::availableModes().size() > 1
		? Ui::RpWindow::Surface::NativeMaterial
		: Ui::RpWindow::Surface::Opaque;
}

void initialize(not_null<Ui::RpWindow*> window) {
	ExtrasFeatures::WindowMaterial::initialize(window);
	const auto body = window->body();
	const auto corners = Ui::CreateChild<Ui::RpWidget>(body);
	corners->setObjectName(u"window.pageCorners"_q);
	corners->setAttribute(Qt::WA_TransparentForMouseEvents);
	body->widthValue() | rpl::on_next([=](int width) {
		corners->setGeometry(0, 0, width, st::windowCardRadius);
	}, corners->lifetime());
	corners->paintRequest() | rpl::on_next([=] {
		if (ExtrasFeatures::WindowMaterial::isActive(body)) {
			return;
		}
		auto p = QPainter(corners);
		paintTopCorners(p, corners->rect(), Window::ShellBackgroundColor(window));
	}, corners->lifetime());
	ExtrasFeatures::WindowMaterial::changes(corners) | rpl::on_next([=] {
		corners->update();
	}, corners->lifetime());
	style::PaletteChanged() | rpl::on_next([=] {
		corners->update();
	}, corners->lifetime());
	window->events() | rpl::on_next([=](not_null<QEvent*> event) {
		if (event->type() == QEvent::WindowActivate
			|| event->type() == QEvent::WindowDeactivate) {
			corners->update();
		}
	}, corners->lifetime());
	base::install_event_filter(corners, body, [=](not_null<QEvent*> event) {
		if (event->type() == QEvent::ChildAdded) {
			InvokeQueued(corners, [=] { corners->raise(); });
		}
		return base::EventFilterResult::Continue;
	});
	corners->show();
}

} // namespace ExtrasUi::WindowAppearance
