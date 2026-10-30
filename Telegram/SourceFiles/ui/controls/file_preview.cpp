#include "ui/controls/file_preview.h"

#include "layout/layout_document_generic_preview.h"
#include "ui/painter.h"

#include <algorithm>

namespace Ui {
namespace {

[[nodiscard]] QColor FileTypeColor(Layout::DocumentFileType type) {
	switch (type) {
	case Layout::DocumentFileType::Application: return QColor(0x04, 0x78, 0x57);
	case Layout::DocumentFileType::Library: return QColor(0xb4, 0x53, 0x09);
	case Layout::DocumentFileType::Code: return QColor(0x1d, 0x4e, 0xd8);
	case Layout::DocumentFileType::Archive: return QColor(0x5b, 0x21, 0xb6);
	case Layout::DocumentFileType::Spreadsheet: return QColor(0x0f, 0x76, 0x6e);
	case Layout::DocumentFileType::Image: return QColor(0xbe, 0x12, 0x3c);
	case Layout::DocumentFileType::Document: return QColor(0x33, 0x41, 0x55);
	}
	Unexpected("File type in FileTypeColor.");
}

} // namespace

MediaButtonColors FilePreviewPalette(
		const Layout::DocumentGenericPreview &preview,
		QColor background) {
	const auto color = FileTypeColor(preview.type());
	return MediaButtonPalette(color, background);
}

float64 FilePreviewRadius(QRectF rect) {
	return rect.width() * 13. / 60.;
}

QRect FilePreviewButtonRect(QRect rect) {
	const auto size = std::min({ rect.width(), rect.height(), style::ConvertScale(44) });
	return QRect(
		rect.x() + (rect.width() - size) / 2,
		rect.y() + (rect.height() - size) / 2,
		size, size);
}

void PaintFilePreviewSurface(
		QPainter &p,
		QRectF rect,
		const MediaButtonColors &colors) {
	p.save();
	PaintMediaButtonSurface(p, FilePreviewButtonRect(rect.toRect()), colors);
	p.restore();
}

void PaintFilePreviewContent(
		QPainter &p,
		QRectF rect,
		const Layout::DocumentGenericPreview &preview,
		const MediaButtonColors &colors,
		float64 contentOpacity) {
	if (contentOpacity <= 0.) {
		return;
	}
	const auto button = QRectF(FilePreviewButtonRect(rect.toRect()));
	const auto size = button.width() * 0.65;
	const auto inner = QRectF(
		button.center().x() - size / 2.,
		button.center().y() - size / 2.,
		size, size);
	p.save();
	p.setOpacity(p.opacity() * contentOpacity);
	PaintMediaIcon(p, inner, *preview.icon(), colors.foreground);
	p.restore();
}

} // namespace Ui
