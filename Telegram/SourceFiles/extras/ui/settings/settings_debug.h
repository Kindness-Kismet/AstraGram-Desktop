#pragma once

#include "settings/settings_common.h"
#include "settings/settings_common_session.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

// Entry point for debug tooling. Listed in ExtrasMain only when
// DebugEntryVisible() holds, so release users never reach it.
class ExtrasDebug : public Section<ExtrasDebug> {
public:
	ExtrasDebug(QWidget *parent, not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();
};

[[nodiscard]] Type ExtrasDebugId();

// Debug builds always expose the section; release builds only after the
// user turned debug logs on.
[[nodiscard]] bool DebugEntryVisible();

} // namespace Settings
