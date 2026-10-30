#include "extras/ui/components/bubble_transition.h"

namespace ExtrasUi {

BubbleTransition::BubbleTransition(
	int fromHeight,
	int fromWidth,
	Ui::Text::String oldText,
	int oldTextWidth)
: _oldText(std::move(oldText))
, _oldTextWidth(oldTextWidth)
, _fromHeight(fromHeight)
, _fromWidth(fromWidth) {
}

BubbleTransition::BubbleTransition(int fromHeight, int fromWidth)
: BubbleTransition(fromHeight, fromWidth, Ui::Text::String(), 0) {
	includeContent();
}

bool BubbleTransition::update(
		int layoutWidth,
		int finalHeight,
		int finalWidth,
		const Ui::Text::String &newText,
		Fn<void()> callback) {
	if (_started) {
		if (layoutWidth != _layoutWidth) {
			return false;
		}
		if (_oldText.isEmpty()
			&& (_toHeight != finalHeight || _toWidth != finalWidth)) {
			// 后续回应数据改变目标时，从当前显示尺寸继续过渡。
			_fromHeight = height();
			_fromWidth = bubbleWidth();
			_toHeight = finalHeight;
			_toWidth = finalWidth;
			_progress.jumpTo(0.);
			_progress.animateTo(1., std::move(callback));
		} else if (!_oldText.isEmpty()) {
			_toHeight = finalHeight;
			_toWidth = finalWidth;
		}
		return _progress.animating();
	}
	_started = true;
	_layoutWidth = layoutWidth;
	_toHeight = finalHeight;
	_toWidth = finalWidth;
	if (_oldText.isEmpty() || _oldText.toString() == newText.toString()) {
		_oldText = Ui::Text::String();
		// 文字和尺寸都没变时，不因视图重建重复播放。
		if (_fromHeight == finalHeight && _fromWidth == finalWidth) {
			return false;
		}
	}
	_progress.animateTo(1., std::move(callback));
	return _progress.animating();
}

bool BubbleTransition::animating() const {
	return _progress.animating();
}

float64 BubbleTransition::progress() const {
	return _progress.value();
}

int BubbleTransition::height() const {
	return anim::interpolate(_fromHeight, _toHeight, progress());
}

int BubbleTransition::bubbleWidth() const {
	return anim::interpolate(_fromWidth, _toWidth, progress());
}

const Ui::Text::String *BubbleTransition::oldText() const {
	return _oldText.isEmpty() ? nullptr : &_oldText;
}

} // namespace ExtrasUi
