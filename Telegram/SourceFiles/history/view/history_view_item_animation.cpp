#include "history/view/history_view_item_animation.h"

#include "history/view/history_view_element.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/message_bubble.h"
#include "ui/effects/premium_3d_support.h"
#include "ui/painter.h"

namespace HistoryView {

crl::time ItemRevealDuration(int height) {
	const auto extra = std::max(0., height * 100. / style::Scale() - 64.);
	return 240 + std::min(40, int(extra * .18));
}

int ItemRevealDisplacement(int height) {
	return std::min(style::ConvertScale(42),
		style::ConvertScale(22) + int(height * .28));
}

float64 ItemExpansionProgress(float64 progress) {
	return Ui::Premium::CubicBezier(.22, .72, .3, 1., progress);
}

float64 ItemRevealOpacity(float64 progress) {
	const auto time = std::clamp(progress / .7, 0., 1.);
	return time * time * (3. - 2. * time);
}

void PaintItemReveal(
		Painter &p,
		QRect bounds,
		QRect clip,
		ItemRevealState state,
		int bottom,
		Fn<void(Painter&, int)> paint) {
	const auto opacity = ItemRevealOpacity(state.progress);
	if (opacity <= 0.) {
		return;
	}
	const auto shift = int(std::round(state.shift));
	if (state.progress >= 1.) {
		p.save();
		p.translate(0, shift);
		paint(p, shift);
		p.restore();
		return;
	}
	const auto edge = bottom - shift;
	const auto fade = style::ConvertScale(10);
	const auto area = clip.translated(0, -shift).intersected(bounds);
	if (area.isEmpty()) {
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	auto image = QImage(area.size() * ratio, QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(Qt::transparent);
	{
		auto painter = Painter(&image);
		painter.translate(-area.topLeft());
		paint(painter, shift);
		painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		auto gradient = QLinearGradient(0, edge - fade, 0, edge);
		gradient.setColorAt(0., Qt::black);
		gradient.setColorAt(1., Qt::transparent);
		painter.fillRect(area, gradient);
	}
	p.save();
	p.setOpacity(p.opacity() * opacity);
	p.drawImage(area.topLeft() + QPoint(0, shift), image);
	p.restore();
}

void PaintRevealingItem(
		Painter &p,
		not_null<Element*> view,
		const Ui::ChatPaintContext &context,
		ItemRevealState state,
		int bottom) {
	const auto padding = Ui::BubbleShadowPadding();
	PaintItemReveal(p,
		QRect(0, -padding, view->width(), view->height() + 2 * padding),
		context.clip, state, bottom, [&](Painter &painter, int shift) {
			// 始终绘制原视图，渐显和上沿遮罩不重建媒体播放器。
			view->draw(painter, context.translated(0, -shift));
		});
}

} // namespace HistoryView
