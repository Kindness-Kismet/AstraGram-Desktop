#include "extras/ui/components/field_height_animation.h"

#include "base/algorithm.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/ui_utility.h"
#include "styles/style_chat_helpers.h"

#include <QtGui/QAbstractTextDocumentLayout>
#include <QtGui/QGuiApplication>
#include <QtGui/QInputMethod>
#include <QtGui/QTextDocument>
#include <QtWidgets/QTextEdit>

namespace ExtrasUi {

FieldHeightAnimation::FieldHeightAnimation(
	not_null<Ui::InputField*> field,
	int minHeight,
	int maxHeight,
	Fn<void()> changed)
: _field(field)
, _changed(std::move(changed))
, _minHeight(minHeight)
, _maxHeight(maxHeight) {
	// 关闭输入框自带的自适应高度，由这里统一计算并过渡。
	_field->setMinHeight(-1);
	QObject::connect(
		_field->document()->documentLayout(),
		&QAbstractTextDocumentLayout::documentSizeChanged,
		_field.get(),
		[weak = base::make_weak(this)] {
			if (const auto strong = weak.get()) {
				strong->scheduleRefresh();
			}
		});
	_target = targetHeight();
	apply(_target);
}

void FieldHeightAnimation::setMaxHeight(int height) {
	if (_maxHeight == height) {
		return;
	}
	_maxHeight = height;
	const auto target = targetHeight();
	if (_target == target) {
		return;
	}
	_target = target;
	_animation.stop();
	apply(target);
}

void FieldHeightAnimation::finish() {
	_target = targetHeight();
	_animation.stop();
	if (apply(_target)) {
		_changed();
	}
}

int FieldHeightAnimation::targetHeight() const {
	Ui::SendPendingMoveResizeEvents(_field);
	// 控件与内部编辑区的高度差即上下边距，与输入框自适应高度的算法一致。
	const auto margins = _field->height() - _field->rawTextEdit()->height();
	const auto content = int(std::ceil(_field->document()->size().height()))
		+ margins;
	const auto max = std::max(_maxHeight, 0);
	return std::clamp(content, std::min(_minHeight, max), max);
}

void FieldHeightAnimation::scheduleRefresh() {
	if (_refreshScheduled) {
		return;
	}
	_refreshScheduled = true;
	// 等本次输入处理完，按钮占位引起的重新换行一并计入再定目标。
	Ui::PostponeCall(this, [=] { refresh(); });
}

void FieldHeightAnimation::refresh() {
	_refreshScheduled = false;
	const auto target = targetHeight();
	if (_target == target) {
		return;
	}
	_target = target;
	if (anim::Disabled() || !_field->isVisible()) {
		_animation.stop();
		if (apply(target)) {
			_changed();
		}
		return;
	}
	const auto step = [=](float64 value) {
		if (!apply(int(base::SafeRound(value)))) {
			return;
		}
		_changed();
		if (_field->hasFocus()) {
			// 光标所在行始终可见，输入法候选框跟随光标。
			_field->ensureCursorVisible();
			QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle);
		}
	};
	// 中途换向时从当前显示高度继续，不排队。
	_animation.start(
		step,
		_field->height(),
		target,
		st::historyComposeResizeDuration,
		anim::easeOutCubic);
}

bool FieldHeightAnimation::apply(int height) {
	if (_field->height() == height) {
		return false;
	}
	_field->resize(_field->width(), height);
	return true;
}

} // namespace ExtrasUi
