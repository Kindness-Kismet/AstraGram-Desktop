#pragma once

#include "base/basic_types.h"

#include <QtCore/QObject>
#include <QtCore/QRect>
#include <QtCore/QTimer>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <vector>

class Painter;
class QPainter;
class QWidget;

namespace ExtrasUi {

class ChatFrostedBackground final : public QObject {
public:
	using Capture = Fn<bool(Painter &, QRect)>;

	ChatFrostedBackground(QWidget *root, Capture capture, Fn<void()> changed);
	void setAreas(std::vector<QRect> areas);
	void invalidate(QRect area = {});
	void clear();
	// 绘制前同步刷新对应区域，模糊层与本帧内容一致。
	void paint(QPainter &p, QRect area, QColor tint);

private:
	struct Tile {
		QRect area;
		QImage sample;
		QImage blurred;
		bool dirty = true;
	};

	void refresh();
	[[nodiscard]] bool refreshTile(Tile &tile);

	QWidget *const _root;
	Capture _capture;
	Fn<void()> _changed;
	QTimer _timer;
	std::vector<Tile> _tiles;
};

} // namespace ExtrasUi
