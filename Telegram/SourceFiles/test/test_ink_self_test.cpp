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

void AppendBackgroundCollinearSelfTest(not_null<Runner*> runner) {
	runner->add({
		.name = u"ink scan self-test: a background-collinear candidate "
			"is refused, and a separated control still classifies"_q,
		.run = [] {
			const auto dayFill = QColor(0xf1, 0xf1, 0xf1);
			const auto controlFill = QColor(0x20, 0x40, 0x80);
			const auto sub = QColor(0x99, 0x99, 0x99);
			const auto fg = QColor(0x00, 0x00, 0x00);
			const auto size = QSize(64, 32);
			const auto band = QRect(8, 8, 48, 12);
			const auto candidates = std::vector<InkCandidate>{
				{ u"sub"_q, sub },
				{ u"fg"_q, fg },
			};
			const auto colliding = std::vector<InkCandidate>{
				{ u"mark"_q, QColor(0xff, 0xff, 0xff) },
				{ u"mark-again"_q, QColor(0xf5, 0xf5, 0xf5) },
			};
			auto refusedImage = QImage(
				size,
				QImage::Format_ARGB32_Premultiplied);
			refusedImage.fill(dayFill);
			{
				auto p = QPainter(&refusedImage);
				p.fillRect(QRect(16, 8, 8, 12), sub);
			}
			auto controlImage = QImage(
				size,
				QImage::Format_ARGB32_Premultiplied);
			controlImage.fill(controlFill);
			{
				auto p = QPainter(&controlImage);
				p.fillRect(QRect(16, 8, 6, 12), sub);
				p.fillRect(QRect(32, 8, 4, 12), fg);
			}
			const auto refused = ScanInk(refusedImage, band, candidates);
			const auto control = ScanInk(controlImage, band, candidates);
			const auto collided = ScanInk(refusedImage, band, colliding);
			const auto subCount = refused.countAt(0);
			const auto fgCount = refused.countAt(1);
			const auto controlSub = control.countAt(0);
			const auto controlFg = control.countAt(1);
			const auto refusedText = FormatInkScan(refused, dayFill);
			const auto controlText = FormatInkScan(control, controlFill);
			const auto forbidden = std::vector<QString>{
				u"candidates-collide"_q,
				u"no-ink"_q,
				u"outside-image"_q,
				u"no-rows-in-band"_q,
				u"the recovered box is empty or the fill is invalid"_q,
				u"no row of the recovered box has the pill fill "
					"as its own background"_q,
				u"the requested fill is the image's background "
					"outside the candidate, so no band can be derived"_q,
				u"no row of the derived band kept the pill fill "
					"as its own background"_q,
				u"the candidates do not separate from each other"_q,
				u"no pixel of the band"_q,
				u"does not intersect the image"_q,
			};
			auto distinct = (InkScanStateName(refused.state)
				== u"background-collinear"_q);
			auto quoted = QString();
			for (const auto &one : forbidden) {
				if (refused.reason.contains(one)
					|| (InkScanStateName(refused.state) == one)) {
					distinct = false;
				}
				if (!quoted.isEmpty()) {
					quoted += u"; "_q;
				}
				quoted += one;
			}
			const auto thresholds = std::vector<QString>{
				u"kInkDelta=%1"_q.arg(kInkDelta),
				u"kOnLine=%1"_q.arg(kOnLine),
				u"kInkMargin=%1"_q.arg(kInkMargin),
				u"kSameTolerance=%1"_q.arg(kSameTolerance),
				u"kBackgroundSame=%1"_q.arg(kBackgroundSame),
			};
			auto thresholdsNamed = true;
			for (const auto &one : thresholds) {
				if (!controlText.contains(one)) {
					thresholdsNamed = false;
				}
			}
			Note(u"ink scan self-test: no window, session, chats list, "
				"network, account or wallet - two synthetic images"_q);
			Check(
				refused.ok
					&& (refused.state
						== InkScanState::BackgroundCollinear)
					&& (refused.state != InkScanState::Classified)
					&& (refused.reason != u"none"_q)
					&& refused.reason.contains(u"sub"_q)
					&& refused.reason.contains(u"fg"_q)
					&& refused.reason.contains(ColorHex(sub))
					&& refused.reason.contains(ColorHex(fg))
					&& refused.reason.contains(ColorHex(dayFill))
					&& (refused.ambiguous == refused.inkPixels)
					&& (refused.inkPixels > 0),
				u"a candidate on the segment from the measured background "
				"to another candidate is refused by name, not classified"_q,
				refusedText);
			Check(
				!subCount.read()
					&& (subCount.count == -1)
					&& !fgCount.read()
					&& (fgCount.count == -1)
					&& InkCountDetails(subCount).contains(u"count=none"_q)
					&& InkCountDetails(fgCount).contains(u"count=none"_q)
					&& InkCountDetails(subCount).contains(
						u"background-collinear"_q),
				u"countAt on that reading refuses both candidates with "
				"count=none"_q,
				InkCountDetails(subCount)
					+ u" | "_q
					+ InkCountDetails(fgCount));
			Check(
				control.ok
					&& (control.state == InkScanState::Classified)
					&& (control.reason == u"none"_q)
					&& controlSub.read()
					&& (controlSub.count == 72)
					&& controlFg.read()
					&& (controlFg.count == 48),
				u"the same two candidates against a background that does "
				"not put one on the other's segment still classify"_q,
				controlText
					+ u" | "_q
					+ InkCountDetails(controlSub)
					+ u" | "_q
					+ InkCountDetails(controlFg));
			Check(
				distinct,
				u"the background-collinear name and reason differ from "
				"candidates-collide, no-ink, outside-image, "
				"no-rows-in-band and every DeriveBand reason"_q,
				u"state=%1 reason=%2 forbidden=[%3]"_q
					.arg(
						InkScanStateName(refused.state),
						refused.reason,
						quoted));
			Check(
				refusedText.contains(ColorHex(sub))
					&& refusedText.contains(ColorHex(fg))
					&& refusedText.contains(ColorHex(dayFill))
					&& refusedText.contains(u"sub"_q)
					&& refusedText.contains(u"fg"_q)
					&& controlText.startsWith(u"fill="_q)
					&& controlText.contains(
						u"sub(%1)=72"_q.arg(ColorHex(sub)))
					&& controlText.contains(
						u"fg(%1)=48"_q.arg(ColorHex(fg)))
					&& controlText.contains(u"collision=none"_q)
					&& !controlText.contains(u"background-collinear"_q)
					&& thresholdsNamed,
				u"the refusing format names both colours and the measured "
				"background, and the control format keeps its counts and "
				"thresholds"_q,
				refusedText + u" || "_q + controlText);
			Check(
				collided.ok
					&& (collided.state == InkScanState::CandidatesCollide)
					&& collided.reason.contains(
						u"the candidates do not separate from each other"_q)
					&& (collided.state
						!= InkScanState::BackgroundCollinear),
				u"a pair that already collides under Separable keeps "
				"CandidatesCollide and its existing text"_q,
				FormatInkScan(collided, dayFill));
		},
	});
}

