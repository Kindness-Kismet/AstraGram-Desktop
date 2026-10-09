#pragma once

#include "base/basic_types.h"
#include "rpl/producer.h"
#include "ui/style/style_core_types.h"
#include <QtGui/QColor>
#include <optional>
#include <vector>

class QWidget;
enum class WindowMaterial;
namespace Ui {
class RpWindow;
class IconButton;
class SeparatePanel;
} // namespace Ui

namespace ExtrasFeatures::WindowMaterial {

void initialize(not_null<Ui::RpWindow*> window);
// 独立面板跟随系统材质：生效时去掉自绘阴影，主体透出材质，平台不支持时保持原样。
void attachPanel(not_null<Ui::SeparatePanel*> panel);
[[nodiscard]] bool isActive(const QWidget *widget);
[[nodiscard]] QColor surfaceColor(
	const QWidget *widget,
	QColor opaque,
	int alpha = 0);
// 主界面与主菜单共用窗口材质，其余弹窗和浮层保持不透明。
void watchSurface(not_null<QWidget*> widget);
// 有效模式或主题变化也会通知，即使生效状态仍为 true。
[[nodiscard]] rpl::producer<bool> changes(not_null<QWidget*> widget);
[[nodiscard]] QColor rootTintColor(const QWidget *widget);
[[nodiscard]] QColor cardColor(const QWidget *widget, QColor opaque);
// 材质背景随桌面变化，灰色图标与文字改用正文色，与设置页导航一致。
// 生效时返回正文色，否则为空，可直接传给控件的颜色覆盖接口。
[[nodiscard]] std::optional<QColor> foregroundOverride(const QWidget *widget);
// 生效时返回正文色，否则返回 normal，供绘制代码直接使用。
[[nodiscard]] QColor foregroundColor(const QWidget *widget, QColor normal);
// 生效时图标常态改用正文色，悬停改用 over（传样式原有悬停色）；否则清除覆盖。
void applyIconButton(
	not_null<Ui::IconButton*> button,
	const style::color &over);
// 材质或主题变化时重新调用 applyIconButton。
void watchIconButton(
	not_null<Ui::IconButton*> button,
	const style::color &over);
[[nodiscard]] std::vector<::WindowMaterial> availableModes();

} // namespace ExtrasFeatures::WindowMaterial
