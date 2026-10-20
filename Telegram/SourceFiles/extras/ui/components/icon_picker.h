#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

class IconPicker : public Ui::RpWidget
{
public:
	IconPicker(QWidget *parent);
	~IconPicker() = default;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

private:
	static constexpr int kColumns = 4;

	void drawIcon(
		QPainter &p,
		const QImage &icon,
		const QRect &rect,
		float strokeOpacity);
	[[nodiscard]] int cellSize() const;
	[[nodiscard]] int columnsCount() const;
	[[nodiscard]] int rowsCount() const;
	[[nodiscard]] QRect iconRect(int index) const;

	Ui::Animations::Simple _animation;
	QString _wasSelected;
	std::unordered_map<QString, QImage> _cachedIcons;
};