void AppendChromaticRasterSelfTest(not_null<Runner*> runner) {
	runner->add({
		.name = u"chromatic raster self-test: a dense mark, an equal-count "
			"smear, and a flat fill"_q,
		.run = [] {
			const auto pen = QColor(0x11, 0x11, 0x11);
			const auto fill = QColor(0xe8, 0xe4, 0xdc);
			const auto red = QColor(0xff, 0x00, 0x00);
			const auto blue = QColor(0x00, 0x00, 0xff);
			const auto mark = QRect(8, 8, 16, 16);
			const auto band = QRect(0, 0, 96, 40);
			auto denseImage = QImage(
				QSize(96, 40),
				QImage::Format_ARGB32_Premultiplied);
			denseImage.fill(fill);
			auto reds = 0;
			auto blues = 0;
			for (auto y = mark.top(); y <= mark.bottom(); ++y) {
				for (auto x = mark.left(); x <= mark.right(); ++x) {
					const auto color = ((x + y) % 2) ? blue : red;
					denseImage.setPixelColor(x, y, color);
					if (color == red) {
						++reds;
					} else {
						++blues;
					}
				}
			}
			const auto painted = reds + blues;
			const auto expectedMean = QColor(
				int((qint64(red.red()) * reds
					+ qint64(blue.red()) * blues) / painted),
				int((qint64(red.green()) * reds
					+ qint64(blue.green()) * blues) / painted),
				int((qint64(red.blue()) * reds
					+ qint64(blue.blue()) * blues) / painted));
			auto sparseImage = QImage(
				QSize(96, 40),
				QImage::Format_ARGB32_Premultiplied);
			sparseImage.fill(fill);
			auto sparsePainted = 0;
			for (auto y = 8; y < 24; ++y) {
				for (auto x = 8; x < 72; ++x) {
					if ((x % 4) != 0) {
						continue;
					}
					const auto color = ((x + y) % 2) ? blue : red;
					sparseImage.setPixelColor(x, y, color);
					++sparsePainted;
				}
			}
			auto flatImage = QImage(
				QSize(96, 40),
				QImage::Format_ARGB32_Premultiplied);
			flatImage.fill(fill);
			const auto dense = ReadChromaticRaster(
				denseImage,
				band,
				pen,
				fill);
			const auto sparse = ReadChromaticRaster(
				sparseImage,
				band,
				pen,
				fill);
			const auto flat = ReadChromaticRaster(
				flatImage,
				band,
				pen,
				fill);
			const auto outside = ReadChromaticRaster(
				flatImage,
				QRect(200, 200, 8, 8),
				pen,
				fill);
			const auto box = dense.readBox();
			const auto mean = dense.readMean();
			const auto density = dense.readDensity();
			const auto sparseBox = sparse.readBox();
			const auto sparseMean = sparse.readMean();
			const auto sparseDensity = sparse.readDensity();
			const auto flatBox = flat.readBox();
			const auto flatMean = flat.readMean();
			const auto flatDensity = flat.readDensity();
			const auto denseText = FormatChromaticRaster(dense);
			const auto sparseText = FormatChromaticRaster(sparse);
			const auto flatText = FormatChromaticRaster(flat);
			const auto outsideText = FormatChromaticRaster(outside);
			Note(u"chromatic raster self-test: no window, session, chats "
				"list, network, account or wallet - three synthetic "
				"images"_q);
			Check(
				dense.found()
					&& box.read()
					&& (box.box == mark)
					&& (box.box != band)
					&& band.contains(box.box)
					&& density.read()
					&& (density.density >= kChromaticDensityFloor)
					&& mean.read()
					&& (ColorHex(mean.color) == ColorHex(expectedMean))
					&& (ColorHex(mean.color) != ColorHex(pen))
					&& (ColorHex(mean.color) != ColorHex(fill))
					&& (painted == 256),
				u"a dense two-colour mark is found at its own box, with "
				"the mean of its ink and neither colour the check passed "
				"in"_q,
				denseText
					+ u" expectedMean="_q
					+ ColorHex(expectedMean)
					+ u" pen="_q
					+ ColorHex(pen)
					+ u" fill="_q
					+ ColorHex(fill));
			Check(
				(dense.totalChromatic == sparse.totalChromatic)
					&& (dense.totalChromatic == sparsePainted)
					&& (dense.totalChromatic > 0)
					&& (sparse.state == ChromaticRasterState::BelowDensity)
					&& !sparse.found()
					&& !sparseBox.read()
					&& sparseBox.box.isEmpty(),
				u"an equal chromatic count spread below the density floor "
				"is refused and has no found box"_q,
				u"denseTotal=%1 sparseTotal=%2 dense=%3 sparse=%4"_q
					.arg(dense.totalChromatic)
					.arg(sparse.totalChromatic)
					.arg(denseText, sparseText));
			Check(
				flat.ok
					&& (flat.state == ChromaticRasterState::NoChromatic)
					&& (flat.totalChromatic == 0)
					&& !flat.found()
					&& !flatBox.read()
					&& (ChromaticRasterStateName(flat.state)
						!= ChromaticRasterStateName(sparse.state))
					&& (flat.reason != sparse.reason)
					&& flat.reason.contains(u"chromatic floor"_q)
					&& sparse.reason.contains(u"density floor"_q),
				u"a flat fill with no chromatic pixel refuses by the other "
				"name and has no found box"_q,
				flatText + u" || "_q + sparseText);
			Check(
				!sparseBox.read()
					&& !sparseMean.read()
					&& !sparseDensity.read()
					&& !flatBox.read()
					&& !flatMean.read()
					&& !flatDensity.read()
					&& sparseBox.refusal.contains(u"below-density"_q)
					&& flatBox.refusal.contains(u"no-chromatic"_q),
				u"asking a refusing raster for its box, mean or density "
				"returns a refusal rather than a value"_q,
				sparseBox.refusal
					+ u" | "_q
					+ sparseMean.refusal
					+ u" | "_q
					+ flatDensity.refusal);
			Check(
				denseText.contains(RectText(mark))
					&& denseText.contains(u"matched=256"_q)
					&& denseText.contains(u"totalChromatic=256"_q)
					&& denseText.contains(
						u"density="_q
							+ QString::number(density.density)
							+ u" mean="_q)
					&& denseText.contains(ColorHex(expectedMean))
					&& denseText.contains(u"state=found"_q)
					&& !denseText.contains(ColorHex(pen))
					&& !denseText.contains(ColorHex(fill))
					&& sparseText.contains(u"state=below-density"_q)
					&& sparseText.contains(u"box=none"_q)
					&& flatText.contains(u"state=no-chromatic"_q)
					&& flatText.contains(u"box=none"_q)
					&& outsideText.contains(u"state=raster-outside-band"_q)
					&& !outside.ok
					&& (outside.state
						!= ChromaticRasterState::NoChromatic)
					&& (outside.state
						!= ChromaticRasterState::BelowDensity),
				u"the found format carries the box, both counts, the "
				"density and the mean, and each refusal names itself"_q,
				denseText
					+ u" || "_q
					+ sparseText
					+ u" || "_q
					+ flatText
					+ u" || "_q
					+ outsideText);
		},
	});
}

