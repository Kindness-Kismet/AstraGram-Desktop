/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_icon.h"

namespace Info {
namespace Profile {

FloatingIcon::FloatingIcon(
	RpWidget *parent,
	const style::icon &icon,
	QPoint position)
: RpWidget(parent)
, _icon(&icon)
, _point(position.x(), std::max(position.y(), 0)) {
	setGeometry(QRect(
		QPoint(0, 0),
		QSize(_point.x() + _icon->width(), _point.y() + _icon->height())));
	setAttribute(Qt::WA_TransparentForMouseEvents);
	if (position.y() < 0) {
		parent->heightValue(
		) | rpl::on_next([=](int height) {
			move(0, (height - _icon->height()) / 2);
		}, lifetime());
	}
}

void FloatingIcon::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	_icon->paint(p, _point, width());
}

} // namespace Profile
} // namespace Info
