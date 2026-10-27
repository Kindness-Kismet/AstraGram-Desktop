#include "extras/ui/components/image_transition.h"

#include <QtGui/QPainter>
#include <utility>

namespace ExtrasUi {

void ImageTransition::paint(
		QPainter &p,
		QRect rect,
		const QImage &image,
		uint64 contentKey,
		Fn<void()> update) {
	if (image.isNull()) {
		return;
	}
	const auto sameSize = (_size == rect.size())
		&& (_image.size() == image.size())
		&& (_image.devicePixelRatio() == image.devicePixelRatio());
	const auto continuing = std::exchange(_continueAnimation, false);
	if ((_contentKey != contentKey || continuing)
		&& sameSize && !anim::Disabled()) {
		_previous = frame();
		_progress.jumpTo(0.);
		_progress.animateTo(1., std::move(update));
	} else if (!sameSize || anim::Disabled()) {
		_previous = QImage();
		_progress.jumpTo(1.);
	}
	_contentKey = contentKey;
	_size = rect.size();
	_image = image;
	if (!_progress.animating()) {
		_previous = QImage();
	}
	p.drawImage(rect, frame());
}

void ImageTransition::paint(
		QPainter &p,
		QRect rect,
		const QPixmap &image,
		uint64 contentKey,
		Fn<void()> update) {
	if (_pixmapKey != image.cacheKey()) {
		_pixmapKey = image.cacheKey();
		_pixmapImage = image.toImage();
	}
	paint(p, rect, _pixmapImage, contentKey, std::move(update));
}

QImage ImageTransition::frame() const {
	if (_previous.isNull() || !_progress.animating()) {
		return _image;
	}
	auto result = _previous.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	{
		auto p = QPainter(&result);
		// Source 按覆盖率插值原画面，透明边缘与不透明区域使用同一比例。
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setOpacity(_progress.value());
		p.drawImage(0, 0, _image);
	}
	return result;
}

void ImageTransition::reset() {
	_progress.jumpTo(1.);
	_image = _previous = _pixmapImage = QImage();
	_size = QSize();
	_contentKey = 0;
	_pixmapKey = 0;
	_continueAnimation = false;
}

void ImageTransition::continueFrom(const ImageTransition &previous) {
	const auto continuing = previous._continueAnimation
		|| previous._progress.animating();
	reset();
	_image = previous.frame();
	_size = previous._size;
	_contentKey = previous._contentKey;
	_continueAnimation = continuing;
}

} // namespace ExtrasUi
