#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "ui/effects/animation_value.h"

namespace ExtrasDebug::Commands {
namespace {

using json = nlohmann::json;

// 下限避免界面长时间停在动画中途，上限避免过渡短到只剩一两帧。
constexpr auto kMinSpeed = 0.01;
constexpr auto kMaxSpeed = 10.;

[[nodiscard]] json animState() {
	const auto multiplier = anim::DurationMultiplier();
	return json{
		{ "speed", 1. / multiplier },
		{ "durationMultiplier", multiplier },
		{ "disabled", anim::Disabled() },
	};
}

// 速度是时长倍率的倒数，只作用于之后启动的过渡动画；只存内存，重启恢复 1。
[[nodiscard]] Result animSpeed(const QStringList &args) {
	if (args.size() > 1) {
		return Result::Err(u"usage: anim.speed [speed]"_q);
	}
	if (args.isEmpty()) {
		return Result::Ok(Compact(animState()));
	}
	auto ok = false;
	const auto speed = args.front().toDouble(&ok);
	// 写成区间内判断，NaN 也会被拒绝。
	if (!ok || !(speed >= kMinSpeed && speed <= kMaxSpeed)) {
		return Result::Err(u"speed must be a number from %1 to %2"_q
			.arg(kMinSpeed)
			.arg(kMaxSpeed));
	}
	anim::SetDurationMultiplier(1. / speed);
	return Result::Ok(Compact(animState()));
}

} // namespace

const HandlerMap &animHandlers() {
	static const auto result = HandlerMap{
		{ u"anim.speed"_q, &animSpeed },
	};
	return result;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
