#pragma once

#include "base/basic_types.h"
#include "rpl/lifetime.h"

#include <QtCore/QPointer>
#include <QtCore/QRect>
#include <QtGui/QColor>
#include <QtGui/QPixmap>
#include <QtWidgets/QGraphicsEffect>
#include <vector>

class QWidget;

namespace ExtrasUi {

class FloatingSurfaceHost;

struct FloatingSurfaceStyle {
	int radius = 0;
	Fn<QColor()> background;
	Fn<QColor()> border;
	int borderWidth = 1;
	bool windowMaterial = false;
	bool maskInput = false;
	Fn<qreal()> opacity;
	Fn<std::vector<QRect>()> rects;
};

// 聊天磨砂表面统一沿用输入框配色，关闭聊天背景时改用扁平配色。
[[nodiscard]] QColor ChatSurfaceBackground();
[[nodiscard]] QColor ChatSurfaceBorder();

// 悬停叠加色：亮色主题 #212121 的 10%，暗色主题 #FAFAFA 的 5%。
[[nodiscard]] QColor HoverOverlay(bool dark);

// 磨砂表面悬停时直接叠加半透明的悬停色，磨砂仍可透出。
[[nodiscard]] QColor ChatSurfaceHover();

class FloatingSurface final : public QGraphicsEffect {
public:
	static void attach(QWidget *widget, FloatingSurfaceStyle style);
	[[nodiscard]] static FloatingSurface *find(QWidget *widget);
	~FloatingSurface();

	[[nodiscard]] QWidget *widget() const;
	[[nodiscard]] bool hasBackdrop() const;
	[[nodiscard]] bool usesWindowMaterial() const;
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
	rpl::lifetime _lifetime;
};

} // namespace ExtrasUi
