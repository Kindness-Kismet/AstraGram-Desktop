#pragma once

#include "ui/controls/media_button.h"

namespace Layout {
struct DocumentGenericPreview;
} // namespace Layout

namespace Ui {

[[nodiscard]] MediaButtonColors FilePreviewPalette(
	const Layout::DocumentGenericPreview &preview,
	QColor background);
[[nodiscard]] float64 FilePreviewRadius(QRectF rect);
[[nodiscard]] QRect FilePreviewButtonRect(QRect rect);
void PaintFilePreview(
	QPainter &p,
	QRectF rect,
	const Layout::DocumentGenericPreview &preview,
	float64 contentOpacity = 1.);

} // namespace Ui
