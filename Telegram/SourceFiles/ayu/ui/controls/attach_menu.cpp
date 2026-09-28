#include "ayu/ui/controls/attach_menu.h"

#include "ayu/ayu_settings.h"
#include "base/event_filter.h"
#include "ui/widgets/dropdown_menu.h"

namespace AyuUi {

void setupAttachMenu(
		not_null<QWidget*> button,
		not_null<Ui::DropdownMenu*> menu) {
	menu->setObjectName(u"compose.attachMenu"_q);
	menu->setOrigin(Ui::PanelAnimation::Origin::BottomLeft);
	const auto hover = AyuSettings::getInstance().showAttachPopup();
	base::install_event_filter(menu, button, [=](not_null<QEvent*> event) {
		const auto type = event->type();
		if (hover) {
			if (type == QEvent::Enter) {
				menu->otherEnter();
			} else if (type == QEvent::Leave) {
				menu->otherLeave();
			}
			return base::EventFilterResult::Continue;
		}
		if (type != QEvent::MouseButtonRelease) {
			return base::EventFilterResult::Continue;
		}
		const auto mouse = static_cast<QMouseEvent*>(event.get());
		if (mouse->button() != Qt::LeftButton
			|| !button->rect().contains(mouse->pos())) {
			return base::EventFilterResult::Continue;
		}
		if (menu->isHidden()) {
			menu->showAnimated();
		} else {
			menu->hideAnimated();
		}
		return base::EventFilterResult::Continue;
	});
}

}
