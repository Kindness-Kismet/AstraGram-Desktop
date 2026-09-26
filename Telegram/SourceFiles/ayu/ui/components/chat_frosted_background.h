#pragma once

#include "base/functional.h"

#include <QtCore/QObject>
#include <QtCore/QRect>
#include <QtCore/QTimer>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <vector>

class Painter;
class QPainter;
class QWidget;

namespace AyuUi {

class ChatFrostedBackground final : public QObject {
public:
	using Capture = Fn<bool(Painter &, QRect)>;

	ChatFrostedBackground(QWidget *root, Capture capture, Fn<void()> changed);
	void setAreas(std::vector<QRect> areas);
	void invalidate(QRect area = {});
	void clear();
	void paint(QPainter &p, QRect area, QColor tint) const;

private:
	struct Tile {
		QRect area;
		QImage sample;
		QImage blurred;
		bool dirty = true;
	};

	void refresh();

	QWidget *const _root;
	Capture _capture;
	Fn<void()> _changed;
	QTimer _timer;
	std::vector<Tile> _tiles;
};

} // namespace AyuUi
