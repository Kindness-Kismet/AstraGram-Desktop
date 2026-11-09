#include "window/section_background.h"

#include "extras/features/window_material/window_material.h"
#include "window/section_widget.h"

namespace Window {

void PaintSectionBackground(
		not_null<AbstractSectionWidget*> section,
		not_null<Ui::ChatTheme*> theme,
		not_null<QWidget*> target,
		QRect clip) {
	if (ExtrasFeatures::WindowMaterial::isActive(section.get())) {
		return;
	}
	SectionWidget::PaintBackground(section->controller(), theme, target, clip);
}

} // namespace Window
