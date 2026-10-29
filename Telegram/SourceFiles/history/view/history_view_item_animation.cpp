#include "history/view/history_view_item_animation.h"

#include "history/view/history_view_element.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/message_bubble.h"
#include "ui/painter.h"

namespace HistoryView {

float64 ItemExpansionProgress(float64 progress) {
	return anim::sineInOut(1., std::clamp(
		progress * kItemRevealDuration / kItemExpandDuration, 0., 1.));
}

float64 ItemRevealOpacity(float64 progress) {
	return anim::sineInOut(1., std::clamp(
		(progress * kItemRevealDuration - kItemExpandDuration)
			/ kItemFadeDuration, 0., 1.));
}

void PaintFadingItem(
		Painter &p,
		not_null<Element*> view,
		const Ui::ChatPaintContext &context,
		float64 opacity) {
	if (opacity <= 0.) {
		return;
	}
	if (opacity >= 1.) {
		view->draw(p, context);
		return;
	}
	const auto padding = Ui::BubbleShadowPadding();
	const auto area = context.clip.intersected(
		QRect(0, -padding, view->width(), view->height() + 2 * padding));
	if (area.isEmpty()) {
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	auto image = QImage(area.size() * ratio, QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(Qt::transparent);
	{
		// 整条消息统一混合，内部控件的透明度不能提前露出边框或文字。
		auto painter = Painter(&image);
		painter.translate(-area.topLeft());
		view->draw(painter, context);
	}
	p.save();
	p.setOpacity(p.opacity() * opacity);
	p.drawImage(area.topLeft(), image);
	p.restore();
}

} // namespace HistoryView
