/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/message_bubble.h"

#include "ui/cached_round_corners.h"
#include "ui/image/image_prepare.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/torn_edge.h"
#include "styles/style_chat.h"
#include "styles/style_chat_style.h"

// AyuGram includes
#include "extras/extras_settings.h"

#include <QtGui/QPainterPath>
#include <cmath>
#include <map>
#include <tuple>

namespace Ui {
namespace {

using Corner = BubbleCornerRounding;

// 拖尾与气泡共用外轮廓，连接处不能闭合描边。
[[nodiscard]] QPainterPath BubbleOutlinePath(
		const QRectF &rect,
		const BubbleRounding &rounding,
		QSize tailSize) {
	const auto removeTail = ExtrasSettings::getInstance().removeMessageTail();
	const auto radiusOf = [&](BubbleCornerRounding corner) {
		if (removeTail && corner == Corner::Tail) {
			corner = Corner::Large;
		}
		const auto radius = (corner == BubbleCornerRounding::Large)
			? BubbleRadiusLarge()
			: (corner == BubbleCornerRounding::Small)
			? BubbleRadiusSmall()
			: 0;
		return std::max(radius - 0.5, 0.);
	};
	const auto tl = radiusOf(rounding.topLeft);
	const auto tr = radiusOf(rounding.topRight);
	const auto br = radiusOf(rounding.bottomRight);
	const auto bl = radiusOf(rounding.bottomLeft);
	const auto tailLeft = !removeTail && rounding.bottomLeft == Corner::Tail;
	const auto tailRight = !removeTail && rounding.bottomRight == Corner::Tail;
	const auto tailWidth = float64(tailSize.width());
	const auto tailHeight = float64(tailSize.height());

	auto path = QPainterPath();
	path.moveTo(rect.left() + tl, rect.top());
	path.lineTo(rect.right() - tr, rect.top());
	if (tr) {
		path.arcTo(
			QRectF(rect.right() - tr * 2, rect.top(), tr * 2, tr * 2),
			90.,
			-90.);
	}
	if (tailRight) {
		path.lineTo(rect.right(), rect.bottom() - tailHeight);
		path.cubicTo(
			rect.right() + tailWidth / 6., rect.bottom() - tailHeight / 2.,
			rect.right() + tailWidth / 3., rect.bottom() - tailHeight / 5.,
			rect.right() + tailWidth, rect.bottom());
	} else {
		path.lineTo(rect.right(), rect.bottom() - br);
	}
	if (br) {
		path.arcTo(
			QRectF(rect.right() - br * 2, rect.bottom() - br * 2, br * 2, br * 2),
			0.,
			-90.);
	}
	if (tailLeft) {
		path.lineTo(rect.left() - tailWidth, rect.bottom());
		path.cubicTo(
			rect.left() - tailWidth / 3., rect.bottom() - tailHeight / 5.,
			rect.left() - tailWidth / 6., rect.bottom() - tailHeight / 2.,
			rect.left(), rect.bottom() - tailHeight);
	} else {
		path.lineTo(rect.left() + bl, rect.bottom());
	}
	if (bl) {
		path.arcTo(
			QRectF(rect.left(), rect.bottom() - bl * 2, bl * 2, bl * 2),
			270.,
			-90.);
	}
	path.lineTo(rect.left(), rect.top() + tl);
	if (tl) {
		path.arcTo(
			QRectF(rect.left(), rect.top(), tl * 2, tl * 2),
			180.,
			-90.);
	}
	path.closeSubpath();
	return path;
}

// 半径按 Skia 的规则换算成高斯标准差，核截断在三个标准差处。
[[nodiscard]] QImage BlurBubbleShadow(const QImage &mask) {
	constexpr auto kRadius = 5;
	constexpr auto kSigma = 2. * 0.57735 + 0.5;
	auto kernel = std::array<double, 2 * kRadius + 1>();
	auto sum = 0.;
	for (auto i = -kRadius; i <= kRadius; ++i) {
		sum += (kernel[i + kRadius] = std::exp(-i * i / (2. * kSigma * kSigma)));
	}
	for (auto &weight : kernel) {
		weight /= sum;
	}
	const auto width = mask.width();
	const auto height = mask.height();
	auto horizontal = std::vector<double>(width * height);
	for (auto y = 0; y != height; ++y) {
		const auto row = reinterpret_cast<const QRgb*>(mask.constScanLine(y));
		for (auto x = 0; x != width; ++x) {
			for (auto i = -kRadius; i <= kRadius; ++i) {
				horizontal[y * width + x] += kernel[i + kRadius]
					* qRed(row[std::clamp(x + i, 0, width - 1)]);
			}
		}
	}
	auto result = QImage(mask.size(), QImage::Format_RGB32);
	result.setDevicePixelRatio(mask.devicePixelRatio());
	for (auto y = 0; y != height; ++y) {
		const auto row = reinterpret_cast<QRgb*>(result.scanLine(y));
		for (auto x = 0; x != width; ++x) {
			auto value = 0.;
			for (auto i = -kRadius; i <= kRadius; ++i) {
				value += kernel[i + kRadius]
					* horizontal[std::clamp(y + i, 0, height - 1) * width + x];
			}
			const auto intensity = int(std::round(value));
			row[x] = qRgb(intensity, intensity, intensity);
		}
	}
	return result;
}

// 阴影参数参考 Nagram 的 MessageDrawable：纵向透明度 21→41，模糊半径 2，下移 1 像素。
[[nodiscard]] QImage PrepareBubbleShadow(
		QSize size,
		const BubbleRounding &rounding,
		QSize tailSize,
		QColor color,
		QMargins margins) {
	const auto ratio = style::DevicePixelRatio();
	const auto outer = QRect(QPoint(), size).marginsAdded(margins).size();
	const auto body = QRectF(QPointF(margins.left(), margins.top()), size);
	auto mask = QImage(outer * ratio, QImage::Format_RGB32);
	mask.setDevicePixelRatio(ratio);
	mask.fill(Qt::black);
	const auto expand = (ratio > 1 || style::ConvertScale(100) > 100)
		? 1. / ratio
		: 0.;
	{
		auto painter = QPainter(&mask);
		painter.setRenderHint(QPainter::Antialiasing);
		auto gradient = QLinearGradient(body.topLeft(), body.bottomLeft());
		gradient.setColorAt(0., QColor(21, 21, 21));
		gradient.setColorAt(1., QColor(41, 41, 41));
		painter.setPen(Qt::NoPen);
		painter.setBrush(gradient);
		painter.drawPath(BubbleOutlinePath(
			body.adjusted(-expand, -expand, expand, expand),
			rounding,
			tailSize));
	}
	auto shifted = QImage(mask.size(), QImage::Format_RGB32);
	shifted.fill(Qt::black);
	{
		auto painter = QPainter(&shifted);
		painter.drawImage(QRect(0, 1, mask.width(), mask.height()), mask);
	}
	const auto blurred = BlurBubbleShadow(shifted);
	auto result = QImage(mask.size(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	const auto red = color.red() * 95 / 255;
	const auto green = color.green() * 101 / 255;
	const auto blue = color.blue() * 105 / 255;
	for (auto y = 0; y != result.height(); ++y) {
		const auto source = reinterpret_cast<const QRgb*>(mask.constScanLine(y));
		const auto shadow = reinterpret_cast<const QRgb*>(blurred.constScanLine(y));
		const auto target = reinterpret_cast<QRgb*>(result.scanLine(y));
		for (auto x = 0; x != result.width(); ++x) {
			const auto intensity = qRed(source[x])
				+ qRed(shadow[x]) * (255 - qRed(source[x])) / 255;
			const auto alpha = std::min(255, intensity * color.alpha() / 41);
			target[x] = qPremultiply(qRgba(red, green, blue, alpha));
		}
	}
	{
		auto painter = QPainter(&result);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
		painter.setPen(Qt::NoPen);
		painter.setBrush(Qt::white);
		painter.drawPath(BubbleOutlinePath(body, rounding, tailSize));
	}
	return result;
}

void PaintBubbleShadow(QPainter &p, const SimpleBubble &args) {
	if (!args.shadowed || args.geometry.isEmpty()
		|| ExtrasSettings::getInstance().disableBubbleShadow()) {
		return;
	}
	const auto &message = args.st->messageStyle(args.outbg, args.selected);
	const auto color = message.msgShadow->c;
	if (!color.alpha()) {
		return;
	}
	const auto tail = message.tailLeft.size();
	const auto small = BubbleRadiusSmall();
	const auto large = BubbleRadiusLarge();
	const auto size = QSize(
		std::min(args.geometry.width(), std::max(
			style::ConvertScale(50), 2 * (large + tail.width()) + 2)),
		std::min(args.geometry.height(), std::max(
			style::ConvertScale(40), 2 * large + 2)));
	const auto padding = 6;
	const auto margins = QMargins(
		padding + tail.width(), padding, padding + tail.width(), padding);
	const auto removeTail = ExtrasSettings::getInstance().removeMessageTail();
	const auto ratio = style::DevicePixelRatio();
	const auto key = std::make_tuple(
		size.width(), size.height(), small, large, tail.width(), tail.height(),
		args.rounding.key(), color.rgba(), removeTail, ratio);
	// 主题预览也会在工作线程绘制，缓存不能跨线程共享。
	static thread_local auto cache = std::map<std::decay_t<decltype(key)>, QImage>();
	auto i = cache.find(key);
	if (i == cache.end()) {
		if (cache.size() >= 64) {
			cache.clear();
		}
		i = cache.emplace(key, PrepareBubbleShadow(
			size, args.rounding, tail, color, margins)).first;
	}
	const auto &image = i->second;
	const auto sourceSize = image.size() / ratio;
	const auto outer = args.geometry.marginsAdded(margins);
	const auto x = std::array{
		0, sourceSize.width() / 2, sourceSize.width() / 2 + 1, sourceSize.width() };
	const auto y = std::array{
		0, sourceSize.height() / 2, sourceSize.height() / 2 + 1, sourceSize.height() };
	const auto targetX = std::array{
		outer.x(), outer.x() + x[1],
		outer.x() + outer.width() - (x[3] - x[2]), outer.x() + outer.width() };
	const auto targetY = std::array{
		outer.y(), outer.y() + y[1],
		outer.y() + outer.height() - (y[3] - y[2]), outer.y() + outer.height() };

	p.save();
	auto clip = outer;
	if (args.rounding.topLeft == Corner::None && args.rounding.topRight == Corner::None) {
		clip.setTop(args.geometry.top());
	}
	if (args.rounding.bottomLeft == Corner::None && args.rounding.bottomRight == Corner::None) {
		clip.setBottom(args.geometry.bottom());
	}
	p.setClipRect(clip, Qt::IntersectClip);
	p.setOpacity(p.opacity() * message.msgBg->c.alphaF());
	for (auto row = 0; row != 3; ++row) {
		for (auto column = 0; column != 3; ++column) {
			if (row == 1 && column == 1) {
				continue;
			}
			p.drawImage(
				QRect(targetX[column], targetY[row],
					targetX[column + 1] - targetX[column],
					targetY[row + 1] - targetY[row]),
				image,
				QRect(x[column] * ratio, y[row] * ratio,
					(x[column + 1] - x[column]) * ratio,
					(y[row + 1] - y[row]) * ratio));
		}
	}
	p.restore();
}

[[nodiscard]] bool UsePatternBubble(const SimpleBubble &args) {
	return !args.selected
		&& args.outbg
		&& args.pattern
		&& !args.patternViewport.isEmpty()
		&& !args.pattern->pixmap.size().isEmpty();
}

[[nodiscard]] float64 PatternBubbleOpacity(
		const SimpleBubble &args,
		float64 wasOpacity) {
	return args.st->msgOutBg()->c.alphaF() * wasOpacity;
}

void PaintBubblePiece(
		QPainter &p,
		const SimpleBubble &args,
		QRect geometry,
		bool fromTop,
		bool tillBottom) {
	auto simple = args;
	simple.geometry = geometry;
	if (!fromTop) {
		simple.rounding.topLeft
			= simple.rounding.topRight
			= Corner::None;
	}
	if (!tillBottom) {
		simple.rounding.bottomLeft
			= simple.rounding.bottomRight
			= Corner::None;
	}
	PaintBubble(p, simple);
}

void PaintBubblePiece(
		QPainter &p,
		const SimpleBubble &args,
		QRect geometry,
		bool selected,
		bool fromTop,
		bool tillBottom) {
	auto simple = args;
	simple.selected = selected;
	PaintBubblePiece(p, simple, geometry, fromTop, tillBottom);
}

template <
	typename FillBg, // fillBg(QRect rect)
	typename FillCorner, // fillCorner(int x, int y, int index, Corner size)
	typename PaintTail> // paintTail(QPoint bottomPosition)
void PaintBubbleGeneric(
		const SimpleBubble &args,
		FillBg &&fillBg,
		FillCorner &&fillCorner,
		PaintTail &&paintTail) {
	using namespace Images;

	const auto topLeft = args.rounding.topLeft;
	const auto topRight = args.rounding.topRight;
	auto bottomWithTailLeft = args.rounding.bottomLeft;
	auto bottomWithTailRight = args.rounding.bottomRight;
	if (topLeft == Corner::None
		&& topRight == Corner::None
		&& bottomWithTailLeft == Corner::None
		&& bottomWithTailRight == Corner::None) {
		fillBg(args.geometry);
		return;
	}

	const auto &settings = ExtrasSettings::getInstance();
	if (settings.removeMessageTail()) {
		if (bottomWithTailLeft == Corner::Tail) {
			bottomWithTailLeft = Corner::Large;
		}
		if (bottomWithTailRight == Corner::Tail) {
			bottomWithTailRight = Corner::Large;
		}
	}

	const auto bottomLeft = (bottomWithTailLeft == Corner::Tail)
		? Corner::None
		: bottomWithTailLeft;
	const auto bottomRight = (bottomWithTailRight == Corner::Tail)
		? Corner::None
		: bottomWithTailRight;
	const auto rect = args.geometry;
	const auto small = BubbleRadiusSmall();
	const auto large = BubbleRadiusLarge();
	const auto cornerSize = [&](Corner corner) {
		return (corner == Corner::Large)
			? large
			: (corner == Corner::Small)
			? small
			: 0;
	};
	const auto verticalSkip = [&](Corner left, Corner right) {
		return std::max(cornerSize(left), cornerSize(right));
	};
	const auto top = verticalSkip(topLeft, topRight);
	const auto bottom = verticalSkip(bottomLeft, bottomRight);
	if (top) {
		const auto left = cornerSize(topLeft);
		const auto right = cornerSize(topRight);
		if (left) {
			fillCorner(rect.left(), rect.top(), kTopLeft, topLeft);
			if (const auto add = top - left) {
				fillBg({ rect.left(), rect.top() + left, left, add });
			}
		}
		if (const auto fill = rect.width() - left - right; fill > 0) {
			fillBg({ rect.left() + left, rect.top(), fill, top });
		}
		if (right) {
			fillCorner(
				rect.left() + rect.width() - right,
				rect.top(),
				kTopRight,
				topRight);
			if (const auto add = top - right) {
				fillBg({
					rect.left() + rect.width() - right,
					rect.top() + right,
					right,
					add,
				});
			}
		}
	}
	if (const auto fill = rect.height() - top - bottom; fill > 0) {
		fillBg({ rect.left(), rect.top() + top, rect.width(), fill });
	}
	if (bottom) {
		const auto left = cornerSize(bottomLeft);
		const auto right = cornerSize(bottomRight);
		if (left) {
			fillCorner(
				rect.left(),
				rect.top() + rect.height() - left,
				kBottomLeft,
				bottomLeft);
			if (const auto add = bottom - left) {
				fillBg({
					rect.left(),
					rect.top() + rect.height() - bottom,
					left,
					add,
				});
			}
		}
		if (const auto fill = rect.width() - left - right; fill > 0) {
			fillBg({
				rect.left() + left,
				rect.top() + rect.height() - bottom,
				fill,
				bottom,
			});
		}
		if (right) {
			fillCorner(
				rect.left() + rect.width() - right,
				rect.top() + rect.height() - right,
				kBottomRight,
				bottomRight);
			if (const auto add = bottom - right) {
				fillBg({
					rect.left() + rect.width() - right,
					rect.top() + rect.height() - bottom,
					right,
					add,
				});
			}
		}
	}
	if (bottomWithTailLeft == Corner::Tail) {
		paintTail({ rect.x(), rect.y() + rect.height() });
	}
	if (bottomWithTailRight == Corner::Tail) {
		paintTail({ rect.x() + rect.width(), rect.y() + rect.height() });
	}
}

void PaintPatternBubble(QPainter &p, const SimpleBubble &args) {
	const auto wasOpacity = p.opacity();
	const auto opacity = PatternBubbleOpacity(args, wasOpacity);
	const auto pattern = args.pattern;
	const auto &tail = (args.rounding.bottomRight == Corner::Tail)
		? pattern->tailRight
		: pattern->tailLeft;
	const auto tailShift = (args.rounding.bottomRight == Corner::Tail
		? QPoint(0, tail.height())
		: QPoint(tail.width(), tail.height())) / int(tail.devicePixelRatio());
	const auto fillBg = [&](const QRect &rect) {
		const auto fill = rect.intersected(args.patternViewport);
		if (!fill.isEmpty()) {
			PaintPatternBubblePart(
				p,
				args.patternViewport,
				pattern->pixmap,
				fill);
		}
	};
	const auto fillPattern = [&](
			int x,
			int y,
			const QImage &mask,
			QImage &cache) {
		PaintPatternBubblePart(
			p,
			args.patternViewport,
			pattern->pixmap,
			QRect(QPoint(x, y), mask.size() / int(mask.devicePixelRatio())),
			mask,
			cache);
	};
	const auto fillCorner = [&](int x, int y, int index, Corner size) {
		auto &corner = (size == Corner::Large)
			? pattern->cornersLarge[index]
			: pattern->cornersSmall[index];
		auto &cache = (size == Corner::Large)
			? (index < 2
				? pattern->cornerTopLargeCache
				: pattern->cornerBottomLargeCache)
			: (index < 2
				? pattern->cornerTopSmallCache
				: pattern->cornerBottomSmallCache);
		fillPattern(x, y, corner, cache);
	};
	const auto paintTail = [&](QPoint bottomPosition) {
		const auto position = bottomPosition - tailShift;
		fillPattern(position.x(), position.y(), tail, pattern->tailCache);
	};

	p.setOpacity(opacity);
	PaintBubbleGeneric(args, fillBg, fillCorner, paintTail);
	p.setOpacity(wasOpacity);
}

void PaintSolidBubble(QPainter &p, const SimpleBubble &args) {
	const auto &st = args.st->messageStyle(args.outbg, args.selected);
	const auto &bg = st.msgBg;
	const auto &tail = (args.rounding.bottomRight == Corner::Tail)
		? st.tailRight
		: st.tailLeft;
	const auto tailShift = (args.rounding.bottomRight == Corner::Tail)
		? QPoint(0, tail.height())
		: QPoint(tail.width(), tail.height());

	PaintBubbleGeneric(args, [&](const QRect &rect) {
		p.fillRect(rect, bg);
	}, [&](int x, int y, int index, Corner size) {
		auto &corners = (size == Corner::Large)
			? st.msgBgCornersLarge
			: st.msgBgCornersSmall;
		p.drawPixmap(x, y, corners.p[index]);
	}, [&](const QPoint &bottomPosition) {
		tail.paint(p, bottomPosition - tailShift, args.outerWidth);
	});

	if (ExtrasSettings::getInstance().showBubbleOutline()) {
		auto color = st.msgShadow->c;
		color.setAlphaF(0.35);
		p.save();
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(QPen(color, 1.));
		p.setBrush(Qt::NoBrush);
		// 描边在填充后画到气泡内，避免零顶边距时越出消息的刷新范围。
		p.drawPath(BubbleOutlinePath(
			QRectF(args.geometry).adjusted(0.5, 0.5, -0.5, -0.5),
			args.rounding,
			tail.size()));
		p.restore();
	}
}

} // namespace

std::unique_ptr<BubblePattern> PrepareBubblePattern(
		not_null<const style::palette*>) {
	auto result = std::make_unique<Ui::BubblePattern>();
	result->cornersSmall = Images::CornersMask(BubbleRadiusSmall());
	result->cornersLarge = Images::CornersMask(BubbleRadiusLarge());
	result->cornerTopSmallCache = QImage(
		result->cornersSmall[0].size(),
		QImage::Format_ARGB32_Premultiplied);
	result->cornerTopLargeCache = QImage(
		result->cornersLarge[0].size(),
		QImage::Format_ARGB32_Premultiplied);
	result->cornerBottomSmallCache = QImage(
		result->cornersSmall[2].size(),
		QImage::Format_ARGB32_Premultiplied);
	result->cornerBottomLargeCache = QImage(
		result->cornersLarge[2].size(),
		QImage::Format_ARGB32_Premultiplied);
	return result;
}

void FinishBubblePatternOnMain(not_null<BubblePattern*> pattern) {
	pattern->tailLeft = st::historyBubbleTailOutLeft.instance(Qt::white);
	pattern->tailRight = st::historyBubbleTailOutRight.instance(Qt::white);
	pattern->tailCache = QImage(
		pattern->tailLeft.size(),
		QImage::Format_ARGB32_Premultiplied);
}

void PaintBubble(QPainter &p, const SimpleBubble &args) {
	PaintBubbleShadow(p, args);
	if (UsePatternBubble(args)) {
		PaintPatternBubble(p, args);
	} else {
		PaintSolidBubble(p, args);
	}
}

void PaintBubble(QPainter &p, const ComplexBubble &args) {
	if (args.selection.empty()) {
		PaintBubble(p, args.simple);
		return;
	}
	PaintBubbleShadow(p, args.simple);
	auto simple = args.simple;
	simple.shadowed = false;
	const auto rect = args.simple.geometry;
	const auto left = rect.x();
	const auto width = rect.width();
	const auto top = rect.y();
	const auto bottom = top + rect.height();
	auto from = top;
	for (const auto &selected : args.selection) {
		if (selected.top > from) {
			PaintBubblePiece(
				p,
				simple,
				QRect(left, from, width, selected.top - from),
				false,
				(from <= top),
				false);
		}
		PaintBubblePiece(
			p,
			simple,
			QRect(left, selected.top, width, selected.height),
			true,
			(selected.top <= top),
			(selected.top + selected.height >= bottom));
		from = selected.top + selected.height;
	}
	if (from < bottom) {
		PaintBubblePiece(
			p,
			simple,
			QRect(left, from, width, bottom - from),
			false,
			false,
			true);
	}
}

void PaintBubble(QPainter &p, const BubbleWithGaps &args) {
	if (args.gaps.empty()) {
		PaintBubble(p, args.simple);
		return;
	}
	const auto rect = args.simple.geometry;
	const auto left = rect.x();
	const auto width = rect.width();
	const auto top = rect.y();
	const auto bottom = top + rect.height();
	const auto pattern = UsePatternBubble(args.simple);
	const auto paintStrip = [&](
			const QImage &mask,
			QImage &patternCache,
			QImage &solidCache,
			QColor &solidColor,
			int y) {
		const auto target = QRect(
			left,
			y,
			width,
			mask.height() / int(mask.devicePixelRatio()));
		if (pattern) {
			const auto wasOpacity = p.opacity();
			p.setOpacity(PatternBubbleOpacity(args.simple, wasOpacity));
			PaintPatternBubblePart(
				p,
				args.simple.patternViewport,
				args.simple.pattern->pixmap,
				target,
				mask,
				patternCache);
			p.setOpacity(wasOpacity);
		} else {
			const auto &st = args.simple.st->messageStyle(
				args.simple.outbg,
				args.simple.selected);
			const auto color = st.msgBg->c;
			if (solidCache.size() != mask.size() || solidColor != color) {
				if (solidCache.size() != mask.size()) {
					solidCache = QImage(
						mask.size(),
						QImage::Format_ARGB32_Premultiplied);
					solidCache.setDevicePixelRatio(mask.devicePixelRatio());
				}
				style::colorizeImage(mask, color, &solidCache);
				solidColor = color;
			}
			p.drawImage(target, solidCache);
		}
	};
	const auto torn = args.torn;
	const auto stripHeight = torn->maskTop.height()
		/ int(torn->maskTop.devicePixelRatio());
	auto from = top;
	for (const auto &gap : args.gaps) {
		const auto gapTop = gap.top;
		const auto gapBottom = gap.top + gap.height;
		if (gapTop > from) {
			PaintBubblePiece(
				p,
				args.simple,
				QRect(left, from, width, gapTop - from),
				(from <= top),
				false);
			paintStrip(
				torn->maskBottom,
				torn->patternCacheBottom,
				torn->solidCacheBottom,
				torn->solidColorBottom,
				gapTop);
		}
		if (gapBottom < bottom) {
			paintStrip(
				torn->maskTop,
				torn->patternCacheTop,
				torn->solidCacheTop,
				torn->solidColorTop,
				gapBottom - stripHeight);
		}
		from = gapBottom;
	}
	if (from < bottom) {
		PaintBubblePiece(
			p,
			args.simple,
			QRect(left, from, width, bottom - from),
			false,
			true);
	}
}

void PaintPatternBubblePart(
		QPainter &p,
		const QRect &viewport,
		const QPixmap &pixmap,
		const QRect &target) {
	const auto factor = pixmap.devicePixelRatio();
	if (viewport.size() * factor == pixmap.size()) {
		const auto fill = target.intersected(viewport);
		if (fill.isEmpty()) {
			return;
		}
		p.drawPixmap(fill, pixmap, QRect(
			(fill.topLeft() - viewport.topLeft()) * factor,
			fill.size() * factor));
	} else {
		const auto to = viewport;
		const auto from = QRect(QPoint(), pixmap.size());
		const auto deviceRect = QRect(
			QPoint(),
			QSize(p.device()->width(), p.device()->height()));
		const auto clip = (target != deviceRect);
		if (clip) {
			p.setClipRect(target);
		}
		p.drawPixmap(to, pixmap, from);
		if (clip) {
			p.setClipping(false);
		}
	}
}

void PaintPatternBubblePart(
		QPainter &p,
		const QRect &viewport,
		const QPixmap &pixmap,
		const QRect &target,
		const QImage &mask,
		QImage &cache) {
	Expects(mask.bytesPerLine() == mask.width() * 4);
	Expects(mask.format() == QImage::Format_ARGB32_Premultiplied);

	if (cache.size() != mask.size()) {
		cache = QImage(
			mask.size(),
			QImage::Format_ARGB32_Premultiplied);
	}
	cache.setDevicePixelRatio(mask.devicePixelRatio());
	Assert(cache.bytesPerLine() == cache.width() * 4);
	memcpy(cache.bits(), mask.constBits(), mask.sizeInBytes());

	auto q = QPainter(&cache);
	q.setCompositionMode(QPainter::CompositionMode_SourceIn);
	PaintPatternBubblePart(
		q,
		viewport.translated(-target.topLeft()),
		pixmap,
		QRect(QPoint(), cache.size() / int(cache.devicePixelRatio())));
	q.end();

	p.drawImage(target, cache);
}

void PaintPatternBubblePart(
		QPainter &p,
		const QRect &viewport,
		const QPixmap &pixmap,
		const QRect &target,
		Fn<void(QPainter&)> paintContent,
		QImage &cache) {
	Expects(paintContent != nullptr);

	const auto targetOrigin = target.topLeft();
	const auto targetSize = target.size();
	if (cache.size() != targetSize * style::DevicePixelRatio()) {
		cache = QImage(
			target.size() * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		cache.setDevicePixelRatio(style::DevicePixelRatio());
	}
	cache.fill(Qt::transparent);
	auto q = QPainter(&cache);
	q.translate(-targetOrigin);
	paintContent(q);
	q.translate(targetOrigin);
	q.setCompositionMode(QPainter::CompositionMode_SourceIn);
	PaintPatternBubblePart(
		q,
		viewport.translated(-targetOrigin),
		pixmap,
		QRect(QPoint(), targetSize));
	q.end();

	p.drawImage(target, cache);
}

} // namespace Ui
