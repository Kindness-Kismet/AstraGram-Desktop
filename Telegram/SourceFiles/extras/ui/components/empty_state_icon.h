#pragma once

#include "base/object_ptr.h"
#include "ui/rp_widget.h"

namespace ExtrasUi {

enum class EmptyStateIcon {
	Search,
	NoResults,
	Chats,
	Blocked,
	Gifts,
};

void paintEmptyStateIcon(
	QPainter &p,
	QRect bounds,
	EmptyStateIcon icon,
	QColor color);

[[nodiscard]] object_ptr<Ui::RpWidget> createEmptyStateIcon(
	not_null<QWidget*> parent,
	EmptyStateIcon icon,
	QSize size,
	QMargins padding = {});

} // namespace ExtrasUi
