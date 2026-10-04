#pragma once

#include "settings/settings_common.h"
#include "settings/settings_common_session.h"

namespace Settings {

class ExtrasArchive : public Section<ExtrasArchive> {
public:
	ExtrasArchive(QWidget *parent, not_null<Window::SessionController*> controller);
	[[nodiscard]] rpl::producer<QString> title() override;
};

class ExtrasText : public Section<ExtrasText> {
public:
	ExtrasText(QWidget *parent, not_null<Window::SessionController*> controller);
	[[nodiscard]] rpl::producer<QString> title() override;
};

} // namespace Settings
