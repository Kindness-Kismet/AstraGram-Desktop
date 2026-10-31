#include "ui/controls/file_preview.h"

#include "layout/layout_document_generic_preview.h"
#include "ui/color_contrast.h"
#include "ui/painter.h"
#include "styles/style_file_preview.h"

#include <algorithm>

namespace Ui {
namespace {

[[nodiscard]] QColor FileTypeColor(Layout::DocumentFileType type) {
	switch (type) {
	case Layout::DocumentFileType::Application: return QColor(0x49, 0x66, 0x53);
	case Layout::DocumentFileType::Library: return QColor(0x7d, 0x66, 0x53);
	case Layout::DocumentFileType::Code: return QColor(0x53, 0x64, 0x7a);
	case Layout::DocumentFileType::Archive: return QColor(0x65, 0x58, 0x78);
	case Layout::DocumentFileType::Spreadsheet: return QColor(0x45, 0x63, 0x5a);
	case Layout::DocumentFileType::Image: return QColor(0x81, 0x50, 0x58);
	case Layout::DocumentFileType::Document: return QColor(0x56, 0x60, 0x71);
	}
	Unexpected("File type in FileTypeColor.");
}

} // namespace

MediaButtonColors FilePreviewPalette(
		const Layout::DocumentGenericPreview &preview,
		QColor background) {
	const auto color = FileTypeColor(preview.type());
	const auto dark = CountContrast(background, Qt::white)
		> CountContrast(background, Qt::black);
	const auto foreground = dark ? anim::color(color, Qt::white, .65) : color;
	auto fill = foreground;
	fill.setAlpha(32);
	return {
		.foreground = foreground,
		.background = anim::color(background, foreground, dark ? .10 : .07),
		.badge = background,
		.fill = fill,
	};
}

float64 FilePreviewRadius(QRectF rect) {
	return std::min<float64>(style::ConvertScale(8), rect.width() / 2.);
}

QRect FilePreviewButtonRect(QRect rect) {
	const auto size = std::min({ rect.width(), rect.height(), style::ConvertScale(44) });
	return QRect(
		rect.x() + (rect.width() - size) / 2,
		rect.y() + (rect.height() - size) / 2,
		size, size);
}

void PaintFilePreview(
		QPainter &p,
		QRectF rect,
		const Layout::DocumentGenericPreview &preview,
		float64 contentOpacity) {
	if (contentOpacity <= 0.) {
		return;
	}
	const auto colors = FilePreviewPalette(preview, st::windowBg->c);
	const auto size = std::min(rect.width(), rect.height()) * .6;
	const auto iconRect = QRectF(
		rect.center() - QPointF(size / 2., size / 2.), QSizeF(size, size));
	p.save();
	p.setOpacity(p.opacity() * contentOpacity);
	PaintMediaButtonSurface(p, rect, colors, false, MediaButtonShape::RoundedRect);
	PaintMediaIcon(p, iconRect, *preview.icon(), colors.foreground);
	p.restore();
}

} // namespace Ui
