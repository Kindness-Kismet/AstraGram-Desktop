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

bool BubbleTransition::update(
		int layoutWidth,
		int finalHeight,
		const Ui::Text::String &newText,
		Fn<void()> callback) {
	_toHeight = finalHeight;
	if (_started) {
		return (layoutWidth == _layoutWidth) && _progress.animating();
	}
	_started = true;
	_layoutWidth = layoutWidth;
	if (_oldText.toString() == newText.toString()) {
		_oldText = Ui::Text::String();
		// 换表情包等原因重建的视图文字和高度都没变，不做过渡。
		if (_fromHeight == finalHeight) {
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

int BubbleTransition::bubbleWidth(int finalWidth) const {
	return anim::interpolate(_fromWidth, finalWidth, progress());
}

const Ui::Text::String *BubbleTransition::oldText() const {
	return _oldText.isEmpty() ? nullptr : &_oldText;
}

} // namespace ExtrasUi
