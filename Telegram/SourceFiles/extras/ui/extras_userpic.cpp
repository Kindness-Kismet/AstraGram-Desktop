#include "extras/ui/extras_userpic.h"

#include "extras/extras_settings.h"
#include "extras/extras_ui_settings.h"
#include "media/streaming/media_streaming_common.h"
#include "ui/image/image_prepare.h"
#include "ui/style/style_core.h"
#include "base/algorithm.h"

#include <QRectF>

namespace ExtrasUserpic {

bool ShouldOverrideShape(Ui::PeerUserpicShape shape) {
	using Shape = Ui::PeerUserpicShape;
	switch (shape) {
	case Shape::Circle:
	case Shape::Auto:
		return true;
	case Shape::Monoforum:
	case Shape::Forum:
		return ExtrasSettings::getInstance().singleCornerRadius();
	}
	return false;
}

int ComputeRadius(int pixelSize) {
	const auto corners = ExtrasUiSettings::getAvatarCorners();
	if (corners >= ExtrasUiSettings::kMaxAvatarCorners) return pixelSize / 2;
	if (corners <= 0) return 0;
	return int(double(corners) / ExtrasUiSettings::kMaxAvatarCorners * pixelSize / 2.0);
}

double ComputeRadiusF(double size) {
	const auto corners = ExtrasUiSettings::getAvatarCorners();
	if (corners >= ExtrasUiSettings::kMaxAvatarCorners) return size / 2.0;
	if (corners <= 0) return 0.0;
	return double(corners) / ExtrasUiSettings::kMaxAvatarCorners * size / 2.0;
}

bool IsCircle() {
	return ExtrasUiSettings::getAvatarCorners() >= ExtrasUiSettings::kMaxAvatarCorners;
}

uint8 PackedState() {
	return uint8(ExtrasUiSettings::getAvatarCorners() & 0x1F)
		| (ExtrasSettings::getInstance().singleCornerRadius() ? 0x20 : 0);
}

void PaintShape(QPainter &p, int x, int y, int size) {
	const auto corners = ExtrasUiSettings::getAvatarCorners();
	if (corners >= ExtrasUiSettings::kMaxAvatarCorners) {
		p.drawEllipse(x, y, size, size);
	} else if (corners <= 0) {
		p.drawRect(x, y, size, size);
	} else {
		const auto r = double(corners) / ExtrasUiSettings::kMaxAvatarCorners * size / 2.0;
		p.drawRoundedRect(x, y, size, size, r, r);
	}
}

void PaintShape(QPainter &p, const QRectF &rect) {
	const auto corners = ExtrasUiSettings::getAvatarCorners();
	if (corners >= ExtrasUiSettings::kMaxAvatarCorners) {
		p.drawEllipse(rect);
	} else if (corners <= 0) {
		p.drawRect(rect);
	} else {
		const auto r = double(corners) / ExtrasUiSettings::kMaxAvatarCorners
			* std::min(rect.width(), rect.height()) / 2.0;
		p.drawRoundedRect(rect, r, r);
	}
}

QPointF OnlineBadgePosition(int photoSize, double badgeSize, double stroke) {
	const auto corners = ExtrasUiSettings::getAvatarCorners();
	const auto r = double(corners) / ExtrasUiSettings::kMaxAvatarCorners * photoSize / 2.0;
	const auto edge = photoSize - r * (1.0 - std::cos(M_PI / 4.0));
	const auto maxPos = (stroke > 0)
		? photoSize - badgeSize / 2.0 - (badgeSize / 2.0 + stroke / 2.0) / std::sqrt(2.0)
		: double(photoSize) - badgeSize;
	const auto pos = std::min(edge - badgeSize / 2.0, maxPos);
	return QPointF(pos, pos);
}

QRect OnlineBadgeRect(int photoSize, int badgeSize, int stroke) {
	const auto pos = OnlineBadgePosition(photoSize, badgeSize, stroke);
	return QRect(
		int(base::SafeRound(pos.x())),
		int(base::SafeRound(pos.y())),
		badgeSize,
		badgeSize);
}

void ApplyFrameRounding(
		::Media::Streaming::FrameRequest &request,
		std::array<QImage, 4> &cornersCache,
		QImage &ellipseCache,
		QSize size) {
	const auto minSide = std::min(size.width(), size.height());
	const auto r = ComputeRadius(minSide);
	const auto ratio = style::DevicePixelRatio();
	if (r > 0 && r < minSide / 2) {
		if (cornersCache[0].width() != r * ratio) {
			cornersCache = Images::CornersMask(r);
		}
		request.rounding = Images::CornersMaskRef(cornersCache);
	} else if (r >= minSide / 2) {
		if (ellipseCache.size() != request.outer) {
			ellipseCache = Images::EllipseMask(size);
		}
		request.mask = ellipseCache;
	}
}

} // namespace ExtrasUserpic
