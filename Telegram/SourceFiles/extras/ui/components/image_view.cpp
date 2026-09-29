#include "extras/ui/components/image_view.h"

#include "extras/features/message_shot/message_shot.h"
#include "extras/utils/telegram_helpers.h"
#include "styles/style_extras_styles.h"
#include "styles/style_chat.h"
#include "ui/painter.h"

ImageView::ImageView(QWidget *parent)
	: RpWidget(parent) {
}

void ImageView::setImage(const QImage &image) {
	if (this->image == image) {
		return;
	}

	const auto set = [=]
	{
		this->prevImage = this->image;
		this->image = image;

		if (!this->prevImage.isNull()
			&& !image.isNull()
			&& this->prevImage.size() == image.size()) {
			computeDiffImages(this->prevImage, image);
		} else {
			this->baseImage = QImage();
			this->prevDiffImage = QImage();
			this->newDiffImage = QImage();
		}

		const auto size = image.size() / style::DevicePixelRatio();
		setMinimumSize(size.grownBy(st::imageViewInnerPadding));

		if (this->animation.animating()) {
			this->animation.stop();
		}

		if (this->prevImage.isNull()) {
			update();
			return;
		}

		this->animation.start(
			[=]
			{
				update();
			},
			0.0,
			1.0,
			300,
			anim::easeInCubic);
	};

	if (this->image.isNull()) {
		set();
		return;
	}

	dispatchToMainThread(set, 100);
}

void ImageView::computeDiffImages(const QImage &prev, const QImage &curr) {
	const auto prevConverted = prev.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	const auto currConverted = curr.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	const auto w = prevConverted.width();
	const auto h = prevConverted.height();

	auto base = currConverted.copy();
	auto prevDiff = prevConverted.copy();
	auto newDiff = currConverted.copy();

	for (auto y = 0; y < h; ++y) {
		const auto *prevLine = reinterpret_cast<const QRgb *>(prevConverted.constScanLine(y));
		const auto *currLine = reinterpret_cast<const QRgb *>(currConverted.constScanLine(y));
		auto *baseLine = reinterpret_cast<QRgb *>(base.scanLine(y));
		auto *prevDiffLine = reinterpret_cast<QRgb *>(prevDiff.scanLine(y));
		auto *newDiffLine = reinterpret_cast<QRgb *>(newDiff.scanLine(y));

		for (auto x = 0; x < w; ++x) {
			if (prevLine[x] == currLine[x]) {
				prevDiffLine[x] = 0;
				newDiffLine[x] = 0;
			} else {
				baseLine[x] = 0;
			}
		}
	}

	this->baseImage = base;
	this->prevDiffImage = prevDiff;
	this->newDiffImage = newDiff;
}

QImage ImageView::getImage() const {
	return image;
}

void ImageView::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto brush = QBrush(ExtrasFeatures::MessageShot::makeDefaultBackgroundColor());

	QPainterPath path;
	path.addRoundedRect(rect(), st::roundRadiusLarge, st::roundRadiusLarge);

	p.fillPath(path, brush);

	// 按图片尺寸居中到内边距以内的区域。
	const auto centered = [&](const QImage &source) {
		const auto realRect = rect().marginsRemoved(st::imageViewInnerPadding);
		const auto ratio = style::DevicePixelRatio();
		return QRect(
			(realRect.width() - source.width() / ratio) / 2 + st::imageViewInnerPadding.left(),
			(realRect.height() - source.height() / ratio) / 2 + st::imageViewInnerPadding.top(),
			source.width() / ratio,
			source.height() / ratio);
	};
	const auto t = animation.value(1.0);

	if (baseImage.isNull()) {
		if (!prevImage.isNull()) {
			p.setOpacity(1.0 - t);
			p.drawImage(centered(prevImage), prevImage);
		}
		if (!image.isNull()) {
			p.setOpacity(t);
			p.drawImage(centered(image), image);
		}
		p.setOpacity(1.0);
		return;
	}

	const auto resizedRect = centered(image);
	p.drawImage(resizedRect, baseImage);
	if (t < 1.0) {
		p.setOpacity(1.0 - t);
		p.drawImage(resizedRect, prevDiffImage);
		p.setOpacity(1.0);
	}
	if (t > 0.0) {
		p.setOpacity(t);
		p.drawImage(resizedRect, newDiffImage);
		p.setOpacity(1.0);
	}
}

void ImageView::mousePressEvent(QMouseEvent *e) {
}
