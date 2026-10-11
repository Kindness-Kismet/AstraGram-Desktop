#pragma once

#include "base/weak_ptr.h"
#include "ui/effects/animations.h"

namespace Ui {
class InputField;
} // namespace Ui

namespace ExtrasUi {

// 接管输入框的自适应高度，行数变化时逐帧过渡并回调；底边位置由调用方保持。
class FieldHeightAnimation final : public base::has_weak_ptr {
public:
	FieldHeightAnimation(
		not_null<Ui::InputField*> field,
		int minHeight,
		int maxHeight,
		Fn<void()> changed);

	// 上限变化立即生效且不回调，与输入框原有的 setMaxHeight 一致。
	void setMaxHeight(int height);
	// 打开聊天等非输入场景直接到达目标高度。
	void finish();

private:
	[[nodiscard]] int targetHeight() const;
	void scheduleRefresh();
	void refresh();
	bool apply(int height);

	const not_null<Ui::InputField*> _field;
	const Fn<void()> _changed;
	Ui::Animations::Simple _animation;
	int _minHeight = 0;
	int _maxHeight = 0;
	int _target = 0;
	bool _refreshScheduled = false;

};

} // namespace ExtrasUi
