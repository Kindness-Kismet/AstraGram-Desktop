#pragma once

#include "base/basic_types.h"
#include "base/object_ptr.h"

class QPaintEvent;

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Window {

// 绘制层与页面同尺寸，内容控件和前景控件由页面分别归属。
[[nodiscard]] object_ptr<Ui::RpWidget> CreateSectionLayer(
	not_null<Ui::RpWidget*> section,
	Fn<void(QPaintEvent*)> paint);

} // namespace Window
