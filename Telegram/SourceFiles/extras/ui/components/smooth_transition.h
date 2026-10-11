#pragma once

#include "ui/effects/animations.h"

namespace ExtrasUi {

// 平滑过渡：输入框、回复条、消息气泡等尺寸变化共用的时长与缓动，
// 参数与 Telegram Android 聊天列表一致；中途换向从当前值继续。
class SmoothTransition final {
public:
	[[nodiscard]] static float64 Ease(float64 progress);

	// 过渡到目标值并逐帧回调；关闭动画或已在目标值时直接到位，不回调。
	void animateTo(float64 target, Fn<void()> callback);
	// 直接到位，不回调。
	void jumpTo(float64 value);

	[[nodiscard]] float64 value() const;
	[[nodiscard]] float64 target() const {
		return _target;
	}
	[[nodiscard]] bool animating() const {
		return _animation.animating();
	}

private:
	Ui::Animations::Simple _animation;
	float64 _target = 0.;

};

} // namespace ExtrasUi
