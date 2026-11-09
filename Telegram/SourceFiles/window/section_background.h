#pragma once

#include "base/basic_types.h"

#include <QtCore/QRect>

class QWidget;

namespace Ui {
class ChatTheme;
} // namespace Ui

namespace Window {

class AbstractSectionWidget;

// 材质策略属于页面，背景可以绘制到页面本身或独立内容层。
void PaintSectionBackground(
	not_null<AbstractSectionWidget*> section,
	not_null<Ui::ChatTheme*> theme,
	not_null<QWidget*> target,
	QRect clip);

} // namespace Window
