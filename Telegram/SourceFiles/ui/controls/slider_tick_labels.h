/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {

struct SliderTickLabelPosition {
	int left = 0;
	int row = 0;
};

// 刻度文字保持在容器内；单行放不下时按刻度位置分行。
[[nodiscard]] std::vector<SliderTickLabelPosition> LayoutSliderTickLabels(
	const std::vector<int> &centers,
	const std::vector<int> &widths,
	int outer,
	int minGap);

} // namespace Ui