void AppendGlyphCoreSelfTest(not_null<Runner*> runner) {
	runner->add({
		.name = u"glyph-core self-test: neutral and link strokes on both "
			"grounds"_q,
		.run = [] {
			const auto band = QRect(8, 8, 80, 32);
			const auto absent = QColor(0xff, 0x00, 0x00);
			const auto paint = [](
					QColor ground,
					QColor pen,
					QColor fringeA,
					QColor fringeB,
					QColor fringeC) {
				auto image = QImage(
					QSize(96, 48),
					QImage::Format_ARGB32_Premultiplied);
				image.fill(ground);
				for (auto y = 12; y < 28; ++y) {
					for (auto x = 20; x < 36; ++x) {
						image.setPixelColor(x, y, pen);
					}
					image.setPixelColor(17, y, fringeA);
					image.setPixelColor(18, y, fringeB);
					image.setPixelColor(36, y, fringeC);
				}
				return image;
			};
			const auto checkGround = [&](
					const QString &ground,
					QColor groundColor,
					QColor neutral,
					QColor link,
					int neutralStrongest,
					int linkStrongest,
					QColor fringeA,
					QColor fringeB,
					QColor fringeC) {
				const auto scanPens = std::vector<InkCandidate>{
					{ u"neutral"_q, neutral },
					{ u"link"_q, link },
				};
				const auto pens = std::vector<InkCandidate>{
					{ u"neutral"_q, neutral },
					{ u"link"_q, link },
					{ u"absent"_q, absent },
				};
				const auto neutralImage = paint(
					groundColor,
					neutral,
					fringeA,
					fringeB,
					fringeC);
				const auto linkImage = paint(
					groundColor,
					link,
					fringeA,
					fringeB,
					fringeC);
				const auto scan = ScanInk(neutralImage, band, scanPens);
				const auto scanNeutral = scan.countAt(0);
				const auto scanLink = scan.countAt(1);
				const auto neutralCore = ReadGlyphCore(
					neutralImage,
					band,
					pens);
				const auto linkCore = ReadGlyphCore(linkImage, band, pens);
				const auto neutralModal = neutralCore.readModal();
				const auto linkModal = linkCore.readModal();
				const auto neutralOfNeutral = neutralCore.solidAt(0);
				const auto linkOfNeutral = neutralCore.solidAt(1);
				const auto absentOfNeutral = neutralCore.solidAt(2);
				const auto neutralOfLink = linkCore.solidAt(0);
				const auto linkOfLink = linkCore.solidAt(1);
				const auto absentOfLink = linkCore.solidAt(2);
				const auto neutralText = FormatGlyphCore(neutralCore);
				const auto linkText = FormatGlyphCore(linkCore);
				const auto quoted = neutralText
					+ u" || "_q
					+ linkText
					+ u" || scan "_q
					+ InkCountDetails(scanNeutral)
					+ u" | "_q
					+ InkCountDetails(scanLink);
				Check(
					(scan.state == InkScanState::Classified)
						&& scanNeutral.read()
						&& (scanNeutral.count == 256)
						&& scanLink.read()
						&& (scanLink.count == 48)
						&& (neutralCore.state == GlyphCoreState::Measured)
						&& (ColorHex(neutralCore.background)
							== ColorHex(groundColor))
						&& (neutralCore.strongest == neutralStrongest)
						&& (neutralCore.cores == 256)
						&& neutralModal.read()
						&& (ColorHex(neutralModal.color) == ColorHex(neutral))
						&& (neutralModal.count == 256)
						&& (neutralModal.cores == 256)
						&& neutralOfNeutral.read()
						&& (neutralOfNeutral.count == 256)
						&& linkOfNeutral.read()
						&& (linkOfNeutral.count == 0)
						&& absentOfNeutral.read()
						&& (absentOfNeutral.count == 0)
						&& (ColorHex(neutralModal.color) != ColorHex(absent))
						&& neutralText.contains(RectText(band))
						&& neutralText.contains(
							u"bg="_q + ColorHex(groundColor))
						&& neutralText.contains(u"kGlyphCoreTolerance=8"_q)
						&& neutralText.contains(u"cores=256"_q)
						&& neutralText.contains(
							u"modal="_q + ColorHex(neutral))
						&& neutralText.contains(u"share=256/256"_q)
						&& neutralText.contains(u"neutral=256"_q)
						&& neutralText.contains(u"link=0"_q)
						&& neutralText.contains(u"absent=0"_q),
					u"on the "_q
						+ ground
						+ u" ground a neutral stroke is classified with "
						"link fringes, and its solid cores are the "
						"neutral pen"_q,
					quoted);
				Check(
					(linkCore.state == GlyphCoreState::Measured)
						&& (linkCore.strongest == linkStrongest)
						&& (linkCore.cores == 256)
						&& linkModal.read()
						&& (ColorHex(linkModal.color) == ColorHex(link))
						&& (ColorHex(linkModal.color) != ColorHex(absent))
						&& neutralOfLink.read()
						&& (neutralOfLink.count == 0)
						&& linkOfLink.read()
						&& (linkOfLink.count == 256)
						&& absentOfLink.read()
						&& (absentOfLink.count == 0)
						&& linkText.contains(u"kGlyphCoreTolerance=8"_q)
						&& linkText.contains(u"cores=256"_q)
						&& linkText.contains(u"modal="_q + ColorHex(link))
						&& linkText.contains(u"share=256/256"_q)
						&& linkText.contains(u"neutral=0"_q)
						&& linkText.contains(u"link=256"_q)
						&& linkText.contains(
							u"bg="_q + ColorHex(groundColor)),
					u"on the "_q
						+ ground
						+ u" ground the same stroke in the link pen has "
						"that pen as its modal core"_q,
					quoted);
			};
			Note(u"glyph-core self-test: no window, session, chats list, "
				"network, account or wallet - synthetic images on two "
				"grounds"_q);
			checkGround(
				u"day"_q,
				QColor(0xff, 0xff, 0xff),
				QColor(0x99, 0x99, 0x99),
				QColor(0x16, 0x8a, 0xcd),
				102,
				233,
				QColor(0xa1, 0xd0, 0xf5),
				QColor(0xa1, 0xd6, 0xf2),
				QColor(0xa1, 0xdb, 0xf0));
			checkGround(
				u"night"_q,
				QColor(0x17, 0x21, 0x2b),
				QColor(0x70, 0x84, 0x99),
				QColor(0x6a, 0xb3, 0xf3),
				110,
				200,
				QColor(0x38, 0x62, 0x8f),
				QColor(0x34, 0x5b, 0x8b),
				QColor(0x3c, 0x6a, 0x93));
		},
	});
	runner->add({
		.name = u"glyph-core self-test: no ink, no solid core, and refused "
			"reads"_q,
		.run = [] {
			const auto band = QRect(8, 8, 80, 32);
			const auto pens = std::vector<InkCandidate>{
				{ u"neutral"_q, QColor(0x99, 0x99, 0x99) },
			};
			auto flatImage = QImage(
				QSize(96, 48),
				QImage::Format_ARGB32_Premultiplied);
			flatImage.fill(QColor(0xf0, 0xf0, 0xf0));
			const auto flat = ReadGlyphCore(flatImage, band, pens);
			const auto flatModal = flat.readModal();
			const auto flatSolid = flat.solidAt(0);
			const auto flatText = FormatGlyphCore(flat);
			auto fringeImage = QImage(
				QSize(40, 16),
				QImage::Format_ARGB32_Premultiplied);
			fringeImage.fill(Qt::white);
			for (auto x = 0; x < 40; ++x) {
				fringeImage.setPixelColor(x, 0, QColor(80, 255, 255));
				fringeImage.setPixelColor(x, 1, QColor(255, 80, 255));
				fringeImage.setPixelColor(x, 2, QColor(255, 255, 80));
				fringeImage.setPixelColor(x, 3, QColor(210, 220, 230));
			}
			const auto fringeBand = QRect(0, 0, 40, 16);
			const auto fringe = ReadGlyphCore(fringeImage, fringeBand, pens);
			const auto fringeModal = fringe.readModal();
			const auto fringeSolid = fringe.solidAt(0);
			const auto fringeText = FormatGlyphCore(fringe);
			const auto outside = ReadGlyphCore(
				flatImage,
				QRect(200, 200, 8, 8),
				pens);
			const auto outsideModal = outside.readModal();
			const auto outsideText = FormatGlyphCore(outside);
			const auto shipped = std::vector<QString>{
				u"not-scanned"_q,
				u"outside-image"_q,
				u"no-rows-in-band"_q,
				u"candidates-collide"_q,
				u"no-ink"_q,
				u"classified"_q,
				u"background-collinear"_q,
				u"raster-outside-band"_q,
				u"no-chromatic"_q,
				u"below-density"_q,
				u"found"_q,
				u"missing"_q,
			};
			const auto flatName = GlyphCoreStateName(flat.state);
			const auto fringeName = GlyphCoreStateName(fringe.state);
			const auto outsideName = GlyphCoreStateName(outside.state);
			auto distinct = (flatName != fringeName)
				&& (flat.reason != fringe.reason)
				&& (flatName != outsideName)
				&& (fringeName != outsideName);
			auto forbidden = QString();
			for (const auto &one : shipped) {
				if ((flatName == one)
					|| (fringeName == one)
					|| (outsideName == one)
					|| flat.reason.contains(one)
					|| fringe.reason.contains(one)) {
					distinct = false;
				}
				if (!forbidden.isEmpty()) {
					forbidden += u", "_q;
				}
				forbidden += one;
			}
			Note(u"glyph-core self-test: no window, session, chats list, "
				"network, account or wallet - a flat band and a fringe "
				"band"_q);
			Check(
				flat.ok
					&& (flat.state == GlyphCoreState::NoPaint)
					&& !flatModal.read()
					&& !flatModal.color.isValid()
					&& (flatModal.count == -1)
					&& (flatModal.cores == -1)
					&& !flatSolid.read()
					&& (flatSolid.count == -1)
					&& flatSolid.refusal.contains(u"no-paint"_q)
					&& flatModal.refusal.contains(u"no-paint"_q),
				u"asking a flat band for its modal colour or a solid "
				"count refuses, and the count is not a measured zero"_q,
				flatModal.refusal + u" | "_q + flatSolid.refusal);
			Check(
				flatText.contains(u"state=no-paint"_q)
					&& flatText.contains(u"modal=none"_q)
					&& flatText.contains(u"cores=none"_q)
					&& flatText.contains(u"share=none"_q)
					&& flatText.contains(u"neutral=none"_q)
					&& !flatText.contains(u"modal=#"_q),
				u"the no-paint format names the refusal and carries no "
				"modal colour"_q,
				flatText);
			Check(
				fringe.ok
					&& (fringe.state == GlyphCoreState::NoSolidCore)
					&& (fringe.inkPixels > 0)
					&& !fringeModal.read()
					&& !fringeModal.color.isValid()
					&& (fringeModal.count == -1)
					&& !fringeSolid.read()
					&& (fringeSolid.count == -1)
					&& fringeText.contains(u"state=no-solid-core"_q)
					&& fringeText.contains(u"modal=none"_q)
					&& !fringeText.contains(u"modal=#"_q),
				u"a fringe band with no full-coverage core refuses by "
				"name and does not present a fringe colour as the pen"_q,
				fringeText + u" | "_q + fringeModal.refusal);
			Check(
				!outside.ok
					&& (outside.state == GlyphCoreState::OutsideBand)
					&& !outsideModal.read()
					&& (outsideModal.count == -1)
					&& outsideText.contains(u"state=core-outside-band"_q)
					&& (outside.state != GlyphCoreState::NoPaint)
					&& (outside.state != GlyphCoreState::NoSolidCore),
				u"a band that misses the image is not the no-paint "
				"refusal"_q,
				outsideText);
			Check(
				distinct
					&& flat.reason.contains(u"no glyph ink"_q)
					&& fringe.reason.contains(u"no solid core"_q),
				u"the no-paint and no-solid-core names and reasons differ "
				"from each other and from every state the module already "
				"ships"_q,
				u"flat=%1 reason=%2 fringe=%3 reason=%4 outside=%5 "
				"forbidden=[%6]"_q
					.arg(
						flatName,
						flat.reason,
						fringeName,
						fringe.reason,
						outsideName,
						forbidden));
		},
	});
}

} // namespace

