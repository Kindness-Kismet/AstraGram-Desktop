#pragma once

#include "data/data_types.h"

class HistoryItem;

namespace ExtrasFeatures::MessageSelection {

struct Endpoints {
	not_null<HistoryItem*> first;
	not_null<HistoryItem*> last;
};

enum class RangeError {
	None,
	NotLoaded,
	TooMany,
};

struct Range {
	HistoryItemsList items;
	RangeError error = RangeError::None;
};

[[nodiscard]] std::optional<Endpoints> findEndpoints(
	const HistoryItemsList &selected);
[[nodiscard]] Range collectRange(
	Endpoints endpoints,
	const Fn<HistoryItem*(not_null<HistoryItem*>)> &next,
	const Fn<bool(not_null<HistoryItem*>)> &canSelect);
[[nodiscard]] QString errorText(RangeError error);

} // namespace ExtrasFeatures::MessageSelection
