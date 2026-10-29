#pragma once

#include "extras/ui/components/smooth_transition.h"
#include "ui/text/text.h"

namespace ExtrasUi {

// 文字或内容尺寸变化时平滑调整气泡；文字变化时旧文字淡出、新文字淡入。
// 内容按最终排版绘制，由调用方裁剪在气泡内。
class BubbleTransition final {
public:
	BubbleTransition(
		int fromHeight,
		int fromWidth,
		Ui::Text::String oldText,
		int oldTextWidth);
	BubbleTransition(int fromHeight, int fromWidth);

	// 排版时调用：首次排版开始过渡，宽度变化或过渡结束返回 false。
	[[nodiscard]] bool update(
		int layoutWidth,
		int finalHeight,
		int finalWidth,
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
	[[nodiscard]] int finalHeight() const { return _toHeight; }
	[[nodiscard]] bool includesContent() const { return _includesContent; }
	[[nodiscard]] bool movesReactions() const { return _movesReactions; }
	void includeContent(bool moveReactions = false) {
		_includesContent = true;
		_movesReactions = _movesReactions || moveReactions;
	}
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
	bool _includesContent = false;
	bool _movesReactions = false;

};

} // namespace ExtrasUi
