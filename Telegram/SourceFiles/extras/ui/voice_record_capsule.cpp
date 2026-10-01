#include "extras/ui/voice_record_capsule.h"
#include "extras/ui/components/floating_surface.h"

#include "lang/lang_keys.h"
#include "ui/abstract_button.h"
#include "ui/painter.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"

#include <QtGui/QContextMenuEvent>

namespace ExtrasUi {

VoiceRecordCapsule::VoiceRecordCapsule(
		not_null<Ui::RpWidget*> parent,
		not_null<Ui::RpWidget*> sendAnchor,
		VoiceRecordActions actions)
: RpWidget(parent)
, _sendAnchor(sendAnchor)
, _actions(std::move(actions))
, _cancel(std::make_unique<Ui::AbstractButton>(this))
, _pause(std::make_unique<Ui::AbstractButton>(this))
, _confirm(std::make_unique<Ui::AbstractButton>(this))
, _play(std::make_unique<Ui::AbstractButton>(this))
, _refresh([=] { refreshPreview(); }) {
	setObjectName(u"recordingCapsule"_q);
	// 复用输入框的磨砂、配色、边框和圆角，录音层只提供内容。
	FloatingSurface::attach(this, {
		.radius = st::historyComposeCapsuleRadius,
		.background = ChatSurfaceBackground,
		.border = ChatSurfaceBorder,
		.borderWidth = st::lineWidth,
		.maskInput = true,
	});
	_cancel->setObjectName(u"recording.cancel"_q);
	_pause->setObjectName(u"recording.pause"_q);
	_confirm->setObjectName(u"recording.confirm"_q);
	_play->setObjectName(u"recording.play"_q);
	setupButton(_cancel.get(), Icon::Cancel, false);
	setupButton(_pause.get(), Icon::Pause, false);
	setupButton(_confirm.get(), Icon::Confirm, true);
	setupButton(_play.get(), Icon::Play, false);
	_cancel->setClickedCallback(_actions.cancel);
	_pause->setClickedCallback(_actions.pause);
	_confirm->setClickedCallback(_actions.confirm);
	_play->setClickedCallback(_actions.play);
	_cancel->setAccessibleName(tr::lng_record_cancel_recording(tr::now));
	_confirm->events() | rpl::on_next([=](not_null<QEvent*> event) {
		if (event->type() == QEvent::ContextMenu && _onceAllowed) {
			showMenu(static_cast<QContextMenuEvent*>(event.get())->globalPos());
		}
	}, lifetime());
	setMouseTracking(true);
	_levels.fill(0.08, 120);
	updateButtons();
	_sendAnchor->geometryValue() | rpl::on_next([=] {
		updateGeometry();
	}, lifetime());
	_refresh.callEach(50);
	show();
}

VoiceRecordCapsule::~VoiceRecordCapsule() = default;

void VoiceRecordCapsule::setupButton(
		Ui::AbstractButton *button,
		Icon icon,
		bool primary) {
	button->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(button);
		p.setRenderHint(QPainter::Antialiasing);
		const auto nativeSide = std::min({
			st::historySend.inner.icon.width() + 2 * st::historySend.sendIconFillPadding,
			st::historySend.inner.icon.height() + 2 * st::historySend.sendIconFillPadding,
			button->width(), button->height() });
		const auto circle = QRect(
			(button->width() - nativeSide) / 2,
			(button->height() - nativeSide) / 2,
			nativeSide, nativeSide);
		const auto center = QRectF(circle).center();
		const auto radius = primary
			? nativeSide / 2.
			: button->width() * .42;
		if (primary || button == _pause.get() || button->isOver()) {
			auto fill = st::windowBgActive->c;
			if (!primary) {
				fill.setAlphaF(button->isOver() ? .18 : .09);
			}
			p.setPen(Qt::NoPen);
			p.setBrush(fill);
			p.drawEllipse(center, radius, radius);
		}
		auto current = icon;
		if (button == _pause.get() && _state != State::Recording) {
			current = Icon::Microphone;
		} else if (button == _confirm.get() && _state == State::Ready) {
			current = Icon::Send;
		} else if (button == _play.get() && _preview.playing) {
			current = Icon::Pause;
		}
		paintIcon(p, current, center, primary
			? st::windowFgActive->c
			: button == _cancel.get()
			? st::historyComposeIconFg->c
			: st::windowBgActive->c);
		if (primary && _once) {
			p.setPen(st::windowFgActive->c);
			p.setFont(st::normalFont);
			p.drawText(button->rect().adjusted(0, 0, -3, -1), Qt::AlignRight | Qt::AlignBottom, u"1"_q);
		}
	}, button->lifetime());
	button->setPointerCursor(true);
	button->show();
}

