#include "history/view/history_view_chat_section.h"

#include "extras/ui/components/floating_surface_host.h"
#include "history/view/history_view_list_widget.h"
#include "mainwidget.h"
#include "ui/chat/chat_style.h"
#include "ui/painter.h"
#include "ui/widgets/elastic_scroll.h"
#include "window/window_session_controller.h"

namespace HistoryView {

void ChatWidget::setupFrostedBackground() {
	_floatingSurfaceHost = std::make_unique<ExtrasUi::FloatingSurfaceHost>(
		this,
		[=](Painter &p, QRect area) {
			if (!_theme || animatingShow() || _scroll->isHidden()) {
				return false;
			}
			const auto content = controller()->content();
			const auto fromY = content->backgroundFromY();
			p.save();
			p.translate(0, fromY);
			Window::SectionWidget::PaintBackground(
				p,
				_theme.get(),
				QSize(width(), content->height()),
				area.translated(0, -fromY),
				controller()->isGifPausedAtLeastFor(
					Window::GifPauseReason::Any));
			p.restore();

			const auto position = _inner->mapTo(this, QPoint());
			const auto clip = area.intersected(_scroll->geometry());
			if (clip.isEmpty()) {
				return true;
			}
			p.translate(position);
			return _inner->paintBackdrop(p, clip.translated(-position));
		});
	paintRequest() | rpl::on_next([=](QRect clip) {
		_floatingSurfaceHost->invalidate(clip);
	}, lifetime());
	_inner->paintRequest() | rpl::on_next([=](QRect clip) {
		_floatingSurfaceHost->invalidate(
			clip.translated(_inner->mapTo(this, QPoint())));
	}, _inner->lifetime());
	// 滚动时 Qt 会复用已绘制的像素，只重绘露出的区域，需要整体刷新。
	_scroll->scrolls() | rpl::on_next([=] {
		_floatingSurfaceHost->invalidate();
	}, lifetime());
	controller()->chatStyle()->paletteChanged(
	) | rpl::on_next([=] {
		_floatingSurfaceHost->clear();
	}, lifetime());
	shownValue() | rpl::on_next([=](bool shown) {
		if (shown) {
			_floatingSurfaceHost->invalidate();
		}
	}, lifetime());
}

} // namespace HistoryView
