#pragma once

class UserData;

namespace Ui {
class RpWidget;
namespace Menu {
struct MenuCallback;
} // namespace Menu
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Extras::SecretChats {

void showChat(not_null<Window::SessionController*> controller, int chatId);
void verifyKey(
	not_null<Window::SessionController*> controller,
	not_null<Ui::RpWidget*> parent,
	int chatId);
void fillMenu(not_null<Window::SessionController*> controller,
	not_null<Ui::RpWidget*> parent, int chatId,
	const Ui::Menu::MenuCallback &callback);
void confirmCreate(
	not_null<Window::SessionController*> controller,
	not_null<UserData*> user);

} // namespace Extras::SecretChats
