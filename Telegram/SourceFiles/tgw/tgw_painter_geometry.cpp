/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_painter_geometry.h"

namespace Tgw {
namespace PainterGeometry {

constexpr auto kKappa = 0.5522847498;
constexpr auto kStopEpsilon = 1e-4;
constexpr auto kMiterLimit = 4.;
constexpr auto kMaxDashSteps = int64(1 << 17);
constexpr auto kDotStep = 1e-3;
constexpr auto kMaxFlattenBytes = int64(64 * 1024 * 1024);
constexpr auto kMaxCurvePoints = int64(512);
constexpr auto kPolygonReserve = int64(16);
constexpr auto kBlockHeader = int64(64);
constexpr auto kPageBytes = int64(4096);
constexpr auto kMaxPathElements = int64(1 << 18);
constexpr auto kMaxPathEdges = int64(1 << 20);
constexpr auto kMaxPathWork = int64(1 << 27);
constexpr auto kElementWork = int64(256);
constexpr auto kStopWork = int64(256);
constexpr auto kStopBytes = int64(4 * sizeof(QGradientStop));
constexpr auto kShapeWork = 4096.;
constexpr auto kDashPointWork = 256.;
constexpr auto kCurveThreshold = 0.25;
constexpr auto kArcCurves = 5.;
constexpr auto kCoordinateLimit = 32767.;
constexpr auto kEdgeSpanLimit = 4. * kCoordinateLimit + 2.;
constexpr auto kCurveSlack = 1e-4;
constexpr auto kClipEdges = 5.;
constexpr auto kOffsetSweeps = 2.;
constexpr auto kMiterTurn = 3.;
constexpr auto kClipWork = 16.;
constexpr auto kRasterBand = 37.;
constexpr auto kRasterBands = 39.;
constexpr auto kRasterAttempts = 8.;
constexpr auto kMapperMargin = 1024.;
// 按 26.6 坐标舍入估算扫描单元，并采用 Windows 上较小的前两级池容量。
constexpr auto kFirstPoolCells = 148.;
constexpr auto kSecondPoolCells = 489.;
constexpr auto kRowSubpixels = 256.;
constexpr auto kOutlineSubpixels = 64.;
constexpr auto kGradientPixel = 8.;

struct Bounds {
	float64 left = std::numeric_limits<float64>::infinity();
	float64 top = std::numeric_limits<float64>::infinity();
	float64 right = -std::numeric_limits<float64>::infinity();
	float64 bottom = -std::numeric_limits<float64>::infinity();
};

class Dasher final {
public:
	Dasher(
		std::vector<float64> pattern,
		float64 offset,
		float64 scale,
		const QRectF &visible,
		int64 &budget,
		bool dots);

	[[nodiscard]] bool add(const QPolygonF &polygon);
	[[nodiscard]] const QPainterPath &result() const;

private:
	[[nodiscard]] bool skip(float64 distance);
	[[nodiscard]] bool walk(QPointF from, QPointF to, float64 length);
	[[nodiscard]] bool next();
	void line(QPointF from, QPointF to);

	const std::vector<float64> _pattern;
	const float64 _period = 0.;
	const float64 _offset = 0.;
	const float64 _scale = 1.;
	const QRectF _visible;
	int64 &_budget;
	const bool _dots = false;
	QPainterPath _result;
	size_t _index = 0;
	float64 _left = 0.;
	bool _drawing = false;

};

class Outline final {
public:
	Outline(
		const QTransform &device,
		QSize size,
		float64 threshold,
		float64 limit);
	Outline(
		const QTransform &device,
		QSize size,
		float64 threshold,
		float64 limit,
		const QPen &pen);

	[[nodiscard]] bool add(const QPainterPath &path);
	[[nodiscard]] bool valid() const;
	[[nodiscard]] float64 edges() const;
	[[nodiscard]] float64 spans() const;
	[[nodiscard]] float64 visibleSpans() const;
	[[nodiscard]] float64 rowSpans() const;
	[[nodiscard]] float64 clampedSpans() const;
	[[nodiscard]] float64 density() const;
	[[nodiscard]] float64 rowCells() const;
	[[nodiscard]] float64 alignedSpans() const;
	[[nodiscard]] float64 alignedRowSpans() const;
	[[nodiscard]] float64 verticalRows() const;
	[[nodiscard]] float64 verticalEdges() const;
	[[nodiscard]] float64 coverage() const;
	[[nodiscard]] QRectF extent() const;
	[[nodiscard]] QRectF anchors() const;

private:
	[[nodiscard]] QPointF map(QPointF point);
	[[nodiscard]] float64 clamped(QPointF from, QPointF to) const;
	void count(
		float64 edges,
		float64 spans,
		float64 clamped,
		float64 density,
		float64 top,
		float64 bottom,
		float64 rise,
		QPointF along = QPointF());
	void move(QPointF to);
	void line(QPointF to);
	void curve(QPointF b, QPointF c, QPointF d);
	void join(QPointF direction);
	void cap(QPointF at, QPointF direction);
	void finish();
	[[nodiscard]] float64 quarter();

