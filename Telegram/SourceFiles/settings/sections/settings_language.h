#pragma once

#include "settings/settings_type.h"
#include "rpl/producer.h"

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

[[nodiscard]] Type LanguageId();
void ShowLanguageSettings(
	not_null<Window::SessionController*> controller,
	const QString &highlightId = QString());

void SetupLanguageTranslationControls(
	not_null<Ui::VerticalLayout*> container,
	not_null<Window::SessionController*> controller,
	rpl::producer<> showFinished);

} // namespace Settings
