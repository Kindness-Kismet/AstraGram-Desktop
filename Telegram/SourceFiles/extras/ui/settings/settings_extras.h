#pragma once

#include "settings/settings_common.h"
#include "settings/settings_common_session.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

class ExtrasGhost : public Section<ExtrasGhost> {
public:
	ExtrasGhost(QWidget *parent, not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();

	not_null<Window::SessionController*> _controller;
};

[[nodiscard]] Type ExtrasGhostId();

} // namespace Settings
