#pragma once

namespace Ui {
class ElasticScroll;
} // namespace Ui

namespace ExtrasUi {

// 只对鼠标滚轮补间，触控板和触屏继续使用原有惯性。
void setupSmoothScroll(
	not_null<Ui::ElasticScroll*> scroll,
	Fn<void()> wheelStarted = nullptr);

} // namespace ExtrasUi
