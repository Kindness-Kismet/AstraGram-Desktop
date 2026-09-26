#pragma once

#include "base/basic_types.h"
#include "rpl/producer.h"
#include <QtGui/QColor>
#include <vector>

class QWidget;
enum class WindowMaterial;
namespace Ui { class RpWindow; class LayerWidget; }

namespace AyuFeatures::WindowMaterial {

void initialize(not_null<Ui::RpWindow*> window);
[[nodiscard]] bool isActive(const QWidget *widget);
[[nodiscard]] QColor surfaceColor(
	const QWidget *widget,
	QColor opaque,
	int alpha = 0);
// 只登记承载主界面的控件；弹窗和浮层保持不透明。
void watchSurface(not_null<QWidget*> widget);
void allowMaterialLayer(not_null<Ui::LayerWidget*> widget);
// 有效模式或主题变化也会通知，即使生效状态仍为 true。
[[nodiscard]] rpl::producer<bool> changes(not_null<QWidget*> widget);
[[nodiscard]] QColor rootTintColor(const QWidget *widget);
[[nodiscard]] QColor cardColor(const QWidget *widget, QColor opaque);
[[nodiscard]] std::vector<::WindowMaterial> availableModes();

} // namespace AyuFeatures::WindowMaterial
