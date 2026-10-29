#include "ui/controls/file_preview.h"

#include "layout/layout_document_generic_preview.h"
#include "ui/color_contrast.h"
#include "ui/painter.h"
#include "styles/style_widgets.h"

#include <QtGui/QPainterPath>
#include <range/v3/algorithm/contains.hpp>

namespace Ui {
namespace {

[[nodiscard]] QColor ContrastingColor(QColor color, QColor background) {
	constexpr auto kMinimumContrast = 4.5;
	if (CountContrast(color, background) >= kMinimumContrast) {
		return color;
	}
	const auto black = QColor(Qt::black);
	const auto white = QColor(Qt::white);
	const auto target = CountContrast(black, background)
		> CountContrast(white, background) ? black : white;
	auto low = 0.;
	auto high = 1.;
	for (auto i = 0; i != 8; ++i) {
		const auto middle = (low + high) / 2.;
		if (CountContrast(anim::color(color, target, middle), background)
				< kMinimumContrast) {
			low = middle;
		} else {
			high = middle;
		}
	}
	return anim::color(color, target, high);
}

void PaintTypeSymbol(QPainter &p, const QString &ext) {
	const auto matches = [&](std::initializer_list<QStringView> extensions) {
		return ranges::contains(extensions, ext);
	};
	auto path = QPainterPath();
	if (matches({ u"py", u"js", u"ts", u"cpp", u"h", u"c", u"rs",
			u"json", u"html", u"css", u"xml", u"sh", u"java", u"qml" })) {
		path.moveTo(8, 7);
		path.lineTo(3, 12);
		path.lineTo(8, 17);
		path.moveTo(16, 7);
		path.lineTo(21, 12);
		path.lineTo(16, 17);
		path.moveTo(13, 5);
		path.lineTo(11, 19);
	} else if (matches({ u"so", u"dll", u"dylib" })) {
		p.drawRoundedRect(QRectF(6, 6, 12, 12), 2, 2);
		p.drawRoundedRect(QRectF(9, 9, 6, 6), 1, 1);
		for (const auto offset : { 9, 15 }) {
			p.drawLine(QPointF(offset, 3), QPointF(offset, 6));
			p.drawLine(QPointF(offset, 18), QPointF(offset, 21));
			p.drawLine(QPointF(3, offset), QPointF(6, offset));
			p.drawLine(QPointF(18, offset), QPointF(21, offset));
		}
	} else if (matches({ u"exe", u"msi", u"apk", u"app", u"appx" })) {
		for (const auto x : { 3, 14 }) {
			for (const auto y : { 3, 14 }) {
				p.drawRoundedRect(QRectF(x, y, 7, 7), 1.5, 1.5);
			}
		}
	} else if (matches({ u"zip", u"rar", u"7z", u"tar", u"gz", u"xz" })) {
		p.drawRoundedRect(QRectF(4, 7, 16, 14), 2, 2);
		p.drawRect(QRectF(3, 3, 18, 4));
		path.moveTo(10, 11);
		path.lineTo(14, 11);
		path.moveTo(12, 7);
		path.lineTo(12, 14);
		path.addRect(QRectF(10, 14, 4, 3));
	} else if (matches({ u"xls", u"xlsx", u"csv" })) {
		p.drawRoundedRect(QRectF(3, 4, 18, 16), 2, 2);
		path.moveTo(3, 10);
		path.lineTo(21, 10);
		path.moveTo(3, 15);
		path.lineTo(21, 15);
		path.moveTo(10, 4);
		path.lineTo(10, 20);
	} else if (matches({ u"png", u"jpg", u"jpeg", u"webp", u"svg", u"bmp" })) {
		p.drawRoundedRect(QRectF(3, 3, 18, 18), 3, 3);
		p.drawEllipse(QPointF(15.5, 8.5), 1, 1);
		path.moveTo(3, 17);
		path.lineTo(9, 11);
		path.lineTo(16, 18);
		path.moveTo(13, 15);
		path.lineTo(16, 12);
		path.lineTo(21, 17);
	} else {
		path.moveTo(13, 3);
		path.lineTo(6, 3);
		path.quadTo(5, 3, 5, 4);
		path.lineTo(5, 20);
		path.quadTo(5, 21, 6, 21);
		path.lineTo(18, 21);
		path.quadTo(19, 21, 19, 20);
		path.lineTo(19, 9);
		path.lineTo(13, 3);
		path.lineTo(13, 9);
		path.lineTo(19, 9);
		path.moveTo(8.5, 13);
		path.lineTo(15.5, 13);
		path.moveTo(8.5, 16.5);
		path.lineTo(13.5, 16.5);
	}
	p.drawPath(path);
}

} // namespace

MediaButtonColors FilePreviewPalette(
		const Layout::DocumentGenericPreview &preview,
		QColor background) {
	const auto wash = anim::color(background, preview.color->c, 0.12);
	const auto foreground = ContrastingColor(preview.color->c, wash);
	auto result = MediaButtonPalette(foreground, wash);
	result.background = wash;
	result.border = anim::color(wash, foreground, 0.23);
	return result;
}

float64 FilePreviewRadius(QRectF rect) {
	return rect.width() * 13. / 60.;
}

QRect FilePreviewButtonRect(QRect rect) {
	const auto inset = qRound(rect.width() * 0.09);
	return rect.adjusted(inset, inset, -inset, -inset);
}

void PaintFilePreviewSurface(
		QPainter &p,
		QRectF rect,
		const MediaButtonColors &colors) {
	p.save();
	auto hq = PainterHighQualityEnabler(p);
	const auto line = style::ConvertScaleExact(1.);
	const auto radius = FilePreviewRadius(rect);
	p.setPen(QPen(colors.border, line));
	p.setBrush(colors.background);
	p.drawRoundedRect(
		rect.adjusted(line / 2., line / 2., -line / 2., -line / 2.),
		radius,
		radius);
	p.restore();
}

void PaintFilePreviewContent(
		QPainter &p,
		QRectF rect,
		const Layout::DocumentGenericPreview &preview,
		const MediaButtonColors &colors,
		float64 contentOpacity) {
	const auto size = rect.width();
	const auto scale = size / 60.;
	const auto font = style::font(
		qRound(11. * scale),
		st::semiboldFont->flags(),
		st::semiboldFont->family());
	const auto ext = (size >= style::ConvertScale(36))
		? font->elided(preview.ext.toUpper(), qRound(size * 0.82))
		: QString();
	p.save();
	auto hq = PainterHighQualityEnabler(p);
	if (contentOpacity > 0.) {
		p.save();
		p.setOpacity(p.opacity() * contentOpacity);
		p.setFont(font);
		p.setPen(colors.foreground);
		if (!ext.isEmpty()) {
			p.drawText(QRectF(rect.x(), rect.y() + size * 0.63, size,
				font->height), Qt::AlignHCenter | Qt::AlignTop, ext);
		}
		const auto symbolSize = size * (ext.isEmpty() ? 0.62 : 0.38);
		p.translate(rect.x() + (size - symbolSize) / 2.,
			rect.y() + (ext.isEmpty() ? (size - symbolSize) / 2. : size * 0.17));
		p.scale(symbolSize / 24., symbolSize / 24.);
		p.setPen(QPen(colors.foreground, 1.65, Qt::SolidLine,
			Qt::RoundCap, Qt::RoundJoin));
		p.setBrush(Qt::NoBrush);
		PaintTypeSymbol(p, preview.ext);
		p.restore();
	}
	if (contentOpacity < 1. && !ext.isEmpty()) {
		const auto tagFont = style::font(
			qRound(std::max(8. * scale, style::ConvertScaleExact(7.))),
			font->flags(), font->family());
		const auto tag = tagFont->elided(preview.ext.toUpper(), qRound(size * 0.55));
		const auto padding = 3. * scale;
		const auto tagWidth = tagFont->width(tag) + 2. * padding;
		const auto tagRect = QRectF(rect.right() - tagWidth, rect.y(),
			tagWidth, tagFont->height + padding);
		p.setOpacity(p.opacity() * (1. - contentOpacity));
		p.setPen(QPen(colors.border, style::ConvertScaleExact(1.)));
		p.setBrush(colors.background);
		p.drawRoundedRect(tagRect, 3. * scale, 3. * scale);
		p.setFont(tagFont);
		p.setPen(colors.foreground);
		p.drawText(tagRect, Qt::AlignCenter, tag);
	}
	p.restore();
}

} // namespace Ui
