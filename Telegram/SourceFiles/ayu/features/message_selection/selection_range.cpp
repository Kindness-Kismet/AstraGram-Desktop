#include "ayu/features/message_selection/selection_range.h"

#include "config.h"
#include "data/data_groups.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"

namespace AyuFeatures::MessageSelection {
namespace {

[[nodiscard]] not_null<HistoryItem*> groupLeader(
		not_null<HistoryItem*> item) {
	const auto group = item->history()->owner().groups().find(item);
	return group ? group->items.front() : item;
}

} // namespace

std::optional<Endpoints> findEndpoints(const HistoryItemsList &selected) {
	auto first = (HistoryItem*)nullptr;
	auto last = (HistoryItem*)nullptr;
	for (const auto item : selected) {
		const auto leader = groupLeader(item);
		if (leader == first || leader == last) {
			continue;
		}
		if (!first) {
			first = leader.get();
			continue;
		}
		if (last) {
			return std::nullopt;
		}
		last = leader.get();
	}
	return (first && last)
		? std::make_optional(Endpoints{ first, last })
		: std::nullopt;
}

Range collectRange(
		Endpoints endpoints,
		const Fn<HistoryItem*(not_null<HistoryItem*>)> &next,
		const Fn<bool(not_null<HistoryItem*>)> &canSelect) {
	auto result = Range();
	auto seen = base::flat_set<not_null<HistoryItem*>>();
	const auto append = [&](not_null<HistoryItem*> item) {
		if (canSelect(item) && seen.emplace(item).second) {
			result.items.push_back(item);
		}
	};
	for (auto current = endpoints.first.get(); current; current = next(current)) {
		const auto group = current->history()->owner().groups().find(current);
		if (group) {
			for (const auto item : group->items) {
				append(item);
			}
		} else {
			append(current);
		}
		// 相册完整计数，超过上限时不返回部分区间。
		if (result.items.size() > MaxSelectedItems) {
			return { .error = RangeError::TooMany };
		}
		if (groupLeader(current) == endpoints.last) {
			return result;
		}
	}
	return { .error = RangeError::NotLoaded };
}

QString errorText(RangeError error) {
	switch (error) {
	case RangeError::NotLoaded:
		return tr::ayu_SelectBetweenNotLoaded(tr::now);
	case RangeError::TooMany:
		return tr::ayu_SelectBetweenLimit(
			tr::now,
			lt_limit,
			QString::number(MaxSelectedItems));
	case RangeError::None:
		return QString();
	}
	Unexpected("Unknown selection range error.");
}

} // namespace AyuFeatures::MessageSelection
