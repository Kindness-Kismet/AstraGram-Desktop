#pragma once

class UserData;

namespace Window {
class SessionController;
} // namespace Window

namespace Extras::SecretChats {

void showChats(not_null<Window::SessionController*> controller);
void showChat(not_null<Window::SessionController*> controller, int chatId);
void confirmCreate(
	not_null<Window::SessionController*> controller,
	not_null<UserData*> user);

} // namespace Extras::SecretChats
