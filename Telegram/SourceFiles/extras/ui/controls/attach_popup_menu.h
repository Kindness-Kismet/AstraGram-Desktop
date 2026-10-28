#pragma once

#include "base/timer.h"
#include "ui/widgets/popup_menu.h"

namespace ExtrasUi {

// 附件菜单复用公共弹出窗口，定位和悬停行为由加号按钮决定。
class AttachMenu final : public Ui::PopupMenu {
public:
	explicit AttachMenu(QWidget *parent);
	~AttachMenu() override;
	void setAnchor(not_null<QWidget*> button, bool hoverToOpen);
	void setAutoHiding(bool enabled);
	void setPrepareCallback(Fn<void()> callback);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;
	void focusOutEvent(QFocusEvent *event) override;

private:
	void showMenu();
	void positionMenu();
	void scheduleHide();

	QPointer<QWidget> _anchor;
	base::Timer _hideTimer;
	Fn<void()> _prepareCallback;
	AnimatePhase _phase = AnimatePhase::Hidden;
	bool _hoverToOpen = false;
	bool _autoHiding = true;
	bool _insideAnchor = false;
	bool _insideMenu = false;
	bool _positioning = false;
};

} // namespace ExtrasUi
