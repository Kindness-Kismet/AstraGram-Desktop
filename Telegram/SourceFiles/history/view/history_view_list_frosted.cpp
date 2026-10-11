#include "history/view/history_view_list_frosted.h"

#include "extras/ui/components/floating_surface_host.h"
#include "history/view/history_view_list_widget.h"
#include "mainwidget.h"
#include "ui/painter.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"

namespace HistoryView {

std::unique_ptr<ExtrasUi::FloatingSurfaceHost> SetupListFrostedBackground(
		ListFrostedDescriptor &&descriptor) {
	const auto root = descriptor.root;
	const auto scroll = descriptor.scroll;
	const auto list = descriptor.list;
	auto result = std::make_unique<ExtrasUi::FloatingSurfaceHost>(
		root,
		[=, paint = std::move(descriptor.paintBackground)](
				Painter &p,
				QRect area) {
			if (scroll->isHidden() || !paint(p, area)) {
				return false;
			}
			const auto position = list->mapTo(root, QPoint());
			const auto clip = area.intersected(scroll->geometry());
			if (clip.isEmpty()) {
				return true;
			}
			p.translate(position);
			return list->paintBackdrop(p, clip.translated(-position));
		});
	const auto host = result.get();
	root->paintRequest() | rpl::on_next([=](QRect clip) {
		host->invalidate(clip);
	}, host->lifetime());
	list->paintRequest() | rpl::on_next([=](QRect clip) {
		host->invalidate(clip.translated(list->mapTo(root, QPoint())));
	}, host->lifetime());
	// 滚动时 Qt 会复用已绘制的像素，只重绘露出的区域，需要整体刷新。
	std::move(descriptor.scrolls) | rpl::on_next([=] {
		host->invalidate();
	}, host->lifetime());
	std::move(descriptor.paletteChanged) | rpl::on_next([=] {
		host->clear();
	}, host->lifetime());
	root->shownValue() | rpl::on_next([=](bool shown) {
		if (shown) {
			host->invalidate();
		}
	}, host->lifetime());
	return result;
}

Fn<bool(QPainter &p, QRect area)> SectionFrostedBackground(
		not_null<Window::SectionWidget*> section,
		Fn<Ui::ChatTheme*()> theme) {
	return [=](QPainter &p, QRect area) {
		const auto chatTheme = theme();
		if (!chatTheme || section->animatingShow()) {
			return false;
		}
		const auto controller = section->controller();
		const auto content = controller->content();
		const auto fromY = content->backgroundFromY();
		p.save();
		p.translate(0, fromY);
		Window::SectionWidget::PaintBackground(
			p,
			chatTheme,
			QSize(section->width(), content->height()),
			area.translated(0, -fromY),
			controller->isGifPausedAtLeastFor(Window::GifPauseReason::Any));
		p.restore();
		return true;
	};
}

} // namespace HistoryView
