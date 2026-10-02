#include "extras/ui/components/empty_state_icon.h"

#include "ui/painter.h"
#include "styles/style_widgets.h"

#include <QtGui/QPainterPath>

namespace ExtrasUi {

void paintEmptyStateIcon(
		QPainter &p,
		QRect bounds,
		EmptyStateIcon icon,
		QColor color) {
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	const auto side = std::min(bounds.width(), bounds.height());
	p.translate(bounds.center().x() - side / 2., bounds.center().y() - side / 2.);
	p.scale(side / 64., side / 64.);
	p.setPen(QPen(color, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	auto fill = color;
	fill.setAlphaF(color.alphaF() * 0.07);
	p.setBrush(fill);

	switch (icon) {
	case EmptyStateIcon::Search:
	case EmptyStateIcon::NoResults:
		p.drawEllipse(QPointF(27, 27), 16, 16);
		p.drawLine(QPointF(39, 39), QPointF(52, 52));
		if (icon == EmptyStateIcon::NoResults) {
			p.drawLine(QPointF(21, 27), QPointF(33, 27));
		}
		break;
	case EmptyStateIcon::Chats: {
		auto path = QPainterPath();
		path.moveTo(18, 12);
		path.lineTo(46, 12);
		path.quadTo(52, 12, 52, 18);
		path.lineTo(52, 39);
		path.quadTo(52, 45, 46, 45);
		path.lineTo(28, 45);
		path.lineTo(16, 52);
		path.lineTo(18, 45);
		path.quadTo(12, 45, 12, 39);
		path.lineTo(12, 18);
		path.quadTo(12, 12, 18, 12);
		p.drawPath(path);
		p.drawLine(QPointF(22, 24), QPointF(42, 24));
		p.drawLine(QPointF(22, 33), QPointF(35, 33));
	} break;
	case EmptyStateIcon::Blocked:
		p.drawEllipse(QPointF(32, 32), 21, 21);
		p.drawLine(QPointF(17, 47), QPointF(47, 17));
		break;
	case EmptyStateIcon::Gifts: {
		p.drawRoundedRect(QRectF(14, 29, 36, 23), 3, 3);
		p.drawRoundedRect(QRectF(11, 21, 42, 9), 2, 2);
		p.drawLine(QPointF(32, 22), QPointF(32, 52));
		auto bow = QPainterPath();
		bow.moveTo(32, 21);
		bow.cubicTo(14, 22, 16, 7, 24, 12);
		bow.quadTo(29, 15, 32, 21);
		bow.cubicTo(50, 22, 48, 7, 40, 12);
		bow.quadTo(35, 15, 32, 21);
		p.drawPath(bow);
	} break;
	}
	p.restore();
}

object_ptr<Ui::RpWidget> createEmptyStateIcon(
		not_null<QWidget*> parent,
		EmptyStateIcon icon,
		QSize size,
		QMargins padding) {
	auto result = object_ptr<Ui::RpWidget>(parent);
	const auto raw = result.data();
	raw->setObjectName(u"emptyStateIcon"_q);
	raw->setAttribute(Qt::WA_TransparentForMouseEvents);
	raw->resize(QRect(QPoint(), size).marginsAdded(padding).size());
	raw->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(raw);
		paintEmptyStateIcon(
			p,
			QRect((raw->width() - size.width()) / 2, padding.top(), size.width(), size.height()),
			icon,
			st::windowSubTextFg->c);
	}, raw->lifetime());
	return result;
}

} // namespace ExtrasUi
