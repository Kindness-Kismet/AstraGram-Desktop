/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "tgw/tgw_painter.h"

namespace Tgw::PainterGeometry {

// 显示列表执行与路径预算使用同一套设备边界。
inline constexpr auto kInkMargin = 2.;
inline constexpr auto kCurveLevels = 9;
inline constexpr auto kRasterCells = 256.;

struct CornerRadius {
	float64 x = 0.;
	float64 y = 0.;

	friend inline bool operator==(CornerRadius, CornerRadius) = default;
};

struct Corners {
	CornerRadius topLeft;
	CornerRadius topRight;
	CornerRadius bottomRight;
	CornerRadius bottomLeft;

	friend inline bool operator==(
		const Corners &,
		const Corners &) = default;
};

[[nodiscard]] QString PaintLimit(const QString &what, int64 max);
[[nodiscard]] QSize DeviceSize(const QPainter &painter);
[[nodiscard]] QRectF VisibleRect(QPainter &painter);
[[nodiscard]] float64 MaxStretch(const QTransform &transform);
[[nodiscard]] bool ValidWidth(float width);
[[nodiscard]] QColor ToColor(twlite::Color color);
[[nodiscard]] bool FiniteRect(const twlite::Rect &rect);
[[nodiscard]] QRectF ToRect(const twlite::Rect &rect);
[[nodiscard]] QPen StrokePen(twlite::Color color, float width);
[[nodiscard]] std::optional<Corners> NormalizedCorners(
	const QRectF &rect,
	const twlite::Radii &radii);
[[nodiscard]] Corners SpreadCorners(
	const QRectF &rect,
	Corners corners,
	float64 spread);
[[nodiscard]] bool Rounded(const Corners &corners);
[[nodiscard]] QPainterPath RoundedPath(
	const QRectF &rect,
	const Corners &corners);
[[nodiscard]] QBrush BackgroundBrush(const twlite::Command &command);
[[nodiscard]] QBrush SolidBrush(twlite::Color color);
[[nodiscard]] QBrush PathBrush(const twlite::Path &path);
[[nodiscard]] int64 StrokeStops(const twlite::Path &path);
[[nodiscard]] float64 MiterLimit(const twlite::Path &path);
[[nodiscard]] QPen PathPen(const twlite::Path &path);

} // namespace Tgw::PainterGeometry
