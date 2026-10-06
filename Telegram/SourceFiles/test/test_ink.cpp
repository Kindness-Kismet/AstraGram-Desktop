/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_ink.h"

#include "test/test_capture.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "ui/color_contrast.h"

#include <QtGui/QPainter>

#include <algorithm>
#include <cmath>

namespace Test {
namespace {

[[nodiscard]] QString ColorHex(QColor color) {
	if (!color.isValid()) {
		return u"invalid"_q;
	}
	return u"#%1%2%3"_q
		.arg(color.red(), 2, 16, QChar('0'))
		.arg(color.green(), 2, 16, QChar('0'))
		.arg(color.blue(), 2, 16, QChar('0'));
}

[[nodiscard]] QString CandidateName(const InkCandidate &candidate) {
	return candidate.name.isEmpty()
		? ColorHex(candidate.color)
		: candidate.name;
}

[[nodiscard]] QString ScanReasonText(const InkScan &scan) {
	return scan.reason.isEmpty()
		? u"this reading was never scanned"_q
		: scan.reason;
}

[[nodiscard]] double SegmentDistance(QColor p, QColor a, QColor b) {
	const auto px = double(p.red());
	const auto py = double(p.green());
	const auto pz = double(p.blue());
	const auto ax = double(a.red());
	const auto ay = double(a.green());
	const auto az = double(a.blue());
	const auto dx = double(b.red()) - ax;
	const auto dy = double(b.green()) - ay;
	const auto dz = double(b.blue()) - az;
	const auto len = dx * dx + dy * dy + dz * dz;
	auto t = 0.;
	if (len > 0.) {
		t = ((px - ax) * dx + (py - ay) * dy + (pz - az) * dz) / len;
		t = std::clamp(t, 0., 1.);
	}
	return std::max({
		std::abs(px - (ax + t * dx)),
		std::abs(py - (ay + t * dy)),
		std::abs(pz - (az + t * dz)) });
}

[[nodiscard]] QColor RowMode(const QImage &image, QRect clip, int y) {
	auto counts = std::vector<std::pair<QRgb, int>>();
	for (auto x = clip.left(); x <= clip.right(); ++x) {
		const auto value = image.pixel(x, y);
		auto found = false;
		for (auto &one : counts) {
			if (one.first == value) {
				++one.second;
				found = true;
				break;
			}
		}
		if (!found && (counts.size() < 8192)) {
			counts.push_back({ value, 1 });
		}
	}
	auto best = QRgb(0);
	auto bestCount = -1;
	for (const auto &one : counts) {
		if (one.second > bestCount) {
			bestCount = one.second;
			best = one.first;
		}
	}
	return (bestCount > 0) ? QColor::fromRgb(best) : QColor();
}

[[nodiscard]] QRect PillBand(QRect box) {
	if (box.isEmpty()) {
		return box;
	}
	const auto radius = box.height() / 2;
	const auto inner = box.adjusted(radius, 1, -radius, -1);
	return (inner.width() >= 8) ? inner : box;
}

[[nodiscard]] QColor RegionMode(
		const QImage &image,
		QRect region,
		QRect exclude) {
	auto counts = std::vector<std::pair<QRgb, int>>();
	const auto clip = region.intersected(image.rect());
	for (auto y = clip.top(); y <= clip.bottom(); ++y) {
		for (auto x = clip.left(); x <= clip.right(); ++x) {
			if (exclude.contains(x, y)) {
				continue;
			}
			const auto value = image.pixel(x, y);
			auto found = false;
			for (auto &one : counts) {
				if (one.first == value) {
					++one.second;
					found = true;
					break;
				}
			}
			if (!found && (counts.size() < 8192)) {
				counts.push_back({ value, 1 });
			}
		}
	}
	auto best = QRgb(0);
	auto bestCount = -1;
	for (const auto &one : counts) {
		if (one.second > bestCount) {
			bestCount = one.second;
			best = one.first;
		}
	}
	return (bestCount > 0) ? QColor::fromRgb(best) : QColor();
}

[[nodiscard]] bool SurroundingsAreFill(
		const QImage &image,
		QRect clip,
		QColor fill) {
	const auto ring = clip.adjusted(-1, -1, 1, 1).intersected(image.rect());
	if (ring.isEmpty() || (ring == clip)) {
		return false;
	}
	const auto mode = RegionMode(image, ring, clip);
	return mode.isValid()
		&& (ChannelDelta(mode, fill) <= kBackgroundSame);
}

// Own-colour distance to its own segment is 0, so a candidate closer than
// kInkMargin to another candidate's segment can never be the unique
// attribution. kOnLine is wider and would also refuse pixels that still
// attribute. The background is the scan's modal row colour: a row that is
// mostly ink reports that ink as its own mode and would refuse a palette
// the band still classifies.
struct CollinearPair {
	int swallowed = -1;
	int swallower = -1;
	double distance = 0.;
};

[[nodiscard]] CollinearPair CollinearAgainstBackground(
		const std::vector<InkCandidate> &candidates,
		QColor background) {
	auto result = CollinearPair();
	if (!background.isValid()) {
		return result;
	}
	for (auto i = 0; i != int(candidates.size()); ++i) {
		for (auto j = 0; j != int(candidates.size()); ++j) {
			if (i == j) {
				continue;
			}
			const auto distance = SegmentDistance(
				candidates[i].color,
				background,
				candidates[j].color);
			if (distance >= kInkMargin) {
				continue;
			}
			if ((result.swallowed >= 0) && (distance >= result.distance)) {
				continue;
			}
			result.swallowed = i;
			result.swallower = j;
			result.distance = distance;
		}
	}
	return result;
}

} // namespace

int ChannelDelta(QColor a, QColor b) {
	if (!a.isValid() || !b.isValid()) {
		return -1;
	}
	return std::max({
		std::abs(a.red() - b.red()),
		std::abs(a.green() - b.green()),
		std::abs(a.blue() - b.blue()) });
}

QString InkScanStateName(InkScanState state) {
	switch (state) {
	case InkScanState::NotScanned:
		return u"not-scanned"_q;
	case InkScanState::OutsideImage:
		return u"outside-image"_q;
	case InkScanState::NoRowsInBand:
		return u"no-rows-in-band"_q;
	case InkScanState::CandidatesCollide:
		return u"candidates-collide"_q;
	case InkScanState::NoInk:
		return u"no-ink"_q;
	case InkScanState::Classified:
		return u"classified"_q;
	case InkScanState::BackgroundCollinear:
		return u"background-collinear"_q;
	}
	return u"missing"_q;
}

bool Separable(const std::vector<InkCandidate> &candidates) {
	for (auto i = 0; i != int(candidates.size()); ++i) {
		for (auto j = i + 1; j != int(candidates.size()); ++j) {
			if (ChannelDelta(candidates[i].color, candidates[j].color)
				<= kSameTolerance) {
				return false;
			}
		}
	}
	return true;
}

QString CollisionDump(const std::vector<InkCandidate> &candidates) {
	for (auto i = 0; i != int(candidates.size()); ++i) {
		for (auto j = i + 1; j != int(candidates.size()); ++j) {
			const auto delta = ChannelDelta(
				candidates[i].color,
				candidates[j].color);
			if (delta <= kSameTolerance) {
				return u"%1(%2) %3(%4) delta=%5"_q
					.arg(
						CandidateName(candidates[i]),
						ColorHex(candidates[i].color))
					.arg(
						CandidateName(candidates[j]),
						ColorHex(candidates[j].color))
					.arg(delta);
			}
		}
	}
	return u"none"_q;
}

InkCount InkScan::countAt(int index) const {
	auto result = InkCount();
	result.index = index;
	const auto size = int(candidates.size());
	const auto named = (index >= 0) && (index < size);
	const auto inRange = named && (index < int(counts.size()));
	if (named) {
		result.name = CandidateName(candidates[index]);
		result.color = candidates[index].color;
	}
	if ((state != InkScanState::Classified)
		&& (state != InkScanState::NoInk)) {
		result.refusal = u"index=%1 of %2: this reading (%3) classified "
			u"nothing: %4"_q
			.arg(index)
			.arg(size)
			.arg(InkScanStateName(state), ScanReasonText(*this));
		return result;
	}
	if (!inRange) {
		auto names = QString();
		for (const auto &candidate : candidates) {
			if (!names.isEmpty()) {
				names += u", "_q;
			}
			names += CandidateName(candidate);
		}
		result.refusal = named
			? u"index=%1 names a candidate this reading filled no count "
				u"for: %2 counts for %3 candidates [%4]"_q
				.arg(index)
				.arg(int(counts.size()))
				.arg(size)
				.arg(names)
			: u"index=%1 is outside this reading's %2 candidates [%3]"_q
				.arg(index)
				.arg(size)
				.arg(names);
		return result;
	}
	result.count = counts[index];
	return result;
}

QString InkCountDetails(const InkCount &reading) {
	const auto line = u"index=%1 name=%2 color=%3 count=%4"_q
		.arg(reading.index)
		.arg(
			reading.name.isEmpty() ? u"none"_q : reading.name,
			ColorHex(reading.color),
			reading.read() ? QString::number(reading.count) : u"none"_q);
	return reading.read()
		? line
		: (line + u" refusal=%1"_q.arg(reading.refusal));
}

DerivedBand DeriveBand(
		const QImage &image,
		QRect box,
		QColor fill) {
	// Keep rows whose modal background is the literal fill, take the columns
	// those rows actually span, inset by the pill radius so rounded caps and
	// the surface behind them are excluded, then re-assert the row background
	// inside that narrower band and drop every row that no longer reads as
	// the fill. A hit-box PillBand without this step inverted ink and fill.
	auto result = DerivedBand();
	const auto clip = box.intersected(image.rect());
	if (clip.isEmpty() || !fill.isValid()) {
		result.reason = u"the recovered box is empty or the fill is invalid"_q;
		return result;
	}
	result.rowsExamined = clip.height();
	auto rows = std::vector<int>();
	auto left = clip.right() + 1;
	auto right = clip.left() - 1;
	for (auto y = clip.top(); y <= clip.bottom(); ++y) {
		if (ChannelDelta(RowMode(image, clip, y), fill) > kBackgroundSame) {
			continue;
		}
		auto first = -1;
		auto last = -1;
		for (auto x = clip.left(); x <= clip.right(); ++x) {
			const auto delta = ChannelDelta(image.pixelColor(x, y), fill);
			if (delta <= kBackgroundSame) {
				if (first < 0) {
					first = x;
				}
				last = x;
			}
		}
		if (first < 0) {
			continue;
		}
		rows.push_back(y);
		left = std::min(left, first);
		right = std::max(right, last);
	}
	result.fillRows = int(rows.size());
	if (rows.empty() || (right < left)) {
		result.reason = u"no row of the recovered box has the pill fill "
			u"as its own background"_q;
		return result;
	}
	if (SurroundingsAreFill(image, clip, fill)) {
		result.reason = u"the requested fill is the image's background "
			u"outside the candidate, so no band can be derived"_q;
		return result;
	}
	result.fillRegion = QRect(
		left,
		rows.front(),
		right - left + 1,
		rows.back() - rows.front() + 1);
	result.band = PillBand(result.fillRegion);
	for (const auto y : rows) {
		if ((y < result.band.top()) || (y > result.band.bottom())) {
			continue;
		}
		if (ChannelDelta(RowMode(image, result.band, y), fill)
			<= kBackgroundSame) {
			result.rows.push_back(y);
		}
	}
	if (result.rows.empty()) {
		result.reason = u"no row of the derived band kept the pill fill "
			u"as its own background"_q;
		return result;
	}
	result.reason = u"none"_q;
	result.ok = true;
	return result;
}

InkScan ScanInk(
		const QImage &image,
		QRect band,
		std::vector<InkCandidate> candidates,
		const std::vector<int> &onlyRows) {
	auto result = InkScan();
	result.candidates = std::move(candidates);
	result.counts.assign(result.candidates.size(), 0);
	const auto clip = band.intersected(image.rect());
	if (clip.isEmpty()) {
		result.state = InkScanState::OutsideImage;
		result.reason = u"the requested band [%1] does not intersect the "
			u"image [%2], so no pixel was scanned"_q
			.arg(RectText(band), RectText(image.rect()));
		return result;
	}
	result.band = clip;
	auto rows = std::vector<int>();
	if (onlyRows.empty()) {
		rows.reserve(clip.height());
		for (auto y = clip.top(); y <= clip.bottom(); ++y) {
			rows.push_back(y);
		}
	} else {
		rows.reserve(onlyRows.size());
		for (const auto y : onlyRows) {
			if ((y >= clip.top()) && (y <= clip.bottom())) {
				rows.push_back(y);
			}
		}
	}
	if (rows.empty()) {
		result.state = InkScanState::NoRowsInBand;
		result.reason = u"none of the %1 requested rows falls inside the "
			u"band [%2], so no pixel was scanned"_q
			.arg(int(onlyRows.size()))
			.arg(RectText(clip));
		return result;
	}
	result.ok = true;
	result.scannedRows = int(rows.size());

	auto rowBackground = std::vector<QColor>();
	rowBackground.reserve(rows.size());
	for (const auto y : rows) {
		rowBackground.push_back(RowMode(image, clip, y));
	}
	auto backgroundCounts = std::vector<std::pair<QRgb, int>>();
	for (const auto &one : rowBackground) {
		auto found = false;
		for (auto &entry : backgroundCounts) {
			if (entry.first == one.rgb()) {
				++entry.second;
				found = true;
				break;
			}
		}
		if (!found) {
			backgroundCounts.push_back({ one.rgb(), 1 });
		}
	}
	auto bestBackground = -1;
	for (const auto &entry : backgroundCounts) {
		if (entry.second > bestBackground) {
			bestBackground = entry.second;
			result.background = QColor::fromRgb(entry.first);
		}
	}

	const auto classify = Separable(result.candidates);
	auto inkSum = std::vector<std::pair<QRgb, int>>();
	auto classifiedSum = std::vector<std::pair<QRgb, int>>();
	for (auto index = 0; index != int(rows.size()); ++index) {
		const auto y = rows[index];
		const auto background = rowBackground[index];
		auto run = 0;
		for (auto x = clip.left(); x <= clip.right(); ++x) {
			const auto color = image.pixelColor(x, y);
			const auto delta = ChannelDelta(color, background);
			++result.total;
			if (delta <= kBackgroundSame) {
				++result.backgroundPixels;
			}
			run = (delta >= kInkDelta) ? (run + 1) : 0;
			if (run > result.widestRun) {
				result.widestRun = run;
				result.widestRunRow = y - clip.top();
			}
			if (delta < kInkDelta) {
				continue;
			}
			++result.inkPixels;
			auto found = false;
			for (auto &one : inkSum) {
				if (one.first == color.rgb()) {
					++one.second;
					found = true;
					break;
				}
			}
			if (!found && (inkSum.size() < 8192)) {
				inkSum.push_back({ color.rgb(), 1 });
			}
			if (!classify) {
				++result.ambiguous;
				continue;
			}
			auto best = -1;
			auto bestDistance = 1e9;
			auto secondDistance = 1e9;
			for (auto i = 0; i != int(result.candidates.size()); ++i) {
				const auto distance = SegmentDistance(
					color,
					background,
					result.candidates[i].color);
				if (distance < bestDistance) {
					secondDistance = bestDistance;
					bestDistance = distance;
					best = i;
				} else if (distance < secondDistance) {
					secondDistance = distance;
				}
			}
			const auto lone = (result.candidates.size() < 2);
			if ((best >= 0)
				&& (bestDistance <= kOnLine)
				&& (lone || (secondDistance - bestDistance >= kInkMargin))) {
				++result.counts[best];
				auto seen = false;
				for (auto &one : classifiedSum) {
					if (one.first == color.rgb()) {
						++one.second;
						seen = true;
						break;
					}
				}
				if (!seen && (classifiedSum.size() < 8192)) {
					classifiedSum.push_back({ color.rgb(), 1 });
				}
			} else {
				++result.ambiguous;
			}
		}
	}
	auto bestInk = -1;
	for (const auto &one : inkSum) {
		if (one.second > bestInk) {
			bestInk = one.second;
			result.ink = QColor::fromRgb(one.first);
		}
	}
	auto bestClassified = -1;
	for (const auto &one : classifiedSum) {
		if (one.second > bestClassified) {
			bestClassified = one.second;
			result.classifiedInk = QColor::fromRgb(one.first);
		}
	}
	const auto collinear = CollinearAgainstBackground(
		result.candidates,
		result.background);
	if (!classify) {
		result.state = InkScanState::CandidatesCollide;
		result.reason = u"the candidates do not separate from each other, "
			u"so no pixel is attributed to one: %1 kSameTolerance=%2"_q
			.arg(
				CollisionDump(result.candidates),
				QString::number(kSameTolerance));
	} else if (!result.inkPixels) {
		result.state = InkScanState::NoInk;
		result.reason = u"no pixel of the band [%1] separated from its "
			u"background %2: total=%3 kInkDelta=%4"_q
			.arg(
				RectText(clip),
				ColorHex(result.background),
				QString::number(result.total),
				QString::number(kInkDelta));
	} else if (collinear.swallowed >= 0) {
		const auto &swallowed = result.candidates[collinear.swallowed];
		const auto &swallower = result.candidates[collinear.swallower];
		result.state = InkScanState::BackgroundCollinear;
		result.reason = u"candidate "_q
			+ CandidateName(swallowed)
			+ u"("_q
			+ ColorHex(swallowed.color)
			+ u") lies on the segment from the measured background "_q
			+ ColorHex(result.background)
			+ u" to candidate "_q
			+ CandidateName(swallower)
			+ u"("_q
			+ ColorHex(swallower.color)
			+ u"), so none of its pixels can be attributed: distance="_q
			+ QString::number(collinear.distance)
			+ u" kInkMargin="_q
			+ QString::number(kInkMargin);
	} else {
		result.state = InkScanState::Classified;
		result.reason = u"none"_q;
	}
	return result;
}

float64 InkContrast(QColor a, QColor b) {
	if (!a.isValid() || !b.isValid()) {
		return 0.;
	}
	return Ui::CountContrast(a, b);
}

QString FormatInkReport(
		QColor fill,
		QColor ink,
		float64 contrast,
		int inkPixels) {
	return u"fill=%1 ink=%2 contrast=%3 inkPx=%4"_q
		.arg(ColorHex(fill), ColorHex(ink))
		.arg(contrast)
		.arg(inkPixels);
}

QString FormatInkScan(const InkScan &scan, QColor fill) {
	// Every clause is resolved into its own fragment and the fragments are
	// concatenated, never chained as one long .arg over a single format
	// string: a chained .arg rescans text an earlier .arg already
	// substituted, so a candidate name or a reason carrying its own
	// %<digit> would be eaten by the next call in the chain.
	const auto report = FormatInkReport(
		fill,
		scan.ink,
		InkContrast(scan.ink, scan.background),
		scan.inkPixels);
	const auto measured = u" state=%1 band=%2 bg=%3 classifiedInk=%4"_q
		.arg(
			InkScanStateName(scan.state),
			RectText(scan.band),
			ColorHex(scan.background),
			ColorHex(scan.classifiedInk));
	const auto counted = u" total=%1 ambiguous=%2 rows=%3"_q
		.arg(scan.total)
		.arg(scan.ambiguous)
		.arg(scan.scannedRows);
	auto listed = QString();
	for (auto i = 0; i != int(scan.candidates.size()); ++i) {
		const auto reading = scan.countAt(i);
		if (!listed.isEmpty()) {
			listed += u" "_q;
		}
		listed += u"%1(%2)=%3"_q
			.arg(
				reading.name,
				ColorHex(reading.color),
				reading.read()
					? QString::number(reading.count)
					: u"none"_q);
	}
	const auto against = u" candidates=[%1] collision=%2"_q
		.arg(listed, CollisionDump(scan.candidates));
	const auto thresholds = u" kInkDelta=%1 kOnLine=%2 kInkMargin=%3 "
		u"kSameTolerance=%4 kBackgroundSame=%5"_q
		.arg(kInkDelta)
		.arg(kOnLine)
		.arg(kInkMargin)
		.arg(kSameTolerance)
		.arg(kBackgroundSame);
	return report
		+ measured
		+ counted
		+ against
		+ thresholds
		+ u" reason=%1"_q.arg(ScanReasonText(scan));
}

InkMeasure MeasurePaintedInk(
		const QImage &image,
		QRect box,
		QColor fill,
		std::vector<InkCandidate> candidates) {
	auto result = InkMeasure();
	result.derived = DeriveBand(image, box, fill);
	result.separable = Separable(candidates);
	result.collision = CollisionDump(candidates);
	if (result.derived.ok) {
		result.scan = ScanInk(
			image,
			result.derived.band,
			std::move(candidates),
			result.derived.rows);
		result.contrast = InkContrast(
			result.scan.ink,
			result.scan.background);
		result.report = FormatInkScan(result.scan, fill);
	} else {
		result.report = result.derived.reason;
	}
	return result;
}

namespace {

[[nodiscard]] int ChromaticSpread(QColor color) {
	if (!color.isValid()) {
		return 0;
	}
	const auto high = std::max({ color.red(), color.green(), color.blue() });
	const auto low = std::min({ color.red(), color.green(), color.blue() });
	return high - low;
}

[[nodiscard]] bool ExcludedColor(QColor color, QColor excluded) {
	return excluded.isValid()
		&& (ChannelDelta(color, excluded) <= kSameTolerance);
}

[[nodiscard]] QString ClusterRefusal(const ChromaticRaster &reading) {
	if (reading.state == ChromaticRasterState::Found) {
		return {};
	}
	const auto reason = reading.reason.isEmpty()
		? u"this reading was never scanned"_q
		: reading.reason;
	return u"this reading (%1) found no chromatic cluster: %2"_q
		.arg(ChromaticRasterStateName(reading.state), reason);
}

} // namespace

QString ChromaticRasterStateName(ChromaticRasterState state) {
	switch (state) {
	case ChromaticRasterState::NotScanned:
		return u"not-scanned"_q;
	case ChromaticRasterState::OutsideBand:
		return u"raster-outside-band"_q;
	case ChromaticRasterState::NoChromatic:
		return u"no-chromatic"_q;
	case ChromaticRasterState::BelowDensity:
		return u"below-density"_q;
	case ChromaticRasterState::Found:
		return u"found"_q;
	}
	return u"missing"_q;
}

ChromaticBox ChromaticRaster::readBox() const {
	auto result = ChromaticBox();
	const auto refusal = ClusterRefusal(*this);
	if (!refusal.isEmpty()) {
		result.refusal = refusal;
		return result;
	}
	result.box = box;
	return result;
}

ChromaticMean ChromaticRaster::readMean() const {
	auto result = ChromaticMean();
	const auto refusal = ClusterRefusal(*this);
	if (!refusal.isEmpty()) {
		result.refusal = refusal;
		return result;
	}
	result.color = mean;
	return result;
}

ChromaticDensity ChromaticRaster::readDensity() const {
	auto result = ChromaticDensity();
	const auto refusal = ClusterRefusal(*this);
	if (!refusal.isEmpty()) {
		result.refusal = refusal;
		return result;
	}
	result.density = density;
	return result;
}

ChromaticRaster ReadChromaticRaster(
		const QImage &image,
		QRect band,
		QColor pen,
		QColor fill) {
	auto result = ChromaticRaster();
	const auto clip = band.intersected(image.rect());
	if (clip.isEmpty()) {
		result.state = ChromaticRasterState::OutsideBand;
		result.reason = u"the requested band ["_q
			+ RectText(band)
			+ u"] does not intersect the image ["_q
			+ RectText(image.rect())
			+ u"], so no chromatic pixel was read"_q;
		return result;
	}
	result.ok = true;
	result.band = clip;
	auto points = std::vector<QPoint>();
	for (auto y = clip.top(); y <= clip.bottom(); ++y) {
		for (auto x = clip.left(); x <= clip.right(); ++x) {
			const auto color = image.pixelColor(x, y);
			if (ChromaticSpread(color) < kChromaticFloor) {
				continue;
			}
			if (ExcludedColor(color, pen) || ExcludedColor(color, fill)) {
				continue;
			}
			points.push_back(QPoint(x, y));
		}
	}
	result.totalChromatic = int(points.size());
	if (points.empty()) {
		result.state = ChromaticRasterState::NoChromatic;
		result.reason = u"no pixel of the band ["_q
			+ RectText(clip)
			+ u"] cleared the chromatic floor "_q
			+ QString::number(kChromaticFloor);
		return result;
	}
	auto top = points.front().y();
	auto bottom = top;
	auto left = points.front().x();
	auto right = left;
	for (const auto &point : points) {
		top = std::min(top, point.y());
		bottom = std::max(bottom, point.y());
		left = std::min(left, point.x());
		right = std::max(right, point.x());
	}
	const auto origin = clip.left();
	const auto columns = clip.width();
	auto columnCount = std::vector<int>(columns, 0);
	for (const auto &point : points) {
		++columnCount[point.x() - origin];
	}
	auto prefix = std::vector<int>(columns + 1, 0);
	for (auto i = 0; i != columns; ++i) {
		prefix[i + 1] = prefix[i] + columnCount[i];
	}
	const auto window = std::max(bottom - top + 1, 1);
	auto bestCount = -1;
	auto bestX = left;
	const auto lastStart = std::max(left, right - window + 1);
	for (auto start = left; start <= lastStart; ++start) {
		const auto from = start - origin;
		const auto to = std::min(columns, from + window);
		if ((from < 0) || (to < from)) {
			continue;
		}
		const auto count = prefix[to] - prefix[from];
		if (count > bestCount) {
			bestCount = count;
			bestX = start;
		}
	}
	auto boxLeft = bestX + window;
	auto boxRight = bestX - 1;
	auto boxTop = bottom + 1;
	auto boxBottom = top - 1;
	auto sumR = qint64(0);
	auto sumG = qint64(0);
	auto sumB = qint64(0);
	auto matched = 0;
	for (const auto &point : points) {
		if ((point.x() < bestX) || (point.x() >= bestX + window)) {
			continue;
		}
		boxLeft = std::min(boxLeft, point.x());
		boxRight = std::max(boxRight, point.x());
		boxTop = std::min(boxTop, point.y());
		boxBottom = std::max(boxBottom, point.y());
		const auto color = image.pixelColor(point);
		sumR += color.red();
		sumG += color.green();
		sumB += color.blue();
		++matched;
	}
	if (matched <= 0) {
		result.state = ChromaticRasterState::NoChromatic;
		result.reason = u"no pixel of the band ["_q
			+ RectText(clip)
			+ u"] cleared the chromatic floor "_q
			+ QString::number(kChromaticFloor);
		return result;
	}
	const auto boxWidth = boxRight - boxLeft + 1;
	const auto boxHeight = boxBottom - boxTop + 1;
	const auto area = std::max(boxWidth * boxHeight, 1);
	const auto density = float64(matched) / float64(area);
	if (density >= kChromaticDensityFloor) {
		result.state = ChromaticRasterState::Found;
		result.reason = u"none"_q;
		result.box = QRect(boxLeft, boxTop, boxWidth, boxHeight);
		result.matched = matched;
		result.density = density;
		result.mean = QColor(
			int(sumR / matched),
			int(sumG / matched),
			int(sumB / matched));
	} else {
		result.state = ChromaticRasterState::BelowDensity;
		result.reason = u"the densest chromatic window stays under the "
			"density floor: total="_q
			+ QString::number(result.totalChromatic)
			+ u" floor="_q
			+ QString::number(kChromaticDensityFloor);
	}
	return result;
}

QString FormatChromaticRaster(const ChromaticRaster &reading) {
	const auto box = reading.readBox();
	const auto mean = reading.readMean();
	const auto density = reading.readDensity();
	const auto reason = reading.reason.isEmpty()
		? u"this reading was never scanned"_q
		: reading.reason;
	return u"state="_q
		+ ChromaticRasterStateName(reading.state)
		+ u" band="_q
		+ RectText(reading.band)
		+ u" box="_q
		+ (box.read() ? RectText(box.box) : u"none"_q)
		+ u" matched="_q
		+ (box.read() ? QString::number(reading.matched) : u"none"_q)
		+ u" totalChromatic="_q
		+ (reading.ok
			? QString::number(reading.totalChromatic)
			: u"none"_q)
		+ u" density="_q
		+ (density.read() ? QString::number(density.density) : u"none"_q)
		+ u" mean="_q
		+ (mean.read() ? ColorHex(mean.color) : u"none"_q)
		+ u" reason="_q
		+ reason;
}

namespace {

[[nodiscard]] QString GlyphReason(const GlyphCore &reading) {
	return reading.reason.isEmpty()
		? u"this reading was never scanned"_q
		: reading.reason;
}

} // namespace

QString GlyphCoreStateName(GlyphCoreState state) {
	switch (state) {
	case GlyphCoreState::Unread:
		return u"core-unread"_q;
	case GlyphCoreState::OutsideBand:
		return u"core-outside-band"_q;
	case GlyphCoreState::NoPaint:
		return u"no-paint"_q;
	case GlyphCoreState::NoSolidCore:
		return u"no-solid-core"_q;
	case GlyphCoreState::Measured:
		return u"core-measured"_q;
	}
	return u"missing"_q;
}

GlyphCoreModal GlyphCore::readModal() const {
	auto result = GlyphCoreModal();
	if (state != GlyphCoreState::Measured) {
		result.refusal = u"this reading ("_q
			+ GlyphCoreStateName(state)
			+ u") measured no solid core: "_q
			+ GlyphReason(*this);
		return result;
	}
	result.color = modal;
	result.count = modalCount;
	result.cores = cores;
	return result;
}

GlyphCoreSolid GlyphCore::solidAt(int index) const {
	auto result = GlyphCoreSolid();
	result.index = index;
	if (state != GlyphCoreState::Measured) {
		result.refusal = u"index="_q
			+ QString::number(index)
			+ u" of "_q
			+ QString::number(int(pens.size()))
			+ u": this reading ("_q
			+ GlyphCoreStateName(state)
			+ u") measured no solid core: "_q
			+ GlyphReason(*this);
		return result;
	}
	if ((index < 0) || (index >= int(pens.size()))) {
		result.refusal = u"index="_q
			+ QString::number(index)
			+ u" is outside this reading's "_q
			+ QString::number(int(pens.size()))
			+ u" pens"_q;
		return result;
	}
	if (index >= int(solid.size())) {
		result.refusal = u"index="_q
			+ QString::number(index)
			+ u" names a pen this reading filled no solid count for"_q;
		return result;
	}
	result.count = solid[index];
	return result;
}

GlyphCore ReadGlyphCore(
		const QImage &image,
		QRect band,
		std::vector<InkCandidate> pens) {
	auto result = GlyphCore();
	result.pens = std::move(pens);
	result.solid.assign(result.pens.size(), -1);
	const auto clip = band.intersected(image.rect());
	if (clip.isEmpty()) {
		result.state = GlyphCoreState::OutsideBand;
		result.reason = u"the requested band lies outside the image, so "
			"no glyph pixel was read: band="_q
			+ RectText(band)
			+ u" image="_q
			+ RectText(image.rect());
		return result;
	}
	result.ok = true;
	result.band = clip;
	auto backgroundCounts = std::vector<std::pair<QRgb, int>>();
	for (auto y = clip.top(); y <= clip.bottom(); ++y) {
		for (auto x = clip.left(); x <= clip.right(); ++x) {
			const auto color = image.pixelColor(x, y);
			const auto rgb = qRgb(color.red(), color.green(), color.blue());
			++result.total;
			auto found = false;
			for (auto &one : backgroundCounts) {
				if (one.first == rgb) {
					++one.second;
					found = true;
					break;
				}
			}
			if (!found && (backgroundCounts.size() < 8192)) {
				backgroundCounts.push_back({ rgb, 1 });
			}
		}
	}
	auto bestBackground = -1;
	auto backgroundRgb = QRgb(0);
	for (const auto &one : backgroundCounts) {
		if (one.second > bestBackground) {
			bestBackground = one.second;
			backgroundRgb = one.first;
		}
	}
	result.background = QColor::fromRgb(backgroundRgb);
	auto strongest = 0;
	auto ink = std::vector<QRgb>();
	for (auto y = clip.top(); y <= clip.bottom(); ++y) {
		for (auto x = clip.left(); x <= clip.right(); ++x) {
			const auto color = image.pixelColor(x, y);
			const auto rgb = qRgb(color.red(), color.green(), color.blue());
			const auto delta = ChannelDelta(
				QColor::fromRgb(rgb),
				result.background);
			strongest = std::max(strongest, delta);
			if (delta >= kInkDelta) {
				ink.push_back(rgb);
			}
		}
	}
	result.strongest = strongest;
	result.inkPixels = int(ink.size());
	if (ink.empty()) {
		result.state = GlyphCoreState::NoPaint;
		result.reason = u"the measured background fills the band, so there "
			"is no glyph ink to read a pen from"_q;
		return result;
	}
	const int base[3] = {
		result.background.red(),
		result.background.green(),
		result.background.blue(),
	};
	int extreme[3] = { base[0], base[1], base[2] };
	int bestAbs[3] = { -1, -1, -1 };
	for (const auto rgb : ink) {
		const int value[3] = { qRed(rgb), qGreen(rgb), qBlue(rgb) };
		for (auto channel = 0; channel != 3; ++channel) {
			const auto distance = std::abs(value[channel] - base[channel]);
			if (distance > bestAbs[channel]) {
				bestAbs[channel] = distance;
				extreme[channel] = value[channel];
			}
		}
	}
	const auto extremeColor = QColor(extreme[0], extreme[1], extreme[2]);
	auto cores = std::vector<QRgb>();
	for (const auto rgb : ink) {
		if (ChannelDelta(QColor::fromRgb(rgb), extremeColor)
			<= kGlyphCoreTolerance) {
			cores.push_back(rgb);
		}
	}
	if (cores.empty()) {
		result.state = GlyphCoreState::NoSolidCore;
		result.reason = u"the band has glyph ink but every ink pixel is a "
			"fringe or partial coverage, so no solid core decides the pen"_q;
		return result;
	}
	auto coreCounts = std::vector<std::pair<QRgb, int>>();
	for (const auto rgb : cores) {
		auto found = false;
		for (auto &one : coreCounts) {
			if (one.first == rgb) {
				++one.second;
				found = true;
				break;
			}
		}
		if (!found && (coreCounts.size() < 8192)) {
			coreCounts.push_back({ rgb, 1 });
		}
	}
	auto bestCore = -1;
	auto modalRgb = cores.front();
	for (const auto &one : coreCounts) {
		if (one.second > bestCore) {
			bestCore = one.second;
			modalRgb = one.first;
		}
	}
	result.modal = QColor::fromRgb(modalRgb);
	result.modalCount = bestCore;
	result.cores = int(cores.size());
	result.state = GlyphCoreState::Measured;
	result.reason = u"none"_q;
	for (auto i = 0; i != int(result.pens.size()); ++i) {
		auto count = 0;
		for (const auto rgb : cores) {
			if (ChannelDelta(QColor::fromRgb(rgb), result.pens[i].color)
				<= kGlyphCoreTolerance) {
				++count;
			}
		}
		result.solid[i] = count;
	}
	return result;
}

QString FormatGlyphCore(const GlyphCore &reading) {
	const auto measured = (reading.state == GlyphCoreState::Measured);
	const auto reason = GlyphReason(reading);
	const auto background = reading.background.isValid()
		? ColorHex(reading.background)
		: u"none"_q;
	const auto strongest = (reading.strongest < 0)
		? u"none"_q
		: QString::number(reading.strongest);
	const auto cores = measured
		? QString::number(reading.cores)
		: u"none"_q;
	const auto modal = (measured && reading.modal.isValid())
		? ColorHex(reading.modal)
		: u"none"_q;
	const auto share = measured
		? (QString::number(reading.modalCount)
			+ u"/"_q
			+ QString::number(reading.cores))
		: u"none"_q;
	auto listed = QString();
	for (auto i = 0; i != int(reading.pens.size()); ++i) {
		auto name = reading.pens[i].name;
		if (name.isEmpty()) {
			name = u"pen"_q + QString::number(i);
		}
		const auto solid = reading.solidAt(i);
		if (!listed.isEmpty()) {
			listed += u" "_q;
		}
		listed += name
			+ u"="_q
			+ (solid.read() ? QString::number(solid.count) : u"none"_q);
	}
	auto text = u"state="_q
		+ GlyphCoreStateName(reading.state)
		+ u" band="_q
		+ RectText(reading.band)
		+ u" bg="_q
		+ background
		+ u" kGlyphCoreTolerance="_q
		+ QString::number(kGlyphCoreTolerance)
		+ u" strongest="_q
		+ strongest
		+ u" cores="_q
		+ cores
		+ u" modal="_q
		+ modal
		+ u" share="_q
		+ share;
	if (!listed.isEmpty()) {
		text += u" "_q + listed;
	}
	return text + u" reason="_q + reason;
}

} // namespace Test

#endif // _DEBUG
