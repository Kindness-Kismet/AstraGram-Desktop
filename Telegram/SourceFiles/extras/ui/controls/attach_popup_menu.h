#pragma once

#include "base/timer.h"
#include "ui/widgets/popup_menu.h"

namespace Ui {
class AbstractButton;
}

namespace ExtrasUi {

// 附件菜单复用公共弹出窗口，定位和悬停行为由加号按钮决定。
class AttachMenu final : public Ui::PopupMenu {
public:
	explicit AttachMenu(QWidget *parent);
	~AttachMenu() override;
	void setAnchor(not_null<Ui::AbstractButton*> button, bool hoverToOpen);
	void setAutoHiding(bool enabled);
	void setPrepareCallback(Fn<void()> callback);
	void setRefreshCallback(Fn<void()> callback);
	void inheritHoverState(const AttachMenu *previous);
	[[nodiscard]] bool deferRefresh(Fn<void()> callback);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;
	void hideEvent(QHideEvent *event) override;
	void closeEvent(QCloseEvent *event) override;
	void focusOutEvent(QFocusEvent *event) override;

private:
	enum class PointerRegion {
		Outside,
		Anchor,
		Menu,
	};

	[[nodiscard]] PointerRegion pointerRegion(QPoint globalPosition) const;
	[[nodiscard]] bool handlePointerEvent(QEvent *event);
	void updatePointer(QPoint globalPosition);
	void dismissHover();
	void showMenu();
	void positionMenu();
	void scheduleHide();

	QPointer<Ui::AbstractButton> _anchor;
	base::Timer _hideTimer;
	Fn<void()> _prepareCallback;
	Fn<void()> _refreshCallback;
	Fn<void()> _refreshAfterHide;
	AnimatePhase _phase = AnimatePhase::Hidden;
	PointerRegion _pointerRegion = PointerRegion::Outside;
	bool _hoverToOpen = false;
	bool _autoHiding = true;
	bool _hoverDismissed = false;
	bool _anchorPressed = false;
	bool _positioning = false;
};

} // namespace ExtrasUi