	const QTransform _device;
	const float64 _width = 0.;
	const float64 _height = 0.;
	const float64 _threshold = 0.;
	const float64 _limit = 0.;
	const bool _stroke = false;
	const float64 _radius = 0.;
	const float64 _deviceRadius = 0.;
	const float64 _reach = 0.;
	const bool _roundJoin = false;
	const Qt::PenCapStyle _cap = Qt::FlatCap;
	bool _aligned = false;
	float64 _quarter = -1.;
	float64 _edges = 0.;
	float64 _spans = 0.;
	float64 _visibleSpans = 0.;
	float64 _rowSpans = 0.;
	float64 _clampedSpans = 0.;
	float64 _density = 0.;
	float64 _rowCells = 0.;
	float64 _alignedSpans = 0.;
	float64 _alignedRowSpans = 0.;
	float64 _verticalRows = 0.;
	float64 _verticalEdges = 0.;
	Bounds _controls;
	Bounds _anchors;
	QPointF _start;
	QPointF _startMapped;
	QPointF _point;
	QPointF _mapped;
	std::optional<QPointF> _first;
	std::optional<QPointF> _last;
	bool _valid = true;
	bool _curved = false;

};

[[nodiscard]] QString PaintLimit(const QString &what, int64 max) {
	return u"Paint limit exceeded: %1 (max %2)"_q.arg(
		what,
		QString::number(max));
}

[[nodiscard]] QSize DeviceSize(const QPainter &painter) {
	const auto device = painter.device();
	return QSize(device->width(), device->height());
}

[[nodiscard]] QRectF DeviceClipBounds(QPainter &painter) {
	const auto world = painter.worldTransform();
	painter.setWorldTransform(QTransform());
	const auto result = painter.clipBoundingRect();
	painter.setWorldTransform(world);
	return result;
}

[[nodiscard]] QRectF VisibleRect(QPainter &painter) {
	const auto device = painter.device();
	const auto result = QRectF(0., 0., device->width(), device->height());
	return painter.hasClipping()
		? result.intersected(DeviceClipBounds(painter))
		: result;
}

[[nodiscard]] float64 MaxStretch(const QTransform &transform) {
	const auto a = transform.m11();
	const auto b = transform.m12();
	const auto c = transform.m21();
	const auto d = transform.m22();
	return (std::hypot(a + d, c - b) + std::hypot(a - d, b + c)) / 2.;
}

[[nodiscard]] bool KeepsAxes(const QTransform &transform) {
	return (transform.m12() == 0.)
		&& (transform.m21() == 0.)
		&& (transform.m13() == 0.)
		&& (transform.m23() == 0.)
		&& (transform.m33() == 1.)
		&& (transform.type() < QTransform::TxProject);
}

[[nodiscard]] bool ValidWidth(float width) {
	return std::isfinite(width) && (width > 0.f);
}

[[nodiscard]] QColor ToColor(twlite::Color color) {
	return QColor(color.r, color.g, color.b, color.a);
}

[[nodiscard]] bool FiniteRect(const twlite::Rect &rect) {
	return std::isfinite(rect.x)
		&& std::isfinite(rect.y)
		&& std::isfinite(rect.w)
		&& std::isfinite(rect.h);
}

[[nodiscard]] QRectF ToRect(const twlite::Rect &rect) {
	return (FiniteRect(rect) && !rect.empty())
		? QRectF(rect.x, rect.y, rect.w, rect.h)
		: QRectF();
}

[[nodiscard]] QPen StrokePen(twlite::Color color, float width) {
	return QPen(
		ToColor(color),
		width,
		Qt::SolidLine,
		Qt::FlatCap,
		Qt::MiterJoin);
}

[[nodiscard]] float64 OverlapFactor(float64 length, float64 sum) {
	return (sum > length) ? (length / sum) : 1.;
}

[[nodiscard]] std::array<CornerRadius*, 4> CornerList(Corners &corners) {
	return {
		&corners.topLeft,
		&corners.topRight,
		&corners.bottomRight,
		&corners.bottomLeft,
	};
}

[[nodiscard]] Corners FitCorners(const QRectF &rect, Corners corners) {
	const auto factor = std::min({
		OverlapFactor(
			rect.width(),
			corners.topLeft.x + corners.topRight.x),
		OverlapFactor(
			rect.width(),
			corners.bottomLeft.x + corners.bottomRight.x),
		OverlapFactor(
			rect.height(),
			corners.topLeft.y + corners.bottomLeft.y),
		OverlapFactor(
			rect.height(),
			corners.topRight.y + corners.bottomRight.y),
	});
	for (const auto corner : CornerList(corners)) {
		corner->x *= factor;
		corner->y *= factor;
		if (corner->x <= 0. || corner->y <= 0.) {
			*corner = CornerRadius();
		}
	}
	return corners;
}

[[nodiscard]] std::optional<Corners> NormalizedCorners(
		const QRectF &rect,
		const twlite::Radii &radii) {
	const auto values = std::array{
		radii.tlx,
		radii.tly,
		radii.trx,
		radii.try_,
		radii.brx,
		radii.bry,
		radii.blx,
		radii.bly,
	};
	const auto finite = [](float value) {
		return std::isfinite(value);
	};
	if (!ranges::all_of(values, finite)) {
		return std::nullopt;
	}
	const auto radius = [](float x, float y) {
		return CornerRadius{
			.x = std::max(float64(x), 0.),
			.y = std::max(float64(y), 0.),
		};
	};
	return FitCorners(rect, Corners{
		.topLeft = radius(radii.tlx, radii.tly),
		.topRight = radius(radii.trx, radii.try_),
		.bottomRight = radius(radii.brx, radii.bry),
		.bottomLeft = radius(radii.blx, radii.bly),
	});
}

[[nodiscard]] Corners SpreadCorners(
		const QRectF &rect,
		Corners corners,
		float64 spread) {
	for (const auto corner : CornerList(corners)) {
		if (corner->x > 0.) {
			corner->x = std::max(corner->x + spread, 0.);
			corner->y = std::max(corner->y + spread, 0.);
		}
	}
	return FitCorners(rect, corners);
}

[[nodiscard]] bool Rounded(const Corners &corners) {
	return (corners.topLeft.x > 0.)
		|| (corners.topRight.x > 0.)
		|| (corners.bottomRight.x > 0.)
		|| (corners.bottomLeft.x > 0.);
}

[[nodiscard]] QPainterPath RoundedPath(
		const QRectF &rect,
		const Corners &corners) {
	const auto left = rect.left();
	const auto top = rect.top();
	const auto right = rect.right();
	const auto bottom = rect.bottom();
	const auto &tl = corners.topLeft;
	const auto &tr = corners.topRight;
	const auto &br = corners.bottomRight;
	const auto &bl = corners.bottomLeft;
	auto result = QPainterPath();
	result.moveTo(left + tl.x, top);
	result.lineTo(right - tr.x, top);
	if (tr.x > 0.) {
		result.cubicTo(
			QPointF(right - tr.x + tr.x * kKappa, top),
			QPointF(right, top + tr.y - tr.y * kKappa),
			QPointF(right, top + tr.y));
	}
	result.lineTo(right, bottom - br.y);
	if (br.x > 0.) {
		result.cubicTo(
			QPointF(right, bottom - br.y + br.y * kKappa),
			QPointF(right - br.x + br.x * kKappa, bottom),
			QPointF(right - br.x, bottom));
	}
	result.lineTo(left + bl.x, bottom);
	if (bl.x > 0.) {
		result.cubicTo(
			QPointF(left + bl.x - bl.x * kKappa, bottom),
			QPointF(left, bottom - bl.y + bl.y * kKappa),
			QPointF(left, bottom - bl.y));
	}
	result.lineTo(left, top + tl.y);
	if (tl.x > 0.) {
		result.cubicTo(
			QPointF(left, top + tl.y - tl.y * kKappa),
			QPointF(left + tl.x - tl.x * kKappa, top),
			QPointF(left + tl.x, top));
	}
	result.closeSubpath();
	return result;
}

[[nodiscard]] QGradientStops OrderedStops(
		const std::vector<twlite::Stop> &stops,
		bool reversed) {
	auto result = QGradientStops();
	result.reserve(int(stops.size()));
	auto previous = 0.;
	for (const auto &stop : stops) {
		if (std::isfinite(stop.offset)) {
			previous = std::clamp(float64(stop.offset), previous, 1.);
			result.push_back(QGradientStop(previous, ToColor(stop.color)));
		}
	}
	if (reversed) {
		std::reverse(result.begin(), result.end());
		for (auto &stop : result) {
			stop.first = 1. - stop.first;
		}
	}
	return result;
}

[[nodiscard]] QGradientStops RunEnds(const QGradientStops &stops) {
	auto result = QGradientStops();
	const auto count = stops.size();
	for (auto i = 0; i != count; ++i) {
		const auto offset = stops[i].first;
		if (i == 0
			|| i + 1 == count
			|| stops[i - 1].first != offset
			|| stops[i + 1].first != offset) {
			result.push_back(stops[i]);
		}
	}
	return result;
}

[[nodiscard]] QGradientStops SeparatedPairs(QGradientStops stops) {
	const auto count = stops.size();
	for (auto i = 0; i + 1 < count; ++i) {
		const auto offset = stops[i].first;
		if (stops[i + 1].first != offset) {
			continue;
		}
		const auto below = (i > 0) ? stops[i - 1].first : 0.;
		const auto above = (i + 2 < count) ? stops[i + 2].first : 1.;
		stops[i].first = offset
			- std::min(kStopEpsilon / 2., (offset - below) / 3.);
		stops[i + 1].first = offset
			+ std::min(kStopEpsilon / 2., (above - offset) / 3.);
		++i;
	}
	return stops;
}

[[nodiscard]] QGradientStops StrictStops(
		const std::vector<twlite::Stop> &stops,
		bool reversed) {
	return SeparatedPairs(RunEnds(OrderedStops(stops, reversed)));
}

[[nodiscard]] bool Uniform(const QGradientStops &stops) {
	const auto &color = stops.front().second;
	return ranges::all_of(stops, [&](const QGradientStop &stop) {
		return (stop.second == color);
	});
}

[[nodiscard]] QBrush StopsBrush(
		QGradient &gradient,
		const QGradientStops &stops) {
	if (stops.isEmpty()) {
		return QBrush();
	} else if (Uniform(stops)) {
		return QBrush(stops.back().second);
	}
	gradient.setStops(stops);
	return QBrush(gradient);
}

[[nodiscard]] QBrush SolidBrush(twlite::Color color) {
	return color.a ? QBrush(ToColor(color)) : QBrush();
}

[[nodiscard]] QBrush RadialBrush(
		QPointF centre,
		float64 radiusX,
		float64 radiusY,
		QPointF focal,
		const std::vector<twlite::Stop> &stops) {
	if (!Finite(centre)
		|| !Finite(focal)
		|| !std::isfinite(radiusX)
		|| !std::isfinite(radiusY)) {
		return QBrush();
	}
	const auto strict = StrictStops(stops, false);
	if (!(radiusX > 0.) || !(radiusY > 0.)) {
		return strict.isEmpty() ? QBrush() : QBrush(strict.back().second);
	}
	const auto offset = focal - centre;
	auto gradient = QRadialGradient(
		QPointF(),
		1.,
		QPointF(offset.x() / radiusX, offset.y() / radiusY));
	auto result = StopsBrush(gradient, strict);
	if (result.gradient()) {
		result.setTransform(QTransform(
			radiusX,
			0.,
			0.,
			radiusY,
			centre.x(),
			centre.y()));
	}
	return result;
}

[[nodiscard]] QBrush BackgroundBrush(const twlite::Command &command) {
	const auto origin = QPointF(command.ax, command.ay);
	switch (command.kind) {
	case twlite::Cmd::Linear:
		return LinearBrush(
			origin,
			QPointF(command.bx, command.by),
			command.stops);
	case twlite::Cmd::Radial:
		return RadialBrush(
			origin,
			command.bx,
			command.by,
			origin,
			command.stops);
	case twlite::Cmd::Conic:
		return ConicBrush(origin, command.bx, command.stops);
	default:
		return QBrush();
	}
}

[[nodiscard]] int PointValues(twlite::Path::Op op) {
	switch (op) {
	case twlite::Path::Move:
	case twlite::Path::Line: return 2;
	case twlite::Path::Quad: return 4;
	case twlite::Path::Cubic: return 6;
	case twlite::Path::Close: return 0;
	}
	return -1;
}

[[nodiscard]] QBrush PaintBrush(
		int kind,
		QPointF from,
		QPointF to,
		float64 radius,
		QPointF focal,
		const std::vector<twlite::Stop> &stops,
		twlite::Color color) {
	const auto gradient = stops.empty() ? 0 : kind;
	switch (gradient) {
	case 1: return LinearBrush(from, to, stops);
	case 2: return RadialBrush(from, radius, radius, focal, stops);
	}
	return SolidBrush(color);
}

[[nodiscard]] QBrush PathBrush(const twlite::Path &path) {
	return PaintBrush(
		path.gradient,
		QPointF(path.x1, path.y1),
		QPointF(path.x2, path.y2),
		path.r,
		QPointF(path.fx, path.fy),
		path.stops,
		path.fill_color);
}

[[nodiscard]] QBrush StrokeBrush(const twlite::Path &path) {
	const auto &g = path.sg;
	return PaintBrush(
		path.stroke_gradient,
		QPointF(g[0], g[1]),
		QPointF(g[2], g[3]),
		g[4],
		QPointF(g[5], g[6]),
		path.stroke_stops,
		path.stroke_color);
}

[[nodiscard]] int64 StrokeStops(const twlite::Path &path) {
	const auto gradient = (path.stroke_gradient == 1)
		|| (path.stroke_gradient == 2);
	return (path.stroke && gradient) ? int64(path.stroke_stops.size()) : 0;
}

[[nodiscard]] float64 MiterLimit(const twlite::Path &path) {
	return std::isfinite(path.miter_limit)
		? std::max(float64(path.miter_limit), 1.)
		: kMiterLimit;
}

[[nodiscard]] QPen PathPen(const twlite::Path &path) {
	auto result = QPen(
		StrokeBrush(path),
		float64(path.stroke_width),
		Qt::SolidLine,
		PathCap(path.line_cap),
		PathJoin(path.line_join));
	result.setMiterLimit(MiterLimit(path));
	return result;
}

[[nodiscard]] float64 Wrap(float64 value, float64 period) {
	const auto result = std::fmod(value, period);
	return (result < 0.) ? std::fmod(result + period, period) : result;
}

[[nodiscard]] std::optional<std::pair<float64, float64>> ClipSegment(
		QPointF from,
		QPointF to,
		const QRectF &rect) {
	const auto delta = to - from;
	const auto edges = std::array{
		std::pair{ -delta.x(), from.x() - rect.left() },
		std::pair{ delta.x(), rect.right() - from.x() },
		std::pair{ -delta.y(), from.y() - rect.top() },
		std::pair{ delta.y(), rect.bottom() - from.y() },
	};
	auto enter = 0.;
	auto leave = 1.;
	for (const auto &[direction, distance] : edges) {
		if (direction < 0.) {
			enter = std::max(enter, distance / direction);
		} else if (direction > 0.) {
			leave = std::min(leave, distance / direction);
		} else if (distance < 0.) {
			return std::nullopt;
		}
	}
	return (enter < leave)
		? std::make_optional(std::make_pair(enter, leave))
		: std::nullopt;
}

[[nodiscard]] bool ReserveFlattening(
		const QPainterPath &path,
		int64 &allowance) {
	auto left = allowance;
	const auto charge = [&](int64 count, int64 size) {
		const auto most = (left - kPageBytes) / 2 - kBlockHeader;
		if (most < 0 || count > most / size) {
			return false;
		}
		left -= 2 * (count * size + kBlockHeader) + kPageBytes;
		return true;
	};
	const auto pointBytes = int64(sizeof(QPointF));
	const auto polygonBytes = int64(sizeof(QPolygonF));
	const auto dashBytes = int64(2 * sizeof(QPainterPath::Element));
	const auto limit = left / pointBytes;
	auto capacity = kPolygonReserve;
	auto points = int64();
	auto vertices = int64();
	auto polygons = int64();
	const auto store = [&] {
		if (points < 2) {
			return true;
		}
		capacity = std::max(capacity, points);
		vertices += points;
		++polygons;
		return charge(capacity, pointBytes);
	};
	for (auto i = 0, count = path.elementCount(); i != count; ++i) {
		switch (path.elementAt(i).type) {
		case QPainterPath::MoveToElement:
			if (!store()) {
				return false;
			}
			points = 1;
			break;
		case QPainterPath::LineToElement: ++points; break;
		case QPainterPath::CurveToElement: points += kMaxCurvePoints; break;
		case QPainterPath::CurveToDataElement: break;
		}
		if (points > limit) {
			return false;
		}
	}
	if (!store()
		|| !charge(capacity, pointBytes)
		|| !charge(polygons, polygonBytes)
		|| !charge(polygons, polygonBytes)
		|| !charge(vertices, dashBytes)
		|| !charge(vertices, dashBytes)) {
		return false;
	}
	allowance = left;
	return true;
}

[[nodiscard]] int64 CurvePieces(
		const std::array<QPointF, 4> &curve,
		float64 threshold,
		float64 limit) {
	struct Piece {
		std::array<QPointF, 4> points;
		int level = 0;
	};
	const auto middle = [](QPointF a, QPointF b) {
		return (a + b) * .5;
	};
	auto stack = std::array<Piece, kCurveLevels + 1>();
	stack[0] = Piece{ .points = curve, .level = kCurveLevels };
	auto top = 0;
	auto result = int64();
	while (top >= 0 && result <= limit) {
		const auto [a, b, c, d] = stack[top].points;
		const auto level = stack[top].level;
		const auto dx = d.x() - a.x();
		const auto dy = d.y() - a.y();
		auto length = std::abs(dx) + std::abs(dy);
		auto distance = 0.;
		if (length > 1.) {
			distance = std::abs(dx * (a.y() - b.y()) - dy * (a.x() - b.x()))
				+ std::abs(dx * (a.y() - c.y()) - dy * (a.x() - c.x()));
		} else {
			distance = std::abs(a.x() - b.x())
				+ std::abs(a.y() - b.y())
				+ std::abs(a.x() - c.x())
				+ std::abs(a.y() - c.y());
			length = 1.;
		}
		if (distance < threshold * length || !level) {
			++result;
			--top;
			continue;
		}
		const auto ab = middle(a, b);
		const auto bc = middle(b, c);
		const auto cd = middle(c, d);
		const auto abc = middle(ab, bc);
		const auto bcd = middle(bc, cd);
		const auto abcd = middle(abc, bcd);
		stack[top] = Piece{
			.points = { abcd, bcd, cd, d },
			.level = level - 1,
		};
		stack[++top] = Piece{
			.points = { a, ab, abc, abcd },
			.level = level - 1,
		};
	}
	return result;
}

[[nodiscard]] float64 Span(QPointF from, QPointF to) {
	return std::abs(to.x() - from.x()) + std::abs(to.y() - from.y());
}

void Include(Bounds &bounds, QPointF point) {
	bounds.left = std::min(bounds.left, point.x());
	bounds.top = std::min(bounds.top, point.y());
	bounds.right = std::max(bounds.right, point.x());
	bounds.bottom = std::max(bounds.bottom, point.y());
}

[[nodiscard]] QRectF BoundsRect(const Bounds &bounds, float64 margin) {
	if (!(bounds.left <= bounds.right) || !(bounds.top <= bounds.bottom)) {
		return QRectF();
	}
	return QRectF(
		QPointF(bounds.left, bounds.top),
		QPointF(bounds.right, bounds.bottom)).adjusted(
			-margin,
			-margin,
			margin,
			margin);
}

[[nodiscard]] std::pair<float64, float64> CurveRange(
		float64 a,
		float64 b,
		float64 c,
		float64 d) {
	auto low = std::min(a, d);
	auto high = std::max(a, d);
	const auto include = [&](float64 t) {
		if (t > 0. && t < 1.) {
			const auto s = 1. - t;
			const auto value = s * s * s * a
				+ 3. * s * s * t * b
				+ 3. * s * t * t * c
				+ t * t * t * d;
			low = std::min(low, value);
			high = std::max(high, value);
		}
	};
	const auto qa = 3. * (b - c) + d - a;
	const auto qb = 2. * (a - 2. * b + c);
	const auto qc = b - a;
	if (qa == 0.) {
		if (qb != 0.) {
			include(-qc / qb);
		}
		return { low, high };
	}
	const auto discriminant = qb * qb - 4. * qa * qc;
	if (discriminant >= 0.) {
		const auto root = std::sqrt(discriminant);
		include((-qb + root) / (2. * qa));
		include((-qb - root) / (2. * qa));
	}
	return { low, high };
}

[[nodiscard]] bool BeyondRaster(const QRectF &rect) {
	return std::max({
		-rect.left(),
		-rect.top(),
		rect.right(),
		rect.bottom(),
		rect.width(),
		rect.height(),
	}) > kCoordinateLimit;
}

[[nodiscard]] float64 Turn(QPointF from, QPointF to) {
	return std::atan2(
		std::abs(from.x() * to.y() - from.y() * to.x()),
		from.x() * to.x() + from.y() * to.y());
}

[[nodiscard]] float64 PolygonTurn(const std::array<QPointF, 4> &curve) {
	auto result = 0.;
	auto previous = std::optional<QPointF>();
	for (auto i = 1; i != 4; ++i) {
		const auto leg = curve[i] - curve[i - 1];
		if (leg.isNull()) {
			continue;
		} else if (previous) {
			result += Turn(*previous, leg);
		}
		previous = leg;
	}
	return result;
}

[[nodiscard]] std::optional<QPointF> Tangent(
		QPointF from,
		std::initializer_list<QPointF> to) {
	for (const auto point : to) {
		if (!(point - from).isNull()) {
			return point - from;
		}
	}
	return std::nullopt;
}

[[nodiscard]] float64 RasterWork(
		const Outline &outline,
		QSize size,
		float64 pixelWeight) {
	const auto frame = QRectF(QPointF(), QSizeF(size));
	const auto extent = outline.extent();
	auto edges = outline.edges();
	if (!extent.intersects(frame)) {
		return edges;
	}
	const auto visible = extent.intersected(frame);
	const auto rows = std::ceil(visible.bottom()) - std::floor(visible.top());
	const auto columns = std::ceil(visible.right())
		- std::floor(visible.left());
	auto spans = outline.spans();
	auto iterated = outline.visibleSpans();
	auto density = std::min(columns + 2., outline.density());
	auto rowCells = std::min(columns + 2., outline.rowCells());
	auto clip = 0.;
	const auto clipping = BeyondRaster(extent);
	if (clipping) {
		clip = kClipWork * edges;
		spans = iterated = BeyondRaster(outline.anchors())
			? (outline.clampedSpans() + (kClipEdges - 1.) * edges)
			: std::min(spans, kClipEdges * edges * kEdgeSpanLimit);
		edges *= kClipEdges;
		density = columns + 2.;
		rowCells = density;
	}
	const auto attempts = std::max(
		(density > kRasterCells)
			? std::min(
				kRasterAttempts,
				1. + std::ceil(std::log2(density / kRasterCells)))
			: 1.,
		(rowCells > kSecondPoolCells + 2.)
			? 3.
			: (rowCells > kFirstPoolCells + 2.)
			? 2.
			: 1.);
	const auto bands = std::clamp(
		std::floor(rows / kRasterBand),
		1.,
		kRasterBands);
	const auto first = rows - (bands - 1.) * kRasterBand;
	const auto crowded = first * density / kRasterCells;
	const auto failing = (crowded > 1.)
		? std::min(std::ceil(std::log2(crowded)), std::ceil(std::log2(first)))
		: 0.;
	const auto cells = std::min(rows * (columns + 2.), edges + spans);
	const auto visits = edges + spans;
	const auto splits = 2. * failing * std::ceil(cells / kRasterCells);
	const auto passes = attempts * std::min(2. * rows, bands + splits);
	const auto levels = std::ceil(std::log2(rows)) + 1.;
	const auto overlap = [&](float64 visibleSpans, float64 rowSpans) {
		const auto deep = 4. * rowSpans + (2. * levels + 2.) * visibleSpans;
		const auto shallow = std::exp2(failing + 2.)
			/ std::min(kRasterBand, rows)
			* rowSpans
			+ (2. * failing + 2.) * visibleSpans;
		return attempts * std::min(deep, shallow);
	};
	const auto full = std::min(
		passes * iterated,
		clipping
			? std::numeric_limits<float64>::infinity()
			: overlap(iterated, outline.rowSpans()));
	// 未触发边界裁剪时，垂直边只遍历当前带内的行，水平边只占一个单元。
	const auto grown = frame.adjusted(
		-kMapperMargin,
		-kMapperMargin,
		kMapperMargin,
		kMapperMargin);
	// Qt 扩展后的设备边界仍受坐标上限约束。
	const auto most = kCoordinateLimit - 2. * kMapperMargin;
	auto lowered = full;
	if (!clipping
		&& frame.width() <= most
		&& frame.height() <= most
		&& grown.contains(extent)
		&& outline.alignedSpans() > 0.) {
		const auto rest = std::max(iterated - outline.alignedSpans(), 0.);
		const auto crossed = std::max(
			outline.rowSpans() - outline.alignedRowSpans(),
			0.);
		lowered = std::min(passes * rest, overlap(rest, crossed))
			+ std::min(passes, attempts * levels) * outline.verticalRows()
			+ 3. * passes * outline.verticalEdges();
	}
	const auto pixels = std::min(rows * columns, outline.coverage());
	return passes * edges
		+ std::min(full, lowered)
		+ pixels * pixelWeight
		+ clip
		+ visits * rowCells;
}

Dasher::Dasher(
		std::vector<float64> pattern,
		float64 offset,
		float64 scale,
		const QRectF &visible,
		int64 &budget,
		bool dots)
: _pattern(std::move(pattern))
, _period(ranges::accumulate(_pattern, 0.))
, _offset(Wrap(offset, _period))
, _scale(scale)
, _visible(visible)
, _budget(budget)
, _dots(dots) {
}

bool Dasher::add(const QPolygonF &polygon) {
	_index = 0;
	_left = _pattern.front();
	_drawing = false;
	if (!skip(_offset)) {
		return false;
	}
	for (auto i = 1; i < polygon.size(); ++i) {
		const auto from = polygon[i - 1];
		const auto delta = polygon[i] - from;
		const auto length = std::hypot(delta.x(), delta.y()) / _scale;
		if (!std::isfinite(length) || !(length > 0.)) {
			continue;
		}
		const auto chord = ClipSegment(from, polygon[i], _visible);
		if (!chord) {
			if (!skip(length)) {
				return false;
			}
			continue;
		}
		const auto [enter, leave] = *chord;
		const auto start = from + delta * enter;
		const auto end = from + delta * leave;
		const auto visible = end - start;
		if (!skip(length * enter)
			|| !walk(
				start,
				end,
				std::hypot(visible.x(), visible.y()) / _scale)
			|| !skip(length * (1. - leave))) {
			return false;
		}
	}
	return true;
}

const QPainterPath &Dasher::result() const {
	return _result;
}

bool Dasher::skip(float64 distance) {
	if (!(distance > 0.)) {
		return true;
	}
	_drawing = false;
	if (distance < _left) {
		_left -= distance;
		return true;
	}
	distance = Wrap(distance - _left, _period);
	if (!next()) {
		return false;
	}
	for (auto i = size_t(); i != _pattern.size(); ++i) {
		if (distance < _left || (distance == 0. && _left == 0.)) {
			break;
		}
		distance -= _left;
		if (!next()) {
			return false;
		}
	}
	_left = std::max(_left - distance, 0.);
	return true;
}

bool Dasher::walk(QPointF from, QPointF to, float64 length) {
	if (!(length > 0.)) {
		return true;
	}
	const auto delta = to - from;
	auto done = 0.;
	while (true) {
		const auto rest = length - done;
		const auto on = !(_index % 2);
		const auto start = from + delta * (done / length);
		if (_left > rest) {
			if (on) {
				line(start, to);
			}
			_left -= rest;
			return true;
		} else if (on) {
			// 零长度线段会被 Qt 丢弃，圆形或方形端点仍需绘制一个点。
			const auto step = (_dots && !(_left > 0.)) ? kDotStep : _left;
			line(start, from + delta * ((done + step) / length));
			_drawing = false;
		}
		done += _left;
		if (!next()) {
			return false;
		} else if (done >= length) {
			return true;
		}
	}
}

bool Dasher::next() {
	if (--_budget < 0) {
		return false;
	}
	_index = (_index + 1) % _pattern.size();
	_left = _pattern[_index];
	return true;
}

void Dasher::line(QPointF from, QPointF to) {
	if (!_drawing) {
		_result.moveTo(from);
		_drawing = true;
	}
	_result.lineTo(to);
}

Outline::Outline(
		const QTransform &device,
		QSize size,
		float64 threshold,
		float64 limit)
: _device(device)
, _width(size.width())
, _height(size.height())
, _threshold(threshold)
, _limit(limit)
, _aligned(KeepsAxes(device)) {
}

Outline::Outline(
		const QTransform &device,
		QSize size,
		float64 threshold,
		float64 limit,
		const QPen &pen)
: _device(device)
, _width(size.width())
, _height(size.height())
, _threshold(threshold)
, _limit(limit)
, _stroke(true)
, _radius(pen.widthF() / 2.)
, _deviceRadius(_radius * MaxStretch(device))
, _reach(StrokeReach(pen.widthF(), pen.miterLimit()) * MaxStretch(device))
, _roundJoin(pen.joinStyle() == Qt::RoundJoin)
, _cap(pen.capStyle())
, _aligned(KeepsAxes(device)
	&& !pen.isCosmetic()
	&& pen.widthF() > 0.
	&& (_cap == Qt::FlatCap || _cap == Qt::SquareCap)
	&& _deviceRadius >= 1.) {
}

bool Outline::add(const QPainterPath &path) {
	if (_stroke && _aligned) {
		const auto exact = [](float64 value) {
			const auto subpixels = value * kOutlineSubpixels;
			return (std::abs(value) <= kCoordinateLimit)
				&& (subpixels == std::trunc(subpixels));
		};
		const auto elements = path.elementCount();
		// 仅独立且对齐 1/64 网格的轴向线段能精确估算描边边界。
		_aligned = exact(_radius) && (elements % 2 == 0);
		for (auto i = 0; _aligned && i != elements; i += 2) {
			const auto &from = path.elementAt(i);
			const auto &to = path.elementAt(i + 1);
			_aligned = from.isMoveTo()
				&& to.isLineTo()
				&& ((from.x == to.x) != (from.y == to.y))
				&& exact(from.x)
				&& exact(from.y)
				&& exact(to.x)
				&& exact(to.y);
		}
	}
	for (auto i = 0, count = path.elementCount(); i != count; ++i) {
		const auto &element = path.elementAt(i);
		switch (element.type) {
		case QPainterPath::MoveToElement: move(element); break;
		case QPainterPath::LineToElement: line(element); break;
		case QPainterPath::CurveToElement:
			curve(element, path.elementAt(i + 1), path.elementAt(i + 2));
			i += 2;
			break;
		case QPainterPath::CurveToDataElement: break;
		}
		if (!(_edges <= _limit)) {
			return false;
		}
	}
	finish();
	return _valid && (_edges <= _limit);
}

bool Outline::valid() const {
	return _valid;
}

float64 Outline::edges() const {
	return _edges;
}

float64 Outline::spans() const {
	return _spans;
}

float64 Outline::visibleSpans() const {
	return _visibleSpans;
}

float64 Outline::rowSpans() const {
	return _rowSpans;
}

float64 Outline::clampedSpans() const {
	return _clampedSpans;
}

float64 Outline::density() const {
	return _density;
}

float64 Outline::rowCells() const {
	return _rowCells;
}

float64 Outline::alignedSpans() const {
	return _alignedSpans;
}

float64 Outline::alignedRowSpans() const {
	return _alignedRowSpans;
}

float64 Outline::verticalRows() const {
	return _verticalRows;
}

float64 Outline::verticalEdges() const {
	return _verticalEdges;
}

float64 Outline::coverage() const {
	return _stroke
		? (_spans * (_deviceRadius + 1.))
		: std::numeric_limits<float64>::infinity();
}

QRectF Outline::extent() const {
	return BoundsRect(_controls, _reach);
}

QRectF Outline::anchors() const {
	const auto magnitude = std::max({
		-_controls.left,
		-_controls.top,
		_controls.right,
		_controls.bottom,
	});
	return BoundsRect(_anchors, -(_reach + 1. + magnitude * kCurveSlack));
}

QPointF Outline::map(QPointF point) {
	const auto result = _device.map(point);
	_valid = _valid && Finite(result);
	Include(_controls, result);
	return result;
}

float64 Outline::clamped(QPointF from, QPointF to) const {
	const auto clamp = [&](QPointF point) {
		return QPointF(
			std::clamp(point.x(), 0., _width),
			std::clamp(point.y(), 0., _height));
	};
	return Span(clamp(from), clamp(to));
}

void Outline::count(
		float64 edges,
		float64 spans,
		float64 clamped,
		float64 density,
		float64 top,
		float64 bottom,
		float64 rise,
		QPointF along) {
	_edges += edges;
	_spans += spans;
	_clampedSpans += clamped;
	const auto from = std::max(top - _reach, 0.);
	const auto till = std::min(bottom + _reach, _height);
	if (from <= till) {
		const auto rows = std::ceil(till) - std::floor(from) + 1.;
		_visibleSpans += spans;
		_rowSpans += spans * rows;
		_density += density;
		const auto padded = (rise > 0.)
			? (rise + 1. / kOutlineSubpixels)
			: rise;
		_rowCells += std::min(
			density,
			edges * (kRowSubpixels * std::min(padded, 1.) + 2.));
		if (_aligned && ((along.x() == 0.) != (along.y() == 0.))) {
			_alignedSpans += spans;
			_alignedRowSpans += spans * rows;
			if (along.x() == 0.) {
				_verticalRows += edges * rows;
				_verticalEdges += edges;
			}
		}
	}
}

void Outline::move(QPointF to) {
	finish();
	_start = _point = to;
	_startMapped = _mapped = map(to);
	Include(_anchors, _mapped);
	if (!_stroke) {
		count(1., 0., 0., 0., _mapped.y(), _mapped.y(), 0.);
	}
}

void Outline::line(QPointF to) {
	const auto mapped = map(to);
	Include(_anchors, mapped);
	const auto span = Span(_mapped, mapped);
	const auto top = std::min(_mapped.y(), mapped.y());
	const auto bottom = std::max(_mapped.y(), mapped.y());
	const auto direction = to - _point;
	const auto inside = clamped(_mapped, mapped);
	const auto slope = std::abs(mapped.x() - _mapped.x())
		/ std::max(bottom - top, 1.)
		+ 2.;
	if (!_stroke) {
		count(
			1.,
			span + 1.,
			inside + 1.,
			slope,
			top,
			bottom,
			bottom - top,
			direction);
	} else if (!direction.isNull()) {
		join(direction);
		count(
			2.,
			2. * (span + 1.),
			2. * (inside + 3. * _deviceRadius + 1.),
			2. * slope,
			top,
			bottom,
			bottom - top,
			direction);
		_last = direction;
	}
	_point = to;
	_mapped = mapped;
}

void Outline::curve(QPointF b, QPointF c, QPointF d) {
	_curved = true;
	const auto a = _point;
	const auto points = std::array{ a, b, c, d };
	const auto mapped = std::array{ _mapped, map(b), map(c), map(d) };
	const auto polygon = Span(mapped[0], mapped[1])
		+ Span(mapped[1], mapped[2])
		+ Span(mapped[2], mapped[3]);
	const auto sideways = std::abs(mapped[1].x() - mapped[0].x())
		+ std::abs(mapped[2].x() - mapped[1].x())
		+ std::abs(mapped[3].x() - mapped[2].x());
	const auto [left, right] = CurveRange(
		mapped[0].x(),
		mapped[1].x(),
		mapped[2].x(),
		mapped[3].x());
	const auto [top, bottom] = CurveRange(
		mapped[0].y(),
		mapped[1].y(),
		mapped[2].y(),
		mapped[3].y());
	Include(_anchors, QPointF(left, top));
	Include(_anchors, QPointF(right, bottom));
	const auto pieces = float64(
		CurvePieces(points, _threshold, _limit - _edges));
	const auto across = 3. * (_width + _height + 2. * _deviceRadius);
	if (!_stroke) {
		count(
			pieces,
			polygon + pieces,
			std::min(polygon, across) + pieces,
			sideways + 2. * pieces,
			top,
			bottom,
			bottom - top);
	} else if (const auto start = Tangent(a, { b, c, d })) {
		const auto turn = PolygonTurn(points);
		const auto offset = 2. * turn * std::sqrt(_radius / (3. * _threshold));
		const auto side = pieces + std::ceil(offset) + 1.;
		const auto length = polygon + 1.5 * turn * _deviceRadius;
		join(*start);
		count(
			2. * side,
			2. * (length + side),
			2. * (std::min(length, kOffsetSweeps * across) + side),
			2. * (sideways + 1.5 * turn * _deviceRadius + 2. * side),
			top,
			bottom,
			bottom - top + 2. * _reach);
		_last = -*Tangent(d, { c, b, a });
	}
	_point = d;
	_mapped = mapped[3];
}

void Outline::join(QPointF direction) {
	if (!_last) {
		_first = direction;
		return;
	}
	const auto turn = Turn(*_last, direction);
	const auto y = _mapped.y();
	if (!_roundJoin) {
		const auto miter = std::tan(std::min(turn, kMiterTurn) / 2.);
		const auto tip = _deviceRadius * (1. + miter);
		const auto spans = 4. + 3. * _deviceRadius + 3. * std::min(_reach, tip);
		count(4., spans, spans, spans + 4., y, y, 2. * _reach);
		return;
	}
	const auto shortest = std::min(
		std::hypot(direction.x(), direction.y()),
		std::hypot(_last->x(), _last->y()));
	const auto inside = (shortest <= _radius);
	const auto quarters = std::ceil(turn / (M_PI / 2.))
		+ 1.
		+ (inside ? kArcCurves : 0.);
	const auto pieces = quarters * quarter();
	const auto sweep = turn + (inside ? (2. * M_PI) : 0.);
	const auto spans = 3.
		+ 3. * _deviceRadius
		+ pieces
		+ 1.5 * sweep * _deviceRadius;
	count(3. + pieces, spans, spans, spans + 3. + pieces, y, y, 2. * _reach);
}

void Outline::cap(QPointF at, QPointF direction) {
	switch (_cap) {
	case Qt::RoundCap: {
		const auto pieces = 2. * quarter();
		const auto spans = pieces + 1.5 * M_PI * _deviceRadius;
		count(
			pieces,
			spans,
			spans,
			spans + pieces,
			at.y(),
			at.y(),
			2. * _reach);
	} break;
	case Qt::SquareCap: {
		const auto spans = 6. * _deviceRadius + 3.;
		count(3., spans, spans, spans + 3., at.y(), at.y(), 2. * _reach);
	} break;
	default: {
		const auto length = std::hypot(direction.x(), direction.y());
		const auto end = QPointF(
			-direction.y() / length,
			direction.x() / length) * (2. * _radius);
		const auto across = std::abs(
			_device.m11() * end.x() + _device.m21() * end.y());
		const auto down = std::abs(
			_device.m12() * end.x() + _device.m22() * end.y());
		const auto straight = !_curved
			&& std::isfinite(across)
			&& std::isfinite(down)
			&& (across > 0. || down > 0.);
		const auto spans = 3. * _deviceRadius + 1.;
		count(
			1.,
			spans,
			spans,
			spans + 1.,
			at.y(),
			at.y(),
			straight ? std::min(across, down) : (2. * _reach));
	} break;
	}
}

void Outline::finish() {
	if (!_stroke) {
		if (_point != _start) {
			const auto top = std::min(_mapped.y(), _startMapped.y());
			const auto bottom = std::max(_mapped.y(), _startMapped.y());
			const auto slope = std::abs(_startMapped.x() - _mapped.x())
				/ std::max(bottom - top, 1.)
				+ 2.;
			count(
				1.,
				Span(_mapped, _startMapped) + 1.,
				clamped(_mapped, _startMapped) + 1.,
				slope,
				top,
				bottom,
				bottom - top,
				_start - _point);
		}
	} else if (_first) {
		if (_point == _start) {
			join(*_first);
		} else {
			cap(_startMapped, *_first);
			cap(_mapped, *_last);
		}
	}
	_first = _last = std::nullopt;
	_curved = false;
}

float64 Outline::quarter() {
	if (_quarter < 0.) {
		const auto radius = _radius;
		const auto handle = radius * kKappa;
		const auto straight = std::array{
			QPointF(radius, 0.),
			QPointF(radius, handle),
			QPointF(handle, radius),
			QPointF(0., radius),
		};
		auto turned = straight;
		for (auto &point : turned) {
			point = QPointF(point.x() - point.y(), point.x() + point.y())
				* std::sqrt(.5);
		}
		const auto most = float64(kMaxCurvePoints);
		_quarter = float64(std::max(
			CurvePieces(straight, _threshold, most),
			CurvePieces(turned, _threshold, most)));
	}
	return _quarter;
}

} // namespace PainterGeometry

using namespace PainterGeometry;

PaintAllowance::PaintAllowance()
: _elements(kMaxPathElements)
, _edges(kMaxPathEdges)
, _work(kMaxPathWork)
, _dashSteps(kMaxDashSteps)
, _flattenBytes(kMaxFlattenBytes) {
}

bool PaintAllowance::chargeElements(int64 count) {
	if (count < 0) {
		return false;
	} else if (count > _elements) {
		return exceed(PaintLimit(
			u"%1 path elements in one frame"_q.arg(
				kMaxPathElements - _elements + count),
			kMaxPathElements));
	} else if (count > _work / kElementWork) {
		return exceed(PaintLimit(
			u"path work of %1 units in one frame"_q.arg(
				kMaxPathWork - _work + count * kElementWork),
			kMaxPathWork));
	}
	_elements -= count;
	_work -= count * kElementWork;
	return true;
}

bool PaintAllowance::chargeStops(int64 count) {
	if (count < 0) {
		return false;
	} else if (count > _work / kStopWork) {
		return exceed(PaintLimit(
			u"path work of %1 units in one frame"_q.arg(
				kMaxPathWork - _work + count * kStopWork),
			kMaxPathWork));
	} else if (count > _flattenBytes / kStopBytes) {
		return exceed(PaintLimit(
			u"%1 flattening bytes in one frame"_q.arg(
				kMaxFlattenBytes - _flattenBytes + count * kStopBytes),
			kMaxFlattenBytes));
	}
	_work -= count * kStopWork;
	_flattenBytes -= count * kStopBytes;
	return true;
}

bool PaintAllowance::chargeFill(
		const QPainter &painter,
		const QPainterPath &path,
		const QBrush &brush) {
	return chargeShape(
		path,
		painter.combinedTransform(),
		DeviceSize(painter),
		nullptr,
		brush.style());
}

bool PaintAllowance::chargeStroke(
		const QPainter &painter,
		const QPainterPath &path,
		const QPen &pen) {
	return chargeShape(
		path,
		painter.combinedTransform(),
		DeviceSize(painter),
		&pen,
		pen.brush().style());
}

bool PaintAllowance::strokeDashes(
		QPainter &painter,
		const QPainterPath &path,
		const QPen &pen,
		std::vector<float64> dashes,
		float64 offset) {
	auto &p = painter;
	const auto scale = MaxStretch(p.combinedTransform());
	auto invertible = false;
	const auto fromDevice = p.combinedTransform().inverted(&invertible)
		* QTransform::fromScale(scale, scale);
	if (!std::isfinite(scale) || !(scale > 0.) || !invertible) {
		return true;
	}
	const auto width = pen.widthF() * scale;
	const auto reach = StrokeReach(width, pen.miterLimit());
	const auto device = VisibleRect(p).adjusted(
		-kInkMargin,
		-kInkMargin,
		kInkMargin,
		kInkMargin);
	const auto visible = fromDevice.mapRect(device).adjusted(
		-reach,
		-reach,
		reach,
		reach);
	auto dasher = Dasher(
		std::move(dashes),
		offset,
		scale,
		visible,
		_dashSteps,
		pen.capStyle() != Qt::FlatCap);
	const auto scaled = QTransform::fromScale(scale, scale);
	if (!ReserveFlattening(path, _flattenBytes)) {
		return exceed(PaintLimit(
			u"more than %1 flattening bytes in one frame"_q.arg(
				kMaxFlattenBytes),
			kMaxFlattenBytes));
	}
	const auto polygons = path.toSubpathPolygons(scaled);
	auto points = 0.;
	for (const auto &polygon : polygons) {
		points += polygon.size();
	}
	if (!charge(0., points * kDashPointWork)) {
		return false;
	}
	for (const auto &polygon : polygons) {
		if (!dasher.add(polygon)) {
			return exceed(PaintLimit(
				u"more than %1 dash steps in one frame"_q.arg(kMaxDashSteps),
				kMaxDashSteps));
		}
	}
	auto stroker = QPainterPathStroker(pen);
	stroker.setWidth(width);
	auto outlinePen = pen;
	outlinePen.setWidthF(width);
	const auto toDevice = QTransform::fromScale(1. / scale, 1. / scale)
		* p.combinedTransform();
	if (!chargeShape(
			dasher.result(),
			toDevice,
			DeviceSize(p),
			&outlinePen,
			pen.brush().style())) {
		return false;
	}
	const auto outline = stroker.createStroke(dasher.result());
	auto brush = pen.brush();
	brush.setTransform(brush.transform() * scaled);
	p.save();
	p.scale(1. / scale, 1. / scale);
	p.fillPath(outline, brush);
	p.restore();
	return true;
}

const QString &PaintAllowance::exceeded() const {
	return _exceeded;
}

bool PaintAllowance::chargeShape(
		const QPainterPath &path,
		const QTransform &device,
		QSize size,
		const QPen *pen,
		Qt::BrushStyle style) {
	const auto stretch = MaxStretch(device);
	if (!std::isfinite(stretch)) {
		return false;
	}
	const auto threshold = (stretch > 0.)
		? (kCurveThreshold / stretch)
		: kCurveThreshold;
	const auto limit = float64(_edges);
	auto outline = pen
		? Outline(device, size, threshold, limit, *pen)
		: Outline(device, size, threshold, limit);
	if (!outline.add(path)) {
		if (outline.valid() && outline.edges() > limit) {
			return exceed(PaintLimit(
				u"more than %1 path edges in one frame"_q.arg(kMaxPathEdges),
				kMaxPathEdges));
		}
		return false;
	}
	const auto weight = (style == Qt::SolidPattern) ? 1. : kGradientPixel;
	return charge(
		outline.edges(),
		RasterWork(outline, size, weight) + kShapeWork);
}

bool PaintAllowance::charge(float64 edges, float64 work) {
	if (!std::isfinite(edges) || !std::isfinite(work)) {
		return false;
	} else if (!(edges <= float64(_edges))) {
		return exceed(PaintLimit(
			u"%1 path edges in one frame"_q.arg(
				std::ceil(float64(kMaxPathEdges - _edges) + edges),
				0,
				'f',
				0),
			kMaxPathEdges));
	} else if (!(work <= float64(_work))) {
		return exceed(PaintLimit(
			u"path work of %1 units in one frame"_q.arg(
				std::ceil(float64(kMaxPathWork - _work) + work),
				0,
				'f',
				0),
			kMaxPathWork));
	}
	_edges -= int64(std::ceil(edges));
	_work -= int64(std::ceil(work));
	return true;
}

bool PaintAllowance::exceed(QString reason) {
	_exceeded = std::move(reason);
	return false;
}

bool Finite(QPointF point) {
	return std::isfinite(point.x()) && std::isfinite(point.y());
}

bool Finite(const QTransform &transform) {
	const auto values = std::array{
		transform.m11(),
		transform.m12(),
		transform.m21(),
		transform.m22(),
		transform.dx(),
		transform.dy(),
	};
	return ranges::all_of(values, [](qreal value) {
		return std::isfinite(value);
	});
}

QBrush GradientBrush(
		QGradient &gradient,
		const std::vector<twlite::Stop> &stops,
		bool reversed) {
	return StopsBrush(gradient, StrictStops(stops, reversed));
}

QBrush LinearBrush(
		QPointF from,
		QPointF to,
		const std::vector<twlite::Stop> &stops) {
	if (!Finite(from) || !Finite(to)) {
		return QBrush();
	}
	const auto strict = StrictStops(stops, false);
	if (from == to && !strict.isEmpty()) {
		return QBrush(strict.back().second);
	}
	auto gradient = QLinearGradient(from, to);
	return StopsBrush(gradient, strict);
}

QBrush ConicBrush(
		QPointF centre,
		float64 from,
		const std::vector<twlite::Stop> &stops) {
	if (!Finite(centre) || !std::isfinite(from)) {
		return QBrush();
	}
	const auto turn = std::fmod(from, 360.);
	// CSS 从十二点方向顺时针旋转，Qt 从三点方向逆时针旋转。
	auto gradient = QConicalGradient(
		centre,
		std::fmod(90. - turn + 360., 360.));
	return GradientBrush(gradient, stops, true);
}

QPainterPath VectorPath(
		const std::vector<twlite::Path::Op> &ops,
		const std::vector<float> &points,
		bool evenodd) {
	auto result = QPainterPath();
	result.setFillRule(evenodd ? Qt::OddEvenFill : Qt::WindingFill);
	const auto finite = [](float value) {
		return std::isfinite(value);
	};
	const auto point = [&](size_t at) {
		return QPointF(points[at], points[at + 1]);
	};
	auto index = size_t();
	auto started = false;
	for (const auto op : ops) {
		const auto count = PointValues(op);
		if (count < 0 || points.size() - index < size_t(count)) {
			break;
		}
		const auto from = points.begin() + index;
		if (!std::all_of(from, from + count, finite)) {
			break;
		}
		switch (op) {
		case twlite::Path::Move:
			result.moveTo(point(index));
			started = true;
			break;
		case twlite::Path::Line:
			if (started) {
				result.lineTo(point(index));
			}
			break;
		case twlite::Path::Quad:
			if (started) {
				result.quadTo(point(index), point(index + 2));
			}
			break;
		case twlite::Path::Cubic:
			if (started) {
				result.cubicTo(
					point(index),
					point(index + 2),
					point(index + 4));
			}
			break;
		case twlite::Path::Close:
			if (started) {
				result.closeSubpath();
			}
			break;
		}
		index += count;
	}
	return result;
}

Qt::PenCapStyle PathCap(int cap) {
	constexpr auto styles = std::array{
		Qt::FlatCap,
		Qt::RoundCap,
		Qt::SquareCap,
	};
	return (cap >= 0 && cap < int(styles.size()))
		? styles[cap]
		: styles.front();
}

Qt::PenJoinStyle PathJoin(int join) {
	constexpr auto styles = std::array{
		Qt::SvgMiterJoin,
		Qt::RoundJoin,
		Qt::BevelJoin,
	};
	return (join >= 0 && join < int(styles.size()))
		? styles[join]
		: styles.front();
}

std::vector<float64> DashPattern(const std::vector<float> &dashes) {
	const auto finite = [](float value) {
		return std::isfinite(value);
	};
	if (dashes.empty() || !ranges::all_of(dashes, finite)) {
		return {};
	}
	const auto count = dashes.size();
	const auto size = (count % 2) ? (count * 2) : count;
	auto result = std::vector<float64>();
	result.reserve(size);
	for (auto i = size_t(); i != size; ++i) {
		result.push_back(std::max(float64(dashes[i % count]), 0.));
	}
	const auto positive = [](float64 value) {
		return (value > 0.);
	};
	return ranges::any_of(result, positive)
		? result
		: std::vector<float64>();
}

float64 StrokeReach(float64 width, float64 miterLimit) {
	return width * std::max(miterLimit, 2.) / 2.;
}

} // namespace Tgw
