#pragma once

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/peer_qr_surface.h"

#include "base/qt/qt_common_adapters.h"
#include "base/weak_qptr.h"
#include "qr/qr_generate.h"
#include "ui/effects/animated_string.h"
#include "ui/image/image_prepare.h"
#include "ui/color_contrast.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <QtSvg/QSvgRenderer>

namespace Ui::PeerQrSurfaceDetails {

constexpr auto kCardHeightRatio = 1.07;

constexpr auto kCardRadiusRatio = 0.125;

constexpr auto kQrSideRatio = 0.70;

constexpr auto kQrTopRatio = 0.12;

constexpr auto kLogoRatio = 0.175;

constexpr auto kInkContrast = 4.4;

constexpr auto kForegroundContrast = 4.5;

constexpr auto kInkChromaScale = 1.25;

constexpr auto kInkChromaAdd = 0.02;

constexpr auto kInkChromaMax = 0.20;

constexpr auto kDarkInkGlassAlpha = 0.35;

constexpr auto kWhiteGlyphGlassAlpha = 0.12;

constexpr auto kWhiteGlyphContrast = 3.;

constexpr auto kGlassAlpha = 0.25;

constexpr auto kGradientRadiusToCard = 1.6;

constexpr auto kGradientSteps = 8;

constexpr auto kGradientMiddleMix = 0.5;

constexpr auto kDarkEdgeFrom = 7.;

constexpr auto kDarkEdgeTill = 11.;

constexpr auto kDarkInkContrast = 2.3;

constexpr auto kShadowAlpha = 0.13;

constexpr auto kImageScale = 4;

constexpr auto kQrOversample = 2;

constexpr auto kSolveIterations = 24;

constexpr auto kPlaneCenterX = 0.46;

constexpr auto kPlaneCenterY = 0.525;

constexpr auto kBloomDuration = crl::time(800);

constexpr auto kBloomSpread = 0.6;

constexpr auto kFlyInDuration = crl::time(500);

constexpr auto kLoopPeriod = crl::time(900);

constexpr auto kPlaneMinWidth = 0.44;

constexpr auto kPlaneAxisAngle = 35.;

constexpr auto kPlaneAxisX = 0.452;

constexpr auto kPlaneAxisY = 0.394;

constexpr auto kPlaneTurn = 8.6;

constexpr auto kFlyInOffset = 0.55;

constexpr auto kFlyInScale = 0.5;

constexpr auto kStreakAlpha = 0.18;

constexpr auto kStreakLength = 0.45;

constexpr auto kStreakWidth = 0.05;

constexpr auto kStreakTravel = 2.5;

constexpr auto kStreakSourceAngle = 39.;

constexpr auto kStreakSourceDistance = 2.5;

constexpr auto kStreakLanes = std::array{ -0.78, -0.58, 0.66, -0.38, 0.80 };

constexpr auto kSwapDelay = crl::time(40);

constexpr auto kSwapDuration = crl::time(450);

constexpr auto kSwapTravel = crl::time(350);

constexpr auto kDustLife = crl::time(100);

constexpr auto kDustPerModule = 3;

constexpr auto kDustSizeMin = 0.35;

constexpr auto kDustSizeMax = 0.7;

constexpr auto kDustRiseMin = 0.6;

constexpr auto kDustRiseMax = 1.2;

constexpr auto kDustDrift = 0.5;

constexpr auto kBounceDuration = crl::time(320);

constexpr auto kBouncePeak = 1.03;

constexpr auto kBounceTrough = 0.995;

constexpr auto kBouncePeakAt = 0.25;

constexpr auto kBounceTroughAt = 0.6;

constexpr auto kWaveDuration = crl::time(600);

constexpr auto kWavePower = 2.4;

constexpr auto kWaveGlowAlpha = 0.38;

constexpr auto kWaveTintMix = 0.55;

// Qt's flattened circle strays from the true one by less than this
constexpr auto kCircleSlack = 1.;

constexpr auto kCircleSlackPerRadius = 0.001;

constexpr auto kDipDuration = crl::time(360);

constexpr auto kDipDepth = 0.03;

constexpr auto kFinderSize = 7;

struct CardLayout {
	QSize size;
	QRect qr;
	QRect logo;
	QRect band;
	int radius = 0;
};

struct UsernameFit {
	QFont font;
	QStringList lines;
};

struct CardContent {
	QImage shadow;
	QImage qr;
	Qr::Data data;
	QImage plane;
	UsernameFit username;
	QColor ink;
	QPoint shadowPosition;
};

struct QrSwap {
	QImage qr;
	Qr::Data data;
	crl::time startedAt = 0;
};

struct PlaneState {
	QPointF shift;
	float64 scale = 1.;
	float64 width = 1.;
	float64 rotation = 0.;
	float64 opacity = 1.;
	float64 streaks = 0.;
	float64 travel = 0.;
};

struct CardFrame {
	PlaneState plane;
	float64 bloom = 1.;
	const QrSwap *swap = nullptr;
	crl::time swapElapsed = 0;
};

struct BackgroundRequest {
	std::array<QImage, 4> corners;
	QSize size;
	QColor center;
	QColor edge;
	QPointF gradientCenter;
	float64 gradientRadius = 0.;
};

[[nodiscard]] QColor SolveLightness(
		float64 hue,
		float64 chroma,
		const QColor &against,
		float64 contrast);

[[nodiscard]] QColor MixLab(
		const QColor &a,
		const QColor &b,
		float64 ratio);

[[nodiscard]] PeerQrColors MixColors(
		const PeerQrColors &a,
		const PeerQrColors &b,
		float64 ratio);

[[nodiscard]] QColor ComputeWaveTint(const PeerQrAppearance &appearance);

[[nodiscard]] float64 InkChroma(float64 chroma);

[[nodiscard]] QColor BackdropInk(const PeerQrAppearance &appearance);

[[nodiscard]] PeerQrColors ComputePeerQrColors(
		const PeerQrAppearance &appearance);

[[nodiscard]] bool SameBackdrop(PeerQrAppearance a, PeerQrAppearance b);

[[nodiscard]] float64 DipScale(crl::time startedAt, crl::time now);

[[nodiscard]] CardLayout ComputeCardLayout(int width, bool withUsername);

[[nodiscard]] QStringList SplitInTwoLines(
		const QString &text,
		const QFontMetricsF &metrics);

[[nodiscard]] bool LinesFit(
		const QStringList &lines,
		const QFontMetricsF &metrics,
		int available,
		int bandHeight);

[[nodiscard]] UsernameFit FitUsername(
		const QString &text,
		int available,
		int bandHeight,
		const std::array<QFont, 3> &steps,
		int floorPixelSize);

[[nodiscard]] QImage GenerateFinalQr(
		const Qr::Data &data,
		int side,
		const QColor &ink);

[[nodiscard]] QImage PrepareQrImage(
		const Qr::Data &data,
		int side,
		const QColor &ink,
		int ratio);

[[nodiscard]] QImage PrepareCardShadow(
		QSize size,
		int radius,
		int blur,
		float64 alpha,
		int ratio);

[[nodiscard]] QImage PreparePlane(int side, int ratio);

[[nodiscard]] CardContent PrepareCardContent(
		const CardLayout &layout,
		const QString &link,
		const QColor &ink,
		int shadowRadius,
		int shadowShift,
		int ratio);

void PaintAppearanceGradient(
		QPainter &p,
		const QRect &rect,
		QPointF center,
		float64 radius,
		const QColor &centerColor,
		const QColor &edgeColor);

[[nodiscard]] QImage ComposeColorBackground(
		const BackgroundRequest &request);

[[nodiscard]] QImage ComposeBackground(const BackgroundRequest &request);

[[nodiscard]] float64 FarthestCorner(QPointF center, QSizeF size);

[[nodiscard]] float64 WaveRadius(float64 farthest, float64 progress);

enum class WaveSide {
	Outside,
	Inside,
	Edge,
};

[[nodiscard]] WaveSide CircleSide(
		QPointF center,
		float64 radius,
		const std::vector<QRect> &parts);

[[nodiscard]] bool MeetsCorner(const QRect &box, const QRect &area);

void PaintWaveGlow(
		QPainter &p,
		const QRect &rect,
		const QRect &area,
		QPointF center,
		float64 radius,
		float64 progress,
		const QColor &tint);

void ClipOutside(
		QPainter &p,
		const QRectF &area,
		const QPainterPath &shapes);

[[nodiscard]] PlaneState ComputePlaneState(
		crl::time elapsed,
		float64 radius);

[[nodiscard]] CardFrame ComputeCardFrame(
		crl::time elapsed,
		float64 radius);

void PaintStreaks(QPainter &p, const QRect &logo, const PlaneState &state);

void PaintPlane(
		QPainter &p,
		const QRect &logo,
		const QImage &plane,
		const PlaneState &state);

void PaintUsername(
		QPainter &p,
		const QRect &band,
		const UsernameFit &username,
		const QColor &ink);

void PaintBloom(
		QPainter &p,
		const QRect &qr,
		const CardContent &content,
		float64 bloom);

[[nodiscard]] float64 DustRandom(uint32 &seed);

void PaintSwap(
		QPainter &p,
		const QRect &qr,
		const CardContent &content,
		const QrSwap &swap,
		crl::time elapsed);

void PaintCard(
		QPainter &p,
		const CardLayout &layout,
		const CardContent &content,
		const CardFrame &frame,
		bool paintUsername = true);

} // namespace Ui::PeerQrSurfaceDetails
