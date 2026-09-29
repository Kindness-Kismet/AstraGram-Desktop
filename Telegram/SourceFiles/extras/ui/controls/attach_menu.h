#pragma once

namespace Ui {
class DropdownMenu;
}

namespace ExtrasUi {

// 加号自身不触发操作：开启悬停弹出时悬停展开，关闭时点击只切换菜单。
void setupAttachMenu(
	not_null<QWidget*> button,
	not_null<Ui::DropdownMenu*> menu);

}
