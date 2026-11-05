#include "history/view/history_view_list_widget.h"

#include "history/history_item.h"
#include "history/view/history_view_item_animation.h"

namespace HistoryView {
namespace {

constexpr auto kItemReorderDuration = crl::time(150);

} // namespace

auto ListWidget::captureItemOrder() const -> std::vector<ItemOrderPosition> {
	if (!_showFinished || _inverted || anim::Disabled()
		|| _visibleBottom <= _visibleTop) {
		return {};
	}
	auto result = std::vector<ItemOrderPosition>();
	result.reserve(_items.size());
	for (const auto view : _items) {
		result.push_back({ view->data(), itemVisualTop(view) - _visibleTop });
	}
	return result;
}

void ListWidget::animateItemReorder(
		const std::vector<ItemOrderPosition> &previous) {
	if (previous.empty() || anim::Disabled()) {
		return;
	}
	auto oldIndices = base::flat_map<not_null<HistoryItem*>, int>();
	for (auto index = 0; index != int(previous.size()); ++index) {
		oldIndices.emplace(previous[index].item, index);
	}
	struct CommonItem {
		not_null<Element*> view;
		int oldIndex = 0;
	};
	auto common = std::vector<CommonItem>();
	auto order = std::vector<int>();
	for (const auto view : _items) {
		if (const auto i = oldIndices.find(view->data()); i != end(oldIndices)) {
			common.push_back({ view, i->second });
			order.push_back(i->second);
		}
	}
	// 只比较共有消息的相对次序，追加、补载或删除不触发换位。
	if (std::is_sorted(begin(order), end(order))) {
		return;
	}
	std::sort(begin(order), end(order));
	auto first = 0;
	auto last = int(order.size());
	while (common[first].oldIndex == order[first]) {
		++first;
	}
	while (common[last - 1].oldIndex == order[last - 1]) {
		--last;
	}
	for (auto index = first; index != last; ++index) {
		const auto &[view, oldIndex] = common[index];
		const auto before = previous[oldIndex].top;
		const auto target = itemVisualTop(view) - _visibleTop - itemReorderShift(view);
		const auto offset = before - target;
		const auto visible = std::max(before, target) + view->height() > 0
			&& std::min(before, target) < _visibleBottom - _visibleTop;
		if (!visible || view->isHidden() || std::abs(offset) < .5) {
			_itemReorderAnimations.remove(view);
			continue;
		}
		// 新确认从当前画面位置续接，不沿用上次动画的坐标基准。
		auto &animation = _itemReorderAnimations[view];
		animation.stop();
		animation.start([=] { reorderItemsCallback(); },
			offset, 0., kItemReorderDuration, anim::easeOutCubic);
	}
	update();
}

void ListWidget::reorderItemsCallback() {
	for (auto i = begin(_itemReorderAnimations); i != end(_itemReorderAnimations);) {
		if (!i->second.animating()) {
			i = _itemReorderAnimations.erase(i);
		} else {
			++i;
		}
	}
	// 重排只影响绘制和命中，不改变列表高度或滚动锚点。
	update();
	if (!_resizePending && _mouseAction == MouseAction::None) {
		mouseActionUpdate();
	}
}

float64 ListWidget::itemReorderShift(not_null<const Element*> view) const {
	if (anim::Disabled()) {
		return 0.;
	}
	const auto i = _itemReorderAnimations.find(view);
	return (i != end(_itemReorderAnimations)) ? i->second.value(0.) : 0.;
}

float64 ListWidget::itemVisualTop(not_null<const Element*> view) const {
	const auto top = itemTop(view);
	auto result = float64(top);
	for (const auto &gap : collapseGaps()) {
		if (top >= gap.absY) {
			result += gap.height;
		}
	}
	return result + itemRevealState(view).shift;
}

QRect ListWidget::itemAnimationClip(QRect clip) const {
	auto result = clip.united(clip.translated(0, _itemsRevealHeight));
	const auto include = [&](not_null<Element*> view) {
		const auto shift = int(std::round(itemRevealState(view).shift));
		result = result.united(clip.translated(0, -shift));
	};
	for (const auto &[view, animation] : _itemRevealAnimations) {
		include(view);
	}
	for (const auto &[view, animation] : _itemReorderAnimations) {
		include(view);
	}
	return result;
}

Element *ListWidget::lookupItemByVisualPoint(QPoint point) const {
	if (_itemReorderAnimations.empty() && _itemRevealAnimations.empty()) {
		return strictFindItemByY(point.y());
	}
	auto fallback = (Element*)nullptr;
	// 交叉期间后绘制的气泡位于上层，点击采用相同次序。
	for (const auto view : ranges::views::reverse(_items)) {
		if (view->isHidden() || itemRevealOpacity(view) <= 0.) {
			continue;
		}
		const auto local = mapPointToItem(point, view);
		if (local.y() < 0 || local.y() >= view->height()) {
			continue;
		}
		if (view->pointState(local) != PointState::Outside) {
			return view;
		}
		if (!fallback) {
			fallback = view;
		}
	}
	return fallback;
}

} // namespace HistoryView
