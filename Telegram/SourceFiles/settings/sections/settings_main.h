/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_type.h"

namespace Window {
class Controller;
class SessionController;
} // namespace Window

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

namespace Settings {

[[nodiscard]] Type MainId();

void FillAccountMenu(
	not_null<Window::SessionController*> controller,
	const Ui::Menu::MenuCallback &addAction,
	Fn<void(Type)> showOther);

void SetupLanguageButton(
	not_null<Window::Controller*> window,
	not_null<Ui::VerticalLayout*> container);
bool HasInterfaceScale();
void SetupInterfaceScale(
	not_null<Window::Controller*> window,
	not_null<Ui::VerticalLayout*> container,
	bool icon = true);

void SetupValidatePhoneNumberSuggestion(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container,
	Fn<void(Type)> showOther);
void SetupValidatePasswordSuggestion(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container,
	Fn<void(Type)> showOther);

void OpenFaq(base::weak_ptr<Window::SessionController> weak);
void OpenAskQuestionConfirm(not_null<Window::SessionController*> window);

} // namespace Settings
