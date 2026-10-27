#pragma once

#include "extras/ui/components/smooth_transition.h"

#include <QtGui/QImage>
#include <QtGui/QPixmap>

class QPainter;

namespace ExtrasUi {

// 静态内容更换共用消息过渡；连续播放使用固定内容标识，避免逐帧重新开始。
class ImageTransition final {
public:
	void paint(QPainter &p, QRect rect, const QImage &image,
		uint64 contentKey, Fn<void()> update);
	void paint(QPainter &p, QRect rect, const QPixmap &image,
		uint64 contentKey, Fn<void()> update);
	void reset();
	void continueFrom(const ImageTransition &previous);
	[[nodiscard]] bool empty() const {
		return _image.isNull();
	}

private:
	[[nodiscard]] QImage frame() const;

	SmoothTransition _progress;
	QImage _image;
	QImage _previous;
	QSize _size;
	uint64 _contentKey = 0;
	qint64 _pixmapKey = 0;
	QImage _pixmapImage;
	bool _continueAnimation = false;
};

} // namespace ExtrasUi
