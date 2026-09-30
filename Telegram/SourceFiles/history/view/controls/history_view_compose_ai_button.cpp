/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_compose_ai_button.h"

#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "styles/style_chat_helpers.h"

namespace HistoryView::Controls {
ComposeAiButton::ComposeAiButton(
	QWidget *parent,
	const style::IconButton &st)
: ComposeAiButton(
	parent,
	st,
	st::historyAiComposeButtonLetters,
	&st::historyComposeIconFgOver) {
}

ComposeAiButton::ComposeAiButton(
	QWidget *parent,
	const style::IconButton &st,
	const style::icon &letters,
	const style::color *overColor)
: RippleButton(parent, st.ripple)
, _st(st)
, _letters(letters)
, _overColor(overColor) {
	resize(_st.width, _st.height);
	setCursor(style::cur_pointer);
}

void ComposeAiButton::setPremiumStar(
		QImage image,
		QPoint position,
		int outline) {
	_premiumStar = std::move(image);
	_premiumStarPosition = position;
	_premiumStarOutline = outline;
	_frame = QImage();
	update();
}

void ComposeAiButton::paintEvent(QPaintEvent *e) {
	Painter p(this);
	PainterHighQualityEnabler hq(p);

	const auto over = isDown() || isOver() || forceRippled();
	paintRipple(p, _st.rippleAreaPosition);

	if (_premiumStar.isNull()) {
		paintLetters(p, over);
		return;
	}
	renderFrame(over);
	p.drawImage(0, 0, _frame);
}

void ComposeAiButton::paintLetters(QPainter &p, bool over) {
	if (over && _overColor) {
		_letters.paintInCenter(p, rect(), (*_overColor)->c);
	} else {
		_letters.paintInCenter(p, rect());
	}
}

void ComposeAiButton::renderFrame(bool over) {
	const auto ratio = style::DevicePixelRatio();
	if (_frame.size() != size() * ratio) {
		_frame = QImage(
			size() * ratio,
			QImage::Format_ARGB32_Premultiplied);
	}
	_frame.setDevicePixelRatio(ratio);
	_frame.fill(Qt::transparent);
	auto q = QPainter(&_frame);
	auto hq = PainterHighQualityEnabler(q);
	paintLetters(q, over);
	const auto outline = _premiumStarOutline;
	q.setCompositionMode(QPainter::CompositionMode_DestinationOut);
	q.drawImage(_premiumStarPosition - QPoint(outline, 0), _premiumStar);
	q.drawImage(_premiumStarPosition + QPoint(outline, 0), _premiumStar);
	q.drawImage(_premiumStarPosition - QPoint(0, outline), _premiumStar);
	q.drawImage(_premiumStarPosition + QPoint(0, outline), _premiumStar);
	q.setCompositionMode(QPainter::CompositionMode_SourceOver);
	q.drawImage(_premiumStarPosition, _premiumStar);
}

void ComposeAiButton::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	update();
}

QImage ComposeAiButton::prepareRippleMask() const {
	return Ui::RippleAnimation::EllipseMask(
		QSize(_st.rippleAreaSize, _st.rippleAreaSize));
}

QPoint ComposeAiButton::prepareRippleStartPosition() const {
	const auto result = mapFromGlobal(QCursor::pos()) - _st.rippleAreaPosition;
	const auto rect = QRect(0, 0, _st.rippleAreaSize, _st.rippleAreaSize);
	return rect.contains(result)
		? result
		: DisabledRippleStartPosition();
}

} // namespace HistoryView::Controls
