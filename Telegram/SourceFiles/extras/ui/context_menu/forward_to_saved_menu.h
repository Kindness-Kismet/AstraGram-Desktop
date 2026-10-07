#pragma once

#include "data/data_types.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace ExtrasUi {

void addForwardToSavedMenu(
	not_null<Ui::PopupMenu*> menu,
	not_null<Main::Session*> session,
	MessageIdsList ids);

} // namespace ExtrasUi
