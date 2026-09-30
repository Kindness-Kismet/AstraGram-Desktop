/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/buttons.h"

namespace HistoryView::Controls {

class ComposeAiButton final : public Ui::RippleButton {
public:
	ComposeAiButton(QWidget *parent, const style::IconButton &st);
	ComposeAiButton(
		QWidget *parent,
		const style::IconButton &st,
		const style::icon &letters,
		const style::color *overColor = nullptr);

	void setPremiumStar(QImage image, QPoint position, int outline);

protected:
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;

	[[nodiscard]] QImage prepareRippleMask() const override;
	[[nodiscard]] QPoint prepareRippleStartPosition() const override;

private:
	void paintLetters(QPainter &p, bool over);
	void renderFrame(bool over);

	const style::IconButton &_st;
	const style::icon &_letters;
	const style::color *_overColor = nullptr;
	QImage _premiumStar;
	QPoint _premiumStarPosition;
	int _premiumStarOutline = 0;
	QImage _frame;

};

} // namespace HistoryView::Controls
