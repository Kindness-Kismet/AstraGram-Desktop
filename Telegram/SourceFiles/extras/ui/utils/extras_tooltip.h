#pragma once

namespace style {
struct Tooltip;
} // namespace style

namespace Ui {
class RpWidget;
} // namespace Ui

namespace ExtrasUi {

// 悬停约 1 秒后按指定样式显示提示，文字在显示时读取；
// Ui::InstallTooltip 会忽略样式参数，提示会贴着指针。
void installTooltip(
	not_null<Ui::RpWidget*> widget,
	Fn<QString()> text,
	const style::Tooltip &st);

} // namespace ExtrasUi
