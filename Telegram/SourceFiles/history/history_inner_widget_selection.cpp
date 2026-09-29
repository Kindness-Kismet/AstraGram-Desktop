#include "history/history_inner_widget.h"

#include "ayu/utils/telegram_helpers.h"
#include "ayu/features/message_selection/selection_range.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_widget.h"
#include "history/view/history_view_element.h"
#include "window/window_session_controller.h"

void HistoryInner::selectItemsBetween() {
	using namespace AyuFeatures::MessageSelection;
	if (hasSelectRestriction()) {
		return;
	}
	auto endpoints = findEndpoints(HistoryItemsList(
		_selected.begin(),
		_selected.end()));
	if (!endpoints) {
		return;
	}
	// 按显示位置定序，导入消息的日期与编号顺序可能相反。
	const auto firstTop = itemTop(endpoints->first);
	const auto lastTop = itemTop(endpoints->last);
	if (firstTop < 0 || lastTop < 0) {
		_controller->showToast(errorText(RangeError::NotLoaded));
		return;
	}
	if (lastTop < firstTop) {
		std::swap(endpoints->first, endpoints->last);
	}
	const auto range = collectRange(*endpoints, [=](not_null<HistoryItem*> item) {
		const auto view = nextItem(viewByItem(item));
		return view ? view->data().get() : nullptr;
	}, [](not_null<HistoryItem*> item) {
		return item->canBeSelected() && !isMessageHidden(item);
	});
	if (range.error != RangeError::None) {
		_controller->showToast(errorText(range.error));
		return;
	}
	clearTextSelection();
	_selected = SelectedItems(range.items.begin(), range.items.end());
	_accessibilitySelectionAnchor = nullptr;
	update();
	_widget->updateTopBarSelection();
}
