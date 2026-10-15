#pragma once

#include "ui/widgets/rp_window.h"

namespace ExtrasUi::WindowAppearance {

void paintTopCorners(
	QPainter &p,
	QRect rect,
	QColor background,
	bool left = true,
	bool right = true);

[[nodiscard]] Ui::RpWindow::Surface surface();
// 独立窗口沿用主窗口的材质与内容区圆角，在创建主体控件后调用。
void initialize(not_null<Ui::RpWindow*> window);

} // namespace ExtrasUi::WindowAppearance
