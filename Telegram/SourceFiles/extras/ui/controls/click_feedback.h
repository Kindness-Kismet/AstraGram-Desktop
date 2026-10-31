#pragma once

#include <optional>

class QWidget;

namespace ExtrasUi {

// 对话框操作按钮使用微缩放，侧边栏和设置页使用亮度反馈。
void initClickFeedback();

#ifdef _DEBUG
struct ClickFeedbackState {
	bool spring = false;
	double scale = 1.;
	double brightness = 1.;
	double contrast = 1.;
};
[[nodiscard]] std::optional<ClickFeedbackState> clickFeedbackState(QWidget *widget);
#endif

} // namespace ExtrasUi
