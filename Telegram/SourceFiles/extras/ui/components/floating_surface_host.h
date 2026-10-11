#pragma once

#include "extras/ui/components/chat_frosted_background.h"
#include "rpl/lifetime.h"

#include <QtCore/QPointer>

namespace ExtrasUi {

class FloatingSurface;

class FloatingSurfaceHost final : public QObject {
public:
	FloatingSurfaceHost(QWidget *root, ChatFrostedBackground::Capture capture);
	~FloatingSurfaceHost();

	void invalidate(QRect area = {});
	void clear();
	// 刷新背景的订阅随宿主销毁，避免宿主先于背景源释放后仍被调用。
	[[nodiscard]] rpl::lifetime &lifetime();

private:
	friend class FloatingSurface;
	[[nodiscard]] static FloatingSurfaceHost *find(QWidget *widget);
	void add(FloatingSurface *surface);
	void remove(FloatingSurface *surface);
	void scheduleAreas();
	void refreshAreas();
	void repaintSurfaces();
	void paint(QPainter &p, FloatingSurface *surface, QColor tint);

	QWidget *const _root;
	ChatFrostedBackground _background;
	QTimer _areasTimer;
	std::vector<QPointer<FloatingSurface>> _surfaces;
	rpl::lifetime _lifetime;
};

} // namespace ExtrasUi
