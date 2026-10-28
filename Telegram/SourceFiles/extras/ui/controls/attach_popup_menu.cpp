#include "extras/ui/controls/attach_popup_menu.h"

#include "ui/ui_utility.h"

#include <QtGui/QMouseEvent>

namespace ExtrasUi {

AttachMenu::AttachMenu(QWidget *parent)
: PopupMenu(parent, st::popupMenuWithIcons)
, _hideTimer([=] {
	if (!_insideAnchor && !_insideMenu && _autoHiding) {
		hideMenu();
	}
}) {
	deleteOnHide(false);
	setForcedOrigin(Ui::PanelAnimation::Origin::BottomLeft);
	animatePhaseValue() | rpl::on_next([=](AnimatePhase phase) {
		_phase = phase;
		if (phase == AnimatePhase::Shown) {
			positionMenu();
		}
	}, lifetime());
	menu()->resizesFromInner() | rpl::on_next([=] {
		Ui::PostponeCall(this, [=] { positionMenu(); });
	}, lifetime());
}

AttachMenu::~AttachMenu() {
	lifetime().destroy();
}

void AttachMenu::setAnchor(not_null<QWidget*> button, bool hoverToOpen) {
	Expects(!_anchor);
	_anchor = button.get();
	_hoverToOpen = hoverToOpen;
	button->installEventFilter(this);
	button->window()->installEventFilter(this);
}

void AttachMenu::setAutoHiding(bool enabled) {
	_autoHiding = enabled;
	if (!enabled) {
		_hideTimer.cancel();
	}
}

void AttachMenu::setPrepareCallback(Fn<void()> callback) {
	_prepareCallback = std::move(callback);
}

void AttachMenu::showMenu() {
	_hideTimer.cancel();
	if (!_anchor || !_anchor->isVisible() || !_anchor->isEnabled()
		|| (isVisible() && _phase != AnimatePhase::StartHide)) {
		return;
	}
	const auto weak = base::make_weak(this);
	if (const auto prepare = _prepareCallback) {
		prepare();
	}
	if (weak) {
		popup(_anchor->mapToGlobal(QPoint()));
	}
}

void AttachMenu::positionMenu() {
	if (!_anchor || _phase != AnimatePhase::Shown || _positioning) {
		return;
	}
	_positioning = true;
	// 分组展开后仍贴住加号上沿，屏幕边界由公共菜单处理。
	prepareGeometryFor(_anchor->mapToGlobal(QPoint()));
	refreshAnimationCache();
	_positioning = false;
}

void AttachMenu::scheduleHide() {
	if (_hoverToOpen && _autoHiding) {
		_hideTimer.callOnce(300);
	}
}

bool AttachMenu::eventFilter(QObject *watched, QEvent *event) {
	const auto type = event->type();
	if (watched == _anchor) {
		if (type == QEvent::Enter) {
			_insideAnchor = true;
			_hideTimer.cancel();
			if (_hoverToOpen) {
				showMenu();
			}
		} else if (type == QEvent::Leave) {
			_insideAnchor = false;
			scheduleHide();
		} else if (type == QEvent::MouseButtonRelease && !_hoverToOpen) {
			const auto mouse = static_cast<QMouseEvent*>(event);
			if (mouse->button() == Qt::LeftButton
				&& _anchor->rect().contains(mouse->pos())) {
				if (isVisible() && _phase != AnimatePhase::StartHide) {
					hideMenu();
				} else {
					showMenu();
				}
			}
		} else if (type == QEvent::Hide) {
			hideMenu(true);
		}
	} else if (watched == this) {
		if (type == QEvent::Enter) {
			_insideMenu = true;
			_hideTimer.cancel();
		} else if (type == QEvent::Leave) {
			_insideMenu = false;
			scheduleHide();
		} else if (type == QEvent::Hide) {
			_insideMenu = false;
			_hideTimer.cancel();
		}
	} else if (_anchor && watched == _anchor->window()
		&& (type == QEvent::Move || type == QEvent::Resize || type == QEvent::Hide)) {
		hideMenu(true);
	}
	return PopupMenu::eventFilter(watched, event);
}

void AttachMenu::focusOutEvent(QFocusEvent *event) {
	if (_autoHiding) {
		PopupMenu::focusOutEvent(event);
	}
}

} // namespace ExtrasUi
