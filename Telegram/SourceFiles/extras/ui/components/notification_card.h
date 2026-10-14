#pragma once

#include "core/core_settings.h"

#include <QtCore/QPoint>
#include <QtCore/QRect>
#include <QtGui/QColor>

class QPainter;
class QImage;

namespace ExtrasUi::NotificationCard {

// 叠放收起时露出边缘的后排张数，更靠后的完全藏起。
inline constexpr auto kStackDepth = 2;
// 叠放时每往后一层，卡片宽度缩小的比例。
inline constexpr auto kStackShrink = 0.05;

// 一行通知的尺寸：头像与两行文字整体垂直居中。
struct RowLayout {
	int height = 0;
	int photoSize = 0;
	QPoint photoPos;
	int textLeft = 0;
	int textTop = 0;
};

[[nodiscard]] int cardWidth(Core::Settings::ScreenCorner corner);
[[nodiscard]] RowLayout rowLayout();
// 按钮圆底与叠放计数的底色：暗色提亮，亮色压暗。
[[nodiscard]] QColor actionColor(int alpha);
// 隐藏全部的胶囊靠屏幕边缘对齐，顶部居中时居中。
[[nodiscard]] int chipLeftSkip(
	int full,
	int width,
	Core::Settings::ScreenCorner corner);
[[nodiscard]] int hideAllChipWidth(int count);

// 底色取主题窗口色，描边是白色高光；圆角不超过高度的一半。
void paintCard(QPainter &p, const QRect &card, bool highlighted = false);
// 叠放收起时的剩余条数，画在标题行右端。
void paintStackBadge(QPainter &p, int right, int titleTop, int more);
// 胶囊里先写“全部隐藏”，后跟灰色的条数。
void paintHideAllChip(QPainter &p, const QRect &chip, int count, bool over);

// 设置页悬停预览：按当前样式画出 count 条示例通知，最新一条靠近 corner。
[[nodiscard]] QImage prepareSample(
	int count,
	Core::Settings::ScreenCorner corner);

} // namespace ExtrasUi::NotificationCard
