#pragma once

#include "settings/settings_type.h"

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

[[nodiscard]] Type ArchiveId();
void ShowArchiveSettings(not_null<Window::SessionController*> controller);
void PreloadArchiveSettings(not_null<::Main::Session*> session);

} // namespace Settings
