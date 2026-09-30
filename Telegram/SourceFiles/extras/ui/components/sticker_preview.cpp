#include "extras/ui/components/sticker_preview.h"

#include "extras/extras_settings.h"
#include "history/view/media/history_view_sticker.h"
#include "ui/chat/chat_theme.h"
#include "ui/painter.h"
#include "window/section_widget.h"
#include "window/themes/window_theme.h"

#include "styles/style_settings.h"

#include <QtSvg/QSvgRenderer>

namespace {

[[nodiscard]] QImage renderSticker(QSvgRenderer &renderer, QSize size) {
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(size * ratio, QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	auto p = QPainter(&result);
	renderer.render(&p, QRectF(QPointF(), size));
	return result;
}

} // namespace

StickerPreview::StickerPreview(QWidget *parent)
: RpWidget(parent)
, _sticker(std::make_unique<QSvgRenderer>(
	u":/gui/icons/extras/sticker_preview.svg"_q))
, _theme(Window::Theme::DefaultChatThemeOn(lifetime())) {
	setObjectName(u"extras/messageStickerPreview"_q);
	rpl::combine(
		widthValue(),
		ExtrasSettings::getInstance().messageStickerScaleValue()
	) | rpl::on_next([=](int width, double) {
		if (width <= 0) {
			return;
		}
		// 沿用聊天贴纸的尺寸计算，窄面板内保持比例。
		const auto padding = st::settingsForwardPrivacyPadding;
		const auto available = std::max(width - 2 * padding, 1);
		const auto full = HistoryView::Sticker::Size();
		const auto size = full.scaled(
			std::min(full.width(), available),
			full.height(),
			Qt::KeepAspectRatio);
		_frame = renderSticker(*_sticker, size);
		resize(width, size.height() + 2 * padding);
		update();
	}, lifetime());
}

StickerPreview::~StickerPreview() = default;

void StickerPreview::paintEvent(QPaintEvent *event) {
	auto p = Painter(this);
	p.setClipRect(event->rect());
	Window::SectionWidget::PaintBackground(
		p,
		_theme.get(),
		QSize(width(), window()->height()),
		event->rect());
	if (_frame.isNull()) {
		return;
	}
	const auto size = _frame.size() / _frame.devicePixelRatio();
	p.drawImage(
		QPoint(
			(width() - size.width()) / 2,
			st::settingsForwardPrivacyPadding),
		_frame);
}
