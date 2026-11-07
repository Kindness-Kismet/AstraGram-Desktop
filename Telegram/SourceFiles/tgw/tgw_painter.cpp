/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_painter.h"

#include "tgw/tgw_backend.h"
#include "tgw/tgw_emoji.h"
#include "tgw/tgw_emoji_images.h"
#include "tgw/tgw_painter_filters.h"
#include "tgw/tgw_painter_geometry.h"
#include "ui/image/image_prepare.h"

namespace Tgw {
namespace {

using namespace PainterGeometry;

constexpr auto kMaxFrameBytes = int64(64 * 1024 * 1024);
constexpr auto kMaxLayerBytes = int64(128 * 1024 * 1024);
constexpr auto kMaxLayerWork = int64(256 * 1024 * 1024);
constexpr auto kMaxFilterWork = int64(1 << 23);
constexpr auto kFilterWorkRatio = 2.;
constexpr auto kMaxFilterWorkRatio = 4.;
constexpr auto kFilterPassWork = int64(4096);
constexpr auto kMaxFilterScratch = int64(64 * 1024 * 1024);
constexpr auto kPixelBytes = 4;
constexpr auto kDeviceMargin = float64(1 << 20);
constexpr auto kMaxMaskBlur = 24.;
constexpr auto kMaxMaskPixels = float64(1 << 20);
constexpr auto kMaskMargin = 2;
constexpr auto kMaxShadowBytes = int64(16 * 1024 * 1024);
constexpr auto kMaxShadows = 128;
constexpr auto kMaxShadowLayers = int64(1 << 12);
constexpr auto kMaxShadowWork = int64(1 << 25);
constexpr auto kShadowLayerWork = int64(1 << 14);
constexpr auto kShadowRowWork = int64(96);
constexpr auto kMaskPixelWork = int64(8);
constexpr auto kCompositePixelWork = int64(3);
constexpr auto kClippedShadowWork = int64(8);
constexpr auto kContourReach = 1024.;
constexpr auto kContourCellWork = int64(128);
constexpr auto kShallowContourCells = 3.;

struct ShadowSource {
	QRectF box;
	Corners boxCorners;
	QRectF area;
	QRectF shape;
	Corners shapeCorners;
	bool inset = false;
};

struct MaskLayout {
	QSize size;
	float64 scale = 0.;
	int padding = 0;
	int radius = 0;

	friend inline bool operator==(
		const MaskLayout &,
		const MaskLayout &) = default;
};

struct ShadowKey {
	float64 x = 0.;
	float64 y = 0.;
	float64 width = 0.;
	float64 height = 0.;
	Corners corners;
	MaskLayout layout;
	QRgb color = 0;
	bool inset = false;

	friend inline bool operator==(
		const ShadowKey &,
		const ShadowKey &) = default;
};

class ShadowCache final {
public:
	[[nodiscard]] QImage find(const ShadowKey &key);
	void insert(const ShadowKey &key, QImage image);

private:
	struct Entry {
		ShadowKey key;
		QImage image;
	};

	std::vector<Entry> _entries;
	int64 _bytes = 0;

};

class Executor final {
public:
	Executor(QPainter &root, const QtBackend &backend, float64 scale);

	[[nodiscard]] bool run(const twlite::DisplayList &list);
	[[nodiscard]] const QString &exceeded() const;

private:
	enum class EntryKind : uchar {
		Clip,
		Group,
		Layer,
		Hidden,
		Filter,
	};
	struct Entry {
		EntryKind kind = EntryKind::Clip;
		bool saved = false;
		bool filter = false;
	};
	struct Layer {
		QImage image;
		QPainter painter;
		QPoint origin;
		float64 opacity = 1.;
		std::shared_ptr<const twlite::Filters> filters;
		float64 filterScale = 0.;
	};

	[[nodiscard]] bool execute(const twlite::Command &command);
	void fillShape(const twlite::Command &command, const QBrush &brush);
	void strokeRect(const twlite::Command &command);
	void fillEllipse(const twlite::Command &command);
	void strokeEllipse(const twlite::Command &command);
	void drawText(const twlite::Command &command);
	void drawImage(const twlite::Command &command);
	[[nodiscard]] bool drawPath(const twlite::Command &command);
	[[nodiscard]] bool drawShadow(const twlite::Command &command);
	[[nodiscard]] bool drawMask(
		const ShadowSource &source,
		const MaskLayout &layout,
		QRgb color);
	[[nodiscard]] bool fillShadow(const ShadowSource &source, QRgb color);
	void clipShadow(const ShadowSource &source, const QRectF &ink);
	[[nodiscard]] bool pushClip(const twlite::Command &command);
	void popClip();
	[[nodiscard]] bool pushGroup(const twlite::Command &command);
	[[nodiscard]] bool pushFilter(const twlite::Command &command);
	[[nodiscard]] bool pushLayer(
		const QTransform &world,
		float64 opacity,
		const QRect &area);
	[[nodiscard]] bool admitFilter(const FilterCost &cost, int64 bytes);
	[[nodiscard]] bool chargeShadow(
		const QRectF &area,
		const QRectF &contour,
		float64 contourCells,
		int64 pixelWork,
		int64 maskPixels);
	[[nodiscard]] bool drawBackdrop(const twlite::Command &command);
	void pushHidden();
	void popGroup();
	void popFilter();
	void popEntry(bool composite);
	void popLayer(bool composite);
	void unwind(bool composite);
	[[nodiscard]] bool exceed(QString reason);

	void clipToRect(const QRectF &rect);
	[[nodiscard]] bool clipToPath(const twlite::Path &source);
	[[nodiscard]] QPainter &painter() const;
	[[nodiscard]] QPoint origin() const;
	[[nodiscard]] QRectF bounded(const QRectF &rect) const;
	[[nodiscard]] QRectF frameInk(const QRectF &rect) const;
	[[nodiscard]] QPolygonF around(const QRectF &ink) const;
	[[nodiscard]] QRectF limit() const;
	[[nodiscard]] bool reachable(QPointF point) const;

