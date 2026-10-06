#pragma once

namespace Platform::Notifications {

[[nodiscard]] bool RegisterApplication();
[[nodiscard]] bool UnregisterApplication(bool includePreviousPaths = false);

} // namespace Platform::Notifications
