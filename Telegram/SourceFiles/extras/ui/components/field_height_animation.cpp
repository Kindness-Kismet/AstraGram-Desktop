#include "extras/ui/components/field_height_animation.h"

#include "base/algorithm.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/ui_utility.h"

#include <QtGui/QAbstractTextDocumentLayout>
#include <QtGui/QGuiApplication>
#include <QtGui/QInputMethod>
#include <QtGui/QTextDocument>
#include <QtWidgets/QScrollBar>
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
	const auto refresh = [weak = base::make_weak(this)] {
		if (const auto strong = weak.get()) {
			strong->scheduleRefresh();
		}
	};
	QObject::connect(
		_field->document()->documentLayout(),
		&QAbstractTextDocumentLayout::documentSizeChanged,
		_field.get(),
		refresh);
	QObject::connect(
		_field->rawTextEdit()->verticalScrollBar(),
		&QScrollBar::valueChanged,
		_field.get(),
		refresh);
	const auto target = targetHeight();
	_height.jumpTo(target);
	apply(target);
}

void FieldHeightAnimation::setMaxHeight(int height) {
	if (_maxHeight == height) {
		return;
	}
	_maxHeight = height;
	const auto target = targetHeight();
	if (_height.target() == target) {
		return;
	}
	_height.jumpTo(target);
	apply(target);
}

void FieldHeightAnimation::finish() {
	const auto target = targetHeight();
	_height.jumpTo(target);
	if (apply(target)) {
		_changed();
	}
}

int FieldHeightAnimation::contentHeight() const {
	Ui::SendPendingMoveResizeEvents(_field);
	// 控件与内部编辑区的高度差即上下边距，与输入框自适应高度的算法一致。
	const auto margins = _field->height() - _field->rawTextEdit()->height();
	return int(std::ceil(_field->document()->size().height()))
		+ margins;
}

int FieldHeightAnimation::targetHeight() const {
	const auto max = std::max(_maxHeight, 0);
	return std::clamp(contentHeight(), std::min(_minHeight, max), max);
}

bool FieldHeightAnimation::keepScrollAtTop() {
	const auto content = contentHeight();
	if (content > _maxHeight || content <= _field->height()) {
		return false;
	}
	// 扩高足以容纳全文时，先保持已有行完整，避免 Qt 提前滚向新行。
	_field->scrollTo(0);
	return true;
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
	keepScrollAtTop();
	if (_height.target() == target) {
		return;
	}
	const auto step = [=] {
		if (!apply(int(base::SafeRound(_height.value())))) {
			return;
		}
		_changed();
		const auto growing = keepScrollAtTop();
		if (_field->hasFocus()) {
			if (!growing) {
				_field->ensureCursorVisible();
			}
			// 输入法候选框跟随光标，扩高完成后恢复光标滚动。
			QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle);
		}
	};
	if (_field->isVisible()) {
		_height.animateTo(target, step);
	}
	if (!_height.animating()) {
		_height.jumpTo(target);
		if (apply(target)) {
			_changed();
		}
	}
}

bool FieldHeightAnimation::apply(int height) {
	if (_field->height() == height) {
		return false;
	}
	_field->resize(_field->width(), height);
	return true;
}

} // namespace ExtrasUi
