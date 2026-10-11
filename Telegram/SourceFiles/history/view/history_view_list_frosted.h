#pragma once

#include "base/basic_types.h"

class QPainter;

namespace ExtrasUi {
class FloatingSurfaceHost;
} // namespace ExtrasUi

namespace Ui {
class RpWidget;
class ChatTheme;
} // namespace Ui

namespace Window {
class SectionWidget;
} // namespace Window

namespace HistoryView {

class ListWidget;

struct ListFrostedDescriptor {
	not_null<Ui::RpWidget*> root;
	not_null<QWidget*> scroll;
	not_null<ListWidget*> list;
	rpl::producer<> scrolls;
	rpl::producer<> paletteChanged;
	// 在 root 坐标系绘制聊天背景，返回 false 表示暂时不能采样。
	Fn<bool(QPainter &p, QRect area)> paintBackground;
};

// 消息列表作为磨砂背景源，浮动卡片和输入区透出模糊后的消息。
[[nodiscard]] std::unique_ptr<ExtrasUi::FloatingSurfaceHost> SetupListFrostedBackground(
	ListFrostedDescriptor &&descriptor);

// 聊天分区的壁纸，坐标与分区自身绘制的背景对齐；切换动画期间不采样。
[[nodiscard]] Fn<bool(QPainter &p, QRect area)> SectionFrostedBackground(
	not_null<Window::SectionWidget*> section,
	Fn<Ui::ChatTheme*()> theme);

} // namespace HistoryView
