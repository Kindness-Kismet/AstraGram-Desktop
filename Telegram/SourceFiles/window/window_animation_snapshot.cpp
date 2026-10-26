#include "window/window_animation_snapshot.h"

#include "extras/features/window_material/window_material.h"
#include "ui/ui_utility.h"

namespace Window {

QPixmap GrabForAnimation(not_null<QWidget*> widget, QRect rect) {
	return Ui::GrabWidget(widget, rect,
		ExtrasFeatures::WindowMaterial::isActive(widget.get())
			? ExtrasFeatures::WindowMaterial::rootTintColor(widget)
			: QColor(Qt::transparent));
}

}
