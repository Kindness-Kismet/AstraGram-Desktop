#include "extras/ui/components/smooth_transition.h"

#include "ui/effects/premium_3d_support.h"

namespace ExtrasUi {
namespace {

// 与 ChatListItemAnimator.DEFAULT_DURATION / DEFAULT_INTERPOLATOR 相同。
constexpr auto kDuration = crl::time(250);
constexpr auto kX1 = 0.19919472913616398;
constexpr auto kY1 = 0.010644531250000006;
constexpr auto kX2 = 0.27920937042459737;
constexpr auto kY2 = 0.91025390625;

} // namespace

float64 SmoothTransition::Ease(float64 progress) {
	return Ui::Premium::CubicBezier(kX1, kY1, kX2, kY2, progress);
}

void SmoothTransition::animateTo(float64 target, Fn<void()> callback) {
	const auto from = value();
	_target = target;
	if (from == target || anim::Disabled()) {
		_animation.stop();
		return;
	}
	_animation.start(
		std::move(callback),
		from,
		target,
		kDuration,
		[](float64 delta, float64 dt) { return delta * Ease(dt); });
}

void SmoothTransition::jumpTo(float64 value) {
	_target = value;
	_animation.stop();
}

float64 SmoothTransition::value() const {
	return _animation.value(_target);
}

} // namespace ExtrasUi
