#pragma once

#include "settings/settings_type.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

[[nodiscard]] Type FileConfirmationsId();
void ShowFileConfirmations(not_null<Window::SessionController*> controller);

} // namespace Settings