	QPainter &_root;
	const QtBackend &_backend;
	std::vector<Entry> _stack;
	std::vector<std::unique_ptr<Layer>> _layers;
	std::vector<QRectF> _inks;
	PaintAllowance _allowance;
	size_t _group = 0;
	int64 _layerBytes = 0;
	int64 _layerWork = 0;
	int64 _maxFilterWork = 0;
	int64 _filterWork = 0;
	int64 _shadowWork = kMaxShadowWork;
	int _hidden = 0;
	bool _pathClips = false;
	bool _refused = false;
	QString _exceeded;

};

[[nodiscard]] bool ValidFrame(QSize size, float64 scale) {
	return !size.isEmpty() && std::isfinite(scale) && (scale > 0.);
}

[[nodiscard]] int64 LayerBytes(QSize size) {
	return int64(size.width()) * size.height() * kPixelBytes;
}

// 滤镜开销随缩放面积增长，预算以两倍缩放为下限，并限制最大增幅。
[[nodiscard]] int64 FilterWorkBound(float64 cssToDevice) {
	const auto factor = std::clamp(
		cssToDevice,
		kFilterWorkRatio,
		kMaxFilterWorkRatio) / kFilterWorkRatio;
	return int64(kMaxFilterWork * factor * factor);
}

[[nodiscard]] QRectF DeviceLimit(const QPaintDevice &device) {
	return QRectF(0., 0., device.width(), device.height()).adjusted(
		-kDeviceMargin,
		-kDeviceMargin,
		kDeviceMargin,
		kDeviceMargin);
}

[[nodiscard]] QRect LayerArea(
		QPainter &parent,
		QPoint origin,
		const QPaintDevice &frame,
		const QRectF &ink,
		float64 spread) {
	const auto visible = VisibleRect(parent);
	if (ink.isEmpty() || visible.isEmpty()) {
		return QRect();
	}
	auto result = QRectF(0., 0., frame.width(), frame.height());
	result = result.intersected(visible.translated(origin).adjusted(
		-spread,
		-spread,
		spread,
		spread));
	result = result.intersected(ink.adjusted(
		-kInkMargin,
		-kInkMargin,
		kInkMargin,
		kInkMargin));
	return result.toAlignedRect();
}

[[nodiscard]] float64 Spread(const QMarginsF &reach) {
	return std::max({
		reach.left(),
		reach.top(),
		reach.right(),
		reach.bottom(),
	});
}

[[nodiscard]] float64 DeviceSpread(
		float64 spread,
		const QPaintDevice &frame) {
	const auto result = spread + kInkMargin;
	const auto most = float64(frame.width()) + frame.height();
	return (result < most) ? result : most;
}

[[nodiscard]] bool Structural(twlite::Cmd kind) {
	using Cmd = twlite::Cmd;
	constexpr auto kinds = std::array{
		Cmd::ClipPush,
		Cmd::ClipPop,
		Cmd::GroupPush,
		Cmd::GroupPop,
		Cmd::FilterPush,
		Cmd::FilterPop,
	};
	return ranges::contains(kinds, kind);
}

[[nodiscard]] bool Shown(const twlite::Command &command) {
	return !command.hidden
		&& std::isfinite(command.opacity)
		&& (command.opacity > 0.f);
}

[[nodiscard]] std::optional<QTransform> GroupTransform(
		const twlite::Command &command,
		const QTransform &parent) {
	const auto &m = command.matrix;
	const auto matrix = QTransform(m[0], m[1], m[2], m[3], m[4], m[5]);
	if (!Finite(matrix) || !matrix.isInvertible()) {
		return std::nullopt;
	}
	const auto result = matrix * parent;
	return (Finite(result) && result.isInvertible())
		? std::make_optional(result)
		: std::nullopt;
}

// 字形可能超出行框，仅在向四周扩展一行后仍不可见时跳过。
[[nodiscard]] bool TextMayReachClip(
		const QPainter &painter,
		const twlite::Command &command) {
	const auto box = ToRect(command.rect);
	if (box.isEmpty()
		|| (painter.combinedTransform().type() >= QTransform::TxProject)) {
		return true;
	}
	const auto margin = box.height();
	return painter.clipBoundingRect().intersects(
		box.adjusted(-margin, -margin, margin, margin));
}

[[nodiscard]] bool WithinContourReach(
		QSize device,
		const QRectF &deviceBounds) {
	const auto frame = QRectF(QPointF(), QSizeF(device));
	return frame.adjusted(
		-kContourReach,
		-kContourReach,
		kContourReach,
		kContourReach).contains(deviceBounds);
}

// Qt 对远端轮廓会额外裁剪；先裁掉设备边界外的曲线，限制每层轮廓开销。
[[nodiscard]] QPainterPath NearRoundedPath(
		const QRectF &rect,
		const Corners &corners,
		const QPainter &painter,
		const QRectF &ink) {
	const auto world = painter.combinedTransform();
	const auto device = DeviceSize(painter);
	auto result = RoundedPath(rect, corners);
	if (WithinContourReach(device, world.mapRect(rect))) {
		return result;
	}
	auto invertible = false;
	const auto inverse = world.inverted(&invertible);
	auto area = QPainterPath();
	area.addRect(ink.adjusted(
		-kInkMargin,
		-kInkMargin,
		kInkMargin,
		kInkMargin));
	result = world.map(result).intersected(area);
	return (invertible
		&& WithinContourReach(device, result.controlPointRect()))
		? inverse.map(result)
		: QPainterPath();
}

[[nodiscard]] QRectF SourceRect(
		QSize size,
		const QRectF &target,
		const QRectF &visible) {
	const auto scaleX = size.width() / target.width();
	const auto scaleY = size.height() / target.height();
	return QRectF(
		(visible.x() - target.x()) * scaleX,
		(visible.y() - target.y()) * scaleY,
		visible.width() * scaleX,
		visible.height() * scaleY);
}

[[nodiscard]] ShadowSource OuterShadow(
		const QRectF &box,
		const Corners &corners,
		QPointF offset,
		float64 spread) {
	const auto shape = box.adjusted(-spread, -spread, spread, spread);
	return {
		.box = box,
		.boxCorners = corners,
		.area = shape.translated(offset),
		.shape = QRectF(QPointF(), shape.size()),
		.shapeCorners = shape.isEmpty()
			? Corners()
			: SpreadCorners(shape, corners, spread),
	};
}

[[nodiscard]] ShadowSource InsetShadow(
		const QRectF &box,
		const Corners &corners,
		QPointF offset,
		float64 spread) {
	const auto inner = QRectF(QPointF(), box.size()).adjusted(
		spread,
		spread,
		-spread,
		-spread);
	const auto hole = inner.translated(offset);
	return {
		.box = box,
		.boxCorners = corners,
		.area = box,
		.shape = hole,
		.shapeCorners = hole.isEmpty()
			? Corners()
			: SpreadCorners(hole, corners, -spread),
		.inset = true,
	};
}

[[nodiscard]] float64 FittingScale(QSizeF size, float64 margin) {
	const auto a = size.width() * size.height();
	const auto b = margin * (size.width() + size.height());
	const auto c = margin * margin - kMaxMaskPixels;
	return -2. * c / (b + std::sqrt(b * b - 4. * a * c));
}

[[nodiscard]] std::optional<MaskLayout> ChooseMaskLayout(
		QSizeF area,
		float64 blur,
		float64 stretch) {
	if (!std::isfinite(stretch) || !(stretch > 0.)) {
		return std::nullopt;
	}
	const auto sigma = blur / 2.;
	const auto limit = std::min(stretch, kMaxMaskBlur / blur);
	const auto margin = 2. * (StackRadius(sigma * limit) + kMaskMargin) + 1.;
	const auto scale = std::min(limit, FittingScale(area, margin));
	if (!std::isfinite(scale) || !(scale > 0.)) {
		return std::nullopt;
	}
	const auto radius = StackRadius(sigma * scale);
	const auto padding = radius + kMaskMargin;
	const auto width = std::ceil(area.width() * scale) + 2. * padding;
	const auto height = std::ceil(area.height() * scale) + 2. * padding;
	if (width * height > kMaxMaskPixels) {
		return std::nullopt;
	}
	return MaskLayout{
		.size = QSize(int(width), int(height)),
		.scale = scale,
		.padding = padding,
		.radius = radius,
	};
}

[[nodiscard]] int64 BlurBytes(QSize size) {
	const auto mask = LayerBytes(size);
	const auto scratch = mask;
	return mask + scratch;
}

void DrawTextWithEmoji(
		QPainter &p,
		const QtFont &font,
		const QString &text,
		const std::vector<EmojiCell> &cells,
		int alpha) {
	const auto fontSize = float64(font.font.pixelSize());
	const auto side = EmojiAdvance(fontSize)
		* MaxStretch(p.combinedTransform());
	const auto opacity = p.opacity();
	const auto draw = [&](float64 x, const QString &piece, EmojiPtr emoji) {
		if (!emoji) {
			p.drawText(QPointF(x, 0.), piece);
			return;
		}
		p.setOpacity(opacity * alpha / 255.);
		PaintEmoji(p, emoji, EmojiBox(x, fontSize), side);
		p.setOpacity(opacity);
	};
	WalkTextWithEmoji(font.metrics, text, cells, fontSize, draw);
}

[[nodiscard]] QRectF EmojiTextInk(
		const QFontMetricsF &metrics,
		const QString &text,
		const std::vector<EmojiCell> &cells,
		float64 fontSize) {
	auto result = QRectF();
	const auto add = [&](float64 x, const QString &piece, EmojiPtr emoji) {
		result |= emoji
			? EmojiBox(x, fontSize)
			: metrics.boundingRect(piece).translated(x, 0.);
	};
	WalkTextWithEmoji(metrics, text, cells, fontSize, add);
	return result;
}

[[nodiscard]] QRectF TextInk(
		const twlite::Command &command,
		const QtBackend &backend) {
	const auto font = backend.font(command.font);
	if (!font || !(font->scale > 0.) || command.text.empty()) {
		return QRectF();
	}
	const auto &metrics = font->metrics;
	const auto margin = metrics.height();
	const auto text = QString::fromUtf8(
		command.text.data(),
		int(command.text.size()));
	const auto cells = FindEmojiCells(text);
	const auto ink = cells.empty()
		? metrics.boundingRect(text)
		: EmojiTextInk(metrics, text, cells, font->font.pixelSize());
	auto transform = QTransform::fromTranslate(command.x, command.y);
	transform.scale(font->scale, font->scale);
	return transform.mapRect(ink.adjusted(
		-margin,
		-margin,
		margin,
		margin));
}

[[nodiscard]] QRectF PathInk(const twlite::Path &path) {
	const auto vector = VectorPath(path.ops, path.points, path.evenodd);
	if (vector.isEmpty()) {
		return QRectF();
	}
	const auto stroked = path.stroke
		&& ValidWidth(path.stroke_width)
		&& (path.stroke_color.a || StrokeStops(path) > 0);
	const auto reach = stroked
		? StrokeReach(path.stroke_width, MiterLimit(path))
		: 0.;
	return vector.controlPointRect().adjusted(-reach, -reach, reach, reach);
}

[[nodiscard]] QRectF ShadowInk(
		const twlite::Command &command,
		float64 stretch) {
	const auto box = ToRect(command.rect);
	if (command.inset || box.isEmpty()) {
		return box;
	}
	const auto offset = QPointF(command.ax, command.ay);
	const auto blur = std::max(float64(command.bx), 0.);
	const auto spread = float64(command.by);
	if (!Finite(offset) || !std::isfinite(blur) || !std::isfinite(spread)) {
		return QRectF();
	}
	const auto shape = box.adjusted(-spread, -spread, spread, spread);
	const auto area = shape.translated(offset);
	const auto layout = (blur > 0. && !area.isEmpty())
		? ChooseMaskLayout(area.size(), blur, stretch)
		: std::nullopt;
	const auto margin = layout ? (layout->padding / layout->scale) : 0.;
	return area.adjusted(-margin, -margin, margin, margin);
}

[[nodiscard]] QRectF CommandInk(
		const twlite::Command &command,
		const QtBackend &backend,
		float64 stretch) {
	using Cmd = twlite::Cmd;
	const auto rect = ToRect(command.rect);
	const auto width = ValidWidth(command.width)
		? float64(command.width)
		: 0.;
	switch (command.kind) {
	case Cmd::FillRect:
	case Cmd::Image:
	case Cmd::Linear:
	case Cmd::Radial:
	case Cmd::Conic:
		return command.has_clip
			? rect.intersected(ToRect(command.clip))
			: rect;
	case Cmd::StrokeRect:
		return rect.isEmpty()
			? QRectF()
			: rect.adjusted(-width, -width, width, width);
	case Cmd::FillEllipse:
	case Cmd::StrokeEllipse: return rect;
	case Cmd::Text: return TextInk(command, backend);
	case Cmd::Path: return command.path ? PathInk(*command.path) : QRectF();
	case Cmd::Shadow: return ShadowInk(command, stretch);
	case Cmd::Backdrop: return rect;
	case Cmd::ClipPush:
	case Cmd::ClipPop:
	case Cmd::GroupPush:
	case Cmd::GroupPop:
	case Cmd::FilterPush:
	case Cmd::FilterPop: break;
	}
	return QRectF();
}

[[nodiscard]] QRectF DeviceInk(
		const QTransform &world,
		const QRectF &rect,
		const QRectF &frame,
		const QMarginsF &reach = QMarginsF()) {
	if (rect.isEmpty()) {
		return QRectF();
	}
	const auto result = world.mapRect(rect);
	if (!Finite(result.topLeft()) || !Finite(result.bottomRight())) {
		return frame;
	}
	const auto most = frame.width() + frame.height();
	const auto capped = [&](float64 value) {
		return (value < most) ? value : most;
	};
	return result.adjusted(
		-capped(reach.left()),
		-capped(reach.top()),
		capped(reach.right()),
		capped(reach.bottom())).intersected(frame);
}

[[nodiscard]] float64 ContourCells(
		const QRectF &rect,
		const Corners &corners,
		const QTransform &world,
		QSize device,
		const QRectF &ink) {
	const auto frame = QRectF(QPointF(), QSizeF(device));
	const auto whole = WithinContourReach(device, world.mapRect(rect));
	const auto bounds = whole ? DeviceInk(world, rect, frame) : ink;
	const auto stretch = MaxStretch(world);
	auto curved = 0.;
	auto pieces = 0.;
	for (const auto &corner : {
			corners.topLeft,
			corners.topRight,
			corners.bottomRight,
			corners.bottomLeft }) {
		curved += (std::abs(world.m11()) + std::abs(world.m12())) * corner.x
			+ (std::abs(world.m21()) + std::abs(world.m22())) * corner.y;
		const auto radius = stretch * std::max(corner.x, corner.y);
		pieces += std::min(
			std::exp2(float64(kCurveLevels)),
			2. * std::sqrt(whole
				? radius
				: std::min(radius, bounds.width() + bounds.height())));
	}
	const auto shallow = [](float64 across, float64 down) {
		return down
			? std::min(kRasterCells, std::abs(across / down))
			: 0.;
	};
	const auto density = std::max(
		shallow(world.m11(), world.m12()),
		shallow(world.m21(), world.m22()));
	const auto perimeter = 2. * (bounds.width() + bounds.height());
	return pieces
		+ (1. + kShallowContourCells * density / kRasterCells)
			* (world.isRotating() ? perimeter : std::min(perimeter, curved));
}

[[nodiscard]] QRectF FilterInk(
		const twlite::Command &command,
		const QTransform &world,
		const QRectF &frame) {
	const auto &chain = command.filter;
	const auto scale = std::sqrt(std::abs(world.determinant()));
	const auto reach = (chain && UsableFilters(*chain))
		? FilterReach(*chain, scale)
		: QMarginsF();
	return DeviceInk(world, ToRect(command.rect), frame, reach);
}

[[nodiscard]] std::vector<QRectF> GroupInks(
		const twlite::DisplayList &list,
		const QtBackend &backend,
		const QTransform &root,
		const QRectF &frame) {
	struct Open {
		size_t slot = 0;
		QTransform world;
		QRectF ink;
		bool hidden = false;
		bool layer = false;
	};
	auto result = std::vector<QRectF>();
	auto stack = std::vector<Open>{ Open{ .world = root } };
	auto layers = 0;
	const auto close = [&] {
		const auto group = stack.back();
		stack.pop_back();
		result[group.slot] = group.ink;
		stack.back().ink |= group.ink;
		if (group.layer) {
			--layers;
		}
	};
	for (const auto &command : list) {
		if (command.kind == twlite::Cmd::GroupPush) {
			const auto parent = stack.back();
			const auto world = (parent.hidden || !Shown(command))
				? std::nullopt
				: GroupTransform(command, parent.world);
			const auto layer = world && (command.opacity < 1.f);
			stack.push_back({
				.slot = result.size(),
				.world = world.value_or(QTransform()),
				.hidden = !world,
				.layer = layer,
			});
			result.emplace_back();
			if (layer) {
				++layers;
			}
		} else if (command.kind == twlite::Cmd::GroupPop) {
			if (stack.size() > 1) {
				close();
			}
		} else if (layers > 0
			&& !stack.back().hidden
			&& !stack.back().ink.contains(frame)) {
			auto &top = stack.back();
			top.ink |= (command.kind == twlite::Cmd::FilterPush)
				? FilterInk(command, top.world, frame)
				: DeviceInk(
					top.world,
					CommandInk(command, backend, MaxStretch(top.world)),
					frame);
		}
	}
	while (stack.size() > 1) {
		close();
	}
	return result;
}

[[nodiscard]] QImage ColorizeMask(QImage mask, QRgb color) {
	auto colors = std::array<QRgb, 256>();
	for (auto coverage = 0; coverage != 256; ++coverage) {
		colors[coverage] = qPremultiply(qRgba(
			qRed(color),
			qGreen(color),
			qBlue(color),
			(qAlpha(color) * coverage + 127) / 255));
	}
	for (auto y = 0; y != mask.height(); ++y) {
		const auto row = reinterpret_cast<QRgb*>(mask.scanLine(y));
		// 模糊函数保留透明度通道，覆盖率取自蓝色通道，与 Ui::BoxShadow 一致。
		std::transform(row, row + mask.width(), row, [&](QRgb pixel) {
			return colors[pixel & 0xFFU];
		});
	}
	return mask;
}

[[nodiscard]] QImage PrepareShadow(const ShadowKey &key) {
	const auto &layout = key.layout;
	auto result = QImage(layout.size, QImage::Format_ARGB32_Premultiplied);
	if (result.isNull()) {
		return QImage();
	}
	result.fill(key.inset ? Qt::white : Qt::transparent);
	auto p = QPainter();
	if (!p.begin(&result)) {
		return QImage();
	}
	p.setRenderHint(QPainter::Antialiasing);
	if (key.inset) {
		p.setCompositionMode(QPainter::CompositionMode_Clear);
	}
	p.translate(layout.padding, layout.padding);
	p.scale(layout.scale, layout.scale);
	p.fillPath(
		NearRoundedPath(
			QRectF(key.x, key.y, key.width, key.height),
			key.corners,
			p,
			QRectF(result.rect())),
		Qt::white);
	p.end();
	return ShadowFromCoverage(std::move(result), layout.radius, key.color);
}

[[nodiscard]] ShadowCache &Shadows() {
	static auto result = ShadowCache();
	return result;
}

QImage ShadowCache::find(const ShadowKey &key) {
	const auto i = ranges::find(_entries, key, &Entry::key);
	if (i == end(_entries)) {
		return QImage();
	}
	std::rotate(i, i + 1, end(_entries));
	return _entries.back().image;
}

void ShadowCache::insert(const ShadowKey &key, QImage image) {
	_bytes += LayerBytes(image.size());
	_entries.push_back({ .key = key, .image = std::move(image) });
	auto evicted = begin(_entries);
	while (_bytes > kMaxShadowBytes
		|| end(_entries) - evicted > kMaxShadows) {
		_bytes -= LayerBytes(evicted->image.size());
		++evicted;
	}
	_entries.erase(begin(_entries), evicted);
}

Executor::Executor(
		QPainter &root,
		const QtBackend &backend,
		float64 scale)
: _root(root)
, _backend(backend)
, _maxFilterWork(FilterWorkBound(scale))
, _filterWork(_maxFilterWork) {
	PrepareHints(_root);
	_root.scale(scale, scale);
}

bool Executor::run(const twlite::DisplayList &list) {
	using Cmd = twlite::Cmd;
	auto passes = int64();
	auto layers = int64();
	for (const auto &command : list) {
		const auto vector = (command.kind == Cmd::Path)
			|| (command.kind == Cmd::ClipPush);
		const auto filter = (command.kind == Cmd::FilterPush)
			|| (command.kind == Cmd::Backdrop);
		if (vector && command.path) {
			_pathClips |= (command.kind == Cmd::ClipPush);
			const auto &path = *command.path;
			if (!_allowance.chargeElements(int64(path.ops.size()))
				|| !_allowance.chargeStops(StrokeStops(path))) {
				return false;
			}
		} else if (filter && command.filter && !command.filter->empty()) {
			++passes;
		} else if (command.kind == Cmd::Shadow) {
			++layers;
		}
	}
	if (passes > _filterWork / kFilterPassWork) {
		return exceed(PaintLimit(
			u"%1 filter passes in one frame"_q.arg(passes),
			_filterWork / kFilterPassWork));
	}
	_filterWork -= passes * kFilterPassWork;
	if (layers > kMaxShadowLayers) {
		return exceed(PaintLimit(
			u"%1 shadow layers in one frame"_q.arg(layers),
			kMaxShadowLayers));
	}
	const auto device = _root.device();
	_inks = GroupInks(
		list,
		_backend,
		_root.worldTransform(),
		QRectF(0., 0., device->width(), device->height()));
	for (const auto &command : list) {
		if (!execute(command) || _refused) {
			unwind(false);
			return false;
		}
	}
	unwind(true);
	return !_refused;
}

const QString &Executor::exceeded() const {
	return _exceeded.isEmpty() ? _allowance.exceeded() : _exceeded;
}

bool Executor::execute(const twlite::Command &command) {
	using Cmd = twlite::Cmd;
	if (_hidden > 0 && !Structural(command.kind)) {
		return true;
	}
	switch (command.kind) {
	case Cmd::FillRect: fillShape(command, SolidBrush(command.color)); break;
	case Cmd::StrokeRect: strokeRect(command); break;
	case Cmd::FillEllipse: fillEllipse(command); break;
	case Cmd::StrokeEllipse: strokeEllipse(command); break;
	case Cmd::Text: drawText(command); break;
	case Cmd::Image: drawImage(command); break;
	case Cmd::Linear:
	case Cmd::Radial:
	case Cmd::Conic: fillShape(command, BackgroundBrush(command)); break;
	case Cmd::ClipPush: return pushClip(command);
	case Cmd::ClipPop: popClip(); break;
	case Cmd::Path: return drawPath(command);
	case Cmd::GroupPush: return pushGroup(command);
	case Cmd::GroupPop: popGroup(); break;
	case Cmd::Shadow: return drawShadow(command);
	case Cmd::FilterPush: return pushFilter(command);
	case Cmd::FilterPop: popFilter(); break;
	case Cmd::Backdrop: return drawBackdrop(command);
	}
	return true;
}

void Executor::fillShape(
		const twlite::Command &command,
		const QBrush &brush) {
	const auto rect = ToRect(command.rect);
	const auto clip = command.has_clip ? ToRect(command.clip) : rect;
	if (brush.style() == Qt::NoBrush || rect.isEmpty() || clip.isEmpty()) {
		return;
	}
	auto &p = painter();
	const auto corners = NormalizedCorners(rect, command.radii);
	if (!corners) {
		return;
	} else if (!Rounded(*corners)) {
		p.fillRect(rect.intersected(clip), brush);
	} else if (clip.contains(rect)) {
		p.fillPath(RoundedPath(rect, *corners), brush);
	} else {
		p.save();
		clipToRect(clip);
		p.fillPath(RoundedPath(rect, *corners), brush);
		p.restore();
	}
}

void Executor::strokeRect(const twlite::Command &command) {
	const auto rect = ToRect(command.rect);
	if (rect.isEmpty() || !command.color.a || !ValidWidth(command.width)) {
		return;
	}
	if (const auto corners = NormalizedCorners(rect, command.radii)) {
		painter().strokePath(
			RoundedPath(rect, *corners),
			StrokePen(command.color, command.width));
	}
}

void Executor::fillEllipse(const twlite::Command &command) {
	const auto rect = ToRect(command.rect);
	if (rect.isEmpty() || !command.color.a) {
		return;
	}
	auto path = QPainterPath();
	path.addEllipse(rect);
	painter().fillPath(path, ToColor(command.color));
}

void Executor::strokeEllipse(const twlite::Command &command) {
	const auto rect = ToRect(command.rect);
	if (rect.isEmpty() || !command.color.a || !ValidWidth(command.width)) {
		return;
	}
	const auto half = command.width / 2.;
	const auto inset = rect.adjusted(half, half, -half, -half);
	if (inset.isEmpty()) {
		return;
	}
	auto path = QPainterPath();
	path.addEllipse(inset);
	painter().strokePath(path, StrokePen(command.color, command.width));
}

void Executor::drawText(const twlite::Command &command) {
	const auto font = _backend.font(command.font);
	const auto baseline = QPointF(command.x, command.y);
	if (!font
		|| !(font->scale > 0.)
		|| command.text.empty()
		|| !command.color.a
		|| !Finite(baseline)
		|| !reachable(baseline)) {
		return;
	}
	auto &p = painter();
	if (p.hasClipping() && !TextMayReachClip(p, command)) {
		return;
	}
	const auto text = QString::fromUtf8(
		command.text.data(),
		int(command.text.size()));
	const auto cells = FindEmojiCells(text);
	p.save();
	p.translate(baseline);
	if (font->scale != 1.) {
		p.scale(font->scale, font->scale);
	}
	p.setFont(font->font);
	p.setPen(ToColor(command.color));
	if (cells.empty()) {
		p.drawText(QPointF(), text);
	} else {
		DrawTextWithEmoji(p, *font, text, cells, command.color.a);
	}
	p.restore();
}

void Executor::drawImage(const twlite::Command &command) {
	const auto image = command.canvas
		? _backend.canvas(command.canvas)
		: _backend.image(command.image);
	const auto target = ToRect(command.rect);
	const auto base = command.has_clip ? ToRect(command.clip) : target;
	if (!image || image->isNull() || target.isEmpty() || base.isEmpty()) {
		return;
	}
	const auto corners = NormalizedCorners(base, command.radii);
	const auto visible = bounded(target.intersected(base));
	if (!corners || visible.isEmpty()) {
		return;
	}
	auto &p = painter();
	const auto rounded = Rounded(*corners);
	if (!rounded && base.contains(target)) {
		p.drawImage(target, *image);
		return;
	}
	p.save();
	if (rounded) {
		p.setClipPath(RoundedPath(base, *corners), Qt::IntersectClip);
	} else {
		clipToRect(base);
	}
	p.drawImage(target, *image);
	p.restore();
}

bool Executor::drawPath(const twlite::Command &command) {
	const auto source = command.path.get();
	if (!source) {
		return true;
	}
	const auto path = VectorPath(source->ops, source->points, source->evenodd);
	if (path.isEmpty()) {
		return true;
	}
	auto &p = painter();
	const auto brush = source->fill ? PathBrush(*source) : QBrush();
	if (brush.style() != Qt::NoBrush) {
		if (!_allowance.chargeFill(p, path, brush)) {
			return false;
		}
		p.fillPath(path, brush);
	}
	if (!source->stroke || !ValidWidth(source->stroke_width)) {
		return true;
	}
	const auto pen = PathPen(*source);
	if (pen.brush().style() == Qt::NoBrush) {
		return true;
	}
	auto dashes = DashPattern(source->dashes);
	if (dashes.empty()) {
		if (!_allowance.chargeStroke(p, path, pen)) {
			return false;
		}
		p.strokePath(path, pen);
		return true;
	}
	return _allowance.strokeDashes(
		p,
		path,
		pen,
		std::move(dashes),
		std::isfinite(source->dash_offset) ? source->dash_offset : 0.);
}

bool Executor::drawShadow(const twlite::Command &command) {
	const auto box = ToRect(command.rect);
	const auto offset = QPointF(command.ax, command.ay);
	const auto blur = std::max(float64(command.bx), 0.);
	const auto spread = float64(command.by);
	const auto corners = box.isEmpty()
		? std::nullopt
		: NormalizedCorners(box, command.radii);
	if (!corners
		|| !command.color.a
		|| !Finite(offset)
		|| !std::isfinite(blur)
		|| !std::isfinite(spread)
		|| (blur == 0. && spread == 0. && offset.isNull())
		|| limit().isEmpty()) {
		return true;
	}
	const auto source = command.inset
		? InsetShadow(box, *corners, offset, spread)
		: OuterShadow(box, *corners, offset, spread);
	const auto color = ToColor(command.color).rgba();
	if (source.shape.isEmpty() && !source.inset) {
		return true;
	} else if (blur == 0. || source.shape.isEmpty()) {
		return fillShadow(source, color);
	}
	const auto layout = ChooseMaskLayout(
		source.area.size(),
		blur,
		MaxStretch(painter().combinedTransform()));
	if (!layout) {
		return true;
	} else if (layout->radius < 1) {
		return fillShadow(source, color);
	}
	return drawMask(source, *layout, color);
}

bool Executor::drawMask(
		const ShadowSource &source,
		const MaskLayout &layout,
		QRgb color) {
	auto &p = painter();
	const auto padding = layout.padding / layout.scale;
	const auto target = QRectF(
		source.area.topLeft() - QPointF(padding, padding),
		QSizeF(layout.size) / layout.scale);
	const auto visible = bounded(target);
	const auto ink = frameInk(source.inset
		? visible.intersected(source.box)
		: visible);
	if (ink.isEmpty()
		|| (p.hasClipping() && !p.clipBoundingRect().intersects(visible))) {
		return true;
	}
	const auto key = ShadowKey{
		.x = source.shape.x(),
		.y = source.shape.y(),
		.width = source.shape.width(),
		.height = source.shape.height(),
		.corners = source.shapeCorners,
		.layout = layout,
		.color = color,
		.inset = source.inset,
	};
	auto image = Shadows().find(key);
	const auto maskPixels = image.isNull()
		? int64(layout.size.width()) * layout.size.height()
		: int64();
	auto cells = ContourCells(
		source.box,
		source.boxCorners,
		p.combinedTransform(),
		DeviceSize(p),
		ink);
	if (image.isNull()) {
		cells += ContourCells(
			QRectF(key.x, key.y, key.width, key.height),
			key.corners,
			QTransform::fromTranslate(layout.padding, layout.padding).scale(
				layout.scale,
				layout.scale),
			layout.size,
			QRectF(QPointF(), QSizeF(layout.size)));
	}
	if (!chargeShadow(
			visible,
			source.box,
			cells,
			kCompositePixelWork,
			maskPixels)) {
		return false;
	}
	if (image.isNull()) {
		const auto bytes = BlurBytes(layout.size);
		if (bytes > kMaxLayerBytes - _layerBytes) {
			return exceed(PaintLimit(
				u"%1 active layer bytes at once"_q.arg(_layerBytes + bytes),
				kMaxLayerBytes));
		}
		image = PrepareShadow(key);
		if (image.isNull()) {
			return false;
		}
		Shadows().insert(key, image);
	}
	p.save();
	clipShadow(source, ink);
	p.drawImage(visible, image, SourceRect(layout.size, target, visible));
	p.restore();
	return true;
}

bool Executor::fillShadow(const ShadowSource &source, QRgb color) {
	const auto drawn = source.inset ? source.box : source.area;
	const auto ink = frameInk(drawn);
	if (ink.isEmpty()) {
		return true;
	}
	const auto shape = source.shape.translated(source.area.topLeft());
	auto &p = painter();
	const auto world = p.combinedTransform();
	const auto device = DeviceSize(p);
	auto cells = ContourCells(
		shape,
		source.shapeCorners,
		world,
		device,
		ink);
	cells += ContourCells(
		source.box,
		source.boxCorners,
		world,
		device,
		ink);
	if (!chargeShadow(
			drawn,
			source.inset ? shape : source.box,
			cells,
			1,
			0)) {
		return false;
	}
	auto path = source.shape.isEmpty()
		? QPainterPath()
		: NearRoundedPath(shape, source.shapeCorners, p, ink);
	if (source.inset) {
		path.addPolygon(around(ink));
	}
	path.setFillRule(Qt::OddEvenFill);
	p.save();
	clipShadow(source, ink);
	p.fillPath(path, QColor::fromRgba(color));
	p.restore();
	return true;
}

void Executor::clipShadow(const ShadowSource &source, const QRectF &ink) {
	if (source.inset && !Rounded(source.boxCorners)) {
		clipToRect(source.box);
		return;
	}
	auto &p = painter();
	auto path = NearRoundedPath(source.box, source.boxCorners, p, ink);
	if (!source.inset) {
		path.addPolygon(around(ink));
		path.setFillRule(Qt::OddEvenFill);
	}
	p.setClipPath(path, Qt::IntersectClip);
}

bool Executor::drawBackdrop(const twlite::Command &command) {
	const auto &chain = command.filter;
	if (!chain || chain->empty()) {
		return true;
	} else if (!UsableFilters(*chain) || !FiniteRect(command.rect)) {
		return false;
	}
	const auto rect = ToRect(command.rect);
	if (rect.isEmpty()) {
		return true;
	}
	auto &p = painter();
	const auto local = p.combinedTransform();
	const auto scale = std::sqrt(std::abs(local.determinant()));
	const auto corners = NormalizedCorners(rect, command.radii);
	if (!corners || !Finite(local) || !std::isfinite(scale)) {
		return false;
	}
	const auto target = dynamic_cast<QImage*>(p.device());
	if (!target || !local.isInvertible()) {
		return true;
	}
	const auto spread = Spread(FilterReach(*chain, scale));
	const auto sample = DeviceInk(
		local,
		rect,
		QRectF(target->rect()),
		QMarginsF(spread, spread, spread, spread));
	const auto from = LayerArea(
		p,
		QPoint(),
		*target,
		sample,
		DeviceSpread(spread, *target));
	if (from.isEmpty()) {
		return true;
	}
	const auto bytes = LayerBytes(from.size());
	const auto cost = MeasureFilters(*chain, from.size(), scale);
	if (!cost || !admitFilter(*cost, bytes)) {
		return false;
	}
	_layerWork += bytes;
	auto copy = target->copy(from);
	if (copy.isNull() || !ApplyFilters(copy, *chain, scale)) {
		return false;
	}
	p.save();
	if (Rounded(*corners)) {
		p.setClipPath(RoundedPath(rect, *corners), Qt::IntersectClip);
	} else {
		clipToRect(rect);
	}
	p.setWorldTransform(QTransform());
	p.setCompositionMode(QPainter::CompositionMode_Source);
	p.drawImage(from.topLeft(), copy);
	p.restore();
	return true;
}

bool Executor::pushClip(const twlite::Command &command) {
	if (_hidden > 0) {
		_stack.push_back({ .kind = EntryKind::Clip });
		return true;
	}
	auto &p = painter();
	p.save();
	_stack.push_back({ .kind = EntryKind::Clip, .saved = true });
	if (command.path) {
		return clipToPath(*command.path);
	}
	const auto rect = ToRect(command.rect);
	const auto corners = rect.isEmpty()
		? std::nullopt
		: NormalizedCorners(rect, command.radii);
	if (!corners) {
		p.setClipRect(QRectF(), Qt::IntersectClip);
	} else if (Rounded(*corners)) {
		p.setClipPath(RoundedPath(rect, *corners), Qt::IntersectClip);
	} else {
		clipToRect(rect);
	}
	return true;
}

void Executor::popClip() {
	if (!_stack.empty() && _stack.back().kind == EntryKind::Clip) {
		popEntry(true);
	}
}

bool Executor::pushGroup(const twlite::Command &command) {
	const auto ink = _inks[_group++];
	auto &p = painter();
	const auto world = (_hidden > 0 || !Shown(command))
		? std::nullopt
		: GroupTransform(command, p.worldTransform());
	if (!world) {
		pushHidden();
		return true;
	} else if (command.opacity < 1.f) {
		const auto area = LayerArea(p, origin(), *_root.device(), ink, 0.);
		return pushLayer(*world, command.opacity, area);
	}
	p.save();
	p.setWorldTransform(*world);
	_stack.push_back({ .kind = EntryKind::Group, .saved = true });
	return true;
}

bool Executor::pushFilter(const twlite::Command &command) {
	const auto &chain = command.filter;
	if (_hidden > 0 || !chain || chain->empty()) {
		_stack.push_back({ .kind = EntryKind::Filter, .filter = true });
		return true;
	} else if (!UsableFilters(*chain) || !FiniteRect(command.rect)) {
		return false;
	}
	auto &p = painter();
	const auto offset = origin();
	const auto device = p.combinedTransform()
		* QTransform::fromTranslate(offset.x(), offset.y());
	const auto scale = std::sqrt(std::abs(device.determinant()));
	if (!Finite(device) || !std::isfinite(scale)) {
		return false;
	}
	const auto &frame = *_root.device();
	const auto rect = ToRect(command.rect);
	const auto reach = FilterReach(*chain, scale);
	const auto area = (rect.isEmpty() || !(scale > 0.))
		? QRect()
		: LayerArea(
			p,
			offset,
			frame,
			DeviceInk(
				device,
				rect,
				QRectF(0., 0., frame.width(), frame.height()),
				reach),
			DeviceSpread(Spread(reach), frame));
	if (area.isEmpty()) {
		pushHidden();
		_stack.back().filter = true;
		return true;
	}
	const auto cost = MeasureFilters(*chain, area.size(), scale);
	if (!cost
		|| !admitFilter(*cost, LayerBytes(area.size()))
		|| !pushLayer(p.worldTransform(), 1., area)) {
		return false;
	}
	auto &layer = *_layers.back();
	layer.filters = chain;
	layer.filterScale = scale;
	_stack.back().filter = true;
	return true;
}

bool Executor::pushLayer(
		const QTransform &world,
		float64 opacity,
		const QRect &area) {
	if (area.isEmpty()) {
		pushHidden();
		return true;
	}
	const auto offset = origin();
	const auto bytes = LayerBytes(area.size());
	if (bytes > kMaxLayerBytes - _layerBytes) {
		return exceed(PaintLimit(
			u"%1 active layer bytes at once"_q.arg(_layerBytes + bytes),
			kMaxLayerBytes));
	} else if (bytes > kMaxLayerWork - _layerWork) {
		return exceed(PaintLimit(
			u"%1 cumulative layer bytes in one frame"_q.arg(
				_layerWork + bytes),
			kMaxLayerWork));
	}
	auto layer = std::make_unique<Layer>();
	layer->image = QImage(area.size(), QImage::Format_ARGB32_Premultiplied);
	if (layer->image.isNull()) {
		return false;
	}
	layer->image.fill(Qt::transparent);
	if (!layer->painter.begin(&layer->image)) {
		return false;
	}
	PrepareHints(layer->painter);
	layer->painter.setWorldTransform(world
		* QTransform::fromTranslate(
			offset.x() - area.x(),
			offset.y() - area.y()));
	layer->origin = area.topLeft();
	layer->opacity = opacity;
	_layerBytes += bytes;
	_layerWork += bytes;
	_layers.push_back(std::move(layer));
	_stack.push_back({ .kind = EntryKind::Layer });
	return true;
}

bool Executor::admitFilter(const FilterCost &cost, int64 bytes) {
	const auto total = bytes + cost.scratch;
	if (cost.work > _filterWork) {
		return exceed(PaintLimit(
			u"filter work of %1 units in one frame"_q.arg(
				_maxFilterWork - _filterWork + cost.work),
			_maxFilterWork));
	} else if (cost.scratch > kMaxFilterScratch) {
		return exceed(PaintLimit(
			u"filter scratch of %1 bytes in one filter pass"_q.arg(
				cost.scratch),
			kMaxFilterScratch));
	} else if (total > kMaxLayerBytes - _layerBytes) {
		return exceed(PaintLimit(
			u"%1 active layer bytes at once"_q.arg(_layerBytes + total),
			kMaxLayerBytes));
	} else if (total > kMaxLayerWork - _layerWork) {
		return exceed(PaintLimit(
			u"%1 cumulative layer bytes in one frame"_q.arg(
				_layerWork + total),
			kMaxLayerWork));
	}
	_filterWork -= cost.work;
	_layerWork += cost.scratch;
	return true;
}

bool Executor::chargeShadow(
		const QRectF &area,
		const QRectF &contour,
		float64 contourCells,
		int64 pixelWork,
		int64 maskPixels) {
	const auto &p = painter();
	const auto world = p.combinedTransform();
	const auto ink = frameInk(area);
	const auto covered = ink.isEmpty() ? QRect() : ink.toAlignedRect();
	const auto pixels = int64(covered.width()) * covered.height();
	const auto reached = WithinContourReach(
			DeviceSize(p),
			world.mapRect(contour))
		? (ink | frameInk(contour)).toAlignedRect()
		: covered;
	const auto drawn = int64(reached.height()) * kShadowRowWork
		+ pixels * pixelWork;
	const auto cells = std::max(contourCells - kRasterCells, 0.);
	// 裁剪路径的跨度会增加图层中每行和每个像素的处理量。
	const auto work = kShadowLayerWork
		+ drawn * (_pathClips ? kClippedShadowWork : 1)
		+ int64(cells) * kContourCellWork
		+ maskPixels * kMaskPixelWork;
	if (work > _shadowWork) {
		return exceed(PaintLimit(
			u"shadow work of %1 units in one frame"_q.arg(
				kMaxShadowWork - _shadowWork + work),
			kMaxShadowWork));
	}
	_shadowWork -= work;
	return true;
}

void Executor::pushHidden() {
	++_hidden;
	_stack.push_back({ .kind = EntryKind::Hidden });
}

void Executor::popGroup() {
	const auto group = [](const Entry &entry) {
		return (entry.kind != EntryKind::Clip) && !entry.filter;
	};
	if (!ranges::any_of(_stack, group)) {
		return;
	}
	while (!group(_stack.back())) {
		popEntry(true);
	}
	popEntry(true);
}

void Executor::popFilter() {
	for (auto i = _stack.size(); i != 0; --i) {
		const auto entry = _stack[i - 1];
		if (entry.filter) {
			while (_stack.size() >= i) {
				popEntry(true);
			}
			return;
		} else if (entry.kind != EntryKind::Clip) {
			return;
		}
	}
}

void Executor::popEntry(bool composite) {
	const auto entry = _stack.back();
	_stack.pop_back();
	if (entry.saved) {
		painter().restore();
	}
	if (entry.kind == EntryKind::Layer) {
		popLayer(composite);
	} else if (entry.kind == EntryKind::Hidden) {
		--_hidden;
	}
}

void Executor::popLayer(bool composite) {
	const auto layer = std::move(_layers.back());
	_layers.pop_back();
	layer->painter.end();
	_layerBytes -= LayerBytes(layer->image.size());
	if (!composite || _refused) {
		return;
	} else if (layer->filters
		&& !ApplyFilters(layer->image, *layer->filters, layer->filterScale)) {
		_refused = true;
		return;
	}
	auto &parent = painter();
	parent.save();
	parent.setWorldTransform(QTransform());
	parent.setOpacity(layer->opacity);
	parent.drawImage(layer->origin - origin(), layer->image);
	parent.restore();
}

void Executor::unwind(bool composite) {
	while (!_stack.empty()) {
		popEntry(composite);
	}
}

bool Executor::exceed(QString reason) {
	_exceeded = std::move(reason);
	return false;
}

void Executor::clipToRect(const QRectF &rect) {
	painter().setClipRect(bounded(rect), Qt::IntersectClip);
}

bool Executor::clipToPath(const twlite::Path &source) {
	auto &p = painter();
	const auto path = VectorPath(source.ops, source.points, source.evenodd);
	if (path.isEmpty()) {
		p.setClipRect(QRectF(), Qt::IntersectClip);
		return true;
	} else if (!_allowance.chargeFill(p, path, QBrush(Qt::SolidPattern))) {
		return false;
	}
	p.setClipPath(path, Qt::IntersectClip);
	return true;
}

QPainter &Executor::painter() const {
	return _layers.empty() ? _root : _layers.back()->painter;
}

QPoint Executor::origin() const {
	return _layers.empty() ? QPoint() : _layers.back()->origin;
}

QRectF Executor::bounded(const QRectF &rect) const {
	const auto area = limit();
	return area.isEmpty() ? QRectF() : rect.intersected(area);
}

QRectF Executor::frameInk(const QRectF &rect) const {
	const auto &p = painter();
	return DeviceInk(
		p.combinedTransform(),
		rect,
		QRectF(QPointF(), QSizeF(DeviceSize(p))));
}

// 使用多边形，避免局部包围矩形在近奇异变换下无限扩张。
QPolygonF Executor::around(const QRectF &ink) const {
	auto invertible = false;
	const auto inverse = painter().combinedTransform().inverted(&invertible);
	return invertible
		? inverse.map(QPolygonF(ink.adjusted(
			-kInkMargin,
			-kInkMargin,
			kInkMargin,
			kInkMargin)))
		: QPolygonF();
}

QRectF Executor::limit() const {
	const auto &p = painter();
	auto invertible = false;
	const auto inverse = p.combinedTransform().inverted(&invertible);
	return invertible
		? inverse.mapRect(DeviceLimit(*p.device()))
		: QRectF();
}

bool Executor::reachable(QPointF point) const {
	const auto &p = painter();
	return DeviceLimit(*p.device()).contains(
		p.combinedTransform().map(point));
}

} // namespace

void PrepareHints(QPainter &painter) {
	painter.setRenderHints(QPainter::Antialiasing
		| QPainter::SmoothPixmapTransform
		| QPainter::TextAntialiasing);
}

int StackRadius(float64 sigma) {
	return int(base::SafeRound(std::sqrt(1. + 6. * sigma * sigma) - 1.));
}

QImage ShadowFromCoverage(QImage mask, int radius, QRgb color) {
	return ColorizeMask(Images::BlurLargeImage(std::move(mask), radius), color);
}

PaintedFrame PaintFrame(
		const twlite::DisplayList &list,
		const QtBackend &backend,
		QSize deviceSize,
		float64 cssToDevice) {
	if (!ValidFrame(deviceSize, cssToDevice)) {
		return {};
	}
	const auto pixels = int64(deviceSize.width()) * deviceSize.height();
	if (pixels > kMaxFrameBytes / kPixelBytes) {
		return {
			.limit = PaintLimit(
				u"a frame of %1 pixels"_q.arg(pixels),
				kMaxFrameBytes / kPixelBytes),
		};
	}
	auto result = QImage(deviceSize, QImage::Format_ARGB32_Premultiplied);
	if (result.isNull()) {
		return {};
	}
	result.fill(Qt::transparent);
	auto painter = QPainter();
	if (!painter.begin(&result)) {
		return {};
	}
	auto executor = Executor(painter, backend, cssToDevice);
	const auto painted = executor.run(list);
	painter.end();
	if (!painted) {
		return { .limit = executor.exceeded() };
	}
	return { .image = std::move(result) };
}

std::optional<QString> RepaintFrame(
		const twlite::DisplayList &list,
		const QtBackend &backend,
		QImage &frame,
		QRect area,
		float64 cssToDevice) {
	area &= QRect(QPoint(), frame.size());
	if (!ValidFrame(frame.size(), cssToDevice)
		|| (frame.format() != QImage::Format_ARGB32_Premultiplied)) {
		return std::nullopt;
	} else if (area.isEmpty()) {
		return QString();
	}
	auto painter = QPainter();
	if (!painter.begin(&frame)) {
		return std::nullopt;
	}
	painter.setClipRect(area);
	painter.setCompositionMode(QPainter::CompositionMode_Source);
	painter.fillRect(area, Qt::transparent);
	painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
	auto executor = Executor(painter, backend, cssToDevice);
	const auto painted = executor.run(list);
	painter.end();
	if (painted) {
		return QString();
	} else if (executor.exceeded().isEmpty()) {
		return std::nullopt;
	}
	return executor.exceeded();
}

} // namespace Tgw
