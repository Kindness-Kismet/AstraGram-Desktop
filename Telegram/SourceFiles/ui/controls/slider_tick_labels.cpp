/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/slider_tick_labels.h"

namespace Ui {

std::vector<SliderTickLabelPosition> LayoutSliderTickLabels(
		const std::vector<int> &centers,
		const std::vector<int> &widths,
		int outer,
		int minGap) {
	Expects(centers.size() == widths.size());

	const auto count = int(centers.size());
	auto result = std::vector<SliderTickLabelPosition>(count);
	auto total = std::max(count - 1, 0) * minGap;
	for (auto i = 0; i != count; ++i) {
		result[i].left = std::clamp(
			centers[i] - widths[i] / 2,
			0,
			std::max(outer - widths[i], 0));
		total += widths[i];
	}
	if (total > outer) {
		auto rowEnds = std::vector<int>();
		for (auto i = 0; i != count; ++i) {
			auto &position = result[i];
			while (position.row < int(rowEnds.size())
				&& position.left < rowEnds[position.row] + minGap) {
				++position.row;
			}
			if (position.row == int(rowEnds.size())) {
				rowEnds.push_back(0);
			}
			rowEnds[position.row] = position.left + widths[i];
		}
		return result;
	}
	for (auto i = 1; i < count; ++i) {
		result[i].left = std::max(
			result[i].left,
			result[i - 1].left + widths[i - 1] + minGap);
	}
	if (count > 0) {
		result[count - 1].left = std::min(
			result[count - 1].left,
			outer - widths[count - 1]);
	}
	for (auto i = count - 1; i > 0; --i) {
		result[i - 1].left = std::min(
			result[i - 1].left,
			result[i].left - minGap - widths[i - 1]);
	}
	for (auto &position : result) {
		position.left = std::max(position.left, 0);
	}
	return result;
}

} // namespace Ui
