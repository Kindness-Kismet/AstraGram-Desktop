#pragma once

#include "extras/ui/components/smooth_transition.h"
#include "ui/text/text.h"

namespace ExtrasUi {

// 气泡过渡：文字更新后气泡从旧尺寸平滑过渡到新尺寸，旧文字淡出、新文字淡入。
// 内容按最终排版绘制，由调用方裁剪在气泡内。
class BubbleTransition final {
public:
	BubbleTransition(
		int fromHeight,
		int fromWidth,
		Ui::Text::String oldText,
		int oldTextWidth);

	// 排版时调用：首次排版开始过渡，宽度变化或过渡结束返回 false。
	[[nodiscard]] bool update(
		int layoutWidth,
		int finalHeight,
		const Ui::Text::String &newText,
		Fn<void()> callback);

	[[nodiscard]] bool started() const {
		return _started;
	}
	[[nodiscard]] int fromHeight() const {
		return _fromHeight;
	}
	[[nodiscard]] int fromWidth() const {
		return _fromWidth;
	}
	[[nodiscard]] bool animating() const;
	[[nodiscard]] float64 progress() const;
	[[nodiscard]] int height() const;
	[[nodiscard]] int bubbleWidth(int finalWidth) const;
	// 文字没有变化时为空，只过渡尺寸。
	[[nodiscard]] const Ui::Text::String *oldText() const;
	[[nodiscard]] int oldTextWidth() const {
		return _oldTextWidth;
	}

private:
	SmoothTransition _progress;
	Ui::Text::String _oldText;
	int _oldTextWidth = 0;
	int _fromHeight = 0;
	int _fromWidth = 0;
	int _toHeight = 0;
	int _layoutWidth = 0;
	bool _started = false;

};

} // namespace ExtrasUi
