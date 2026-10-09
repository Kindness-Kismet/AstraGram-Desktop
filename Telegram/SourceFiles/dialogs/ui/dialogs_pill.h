/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QRect>
#include <QtGui/QColor>

class QPainter;
class QWidget;

namespace Ui {
class BoxShadow;
} // namespace Ui

namespace Dialogs {

void PaintPillBackground(
	QPainter &p,
	const Ui::BoxShadow &shadow,
	const QRect &pill,
	int radius);
void PaintPillOutline(QPainter &p, const QRect &pill, int radius);
void PaintTopFade(QPainter &p, int outerWidth, int fadeHeight, QColor bg);
void PaintBottomFade(QPainter &p, int outerWidth, int fadeHeight, QColor bg);
// 搜索分组标题底色，内缩与圆角同会话行高亮一致；窗口材质生效时不绘制。
void PaintSearchedBarBg(QPainter &p, const QWidget *widget, QRect rect);

} // namespace Dialogs
