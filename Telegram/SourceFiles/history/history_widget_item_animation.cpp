#include "history/history_widget.h"

#include "chat_helpers/message_field.h"
#include "history/history_inner_widget.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_item_animation.h"
#include "ui/chat/chat_style.h"
#include "styles/style_chat_helpers.h"

HistoryView::ItemRevealState HistoryWidget::itemRevealState(
		not_null<HistoryItem*> item) const {
	if (anim::Disabled()) {
		return {};
	}
	if (_itemRevealPending.contains(item)) {
		return { .progress = 0. };
	}
	const auto i = _itemRevealAnimations.find(item);
	if (i == end(_itemRevealAnimations)) {
		return {};
	}
	const auto progress = i->second.animation.value(1.);
	const auto top = _list->itemTop(item);
	auto revealHeight = 0;
	// 后来的消息只随列表展开让位，不立即改变前一条的入场位置。
	for (const auto &[other, animation] : _itemRevealAnimations) {
		if (_list->itemTop(other) <= top) {
			revealHeight += anim::interpolate(animation.startHeight, 0,
				HistoryView::ItemExpansionProgress(animation.animation.value(1.)));
		}
	}
	const auto fieldShrink = _field->getTextWithTags().text.isEmpty()
		? std::max(0, _field->height() - MessageFieldMinHeight())
		: 0;
	const auto headerShrink = hasComposeHeader() ? 0 : composeHeaderHeight();
	return {
		.progress = progress,
		.shift = -revealHeight + fieldShrink + headerShrink
			+ i->second.displacement
				* (1. - HistoryView::ItemExpansionProgress(progress)),
	};
}

QRect HistoryWidget::itemRevealClip(QRect clip) const {
	auto result = clip;
	for (const auto &[item, animation] : _itemRevealAnimations) {
		const auto shift = int(std::round(itemRevealState(item).shift));
		result = result.united(clip.translated(0, -shift));
	}
	return result;
}

void HistoryWidget::paintRevealingItem(
		Painter &p,
		not_null<HistoryView::Element*> view,
		const Ui::ChatPaintContext &context) const {
	const auto extension = composeOverlap()
		? st::historyComposeCapsuleMargin / 2 - style::ConvertScale(2)
		: 0;
	HistoryView::PaintRevealingItem(p, view, context,
		itemRevealState(view->data()),
		context.area.y() + context.area.height() + extension);
}