void AppendDeriveBandSelfTest(not_null<Runner*> runner) {
	runner->add({
		.name = u"derive-band self-test: underivable fill, separable "
			"control, and no-rows refusal"_q,
		.run = [] {
			const auto fill = QColor(0x29, 0xb0, 0x71);
			const auto surround = QColor(0x17, 0x21, 0x2b);
			const auto size = QSize(64, 32);
			const auto candidate = QRect(8, 4, 48, 24);
			const auto underivableReason = u"the requested fill is the "
				"image's background outside the candidate, so no band "
				"can be derived"_q;
			const auto noRowsReason = u"no row of the recovered box has "
				"the pill fill as its own background"_q;
			auto same = QImage(size, QImage::Format_ARGB32_Premultiplied);
			same.fill(fill);
			const auto underivable = DeriveBand(same, candidate, fill);
			const auto measured = MeasurePaintedInk(
				same,
				candidate,
				fill,
				{ { u"ink"_q, QColor(255, 255, 255) } });
			auto separable = QImage(
				size,
				QImage::Format_ARGB32_Premultiplied);
			separable.fill(surround);
			{
				auto p = QPainter(&separable);
				p.fillRect(candidate, fill);
			}
			const auto derived = DeriveBand(separable, candidate, fill);
			const auto measuredOk = MeasurePaintedInk(
				separable,
				candidate,
				fill,
				{ { u"ink"_q, QColor(255, 255, 255) } });
			auto none = QImage(size, QImage::Format_ARGB32_Premultiplied);
			none.fill(surround);
			const auto noRows = DeriveBand(none, candidate, fill);
			Note(u"derive-band self-test: no window, session, chats list, "
				"network, account or wallet - three synthetic images"_q);
			Check(
				!underivable.ok
					&& (underivable.reason == underivableReason)
					&& (underivable.fillRows > 0)
					&& underivable.rows.empty(),
				u"a candidate whose fill is the surrounding background "
				"is refused with the underivable-band reason"_q,
				u"ok=%1 fillRows=%2 rows=%3 reason=%4"_q
					.arg(underivable.ok ? 1 : 0)
					.arg(underivable.fillRows)
					.arg(int(underivable.rows.size()))
					.arg(underivable.reason));
			Check(
				!measured.derived.ok
					&& measured.report.contains(underivableReason),
				u"MeasurePaintedInk carries that reason into its report "
				"rather than a bare zero"_q,
				u"report=%1 derivedOk=%2"_q
					.arg(measured.report)
					.arg(measured.derived.ok ? 1 : 0));
			Check(
				derived.ok
					&& (derived.reason == u"none"_q)
					&& !derived.rows.empty()
					&& !derived.fillRegion.isEmpty()
					&& !derived.band.isEmpty(),
				u"a genuinely separable band still derives with its rows "
				"and region"_q,
				u"ok=%1 rows=%2 fillRows=%3 fillRegion=%4x%5 band=%6x%7 "
				"reason=%8"_q
					.arg(derived.ok ? 1 : 0)
					.arg(int(derived.rows.size()))
					.arg(derived.fillRows)
					.arg(derived.fillRegion.width())
					.arg(derived.fillRegion.height())
					.arg(derived.band.width())
					.arg(derived.band.height())
					.arg(derived.reason));
			Check(
				measuredOk.derived.ok
					&& measuredOk.report.startsWith(u"fill="_q),
				u"MeasurePaintedInk on a separable band still writes a "
				"FormatInkReport line"_q,
				u"report=%1 derivedOk=%2"_q
					.arg(measuredOk.report)
					.arg(measuredOk.derived.ok ? 1 : 0));
			Check(
				!noRows.ok
					&& (noRows.reason == noRowsReason)
					&& (noRows.reason != underivableReason)
					&& noRows.rows.empty(),
				u"an image whose rows exist but none matches the fill "
				"still produces the existing no-rows refusal"_q,
				u"ok=%1 fillRows=%2 reason=%3"_q
					.arg(noRows.ok ? 1 : 0)
					.arg(noRows.fillRows)
					.arg(noRows.reason));
			Check(
				ChannelDelta(fill, surround) > kBackgroundSame,
				u"the separable control's surround is farther from the "
				"fill than kBackgroundSame"_q,
				u"delta=%1 kBackgroundSame=%2"_q
					.arg(ChannelDelta(fill, surround))
					.arg(kBackgroundSame));
		},
	});
	runner->add({
		.name = u"ink scan self-test: a classifying band, its counts read "
			"back, and an out-of-range ask refused"_q,
		.run = [] {
			const auto background = QColor(0x17, 0x21, 0x2b);
			const auto mark = QColor(0xff, 0xff, 0xff);
			const auto text = QColor(0x29, 0xb0, 0x71);
			const auto size = QSize(64, 32);
			const auto band = QRect(8, 8, 48, 12);
			const auto candidates = std::vector<InkCandidate>{
				{ u"mark"_q, mark },
				{ u"text"_q, text },
			};
			auto painted = QImage(
				size,
				QImage::Format_ARGB32_Premultiplied);
			painted.fill(background);
			{
				auto p = QPainter(&painted);
				p.fillRect(QRect(16, 8, 6, 12), mark);
				p.fillRect(QRect(32, 8, 4, 12), text);
			}
			auto blank = QImage(size, QImage::Format_ARGB32_Premultiplied);
			blank.fill(background);
			const auto scan = ScanInk(painted, band, candidates);
			const auto blankScan = ScanInk(blank, band, candidates);
			const auto lone = ScanInk(
				painted,
				band,
				{ { u"mark"_q, mark } });
			const auto markCount = scan.countAt(0);
			const auto textCount = scan.countAt(1);
			const auto blankCount = blankScan.countAt(0);
			const auto loneCount = lone.countAt(0);
			const auto pastEnd = scan.countAt(2);
			const auto beforeStart = scan.countAt(-1);
			const auto formatted = FormatInkScan(scan, background);
			const auto thresholds = std::vector<QString>{
				u"kInkDelta=%1"_q.arg(kInkDelta),
				u"kOnLine=%1"_q.arg(kOnLine),
				u"kInkMargin=%1"_q.arg(kInkMargin),
				u"kSameTolerance=%1"_q.arg(kSameTolerance),
				u"kBackgroundSame=%1"_q.arg(kBackgroundSame),
			};
			auto thresholdsNamed = true;
			for (const auto &one : thresholds) {
				if (!formatted.contains(one)) {
					thresholdsNamed = false;
				}
			}
			Note(u"ink scan self-test: no window, session, chats list, "
				"network, account or wallet - two synthetic images"_q);
			Check(
				scan.ok
					&& (scan.state == InkScanState::Classified)
					&& (scan.reason == u"none"_q)
					&& (scan.candidates.size() == 2)
					&& (scan.counts.size() == scan.candidates.size()),
				u"a band whose two candidates separate classifies, with "
				"its counts vector as long as its candidates"_q,
				formatted);
			Check(
				markCount.read()
					&& (markCount.name == u"mark"_q)
					&& (markCount.count == 72)
					&& textCount.read()
					&& (textCount.name == u"text"_q)
					&& (textCount.count == 48)
					&& blankCount.read()
					&& (blankCount.count == 0),
				u"each candidate's count reads back through countAt as the "
				"ink the fixture painted, against an image that painted "
				"none"_q,
				InkCountDetails(markCount)
					+ u" | "_q
					+ InkCountDetails(textCount)
					+ u" | control "_q
					+ InkCountDetails(blankCount));
			Check(
				!pastEnd.read()
					&& (pastEnd.count == -1)
					&& pastEnd.refusal.contains(
						u"outside this reading's 2 candidates"_q)
					&& pastEnd.refusal.contains(u"mark, text"_q)
					&& !beforeStart.read()
					&& (beforeStart.count == -1),
				u"an index this reading does not have is refused by name "
				"instead of read past the end of its counts"_q,
				InkCountDetails(pastEnd)
					+ u" | "_q
					+ InkCountDetails(beforeStart));
			Check(
				formatted.startsWith(u"fill="_q)
					&& formatted.contains(
						u"mark(%1)=72"_q.arg(ColorHex(mark)))
					&& formatted.contains(
						u"text(%1)=48"_q.arg(ColorHex(text)))
					&& formatted.contains(u"collision=none"_q)
					&& thresholdsNamed,
				u"the passing reading's own text names the candidate "
				"colours, their counts and every threshold it classified "
				"against"_q,
				formatted);
			Check(
				lone.ok
					&& (lone.state == InkScanState::Classified)
					&& loneCount.read()
					&& (loneCount.count == 72)
					&& (lone.ambiguous == 48),
				u"the collision gate leaves a lone-candidate reading "
				"untouched, which is the shape the only tracked caller "
				"passes"_q,
				FormatInkScan(lone, background)
					+ u" | "_q
					+ InkCountDetails(loneCount));
		},
	});
	runner->add({
		.name = u"ink scan self-test: four named refusals, a collision the "
			"reading names, and a scanned band with no ink"_q,
		.run = [] {
			// The pill control's fill is the same green the band paints as
			// its text ink: that control's derived band is nothing but the
			// fill, so every pixel in it reads as its own background and
			// the reading is a measured NoInk rather than a refusal, which
			// is what keeps the existing stage's fill= report check true.
			const auto surround = QColor(0x17, 0x21, 0x2b);
			const auto fill = QColor(0x29, 0xb0, 0x71);
			const auto mark = QColor(0xff, 0xff, 0xff);
			const auto nearMark = QColor(0xf5, 0xf5, 0xf5);
			const auto size = QSize(64, 32);
			const auto candidate = QRect(8, 4, 48, 24);
			const auto band = QRect(8, 8, 48, 12);
			const auto away = QRect(96, 96, 8, 8);
			const auto candidates = std::vector<InkCandidate>{
				{ u"mark"_q, mark },
				{ u"text"_q, fill },
			};
			const auto colliding = std::vector<InkCandidate>{
				{ u"mark"_q, mark },
				{ u"mark-again"_q, nearMark },
			};
			const auto emptyBoxReason = u"the recovered box is empty or "
				"the fill is invalid"_q;
			const auto noFillRowsReason = u"no row of the recovered box "
				"has the pill fill as its own background"_q;
			const auto underivableReason = u"the requested fill is the "
				"image's background outside the candidate, so no band can "
				"be derived"_q;
			const auto noBandRowsReason = u"no row of the derived band "
				"kept the pill fill as its own background"_q;
			auto painted = QImage(
				size,
				QImage::Format_ARGB32_Premultiplied);
			painted.fill(surround);
			{
				auto p = QPainter(&painted);
				p.fillRect(QRect(16, 8, 6, 12), mark);
				p.fillRect(QRect(32, 8, 4, 12), fill);
			}
			auto blank = QImage(size, QImage::Format_ARGB32_Premultiplied);
			blank.fill(surround);
			auto solid = QImage(size, QImage::Format_ARGB32_Premultiplied);
			solid.fill(surround);
			{
				auto p = QPainter(&solid);
				p.fillRect(candidate, fill);
			}
			const auto outside = ScanInk(painted, away, candidates);
			const auto noRows = ScanInk(
				painted,
				band,
				candidates,
				{ 0, 31 });
			const auto collided = ScanInk(painted, band, colliding);
			const auto noInk = ScanInk(blank, band, candidates);
			const auto outsideCount = outside.countAt(0);
			const auto collidedCount = collided.countAt(0);
			const auto noInkCount = noInk.countAt(0);
			const auto outsideText = FormatInkScan(outside, surround);
			const auto collidedText = FormatInkScan(collided, surround);
			const auto collisionDump = CollisionDump(colliding);
			const auto outsideReason = u"the requested band [%1] does not "
				"intersect the image [%2], so no pixel was scanned"_q
				.arg(RectText(away), RectText(painted.rect()));
			const auto noRowsReason = u"none of the %1 requested rows "
				"falls inside the band [%2], so no pixel was scanned"_q
				.arg(2)
				.arg(RectText(band));
			const auto deriveReasons = std::vector<QString>{
				emptyBoxReason,
				noFillRowsReason,
				underivableReason,
				noBandRowsReason,
			};
			auto distinct = (outside.reason != noRows.reason);
			auto deriveList = QString();
			for (const auto &one : deriveReasons) {
				if ((outside.reason == one) || (noRows.reason == one)) {
					distinct = false;
				}
				if (!deriveList.isEmpty()) {
					deriveList += u"; "_q;
				}
				deriveList += one;
			}
			auto attributed = 0;
			for (const auto count : collided.counts) {
				attributed += count;
			}
			const auto measuredOk = MeasurePaintedInk(
				solid,
				candidate,
				fill,
				candidates);
			const auto measuredCollide = MeasurePaintedInk(
				solid,
				candidate,
				fill,
				colliding);
			const auto measuredRefused = MeasurePaintedInk(
				blank,
				candidate,
				fill,
				candidates);
			const auto refusedCount = measuredRefused.scan.countAt(0);
			const auto thresholds = std::vector<QString>{
				u"kInkDelta=%1"_q.arg(kInkDelta),
				u"kOnLine=%1"_q.arg(kOnLine),
				u"kInkMargin=%1"_q.arg(kInkMargin),
				u"kSameTolerance=%1"_q.arg(kSameTolerance),
				u"kBackgroundSame=%1"_q.arg(kBackgroundSame),
			};
			auto thresholdsNamed = true;
			for (const auto &one : thresholds) {
				if (!measuredOk.report.contains(one)) {
					thresholdsNamed = false;
				}
			}
			Note(u"ink scan self-test: no window, session, chats list, "
				"network, account or wallet - three synthetic images"_q);
			Check(
				!outside.ok
					&& (outside.state == InkScanState::OutsideImage)
					&& (outside.reason == outsideReason)
					&& outside.reason.contains(
						u"does not intersect the image"_q)
					&& (outside.band == QRect()),
				u"a band that does not intersect the image refuses by name "
				"and quotes the band it was asked for"_q,
				outsideText);
			Check(
				!noRows.ok
					&& (noRows.state == InkScanState::NoRowsInBand)
					&& (noRows.reason == noRowsReason)
					&& (noRows.band == band),
				u"an explicit row list with no row inside the band refuses "
				"by name and still carries the clip"_q,
				FormatInkScan(noRows, surround));
			Check(
				distinct,
				u"the two scan refusals differ from each other and from "
				"all four DeriveBand reasons read back here"_q,
				u"outside=%1 noRows=%2 derive=[%3]"_q
					.arg(outside.reason, noRows.reason, deriveList));
			Check(
				(outside.candidates.size() == 2)
					&& (outside.counts.size() == outside.candidates.size())
					&& (outsideCount.name == u"mark"_q)
					&& !outsideCount.read()
					&& (outsideCount.count == -1)
					&& outsideCount.refusal.contains(
						u"classified nothing"_q),
				u"a refused scan still names the candidates it was asked "
				"to classify against and refuses every count"_q,
				outsideText + u" | "_q + InkCountDetails(outsideCount));
			Check(
				collided.ok
					&& (collided.state == InkScanState::CandidatesCollide)
					&& (collided.inkPixels == 120)
					&& (collided.ambiguous == collided.inkPixels)
					&& !attributed
					&& !collided.classifiedInk.isValid()
					&& !collidedCount.read()
					&& (collidedCount.name == u"mark"_q)
					&& collidedText.startsWith(u"fill="_q)
					&& collidedText.contains(collisionDump)
					&& collidedText.contains(
						u"kSameTolerance=%1"_q.arg(kSameTolerance)),
				u"a colliding candidate pair is still a scanned reading "
				"whose text names the collision"_q,
				collidedText + u" | "_q + InkCountDetails(collidedCount));
			Check(
				noInk.ok
					&& (noInk.state == InkScanState::NoInk)
					&& !noInk.inkPixels
					&& noInkCount.read()
					&& (noInkCount.count == 0)
					&& noInk.reason.contains(u"no pixel of the band"_q),
				u"a band that was scanned and held no ink reads back a "
				"real zero rather than a refusal"_q,
				FormatInkScan(noInk, surround)
					+ u" | "_q
					+ InkCountDetails(noInkCount));
			Check(
				!outsideText.isEmpty()
					&& outsideText.startsWith(u"fill="_q)
					&& outsideText.contains(outside.reason),
				u"FormatInkScan over a refused scan reading carries that "
				"reading's own reason and never nothing, which is the text "
				"MeasurePaintedInk's scan branch writes"_q,
				outsideText);
			Check(
				measuredOk.derived.ok
					&& measuredOk.report.startsWith(u"fill="_q)
					&& measuredOk.report.contains(u"state=no-ink"_q)
					&& thresholdsNamed,
				u"a passing MeasurePaintedInk still starts with its "
				"measured fill and now says what it was measured against"_q,
				measuredOk.report);
			Check(
				measuredCollide.derived.ok
					&& measuredCollide.report.startsWith(u"fill="_q)
					&& measuredCollide.report.contains(collisionDump),
				u"MeasurePaintedInk end to end on a colliding candidate "
				"pair names the collision in its report"_q,
				measuredCollide.report);
			Check(
				!measuredRefused.derived.ok
					&& (measuredRefused.scan.state
						== InkScanState::NotScanned)
					&& !measuredRefused.report.isEmpty()
					&& !refusedCount.read()
					&& (refusedCount.count == -1)
					&& refusedCount.refusal.contains(u"not-scanned"_q),
				u"the reading a refused derivation leaves behind names itself "
				"not-scanned and refuses every count instead of reading past "
				"the end of a vector it never filled"_q,
				measuredRefused.report
					+ u" | "_q
					+ InkCountDetails(refusedCount));
		},
	});
	AppendBackgroundCollinearSelfTest(runner);
	AppendChromaticRasterSelfTest(runner);
	AppendGlyphCoreSelfTest(runner);
}

} // namespace Test

#endif // _DEBUG
