#include "extras/ui/controls/attach_popup_menu.h"

#include "base/event_filter.h"
#include "base/invoke_queued.h"
#include "ui/abstract_button.h"
#include "ui/ui_utility.h"

#include <QtGui/QCloseEvent>
#include <QtGui/QCursor>
#include <QtGui/QEnterEvent>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QApplication>

namespace ExtrasUi {

AttachMenu::AttachMenu(QWidget *parent)
: PopupMenu(parent, st::popupMenuWithIcons)
, _hideTimer([=] {
	if (_autoHiding && _pointerRegion == PointerRegion::Outside) {
		hideMenu();
	}
}) {
	deleteOnHide(false);
	setForcedOrigin(Ui::PanelAnimation::Origin::BottomLeft);
	animatePhaseValue() | rpl::on_next([=](AnimatePhase phase) {
		_phase = phase;
		if (phase == AnimatePhase::Shown) {
			positionMenu();
		} else if (phase == AnimatePhase::StartHide) {
			dismissHover();
		}
	}, lifetime());
	menu()->resizesFromInner() | rpl::on_next([=] {
		Ui::PostponeCall(this, [=] { positionMenu(); });
	}, lifetime());
}

AttachMenu::~AttachMenu() {
	lifetime().destroy();
	if (_anchor) {
		_anchor->setSynteticOver(false);
	}
}

void AttachMenu::setAnchor(not_null<Ui::AbstractButton*> button, bool hoverToOpen) {
	Expects(!_anchor);
	_anchor = button.get();
	_hoverToOpen = hoverToOpen;
	button->installEventFilter(this);
	button->window()->installEventFilter(this);
	// 原生菜单捕获鼠标后，移动事件可能发给菜单的任意子控件。
	base::install_event_filter(this, qApp, [=](not_null<QEvent*> event) {
		return handlePointerEvent(event.get())
			? base::EventFilterResult::Cancel
			: base::EventFilterResult::Continue;
	});
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

void AttachMenu::setRefreshCallback(Fn<void()> callback) {
	_refreshCallback = std::move(callback);
}

void AttachMenu::inheritHoverState(const AttachMenu *previous) {
	if (!previous) {
		return;
	}
	_pointerRegion = previous->_pointerRegion;
	_hoverDismissed = previous->_hoverDismissed;
}

bool AttachMenu::deferRefresh(Fn<void()> callback) {
	if (_phase == AnimatePhase::Hidden) {
		return false;
	}
	_refreshAfterHide = std::move(callback);
	return true;
}

void AttachMenu::showMenu() {
	_hideTimer.cancel();
	if (!_anchor || !_anchor->isVisible() || !_anchor->isEnabled()
		|| (isVisible() && _phase != AnimatePhase::StartHide)) {
		return;
	}
	if (_phase == AnimatePhase::StartHide) {
		const auto region = _pointerRegion;
		hideMenu(true);
		_pointerRegion = region;
	}
	_hoverDismissed = false;
	_anchor->setSynteticOver(_pointerRegion == PointerRegion::Anchor);
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

AttachMenu::PointerRegion AttachMenu::pointerRegion(QPoint globalPosition) const {
	if (_anchor && _anchor->isVisible() && _anchor->isEnabled()
		&& _anchor->rect().contains(_anchor->mapFromGlobal(globalPosition))) {
		return PointerRegion::Anchor;
	}
	if (isVisible() && rect().contains(mapFromGlobal(globalPosition))) {
		return PointerRegion::Menu;
	}
	return PointerRegion::Outside;
}

bool AttachMenu::handlePointerEvent(QEvent *event) {
	if (!isVisible() || !_anchor) {
		return false;
	}
	const auto type = event->type();
	if (type != QEvent::MouseMove
		&& type != QEvent::MouseButtonPress
		&& type != QEvent::MouseButtonRelease) {
		return false;
	}
	const auto mouse = static_cast<QMouseEvent*>(event);
	updatePointer(mouse->globalPos());
	if (type == QEvent::MouseMove || mouse->button() != Qt::LeftButton) {
		return false;
	}
	// 加号属于菜单的控制按钮，必须先于公共菜单的外部点击判断处理。
	if (type == QEvent::MouseButtonPress
		&& _pointerRegion == PointerRegion::Anchor) {
		_anchorPressed = true;
		_anchor->setSynteticDown(true);
		return true;
	}
	if (type != QEvent::MouseButtonRelease || !_anchorPressed) {
		return false;
	}
	_anchorPressed = false;
	_anchor->setSynteticDown(false);
	if (!_hoverToOpen && _pointerRegion == PointerRegion::Anchor) {
		hideMenu();
	}
	return true;
}

void AttachMenu::updatePointer(QPoint globalPosition) {
	const auto previous = _pointerRegion;
	_pointerRegion = pointerRegion(globalPosition);
	if (_pointerRegion != PointerRegion::Anchor) {
		_hoverDismissed = false;
	}
	if (_anchor) {
		_anchor->setSynteticOver(
			_pointerRegion == PointerRegion::Anchor && !_hoverDismissed);
	}
	if (!_hoverToOpen || !_autoHiding) {
		return;
	}
	if (_pointerRegion == PointerRegion::Outside) {
		scheduleHide();
		return;
	}
	_hideTimer.cancel();
	if ((_pointerRegion == PointerRegion::Anchor
		&& previous != PointerRegion::Anchor && !_hoverDismissed)
		|| (_pointerRegion == PointerRegion::Menu
			&& _phase == AnimatePhase::StartHide)) {
		showMenu();
	}
}

void AttachMenu::dismissHover() {
	_hideTimer.cancel();
	_hoverDismissed = (_pointerRegion == PointerRegion::Anchor);
	if (_anchor) {
		_anchor->setSynteticOver(false);
		if (base::take(_anchorPressed)) {
			_anchor->setSynteticDown(false);
		}
	}
}

void AttachMenu::scheduleHide() {
	if (_hoverToOpen && _autoHiding && isVisible() && !_hideTimer.isActive()) {
		_hideTimer.callOnce(300);
	}
}

bool AttachMenu::eventFilter(QObject *watched, QEvent *event) {
	if (handlePointerEvent(event)) {
		return true;
	}
	const auto type = event->type();
	if (watched == _anchor) {
		if (type == QEvent::Enter) {
			updatePointer(static_cast<QEnterEvent*>(event)->globalPosition().toPoint());
			return true;
		}
		if (type == QEvent::Leave) {
			updatePointer(QCursor::pos());
			return true;
		}
		if (type == QEvent::MouseMove) {
			updatePointer(static_cast<QMouseEvent*>(event)->globalPos());
			return true;
		}
		if (type == QEvent::MouseButtonRelease && !_hoverToOpen) {
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
			updatePointer(static_cast<QEnterEvent*>(event)->globalPosition().toPoint());
		} else if (type == QEvent::Leave) {
			updatePointer(QCursor::pos());
		}
	} else if (_anchor && watched == _anchor->window()
		&& (type == QEvent::Move || type == QEvent::Resize || type == QEvent::Hide)) {
		hideMenu(true);
	}
	return PopupMenu::eventFilter(watched, event);
}

void AttachMenu::hideEvent(QHideEvent *event) {
	_phase = AnimatePhase::Hidden;
	dismissHover();
	if (_pointerRegion == PointerRegion::Menu) {
		_pointerRegion = PointerRegion::Outside;
	}
	PopupMenu::hideEvent(event);
	// 原生窗口只能在当前 Qt 事件完整返回后销毁，避免中断正在合成的画布。
	InvokeQueued(this, [=] {
		if (!isHidden()) {
			return;
		}
		const auto refresh = _refreshAfterHide
			? base::take(_refreshAfterHide)
			: _refreshCallback;
		if (refresh) {
			refresh();
		}
	});
}

void AttachMenu::closeEvent(QCloseEvent *event) {
	// Qt 的外部点击也走完整隐藏流程，停止动画并释放菜单状态。
	hideMenu(true);
	event->accept();
}

void AttachMenu::focusOutEvent(QFocusEvent *event) {
	if (_autoHiding) {
		PopupMenu::focusOutEvent(event);
	}
}

} // namespace ExtrasUi
