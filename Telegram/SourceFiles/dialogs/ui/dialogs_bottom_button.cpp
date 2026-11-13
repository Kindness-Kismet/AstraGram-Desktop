/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_bottom_button.h"

#include "extras/features/window_material/window_material.h"
#include "ui/effects/radial_animation.h"
#include "ui/painter.h"
#include "styles/style_dialogs.h"
#include "styles/style_dialogs_widget.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

namespace Dialogs {

BottomButton::BottomButton(
	QWidget *parent,
	const QString &text,
	const style::FlatButton &st,
	const style::icon &icon,
	const style::icon &iconOver)
: RippleButton(parent, st.ripple)
, _text(text)
, _st(st)
, _icon(icon)
, _iconOver(iconOver) {
	ExtrasFeatures::WindowMaterial::watchSurface(this);
	resize(st::columnMinimalWidthLeft, _st.height);
}

void BottomButton::setText(const QString &text) {
	_text = text;
	update();
}

void BottomButton::radialAnimationCallback() {
	if (!anim::Disabled() && width() < st::columnMinimalWidthLeft) {
		update();
	}
}

void BottomButton::onStateChanged(
		State was,
		StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	if ((was & StateFlag::Disabled) != (state() & StateFlag::Disabled)) {
		_loading = isDisabled()
			? std::make_unique<Ui::InfiniteRadialAnimation>(
				[=] { radialAnimationCallback(); },
				st::dialogsLoadMoreLoading)
			: nullptr;
		if (_loading) {
			_loading->start();
		}
	}
	update();
}

void BottomButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto over = isOver() && !isDisabled();

	auto r = QRect(0, height() - _st.height, width(), _st.height);

	p.fillRect(r, over ? _st.overBgColor->c
		: ExtrasFeatures::WindowMaterial::surfaceColor(this, _st.bgColor->c));
	if (!isDisabled()) {
		paintRipple(p, 0, 0);
	}

	const auto &font = over ? _st.overFont : _st.font;
	p.setFont(font);
	p.setRenderHint(QPainter::TextAntialiasing);
	p.setPen(over ? _st.overColor : _st.color);

	if (width() >= st::columnMinimalWidthLeft) {
		r.setTop(_st.textTop);
		p.drawText(r, _text, style::al_top);
	} else if (isDisabled() && _loading) {
		_loading->draw(
			p,
			QPoint(
				(width() - st::dialogsLoadMoreLoading.size.width()) / 2,
				(height() - st::dialogsLoadMoreLoading.size.height()) / 2),
			width());
	} else {
		(over ? _iconOver : _icon).paintInCenter(p, r);
	}
}

} // namespace Dialogs
