#pragma once

#include "ui/effects/animations.h"
#include "ui/round_rect.h"
#include "ui/rp_widget.h"
#include "ui/widgets/shadow.h"

class History;

namespace Data {
class Thread;
} // namespace Data

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class PanelAnimation;
class ScrollArea;
class TabbedSearchField;
} // namespace Ui

namespace ExtrasFeatures::TopicSwitcher {

// 列表首行的名称：论坛为全部话题，频道私信为全部会话。
[[nodiscard]] QString AllLabel(not_null<History*> history);

struct PanelDescriptor {
	not_null<Window::SessionController*> controller;
	not_null<History*> history;
	not_null<Data::Thread*> active;
	// 点在切换按钮上由按钮自己处理开关，不算面板外。
	not_null<QWidget*> toggle;
	Fn<void(not_null<Data::Thread*> thread, bool newWindow)> chosen;
	// 开始收起时通知；淡出结束后面板自行销毁。
	Fn<void()> hiding;
};

// 顶栏下方的话题面板：搜索、全部入口与话题列表。
class Panel final : public Ui::RpWidget {
public:
	Panel(not_null<QWidget*> parent, PanelDescriptor &&descriptor);
	~Panel();

	// 内容区左上角放在 position，底边不超过 maxBottom。
	void showAt(QPoint position, int maxBottom);
	void hideAnimated();

private:
	class List;

	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	bool eventFilter(QObject *o, QEvent *e) override;

	void setupSearch();
	void updateSize();
	void startShowAnimation();
	void showAnimationCallback();
	void opacityAnimationCallback();
	[[nodiscard]] QImage grabForAnimation();
	[[nodiscard]] QRect innerRect() const;
	[[nodiscard]] bool outside(QObject *o) const;

	const not_null<QWidget*> _toggle;
	const Fn<void()> _hidingCallback;
	Ui::BoxShadow _shadow;
	Ui::RoundRect _roundRect;
	const not_null<Ui::TabbedSearchField*> _search;
	const not_null<Ui::ScrollArea*> _scroll;
	const not_null<List*> _list;
	int _maxBottom = 0;

	std::unique_ptr<Ui::PanelAnimation> _showAnimation;
	Ui::Animations::Simple _a_show;
	Ui::Animations::Simple _a_opacity;
	QPixmap _cache;
	bool _hiding = false;

};

} // namespace ExtrasFeatures::TopicSwitcher
