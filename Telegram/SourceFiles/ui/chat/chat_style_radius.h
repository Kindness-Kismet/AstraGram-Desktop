/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {

inline constexpr auto kBubbleRadiusSliderMin = 0;
inline constexpr auto kBubbleRadiusSliderMax = 16;
inline constexpr auto kBubbleRadiusSliderMidpoint
	= (kBubbleRadiusSliderMin + kBubbleRadiusSliderMax) / 2;

class BubbleRadiusOverride final {
public:
	explicit BubbleRadiusOverride(int value);
	BubbleRadiusOverride(const BubbleRadiusOverride &) = delete;
	BubbleRadiusOverride &operator=(const BubbleRadiusOverride &) = delete;
	~BubbleRadiusOverride();

private:
	int _previous = -1;

};

void SetAppliedBubbleRadius(int value);

enum class BubbleCornerRounding : uchar;

[[nodiscard]] int BubbleRadiusSmall();
[[nodiscard]] int BubbleRadiusLarge();
[[nodiscard]] int BubbleCornerRadius(BubbleCornerRounding corner);

[[nodiscard]] int MsgFileThumbRadiusSmall();
[[nodiscard]] int MsgFileThumbRadiusLarge();
[[nodiscard]] bool TakeLegacySmallBubbleRadius();

extern const char kOptionUseSmallMsgBubbleRadius[];

} // namespace Ui
