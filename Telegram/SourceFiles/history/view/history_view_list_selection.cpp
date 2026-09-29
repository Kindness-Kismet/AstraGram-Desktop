#include "history/view/history_view_list_widget.h"

#include "extras/utils/telegram_helpers.h"
#include "extras/features/message_selection/selection_range.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"

namespace HistoryView {

HistoryItemsList ListWidget::selectedRangeItems() const {
	auto result = HistoryItemsList();
	result.reserve(_selected.size());
	for (const auto &[id, selection] : _selected) {
		if (const auto item = session().data().message(id)) {
			result.push_back(item);
		}
	}
	return result;
}

bool ListWidget::canSelectItemsBetween() const {
	return !hasSelectRestriction()
		&& ExtrasFeatures::MessageSelection::findEndpoints(selectedRangeItems()).has_value();
}

void ListWidget::selectItemsBetween() {
	using namespace ExtrasFeatures::MessageSelection;
	if (hasSelectRestriction()) {
		return;
	}
	auto endpoints = findEndpoints(selectedRangeItems());
	if (!endpoints) {
		return;
	}
	const auto size = int(_items.size());
	const auto indexOf = [=](not_null<HistoryItem*> item) {
		const auto view = viewForItem(item);
		return int(ranges::find_if(_items, [&](not_null<Element*> e) {
			return e.get() == view;
		}) - begin(_items));
	};
	const auto firstIndex = indexOf(endpoints->first);
	const auto lastIndex = indexOf(endpoints->last);
	if (firstIndex == size || lastIndex == size) {
		controller()->showToast(errorText(RangeError::NotLoaded));
		return;
	}
	// 按显示顺序遍历，导入消息的日期与编号顺序可能相反。
	if (lastIndex < firstIndex) {
		std::swap(endpoints->first, endpoints->last);
	}
	const auto range = collectRange(*endpoints, [=](not_null<HistoryItem*> item) {
		const auto index = indexOf(item) + 1;
		return (index < size) ? _items[index]->data().get() : nullptr;
	}, [=](not_null<HistoryItem*> item) {
		return _delegate->listIsItemGoodForSelection(item)
			&& !isMessageHidden(item);
	});
	if (range.error != RangeError::None) {
		controller()->showToast(errorText(range.error));
		return;
	}
	auto selected = SelectedMap();
	for (const auto item : range.items) {
		addToSelection(selected, item);
	}
	clearTextSelection();
	_selected = std::move(selected);
	_accessibilitySelectionAnchor = nullptr;
	pushSelectedItems();
	update();
}

} // namespace HistoryView
