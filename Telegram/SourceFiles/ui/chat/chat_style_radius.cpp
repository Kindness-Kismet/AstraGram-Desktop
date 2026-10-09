/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/chat_style_radius.h"

#include "base/options.h"
#include "ui/chat/message_bubble.h"

#include <algorithm>

#include "styles/style_chat.h"

namespace Ui {
namespace {

constexpr auto kNoBubbleRadiusOverride = -1;
constexpr auto kZeroBubbleRadius = 0;

auto AppliedBubbleRadius = kBubbleRadiusSliderMax;
thread_local auto BubbleRadiusOverrideValue = kNoBubbleRadiusOverride;

[[nodiscard]] int ClampBubbleRadiusValue(int value) {
	return (value < kBubbleRadiusSliderMin)
		? kBubbleRadiusSliderMin
		: (value > kBubbleRadiusSliderMax)
		? kBubbleRadiusSliderMax
		: value;
}

[[nodiscard]] int EffectiveBubbleRadiusValue() {
	return (BubbleRadiusOverrideValue != kNoBubbleRadiusOverride)
		? BubbleRadiusOverrideValue
		: AppliedBubbleRadius;
}

[[nodiscard]] constexpr int InterpolateBubbleRadius(
		int sliderValue,
		int sliderFrom,
		int sliderTill,
		int radiusFrom,
		int radiusTill) {
	const auto sliderDistance = sliderTill - sliderFrom;
	const auto radiusDistance = radiusTill - radiusFrom;
	const auto progress = sliderValue - sliderFrom;
	return radiusFrom
		+ ((progress * radiusDistance + (sliderDistance / 2))
			/ sliderDistance);
}

[[nodiscard]] constexpr int MapBubbleRadius(
		int sliderValue,
		int midpointRadius,
		int maximum) {
	if (sliderValue <= kBubbleRadiusSliderMin
		|| maximum <= kZeroBubbleRadius) {
		return kZeroBubbleRadius;
	} else if (sliderValue >= kBubbleRadiusSliderMax) {
		return maximum;
	}
	const auto midpoint = std::clamp(
		midpointRadius,
		kBubbleRadiusSliderMin,
		maximum);
	return (sliderValue <= kBubbleRadiusSliderMidpoint)
		? InterpolateBubbleRadius(
			sliderValue,
			kBubbleRadiusSliderMin,
			kBubbleRadiusSliderMidpoint,
			kZeroBubbleRadius,
			midpoint)
		: InterpolateBubbleRadius(
			sliderValue,
			kBubbleRadiusSliderMidpoint,
			kBubbleRadiusSliderMax,
			midpoint,
			maximum);
}

base::options::toggle UseSmallMsgBubbleRadius({
	.id = kOptionUseSmallMsgBubbleRadius,
	.name = "Use small message bubble radius",
	.description = "Makes most message bubbles square-ish.",
	.restartRequired = true,
});

} // namespace

const char kOptionUseSmallMsgBubbleRadius[] = "use-small-msg-bubble-radius";

BubbleRadiusOverride::BubbleRadiusOverride(int value)
: _previous(BubbleRadiusOverrideValue) {
	BubbleRadiusOverrideValue = ClampBubbleRadiusValue(value);
}

BubbleRadiusOverride::~BubbleRadiusOverride() {
	BubbleRadiusOverrideValue = _previous;
}

void SetAppliedBubbleRadius(int value) {
	AppliedBubbleRadius = ClampBubbleRadiusValue(value);
}

int BubbleRadiusSmall() {
	return MapBubbleRadius(
		EffectiveBubbleRadiusValue(),
		st::bubbleRadiusSmall,
		st::bubbleRadiusSmall);
}

int BubbleRadiusLarge() {
	return MapBubbleRadius(
		EffectiveBubbleRadiusValue(),
		st::bubbleRadiusSmall,
		st::bubbleRadiusLarge);
}

int BubbleCornerRadius(BubbleCornerRounding corner) {
	switch (corner) {
	case BubbleCornerRounding::None:
	case BubbleCornerRounding::Tail: return 0;
	case BubbleCornerRounding::Small: return BubbleRadiusSmall();
	case BubbleCornerRounding::Large: return BubbleRadiusLarge();
	}
	Unexpected("Corner in BubbleCornerRadius.");
}

int MsgFileThumbRadiusSmall() {
	return MapBubbleRadius(
		EffectiveBubbleRadiusValue(),
		st::msgFileThumbRadiusSmall,
		st::msgFileThumbRadiusSmall);
}

int MsgFileThumbRadiusLarge() {
	return MapBubbleRadius(
		EffectiveBubbleRadiusValue(),
		st::msgFileThumbRadiusSmall,
		st::msgFileThumbRadiusLarge);
}

bool TakeLegacySmallBubbleRadius() {
	const auto result = UseSmallMsgBubbleRadius.value();
	if (result) {
		UseSmallMsgBubbleRadius.set(false);
	}
	return result;
}

} // namespace Ui
