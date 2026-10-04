#pragma once

#include "settings/settings_type.h"

namespace Main {
class Session;
} // namespace Main

namespace Settings {

[[nodiscard]] Type ArchiveId();
void PreloadArchiveSettings(not_null<::Main::Session*> session);

} // namespace Settings
