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
void PaintFilePreviewSurface(
	QPainter &p,
	QRectF rect,
	const MediaButtonColors &colors);
void PaintFilePreviewContent(
	QPainter &p,
	QRectF rect,
	const Layout::DocumentGenericPreview &preview,
	const MediaButtonColors &colors,
	float64 contentOpacity = 1.);

} // namespace Ui
