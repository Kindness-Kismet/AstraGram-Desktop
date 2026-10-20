#pragma once

#include "ui/rp_widget.h"

#include <memory>

class QSvgRenderer;

class StickerPreview final : public Ui::RpWidget {
public:
	explicit StickerPreview(QWidget *parent);
	~StickerPreview();

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	const std::unique_ptr<QSvgRenderer> _sticker;
	QImage _frame;
};
