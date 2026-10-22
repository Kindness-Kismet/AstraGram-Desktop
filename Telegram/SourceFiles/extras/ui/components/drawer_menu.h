#pragma once

class QWidget;

namespace Ui {
class AbstractButton;
class VerticalLayout;
} // namespace Ui

namespace ExtrasUi {

void addDrawerIcon(
	not_null<Ui::AbstractButton*> button,
	not_null<const style::icon*> icon);
void fitDrawerIcon(not_null<QWidget*> icon);

// 每个直接子行用 drawerItem 标记，共享排序并在长按后启动拖动。
void setupDrawerReorder(not_null<Ui::VerticalLayout*> layout);

// 给每行左侧加六点抓手，提示可以长按拖动；只用于设置页的列表。
void addDrawerGrips(not_null<Ui::VerticalLayout*> layout);

} // namespace ExtrasUi
