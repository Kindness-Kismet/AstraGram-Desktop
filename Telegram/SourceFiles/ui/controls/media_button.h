#pragma once

#include "ui/effects/radial_animation.h"
#include "ui/style/style_core_icon.h"
#include <QtGui/QColor>

namespace Ui {

struct MediaButtonColors {
	QColor foreground;
	QColor background;
	QColor badge;
	QColor border;
	QColor fill;
};

[[nodiscard]] MediaButtonColors MediaButtonPalette(
	QColor foreground,
	QColor background,
	bool overImage = false);

void PaintMediaButtonSurface(
	QPainter &p,
	QRectF inner,
	const MediaButtonColors &colors,
	bool hovered = false);
void PaintMediaCancelBadge(
	QPainter &p,
	QRectF inner,
	const MediaButtonColors &colors,
	bool hovered = false);
[[nodiscard]] bool MediaCancelBadgeContains(QRectF inner, QPoint point);
[[nodiscard]] QRect MediaButtonPaintRect(QRect inner);

// 沿用现有下载刷新驱动，只改变进度呈现，不控制文件传输。
class MediaProgressAnimation {
public:
	template <typename Callback>
	explicit MediaProgressAnimation(Callback &&callback)
	: _radial(std::forward<Callback>(callback)) {
	}

	void start(float64 progress);
	bool update(float64 progress, bool finished, crl::time now, bool loaded);
	[[nodiscard]] bool animating() const { return _radial.animating(); }
	[[nodiscard]] float64 opacity() const { return _radial.opacity(); }
	void draw(
		QPainter &p,
		QRectF inner,
		float64 thickness,
		style::color color) const;
	void paint(
		QPainter &p,
		QRectF inner,
		const MediaButtonColors &colors,
		bool cancellable,
		bool hovered = false,
		const style::icon *readyIcon = nullptr) const;
	void paintFill(
		QPainter &p,
		QRectF inner,
		const MediaButtonColors &colors,
		bool hovered = false) const;

private:
	[[nodiscard]] float64 progressAt(crl::time now) const;
	[[nodiscard]] bool showingCheck() const;

	RadialAnimation _radial;
	float64 _from = 0.;
	float64 _target = 0.;
	crl::time _changedAt = 0;
	crl::time _completedAt = 0;
	crl::time _numberAt = 0;
	int _percent = 0;
	float64 _durationMultiplier = 1.;
};

} // namespace Ui
