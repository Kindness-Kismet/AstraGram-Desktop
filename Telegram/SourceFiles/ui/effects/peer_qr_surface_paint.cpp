#include "ui/effects/peer_qr_surface_internal.h"

namespace Ui::PeerQrSurfaceDetails {

[[nodiscard]] QColor SolveLightness(
		float64 hue,
		float64 chroma,
		const QColor &against,
		float64 contrast) {
	auto from = 0.;
	auto till = ToLab(against).lightness;
	for (auto i = 0; i != kSolveIterations; ++i) {
		const auto middle = (from + till) / 2.;
		const auto color = FromLch(middle, chroma, hue);
		if (CountContrast(color, against) >= contrast) {
			from = middle;
		} else {
			till = middle;
		}
	}
	return FromLch(from, chroma, hue);
}

[[nodiscard]] QColor MixLab(
		const QColor &a,
		const QColor &b,
		float64 ratio) {
	const auto mix = [&](float64 from, float64 till) {
		return from + (till - from) * ratio;
	};
	const auto from = ToLab(a);
	const auto till = ToLab(b);
	const auto lab = Lab{
		.lightness = mix(from.lightness, till.lightness),
		.a = mix(from.a, till.a),
		.b = mix(from.b, till.b),
	};
	auto result = FromLch(lab.lightness, Chroma(lab), Hue(lab));
	result.setAlphaF(mix(a.alphaF(), b.alphaF()));
	return result;
}

[[nodiscard]] PeerQrColors MixColors(
		const PeerQrColors &a,
		const PeerQrColors &b,
		float64 ratio) {
	return {
		.ink = MixLab(a.ink, b.ink, ratio),
		.foreground = MixLab(a.foreground, b.foreground, ratio),
		.control = MixLab(a.control, b.control, ratio),
		.glass = MixLab(a.glass, b.glass, ratio),
	};
}

[[nodiscard]] QColor ComputeWaveTint(const PeerQrAppearance &appearance) {
	const auto center = appearance.center.isValid()
		? appearance.center
		: appearance.edge;
	return MixLab(center, QColor(255, 255, 255), kWaveTintMix);
}

[[nodiscard]] float64 InkChroma(float64 chroma) {
	return std::min(chroma * kInkChromaScale + kInkChromaAdd, kInkChromaMax);
}

[[nodiscard]] QColor BackdropInk(const PeerQrAppearance &appearance) {
	const auto white = QColor(255, 255, 255);
	const auto edge = ToLab(appearance.edge);
	const auto hue = Hue(edge);
	const auto shipped = SolveLightness(
		hue,
		InkChroma(Chroma(edge)),
		white,
		kInkContrast);
	const auto darkness = std::clamp(
		(CountContrast(appearance.edge, white) - kDarkEdgeFrom)
			/ (kDarkEdgeTill - kDarkEdgeFrom),
		0.,
		1.);
	if (darkness <= 0.) {
		return shipped;
	}
	// The recorded dark neutrals' ink is that much darker than their centre.
	const auto shade = SolveLightness(
		hue,
		Chroma(edge),
		appearance.center,
		kDarkInkContrast);
	if (CountContrast(shade, white) <= CountContrast(shipped, white)) {
		return shipped;
	}
	return (darkness >= 1.) ? shade : MixLab(shipped, shade, darkness);
}

[[nodiscard]] PeerQrColors ComputePeerQrColors(
		const PeerQrAppearance &appearance) {
	const auto white = QColor(255, 255, 255);
	const auto edge = ToLab(appearance.edge);
	const auto hue = Hue(edge);
	const auto chroma = InkChroma(Chroma(edge));
	const auto reference = MixLab(
		appearance.center,
		appearance.edge,
		kGradientMiddleMix);
	const auto stops = std::vector{
		appearance.center,
		reference,
		appearance.edge,
	};
	const auto darkness = [&](const QColor &color) {
		return CountContrast(color, white);
	};
	const auto darkest = *ranges::max_element(
		stops,
		ranges::less(),
		darkness);
	const auto dark = SolveLightness(
		hue,
		chroma,
		darkest,
		kForegroundContrast);
	const auto glass = [&](float64 alpha) {
		return anim::color(reference, white, alpha);
	};
	const auto whiteContrast = CountContrast(
		white,
		glass(kWhiteGlyphGlassAlpha));
	const auto whiteGlyphs = (whiteContrast >= kWhiteGlyphContrast)
		|| (whiteContrast >= CountContrast(dark, glass(kDarkInkGlassAlpha)));
	return {
		.ink = BackdropInk(appearance),
		.foreground = whiteGlyphs ? white : dark,
		.control = anim::with_alpha(
			white,
			whiteGlyphs ? kWhiteGlyphGlassAlpha : kDarkInkGlassAlpha),
		.glass = anim::with_alpha(white, kGlassAlpha),
	};
}

[[nodiscard]] bool SameBackdrop(PeerQrAppearance a, PeerQrAppearance b) {
	a.emoji.clear();
	b.emoji.clear();
	return a == b;
}

[[nodiscard]] float64 DipScale(crl::time startedAt, crl::time now) {
	const auto progress = float64(now - startedAt) / kDipDuration;
	return (!startedAt || progress <= 0. || progress >= 1.)
		? 1.
		: (1. - kDipDepth * std::sin(M_PI * progress));
}

[[nodiscard]] CardLayout ComputeCardLayout(int width, bool withUsername) {
	const auto part = [&](float64 ratio) {
		return int(base::SafeRound(width * ratio));
	};
	const auto height = withUsername ? part(kCardHeightRatio) : width;
	const auto side = part(kQrSideRatio);
	const auto left = (width - side) / 2;
	const auto qr = QRect(
		left,
		withUsername ? part(kQrTopRatio) : left,
		side,
		side);
	const auto logo = int(base::SafeRound(side * kLogoRatio));
	const auto bottom = qr.y() + qr.height();
	return {
		.size = QSize(width, height),
		.qr = qr,
		.logo = QRect(
			qr.x() + (side - logo) / 2,
			qr.y() + (side - logo) / 2,
			logo,
			logo),
		.band = QRect(0, bottom, width, height - bottom),
		.radius = part(kCardRadiusRatio),
	};
}

[[nodiscard]] QStringList SplitInTwoLines(
		const QString &text,
		const QFontMetricsF &metrics) {
	auto best = 1;
	auto bestWidth = std::numeric_limits<float64>::max();
	for (auto i = 1; i < text.size(); ++i) {
		const auto wider = std::max(
			metrics.horizontalAdvance(text.left(i)),
			metrics.horizontalAdvance(text.mid(i)));
		if (wider < bestWidth) {
			best = i;
			bestWidth = wider;
		}
	}
	return { text.left(best), text.mid(best) };
}

[[nodiscard]] bool LinesFit(
		const QStringList &lines,
		const QFontMetricsF &metrics,
		int available,
		int bandHeight) {
	return (lines.size() * metrics.height() <= bandHeight)
		&& ranges::all_of(lines, [&](const QString &line) {
			return metrics.horizontalAdvance(line) <= available;
		});
}

[[nodiscard]] UsernameFit FitUsername(
		const QString &text,
		int available,
		int bandHeight,
		const std::array<QFont, 3> &steps,
		int floorPixelSize) {
	if (text.isEmpty()) {
		return {};
	}
	const auto single = QStringList{ text };
	const auto fitsLine = [&](const QFont &font) {
		return LinesFit(single, QFontMetricsF(font), available, bandHeight);
	};
	for (const auto &font : steps) {
		if (fitsLine(font)) {
			return { .font = font, .lines = single };
		}
	}
	auto font = steps.back();
	const auto small = font.pixelSize();
	for (auto size = small - 1; size >= floorPixelSize; --size) {
		font.setPixelSize(size);
		if (fitsLine(font)) {
			return { .font = font, .lines = single };
		}
	}
	for (auto size = small; ; --size) {
		font.setPixelSize(size);
		const auto metrics = QFontMetricsF(font);
		auto lines = SplitInTwoLines(text, metrics);
		const auto fits = LinesFit(lines, metrics, available, bandHeight);
		if (fits || size <= floorPixelSize) {
			return { .font = font, .lines = std::move(lines) };
		}
	}
}

[[nodiscard]] QImage GenerateFinalQr(
		const Qr::Data &data,
		int side,
		const QColor &ink) {
	const auto pixel = (kQrOversample * side + data.size - 1) / data.size;
	return Qr::Generate(data, pixel, ink, Qt::transparent).scaled(
		side,
		side,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
}

[[nodiscard]] QImage PrepareQrImage(
		const Qr::Data &data,
		int side,
		const QColor &ink,
		int ratio) {
	auto result = (data.size > 0)
		? GenerateFinalQr(data, side * ratio, ink)
		: QImage();
	result.setDevicePixelRatio(ratio);
	return result;
}

[[nodiscard]] QImage PrepareCardShadow(
		QSize size,
		int radius,
		int blur,
		float64 alpha,
		int ratio) {
	auto mask = QImage(
		(size + QSize(2 * blur, 2 * blur)) * ratio,
		QImage::Format_RGB32);
	mask.fill(Qt::black);
	mask.setDevicePixelRatio(ratio);
	{
		auto p = QPainter(&mask);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		p.drawRoundedRect(QRect(QPoint(blur, blur), size), radius, radius);
	}
	return style::colorizeImage(
		Images::BlurLargeImage(std::move(mask), blur * ratio),
		anim::with_alpha(QColor(0, 0, 0), alpha));
}

[[nodiscard]] QImage PreparePlane(int side, int ratio) {
	auto result = QImage(
		QSize(side, side) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	{
		auto p = QPainter(&result);
		auto hq = PainterHighQualityEnabler(p);
		auto svg = QSvgRenderer(u":/gui/plane_white.svg"_q);
		svg.render(&p, QRectF(QPointF(), QSizeF(result.size())));
	}
	result.setDevicePixelRatio(ratio);
	return result;
}

[[nodiscard]] CardContent PrepareCardContent(
		const CardLayout &layout,
		const QString &link,
		const QColor &ink,
		int shadowRadius,
		int shadowShift,
		int ratio) {
	auto data = link.isEmpty() ? Qr::Data() : Qr::Encode(link);
	auto qr = PrepareQrImage(data, layout.qr.width(), ink, ratio);
	return {
		.shadow = PrepareCardShadow(
			layout.size,
			layout.radius,
			shadowRadius,
			kShadowAlpha,
			ratio),
		.qr = std::move(qr),
		.data = std::move(data),
		.plane = PreparePlane(layout.logo.width(), ratio),
		.ink = ink,
		.shadowPosition = QPoint(-shadowRadius, shadowShift - shadowRadius),
	};
}

void PaintAppearanceGradient(
		QPainter &p,
		const QRect &rect,
		QPointF center,
		float64 radius,
		const QColor &centerColor,
		const QColor &edgeColor) {
	auto stops = QGradientStops();
	stops.reserve(kGradientSteps + 1);
	for (auto i = 0; i <= kGradientSteps; ++i) {
		const auto ratio = float64(i) / kGradientSteps;
		stops.push_back({ ratio, MixLab(centerColor, edgeColor, ratio) });
	}
	auto gradient = QRadialGradient(center, radius);
	gradient.setStops(stops);
	p.fillRect(rect, gradient);
}

[[nodiscard]] QImage ComposeColorBackground(
		const BackgroundRequest &request) {
	auto result = QImage(request.size, QImage::Format_ARGB32_Premultiplied);
	auto p = QPainter(&result);
	const auto rect = QRect(QPoint(), request.size);
	PaintAppearanceGradient(
		p,
		rect,
		request.gradientCenter,
		request.gradientRadius,
		request.center,
		request.edge);
	p.end();
	return result;
}

[[nodiscard]] QImage ComposeBackground(const BackgroundRequest &request) {
	if (request.size.isEmpty() || !request.center.isValid()) {
		return QImage();
	}
	auto result = ComposeColorBackground(request);
	return request.corners.front().isNull()
		? result
		: Images::Round(std::move(result), request.corners);
}

[[nodiscard]] float64 FarthestCorner(QPointF center, QSizeF size) {
	return std::hypot(
		std::max(center.x(), size.width() - center.x()),
		std::max(center.y(), size.height() - center.y()));
}

[[nodiscard]] float64 WaveRadius(float64 farthest, float64 progress) {
	return (farthest + st::peerQrWaveOvershoot)
		* (1. - std::pow(1. - progress, kWavePower));
}

[[nodiscard]] WaveSide CircleSide(
		QPointF center,
		float64 radius,
		const std::vector<QRect> &parts) {
	const auto slack = kCircleSlack + radius * kCircleSlackPerRadius;
	auto inside = true;
	auto outside = true;
	for (const auto &part : parts) {
		const auto box = QRectF(part);
		const auto nearest = std::hypot(
			std::max({ box.left() - center.x(), 0., center.x() - box.right() }),
			std::max({ box.top() - center.y(), 0., center.y() - box.bottom() }));
		const auto farthest = std::hypot(
			std::max(
				std::abs(box.left() - center.x()),
				std::abs(box.right() - center.x())),
			std::max(
				std::abs(box.top() - center.y()),
				std::abs(box.bottom() - center.y())));
		inside = inside && (farthest <= radius - slack);
		outside = outside && (nearest >= radius + slack);
	}
	return inside
		? WaveSide::Inside
		: outside
		? WaveSide::Outside
		: WaveSide::Edge;
}

[[nodiscard]] bool MeetsCorner(const QRect &box, const QRect &area) {
	const auto radius = st::boxRadius;
	return !box.marginsRemoved({ radius, 0, radius, 0 }).contains(area)
		&& !box.marginsRemoved({ 0, radius, 0, radius }).contains(area);
}

void PaintWaveGlow(
		QPainter &p,
		const QRect &rect,
		const QRect &area,
		QPointF center,
		float64 radius,
		float64 progress,
		const QColor &tint) {
	const auto alpha = kWaveGlowAlpha * (1. - progress);
	const auto half = (st::peerQrWaveGlowFrom
		+ (st::peerQrWaveGlowTill - st::peerQrWaveGlowFrom) * progress) / 2.;
	if (radius <= 0. || alpha <= 0. || half <= 0.) {
		return;
	}
	const auto outer = radius + half;
	auto transparent = tint;
	transparent.setAlpha(0);
	auto peak = tint;
	peak.setAlphaF(alpha);
	auto stops = QGradientStops();
	if (radius > half) {
		stops.push_back({ (radius - half) / outer, transparent });
	} else {
		auto inner = tint;
		inner.setAlphaF(alpha * (1. - radius / half));
		stops.push_back({ 0., inner });
	}
	stops.push_back({ radius / outer, peak });
	stops.push_back({ 1., transparent });
	auto gradient = QRadialGradient(center, outer);
	gradient.setStops(stops);
	const auto bounds = QRectF(
		center - QPointF(outer, outer),
		QSizeF(2. * outer, 2. * outer));
	const auto fill = bounds.intersected(QRectF(area));
	auto hq = PainterHighQualityEnabler(p);
	if (!MeetsCorner(rect, area)) {
		p.fillRect(fill, gradient);
		return;
	}
	auto rounded = QPainterPath();
	rounded.addRoundedRect(QRectF(rect), st::boxRadius, st::boxRadius);
	p.save();
	p.setClipPath(rounded, Qt::IntersectClip);
	p.fillRect(fill, gradient);
	p.restore();
}

void ClipOutside(
		QPainter &p,
		const QRectF &area,
		const QPainterPath &shapes) {
	if (shapes.isEmpty()) {
		return;
	}
	auto outside = QPainterPath();
	outside.setFillRule(Qt::OddEvenFill);
	outside.addRect(area);
	outside.addPath(shapes);
	p.setClipPath(outside, Qt::IntersectClip);
}

[[nodiscard]] PlaneState ComputePlaneState(
		crl::time elapsed,
		float64 radius) {
	const auto travel = float64(elapsed % kLoopPeriod) / kLoopPeriod;
	if (elapsed < kFlyInDuration) {
		const auto progress = float64(elapsed) / kFlyInDuration;
		const auto eased = anim::easeOutCubic(1., progress);
		const auto offset = kFlyInOffset * radius * (1. - eased) / M_SQRT2;
		return {
			.shift = QPointF(-offset, offset),
			.scale = kFlyInScale + (1. - kFlyInScale) * eased,
			.opacity = std::min(2. * progress, 1.),
			.streaks = progress,
			.travel = travel,
		};
	}
	const auto phase = float64((elapsed - kFlyInDuration) % kLoopPeriod)
		/ kLoopPeriod;
	const auto roll = (1. - std::cos(2. * M_PI * phase)) / 2.;
	return {
		.width = 1. - (1. - kPlaneMinWidth) * roll,
		.rotation = -kPlaneTurn * roll,
		.streaks = 1.,
		.travel = travel,
	};
}

[[nodiscard]] CardFrame ComputeCardFrame(
		crl::time elapsed,
		float64 radius) {
	return {
		.plane = ComputePlaneState(elapsed, radius),
		.bloom = std::min(float64(elapsed) / kBloomDuration, 1.),
	};
}

void PaintStreaks(QPainter &p, const QRect &logo, const PlaneState &state) {
	const auto radius = logo.width() / 2.;
	const auto center = QRectF(logo).center();
	const auto half = kStreakLength * radius / 2.;
	const auto angle = kStreakSourceAngle * M_PI / 180.;
	const auto distance = kStreakSourceDistance * radius;
	// Lanes share one vanishing point, so no two streaks cross in the circle.
	const auto source = center
		+ QPointF(std::cos(angle), -std::sin(angle)) * distance;
	auto color = QColor(255, 255, 255);
	color.setAlphaF(kStreakAlpha * state.streaks);
	p.setPen(QPen(
		color,
		kStreakWidth * radius,
		Qt::SolidLine,
		Qt::RoundCap));
	const auto count = int(kStreakLanes.size());
	for (auto i = 0; i != count; ++i) {
		const auto lane = angle
			+ std::asin(kStreakLanes[i] / kStreakSourceDistance);
		const auto along = QPointF(std::cos(lane), -std::sin(lane));
		const auto foot = source
			- along * QPointF::dotProduct(source - center, along);
		const auto travel = std::fmod(state.travel + float64(i) / count, 1.);
		const auto middle = foot
			+ along * (kStreakTravel * radius * (0.5 - travel));
		p.drawLine(middle - along * half, middle + along * half);
	}
}

void PaintPlane(
		QPainter &p,
		const QRect &logo,
		const QImage &plane,
		const PlaneState &state) {
	if (state.opacity <= 0.) {
		return;
	}
	const auto center = QPointF(
		logo.x() + logo.width() * kPlaneCenterX,
		logo.y() + logo.height() * kPlaneCenterY);
	const auto anchor = QPointF(
		logo.x() + logo.width() * kPlaneAxisX,
		logo.y() + logo.height() * kPlaneAxisY);
	auto circle = QPainterPath();
	circle.addEllipse(logo);
	p.save();
	p.setClipPath(circle);
	if (state.streaks > 0.) {
		PaintStreaks(p, logo, state);
	}
	p.translate(center + state.shift);
	p.scale(state.scale, state.scale);
	p.translate(anchor - center);
	p.rotate(state.rotation - kPlaneAxisAngle);
	p.scale(1., state.width);
	p.rotate(kPlaneAxisAngle);
	p.translate(-anchor);
	p.setOpacity(state.opacity);
	p.drawImage(logo, plane);
	p.restore();
}

void PaintUsername(
		QPainter &p,
		const QRect &band,
		const UsernameFit &username,
		const QColor &ink) {
	const auto metrics = QFontMetricsF(username.font);
	const auto height = metrics.height();
	const auto lines = int(username.lines.size());
	auto top = band.y() + (band.height() - lines * height) / 2.;
	p.setFont(username.font);
	p.setPen(ink);
	for (const auto &line : username.lines) {
		const auto width = metrics.horizontalAdvance(line);
		const auto left = band.x() + (band.width() - width) / 2.;
		p.drawText(QPointF(left, top + metrics.ascent()), line);
		top += height;
	}
}

void PaintBloom(
		QPainter &p,
		const QRect &qr,
		const CardContent &content,
		float64 bloom) {
	const auto &data = content.data;
	const auto count = data.size;
	const auto cell = qr.width() / float64(count);
	const auto middle = count / 2.;
	const auto corner = std::hypot(middle - 0.5, middle - 0.5);
	const auto blankFrom = (count - Qr::ReplaceSize(data, 1)) / 2;
	const auto blankTill = count - blankFrom;
	const auto blank = [&](int index) {
		return (index >= blankFrom) && (index < blankTill);
	};
	const auto dark = [&](int row, int column) {
		return data.values[row * count + column]
			&& !(blank(row) && blank(column));
	};
	const auto progress = [&](int row, int column) {
		const auto distance = std::hypot(
			column + 0.5 - middle,
			row + 0.5 - middle) / corner;
		return std::clamp(
			(bloom - kBloomSpread * distance) / (1. - kBloomSpread),
			0.,
			1.);
	};
	auto finished = QPainterPath();
	p.setBrush(content.ink);
	for (auto row = 0; row != count; ++row) {
		const auto top = qr.y() + row * cell;
		auto from = -1;
		for (auto column = 0; column <= count; ++column) {
			const auto value = (column < count) ? progress(row, column) : 0.;
			if (value >= 1.) {
				if (from < 0) {
					from = column;
				}
				continue;
			} else if (from >= 0) {
				finished.addRect(QRectF(
					qr.x() + from * cell,
					top,
					(column - from) * cell,
					cell));
				from = -1;
			}
			if (value > 0. && dark(row, column)) {
				const auto radius = cell * anim::easeOutCubic(1., value) / 2.;
				p.drawEllipse(
					QPointF(qr.x() + (column + 0.5) * cell, top + cell / 2.),
					radius,
					radius);
			}
		}
	}
	if (!finished.isEmpty()) {
		p.save();
		p.setClipPath(finished);
		p.drawImage(qr, content.qr);
		p.restore();
	}
}

[[nodiscard]] float64 DustRandom(uint32 &seed) {
	seed = PeerQrHash(seed + 0x9E3779B9U);
	return seed / 4294967296.;
}

void PaintSwap(
		QPainter &p,
		const QRect &qr,
		const CardContent &content,
		const QrSwap &swap,
		crl::time elapsed) {
	if (elapsed < 0) {
		p.drawImage(qr, swap.qr);
		return;
	}
	const auto travel = std::min(float64(elapsed) / kSwapTravel, 1.);
	const auto bottom = qr.y() + qr.height();
	const auto front = bottom - travel * qr.height();
	const auto paintPart = [&](
			const QImage &image,
			float64 from,
			float64 till) {
		if (till > from) {
			p.save();
			p.setClipRect(
				QRectF(qr.x(), from, qr.width(), till - from),
				Qt::IntersectClip);
			p.drawImage(qr, image);
			p.restore();
		}
	};
	paintPart(swap.qr, qr.y(), front);
	paintPart(content.qr, front, bottom);

	const auto &data = swap.data;
	const auto count = data.size;
	if (count <= 0) {
		return;
	}
	const auto cell = qr.width() / float64(count);
	const auto speed = qr.height() / float64(kSwapTravel);
	const auto blankFrom = (count - Qr::ReplaceSize(data, 1)) / 2;
	const auto blankTill = count - blankFrom;
	const auto blank = [&](int index) {
		return (index >= blankFrom) && (index < blankTill);
	};
	const auto leading = [&](int index) {
		return index < kFinderSize;
	};
	const auto trailing = [&](int index) {
		return index >= count - kFinderSize;
	};
	const auto dusty = [&](int row, int column) {
		return data.values[row * count + column]
			&& !(blank(row) && blank(column))
			&& !(leading(row) && (leading(column) || trailing(column)))
			&& !(trailing(row) && leading(column));
	};
	for (auto row = 0; row != count; ++row) {
		const auto release = kSwapTravel * (1. - (row + 0.5) / count);
		const auto age = elapsed - release;
		if (age < 0. || age >= kDustLife) {
			continue;
		}
		const auto progress = age / kDustLife;
		auto color = content.ink;
		color.setAlphaF(color.alphaF() * (1. - progress));
		p.setBrush(color);
		for (auto column = 0; column != count; ++column) {
			if (!dusty(row, column)) {
				continue;
			}
			for (auto k = 0; k != kDustPerModule; ++k) {
				auto seed = (uint32(row) << 16)
					| (uint32(column) << 4)
					| uint32(k);
				const auto x = column + DustRandom(seed);
				const auto y = row + DustRandom(seed);
				const auto size = kDustSizeMin
					+ (kDustSizeMax - kDustSizeMin) * DustRandom(seed);
				const auto rise = kDustRiseMin
					+ (kDustRiseMax - kDustRiseMin) * DustRandom(seed);
				const auto drift = kDustDrift * (2. * DustRandom(seed) - 1.);
				const auto radius = size * cell / 2.;
				p.drawEllipse(
					QPointF(
						qr.x() + (x + drift * progress) * cell,
						qr.y() + y * cell - rise * speed * age),
					radius,
					radius);
			}
		}
	}
}

void PaintCard(
		QPainter &p,
		const CardLayout &layout,
		const CardContent &content,
		const CardFrame &frame,
		bool paintUsername) {
	p.drawImage(content.shadowPosition, content.shadow);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(Qt::white);
	p.drawRoundedRect(
		QRect(QPoint(), layout.size),
		layout.radius,
		layout.radius);
	if (frame.swap || !content.qr.isNull()) {
		if (frame.swap) {
			p.save();
			p.setClipRect(QRect(QPoint(), layout.size));
			PaintSwap(p, layout.qr, content, *frame.swap, frame.swapElapsed);
			p.restore();
		} else if (frame.bloom >= 1.) {
			p.drawImage(layout.qr, content.qr);
		} else {
			PaintBloom(p, layout.qr, content, frame.bloom);
		}
		p.setBrush(content.ink);
		p.drawEllipse(layout.logo);
		PaintPlane(p, layout.logo, content.plane, frame.plane);
	}
	if (paintUsername && !content.username.lines.isEmpty()) {
		PaintUsername(p, layout.band, content.username, content.ink);
	}
}

} // namespace Ui::PeerQrSurfaceDetails