void VoiceRecordCapsule::paintIcon(
		QPainter &p,
		Icon icon,
		QPointF center,
		QColor color) {
	p.save();
	p.translate(center);
	const auto scale = st::historySend.inner.width / 40.;
	p.scale(scale, scale);
	p.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.setBrush(Qt::NoBrush);
	switch (icon) {
	case Icon::Cancel:
		p.drawLine(QPointF(-5, -5), QPointF(5, 5));
		p.drawLine(QPointF(5, -5), QPointF(-5, 5));
		break;
	case Icon::Pause:
		p.drawLine(QPointF(-3, -6), QPointF(-3, 6));
		p.drawLine(QPointF(3, -6), QPointF(3, 6));
		break;
	case Icon::Confirm:
		p.drawPolyline(QPolygonF(QVector<QPointF>{ QPointF(-7, 0), QPointF(-2, 5), QPointF(8, -6) }));
		break;
	case Icon::Microphone:
		p.drawRoundedRect(QRectF(-3, -8, 6, 11), 3, 3);
		p.drawArc(QRectF(-6, -4, 12, 11), 180 * 16, 180 * 16);
		p.drawLine(QPointF(0, 7), QPointF(0, 10));
		p.drawLine(QPointF(-3, 10), QPointF(3, 10));
		break;
	case Icon::Play:
		p.setBrush(color);
		p.drawPolygon(QPolygonF(QVector<QPointF>{ QPointF(-3, -6), QPointF(6, 0), QPointF(-3, 6) }));
		break;
	case Icon::Send:
		p.setBrush(color);
		p.drawPolygon(QPolygonF(QVector<QPointF>{ QPointF(-7, -7), QPointF(9, 0), QPointF(-7, 7), QPointF(-4, 0) }));
		break;
	}
	p.restore();
}

void VoiceRecordCapsule::setState(State state) {
	if (_state == state) {
		return;
	}
	_state = state;
	_drag = Drag::None;
	if (state == State::Recording) {
		_duration = _preview.duration;
		_preview = {};
		_levels.fill(.08);
	}
	updateButtons();
	refreshPreview();
}

void VoiceRecordCapsule::updateButtons() {
	_play->setVisible(_state != State::Recording);
	_pause->setVisible(_state != State::Ready);
	_pause->setAccessibleName((_state == State::Recording
		? tr::lng_record_lock_pause
		: tr::lng_record_lock_resume)(tr::now));
	_confirm->setAccessibleName((_state == State::Ready
		? tr::lng_send_button : tr::lng_box_ok)(tr::now));
	_play->setAccessibleName((_preview.playing
		? tr::lng_record_lock_pause : tr::lng_record_lock_play)(tr::now));
	updateGeometry();
	update();
	_pause->update();
	_confirm->update();
	_play->update();
}

void VoiceRecordCapsule::updateGeometry() {
	const auto unit = st::historySend.inner.width;
	const auto gap = st::historyComposeCapsulePadding;
	const auto position = mapFromGlobal(_sendAnchor->mapToGlobal(QPoint()));
	const auto h = _sendAnchor->height();
	// 输入区调整高度时，发送按钮和录音条的位置分步更新，纵向以自身中心为准。
	const auto top = (height() - h) / 2;
	_confirm->setGeometry(position.x(), top, _sendAnchor->width(), h);
	_cancel->setGeometry(gap, top, unit, h);
	_pause->setGeometry(_confirm->x() - unit - gap, top, unit, h);
	_play->setGeometry(_cancel->geometry().right() + gap, top, unit / 2, h);
}

void VoiceRecordCapsule::resizeEvent(QResizeEvent *event) {
	RpWidget::resizeEvent(event);
	updateGeometry();
}

QRectF VoiceRecordCapsule::waveformRect() const {
	const auto unit = st::historySend.inner.width;
	const auto left = _play->geometry().right()
		+ st::historyComposeCapsulePadding * 4 + unit * 1.1;
	const auto right = (_state == State::Ready ? _confirm->x() : _pause->x())
		- st::historyComposeCapsulePadding * 2;
	const auto waveHeight = height() * .46;
	return QRectF(left, (height() - waveHeight) / 2,
		std::max(1., right - left), waveHeight);
}

