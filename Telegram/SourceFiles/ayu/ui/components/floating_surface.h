#pragma once

#include "base/basic_types.h"

#include <QtCore/QPointer>
#include <QtCore/QRect>
#include <QtGui/QColor>
#include <QtGui/QPixmap>
#include <QtWidgets/QGraphicsEffect>
#include <vector>

class QWidget;

namespace AyuUi {

class FloatingSurfaceHost;

struct FloatingSurfaceStyle {
	int radius = 0;
	Fn<QColor()> background;
	Fn<QColor()> border;
	int borderWidth = 1;
	bool maskInput = false;
	Fn<qreal()> opacity;
	Fn<std::vector<QRect>()> rects;
};

// 聊天磨砂表面统一沿用输入框配色，关闭聊天背景时改用扁平配色。
[[nodiscard]] QColor ChatSurfaceBackground();
[[nodiscard]] QColor ChatSurfaceBackgroundOver();
[[nodiscard]] QColor ChatSurfaceBorder();

class FloatingSurface final : public QGraphicsEffect {
public:
	static void attach(QWidget *widget, FloatingSurfaceStyle style);
	[[nodiscard]] static FloatingSurface *find(QWidget *widget);
	~FloatingSurface();

	[[nodiscard]] QWidget *widget() const;
	[[nodiscard]] bool hasBackdrop() const;
	void refreshGeometry();

protected:
	void draw(QPainter *p) override;
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	friend class FloatingSurfaceHost;
	FloatingSurface(QWidget *widget, FloatingSurfaceStyle style);
	void rebind();
	void setHost(FloatingSurfaceHost *host);
	void updateInputMask();
	void updateMask(QSize size, qreal ratio);
	[[nodiscard]] QRect visibleArea(QWidget *root) const;

	QPointer<QWidget> _widget;
	QPointer<FloatingSurfaceHost> _host;
	FloatingSurfaceStyle _style;
	std::vector<QPointer<QWidget>> _ancestors;
	std::vector<QRect> _rects;
	QPixmap _mask;
};

} // namespace AyuUi
