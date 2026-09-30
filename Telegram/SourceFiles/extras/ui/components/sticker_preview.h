#pragma once

#include "ui/rp_widget.h"

#include <memory>

class QSvgRenderer;

namespace Ui {
class ChatTheme;
} // namespace Ui

class StickerPreview final : public Ui::RpWidget {
public:
	explicit StickerPreview(QWidget *parent);
	~StickerPreview();

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	const std::unique_ptr<QSvgRenderer> _sticker;
	const std::unique_ptr<Ui::ChatTheme> _theme;
	QImage _frame;
};
