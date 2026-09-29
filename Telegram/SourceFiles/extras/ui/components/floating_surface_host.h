#pragma once

#include "extras/ui/components/chat_frosted_background.h"

#include <QtCore/QPointer>

namespace ExtrasUi {

class FloatingSurface;

class FloatingSurfaceHost final : public QObject {
public:
	FloatingSurfaceHost(QWidget *root, ChatFrostedBackground::Capture capture);
	~FloatingSurfaceHost();

	void invalidate(QRect area = {});
	void clear();

private:
	friend class FloatingSurface;
	[[nodiscard]] static FloatingSurfaceHost *find(QWidget *widget);
	void add(FloatingSurface *surface);
	void remove(FloatingSurface *surface);
	void scheduleAreas();
	void refreshAreas();
	void repaintSurfaces();
	void paint(QPainter &p, FloatingSurface *surface, QColor tint) const;

	QWidget *const _root;
	ChatFrostedBackground _background;
	QTimer _areasTimer;
	std::vector<QPointer<FloatingSurface>> _surfaces;
};

} // namespace ExtrasUi