void VoiceRecordCapsule::paintEvent(QPaintEvent *event) {
	auto p = QPainter(this);
	p.setRenderHint(QPainter::Antialiasing);
	const auto unit = st::historySend.inner.width;
	const auto textX = _play->geometry().right()
		+ st::historyComposeCapsulePadding * 2;
	const auto shownDuration = _state == State::Recording ? _duration : _preview.duration;
	const auto seconds = shownDuration / 1000;
	const auto text = u"%1:%2"_q.arg(seconds / 60, 2, 10, QChar('0'))
		.arg(seconds % 60, 2, 10, QChar('0'));
	p.setFont(st::normalFont);
	p.setPen(st::historyRecordDurationFg);
	p.drawText(QRectF(textX, 0, unit * 1.1, height()), Qt::AlignVCenter | Qt::AlignLeft, text);
	if (_state == State::Recording) {
		p.setPen(Qt::NoPen);
		p.setBrush(st::historyRecordVoiceFgInactive->c);
		const auto radius = std::max(2., unit * .045);
		p.drawEllipse(QPointF(_cancel->geometry().right() + unit * .3, height() / 2.), radius, radius);
	}
	const auto wave = waveformRect();
	const auto step = std::max(4., unit * .115);
	const auto count = std::max(1, int(wave.width() / step));
	p.setPen(Qt::NoPen);
	for (auto i = 0; i != count; ++i) {
		const auto progress = (i + .5) / count;
		const auto value = _state == State::Recording
			? _levels[std::clamp(int(_levels.size()) - count + i, 0, int(_levels.size()) - 1)]
			: _preview.waveform.empty() ? .1
			: uchar(_preview.waveform[std::min(int(_preview.waveform.size()) - 1,
				int(progress * _preview.waveform.size()))]) / 31.;
		const auto h = std::max(3., wave.height() * value);
		auto color = st::windowBgActive->c;
		if (_state != State::Recording && (progress < _preview.trimLeft
			|| progress > _preview.trimRight)) {
			color.setAlphaF(.22);
		} else if (_state != State::Recording && progress > _preview.progress) {
			color.setAlphaF(.4);
		}
		p.setBrush(color);
		p.drawRoundedRect(QRectF(wave.x() + i * step,
			wave.center().y() - h / 2, step * .45, h), step * .22, step * .22);
	}
	if (_state != State::Recording) {
		auto color = st::windowBgActive->c;
		color.setAlphaF(.65);
		p.setPen(QPen(color, 2., Qt::SolidLine, Qt::RoundCap));
		for (const auto progress : { _preview.trimLeft, _preview.trimRight }) {
			const auto x = wave.x() + progress * wave.width();
			p.drawLine(QPointF(x, wave.y() - 2), QPointF(x, wave.bottom() + 2));
		}
	}
}

void VoiceRecordCapsule::updateLevel(ushort level, crl::time duration) {
	_duration = duration;
	_levels.pop_front();
	_levels.push_back(std::clamp(level / 12000., .08, 1.));
	update();
}

void VoiceRecordCapsule::refreshPreview() {
	if (_state == State::Recording || _drag == Drag::Seek || !isVisible()) {
		return;
	}
	_preview = _actions.preview();
	_play->setAccessibleName((_preview.playing
		? tr::lng_record_lock_pause : tr::lng_record_lock_play)(tr::now));
	_play->update();
	update();
}

qreal VoiceRecordCapsule::progressAt(int x) const {
	const auto wave = waveformRect();
	return std::clamp((x - wave.x()) / wave.width(), 0., 1.);
}

void VoiceRecordCapsule::mousePressEvent(QMouseEvent *event) {
	if (_state == State::Recording || event->button() != Qt::LeftButton) {
		return;
	}
	const auto wave = waveformRect();
	if (!wave.adjusted(-6, -6, 6, 6).contains(event->pos())) {
		return;
	}
	const auto x = event->pos().x();
	const auto hit = st::historyComposeCapsulePadding * 2;
	_drag = (std::abs(x - (wave.x() + wave.width() * _preview.trimLeft)) <= hit)
		? Drag::Left
		: (std::abs(x - (wave.x() + wave.width() * _preview.trimRight)) <= hit)
		? Drag::Right : Drag::Seek;
	if (_drag == Drag::Seek) {
		_actions.beginSeek();
	}
	updateDrag(event->pos());
	event->accept();
}

void VoiceRecordCapsule::updateDrag(QPoint position) {
	const auto progress = progressAt(position.x());
	if (_drag == Drag::Seek) {
		_preview.progress = std::clamp(progress, _preview.trimLeft, _preview.trimRight);
		update();
		return;
	}
	const auto minRange = _preview.duration ? std::min(1., 200. / _preview.duration) : 1.;
	if (_drag == Drag::Left) {
		_preview.trimLeft = std::clamp(progress, 0., std::max(0., _preview.trimRight - minRange));
	} else if (_drag == Drag::Right) {
		_preview.trimRight = std::clamp(progress, std::min(1., _preview.trimLeft + minRange), 1.);
	} else {
		return;
	}
	_actions.trim(_preview.trimLeft, _preview.trimRight);
	update();
}

void VoiceRecordCapsule::mouseMoveEvent(QMouseEvent *event) {
	if (_drag != Drag::None) {
		updateDrag(event->pos());
	}
}

void VoiceRecordCapsule::mouseReleaseEvent(QMouseEvent *event) {
	if (_drag != Drag::None && event->button() == Qt::LeftButton) {
		updateDrag(event->pos());
		if (_drag == Drag::Seek) {
			_actions.seek(_preview.progress);
		}
		_drag = Drag::None;
	}
}

void VoiceRecordCapsule::setOnceAllowed(bool allowed) {
	_onceAllowed = allowed;
}

bool VoiceRecordCapsule::once() const {
	return _once;
}

bool VoiceRecordCapsule::takeOnce() {
	const auto result = _once;
	_once = false;
	_confirm->update();
	return result;
}

void VoiceRecordCapsule::showMenu(QPoint position) {
	_menu = std::make_unique<Ui::PopupMenu>(this);
	const auto action = _menu->addAction(tr::lng_ttl_period_once(tr::now), [=] {
		_once = !_once;
		_confirm->update();
	});
	action->setCheckable(true);
	action->setChecked(_once);
	_menu->popup(position);
}

} // namespace ExtrasUi
