#pragma once

#include "base/basic_types.h"
#include <QtCore/QRect>
#include <QtGui/QPixmap>

class QWidget;

namespace Window {

// 动画整帧替换窗口内容，缓存必须包含根部混色，连续截取时也只保留一层。
[[nodiscard]] QPixmap GrabForAnimation(
	not_null<QWidget*> widget,
	QRect rect = QRect());

}
